#include "phi/adapter/net/http_fetch.h"

#include "net_tls.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <string_view>

#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace phicore::adapter::net {

namespace {

using Clock = std::chrono::steady_clock;

constexpr int kReadChunk = 16 * 1024;

std::string errnoText(int err)
{
    return std::string(std::strerror(err));
}

/// Waits for `events` on the descriptor until the deadline. False on timeout
/// or a poll failure.
bool waitFd(int fd, short events, Clock::time_point deadline)
{
    for (;;) {
        const auto left =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
        if (left.count() <= 0)
            return false;
        pollfd pfd{};
        pfd.fd = fd;
        pfd.events = events;
        const int rc = ::poll(&pfd, 1, static_cast<int>(left.count()));
        if (rc > 0)
            return true;
        if (rc == 0 || errno != EINTR)
            return false;
    }
}

/// One connection, plain or TLS, whose reads and writes are each bounded by
/// the request's deadline. The socket is non-blocking throughout, so a peer
/// that stops talking costs the deadline and not the thread.
class Connection
{
public:
    ~Connection()
    {
        if (m_ssl)
            SSL_free(m_ssl);
        if (m_ctx)
            SSL_CTX_free(m_ctx);
        if (m_fd >= 0)
            ::close(m_fd);
    }

    bool open(const std::string &host, const std::string &port, bool secure,
              const v1::TlsSettings &tls, Clock::time_point deadline, std::string *error)
    {
        if (!connectTcp(host, port, deadline, error))
            return false;
        return !secure || startTls(host, tls, deadline, error);
    }

    bool writeAll(const std::string &bytes, Clock::time_point deadline, std::string *error)
    {
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            const char *data = bytes.data() + offset;
            const std::size_t left = bytes.size() - offset;
            if (m_ssl) {
                const int n = SSL_write(m_ssl, data, static_cast<int>(left));
                if (n > 0) {
                    offset += static_cast<std::size_t>(n);
                    continue;
                }
                if (!waitSsl(SSL_get_error(m_ssl, n), deadline, error, "TLS write"))
                    return false;
                continue;
            }
            const ssize_t n = ::send(m_fd, data, left, MSG_NOSIGNAL);
            if (n > 0) {
                offset += static_cast<std::size_t>(n);
                continue;
            }
            if (n < 0 && errno == EINTR)
                continue;
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                if (waitFd(m_fd, POLLOUT, deadline))
                    continue;
                *error = "write timed out";
                return false;
            }
            *error = "write failed: " + errnoText(errno);
            return false;
        }
        return true;
    }

    /// Bytes read into `out`; 0 at end of stream; -1 on failure or timeout.
    int read(char *out, int capacity, Clock::time_point deadline, std::string *error)
    {
        for (;;) {
            if (m_ssl) {
                const int n = SSL_read(m_ssl, out, capacity);
                if (n > 0)
                    return n;
                const int reason = SSL_get_error(m_ssl, n);
                if (reason == SSL_ERROR_ZERO_RETURN)
                    return 0;
                // The peer closed without close_notify; the body framing
                // decides whether that was the end or a truncation.
                if (reason == SSL_ERROR_SYSCALL && n == 0)
                    return 0;
                if (!waitSsl(reason, deadline, error, "TLS read"))
                    return -1;
                continue;
            }
            const ssize_t n = ::recv(m_fd, out, static_cast<std::size_t>(capacity), 0);
            if (n >= 0)
                return static_cast<int>(n);
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if (waitFd(m_fd, POLLIN, deadline))
                    continue;
                *error = "read timed out";
                return -1;
            }
            *error = "read failed: " + errnoText(errno);
            return -1;
        }
    }

private:
    bool connectTcp(const std::string &host, const std::string &port, Clock::time_point deadline,
                    std::string *error)
    {
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = AI_ADDRCONFIG;
        addrinfo *results = nullptr;
        const int rc = ::getaddrinfo(host.c_str(), port.c_str(), &hints, &results);
        if (rc != 0) {
            *error = "could not resolve " + host + ": " + ::gai_strerror(rc);
            return false;
        }

        std::string lastError = "no address for " + host;
        for (const addrinfo *entry = results; entry; entry = entry->ai_next) {
            const int fd = ::socket(entry->ai_family, entry->ai_socktype | SOCK_NONBLOCK | SOCK_CLOEXEC,
                                    entry->ai_protocol);
            if (fd < 0) {
                lastError = errnoText(errno);
                continue;
            }
            int crc = 0;
            do {
                crc = ::connect(fd, entry->ai_addr, entry->ai_addrlen);
            } while (crc < 0 && errno == EINTR);
            if (crc < 0 && errno == EINPROGRESS) {
                if (!waitFd(fd, POLLOUT, deadline)) {
                    lastError = "connect timed out";
                    ::close(fd);
                    break;
                }
                int soError = 0;
                socklen_t len = sizeof(soError);
                if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &soError, &len) < 0)
                    soError = errno;
                if (soError != 0) {
                    lastError = errnoText(soError);
                    ::close(fd);
                    continue;
                }
            } else if (crc < 0) {
                lastError = errnoText(errno);
                ::close(fd);
                continue;
            }
            m_fd = fd;
            break;
        }
        ::freeaddrinfo(results);
        if (m_fd < 0) {
            *error = "could not connect to " + host + ":" + port + ": " + lastError;
            return false;
        }
        return true;
    }

    bool startTls(const std::string &host, const v1::TlsSettings &tls, Clock::time_point deadline,
                  std::string *error)
    {
        m_ctx = detail::makeClientContext(tls, error);
        if (!m_ctx)
            return false;
        m_ssl = SSL_new(m_ctx);
        if (!m_ssl) {
            *error = detail::opensslText("TLS session");
            return false;
        }
        if (!detail::prepareSession(m_ssl, host, tls.verifyHostname, error))
            return false;
        if (SSL_set_fd(m_ssl, m_fd) != 1) {
            *error = detail::opensslText("TLS socket");
            return false;
        }
        for (;;) {
            const int rc = SSL_connect(m_ssl);
            if (rc == 1)
                return true;
            if (!waitSsl(SSL_get_error(m_ssl, rc), deadline, error, "TLS handshake"))
                return false;
        }
    }

    /// Waits out a WANT_READ/WANT_WRITE; anything else ends the exchange.
    bool waitSsl(int reason, Clock::time_point deadline, std::string *error, const char *what)
    {
        bool forRead = true;
        if (detail::wantsIo(reason, &forRead)) {
            if (waitFd(m_fd, forRead ? POLLIN : POLLOUT, deadline))
                return true;
            *error = std::string(what) + " timed out";
            return false;
        }
        *error = detail::sessionError(m_ssl, reason, what);
        return false;
    }

    int m_fd = -1;
    SSL_CTX *m_ctx = nullptr;
    SSL *m_ssl = nullptr;
};

/// `host:port` as the Host header wants it: the port only when it is not the
/// scheme's own, and an IPv6 literal back inside its brackets.
std::string authorityOf(const std::string &host, const std::string &port, bool secure)
{
    std::string out = host.find(':') == std::string::npos ? host : "[" + host + "]";
    if (port != (secure ? "443" : "80"))
        out += ":" + port;
    return out;
}

/**
 * @brief A Location header turned back into an absolute URL.
 *
 * Absolute, protocol-relative, rooted and relative, which is every form a
 * redirect actually arrives in. Dot segments are left alone: a server that
 * sends `../` in a Location is not one of ours, and quietly guessing what it
 * meant is worse than following it as written.
 */
std::string resolveLocation(bool secure, const std::string &authority, const std::string &target,
                            std::string location)
{
    const std::size_t fragment = location.find('#');
    if (fragment != std::string::npos)
        location.resize(fragment);
    if (location.rfind("http://", 0) == 0 || location.rfind("https://", 0) == 0)
        return location;

    const std::string scheme = secure ? "https" : "http";
    if (location.rfind("//", 0) == 0)
        return scheme + ":" + location;
    if (location.rfind('/', 0) == 0)
        return scheme + "://" + authority + location;

    std::string base = target.substr(0, target.find('?'));
    const std::size_t lastSlash = base.rfind('/');
    base = lastSlash == std::string::npos ? "/" : base.substr(0, lastSlash + 1);
    return scheme + "://" + authority + base + location;
}

bool isRedirect(int status)
{
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

FetchResult failed(std::string error)
{
    FetchResult result;
    result.error = std::move(error);
    return result;
}

} // namespace

FetchResult fetch(const Fetch &request)
{
    const Clock::time_point deadline =
        Clock::now() + std::max(std::chrono::milliseconds(1), request.timeout);

    std::string url = request.url;
    std::string method = request.method;
    std::string body = request.body;
    int redirectsLeft = std::max(0, request.maxRedirects);

    for (;;) {
        bool secure = false;
        std::string host;
        std::string port;
        std::string target;
        if (!parseUrl(url, &secure, &host, &port, &target))
            return failed("not an http(s) URL: " + url);

        Connection connection;
        std::string error;
        if (!connection.open(host, port, secure, request.tls, deadline, &error))
            return failed(std::move(error));

        Request wire;
        wire.method = method;
        wire.target = target;
        wire.authority = authorityOf(host, port, secure);
        wire.headers = request.headers;
        wire.body = body;
        if (!connection.writeAll(serializeRequest(wire), deadline, &error))
            return failed(std::move(error));

        ResponseParser parser;
        ResponseParser::State state = ResponseParser::State::NeedMore;
        char chunk[kReadChunk];
        while (state == ResponseParser::State::NeedMore) {
            const int n = connection.read(chunk, sizeof(chunk), deadline, &error);
            if (n < 0)
                return failed(std::move(error));
            state = n == 0 ? parser.finish()
                           : parser.consume(std::string_view(chunk, static_cast<std::size_t>(n)));
        }
        if (state != ResponseParser::State::Complete) {
            // A status line the parser never accepted and a body that stopped
            // halfway are different things to whoever reads the log.
            return failed(parser.response().status == 0
                              ? "not an HTTP response"
                              : "the connection closed before the response was complete");
        }

        const Response &response = parser.response();
        const std::string location = response.header("Location");
        if (isRedirect(response.status) && !location.empty() && redirectsLeft > 0) {
            --redirectsLeft;
            const std::string next =
                resolveLocation(secure, wire.authority, target, location);
            // Never step down from https to http on a redirect: the caller
            // asked for a protected connection and nobody but the redirect
            // said otherwise.
            if (secure && next.rfind("https://", 0) != 0)
                return failed("redirect from https to a plain connection refused");
            if (response.status != 307 && response.status != 308) {
                method = "GET";
                body.clear();
            }
            url = next;
            continue;
        }

        FetchResult result;
        result.status = response.status;
        result.body = response.body;
        result.headers = response.headers;
        if (isRedirect(response.status) && !location.empty())
            result.error = "HTTP " + std::to_string(response.status) + " redirect not followed";
        else if (response.status >= 400)
            result.error = "HTTP " + std::to_string(response.status);
        else
            result.ok = true;
        return result;
    }
}

} // namespace phicore::adapter::net
