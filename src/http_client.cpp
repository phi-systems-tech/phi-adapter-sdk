#include "phi/adapter/net/http_client.h"

#include <cerrno>
#include <cstring>
#include <utility>

#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "phi/runtime/loop.h"

namespace phicore::adapter::net {

namespace {

constexpr std::size_t kReadChunk = 8192;

std::string systemError(int code)
{
    return std::string(std::strerror(code));
}

/**
 * @brief Resolves and starts a connection, non-blocking.
 *
 * A numeric host costs no lookup. A name does, and getaddrinfo blocks the loop
 * for its length - a device on the LAN is reached by address in every
 * configuration that has gone through phi-core, which resolves names in the
 * background, so this is the path before that lands rather than the usual one.
 */
int openConnection(const std::string &host, const std::string &port, std::string *error,
                   bool *inProgress)
{
    *inProgress = false;

    ::addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_NUMERICHOST;

    ::addrinfo *resolved = nullptr;
    int rc = ::getaddrinfo(host.c_str(), port.c_str(), &hints, &resolved);
    if (rc != 0) {
        hints.ai_flags = 0;
        rc = ::getaddrinfo(host.c_str(), port.c_str(), &hints, &resolved);
    }
    if (rc != 0 || resolved == nullptr) {
        if (error)
            *error = std::string("Cannot resolve host: ") + ::gai_strerror(rc);
        return -1;
    }

    int fd = -1;
    std::string lastError = "No address to connect to";
    for (::addrinfo *entry = resolved; entry != nullptr; entry = entry->ai_next) {
        fd = ::socket(entry->ai_family, entry->ai_socktype | SOCK_NONBLOCK | SOCK_CLOEXEC,
                      entry->ai_protocol);
        if (fd < 0) {
            lastError = systemError(errno);
            continue;
        }
        const int one = 1;
        ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        if (::connect(fd, entry->ai_addr, entry->ai_addrlen) == 0)
            break;
        if (errno == EINPROGRESS) {
            *inProgress = true;
            break;
        }
        lastError = systemError(errno);
        ::close(fd);
        fd = -1;
    }
    ::freeaddrinfo(resolved);

    if (fd < 0 && error)
        *error = lastError;
    return fd;
}

} // namespace

struct HttpClient::Impl {
    explicit Impl(phi::runtime::Loop &loopRef) : loop(loopRef) {}

    phi::runtime::Loop &loop;
    Done done;
    Call call;

    // Where the call is going, once the URL has been taken apart.
    std::string host;
    std::string port;
    std::string target;
    std::string authority;

    int fd = -1;
    phi::runtime::FdWatch readWatch;
    phi::runtime::FdWatch writeWatch;
    phi::runtime::Timer deadline;

    std::string outbound;
    std::size_t written = 0;
    ResponseParser parser;

    /// The challenge this device last sent, kept so the 401 round trip happens
    /// once rather than on every request.
    Challenge challenge;
    std::string challengeOrigin;
    std::uint32_t nonceCount = 0;
    /// Whether this call has already been repeated with an answer. One repeat:
    /// a second 401 is the device saying no, not asking again.
    bool retried = false;

    bool busy = false;

    void closeSocket()
    {
        readWatch.reset();
        writeWatch.reset();
        if (fd >= 0) {
            // Half-close so what has been written leaves with the FIN, then
            // take what is waiting off the queue: close() with unread data
            // makes the kernel send a reset, and a reset discards our own
            // unacknowledged bytes.
            ::shutdown(fd, SHUT_WR);
            char discard[1024];
            for (int round = 0; round < 32; ++round) {
                const ssize_t got = ::recv(fd, discard, sizeof(discard), MSG_DONTWAIT);
                if (got > 0)
                    continue;
                if (got < 0 && errno == EINTR)
                    continue;
                break;
            }
            ::close(fd);
            fd = -1;
        }
        outbound.clear();
        written = 0;
        parser.reset();
    }

    void finish(Result result)
    {
        closeSocket();
        deadline.reset();
        busy = false;
        // Moved out first: the callback may start the next call from inside
        // itself and must find an idle client.
        Done callback = std::move(done);
        done = nullptr;
        if (callback)
            callback(std::move(result));
    }

    void fail(std::string error)
    {
        Result result;
        result.error = std::move(error);
        finish(std::move(result));
    }

    std::string authorizationHeader()
    {
        if (call.credentials.user.empty() && call.credentials.password.empty())
            return {};
        if (!challenge.usable() || challengeOrigin != authority)
            return {};
        ++nonceCount;
        return buildAuthorization(challenge, call.credentials, call.method, target, nonceCount,
                                  makeClientNonce());
    }

    void begin()
    {
        Request request;
        request.method = call.method;
        request.target = target;
        request.authority = authority;
        request.headers = call.headers;
        request.body = call.body;
        const std::string authorization = authorizationHeader();
        if (!authorization.empty())
            request.headers.emplace_back("Authorization", authorization);

        outbound = serializeRequest(request);
        written = 0;
        parser.reset();

        std::string error;
        bool inProgress = false;
        fd = openConnection(host, port, &error, &inProgress);
        if (fd < 0) {
            fail(error);
            return;
        }
        if (!inProgress) {
            onConnectable();
            return;
        }
        writeWatch = loop.watchFd(fd, phi::runtime::Loop::FdEvent::Write,
                                  [this]() { onConnectable(); });
    }

    void onConnectable()
    {
        int soError = 0;
        ::socklen_t length = sizeof(soError);
        if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &soError, &length) != 0)
            soError = errno;
        if (soError != 0) {
            fail(systemError(soError));
            return;
        }
        writeWatch.reset();
        readWatch = loop.watchFd(fd, phi::runtime::Loop::FdEvent::Read, [this]() { onReadable(); });
        writeOut();
    }

    void writeOut()
    {
        while (written < outbound.size()) {
            const ssize_t sent = ::send(fd, outbound.data() + written, outbound.size() - written,
                                        MSG_NOSIGNAL);
            if (sent > 0) {
                written += static_cast<std::size_t>(sent);
                continue;
            }
            if (sent < 0 && errno == EINTR)
                continue;
            if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                writeWatch = loop.watchFd(fd, phi::runtime::Loop::FdEvent::Write, [this]() {
                    writeWatch.reset();
                    writeOut();
                });
                return;
            }
            fail("Write failed: " + systemError(errno));
            return;
        }
        writeWatch.reset();
    }

    void onReadable()
    {
        for (;;) {
            char buffer[kReadChunk];
            const ssize_t got = ::recv(fd, buffer, sizeof(buffer), 0);
            if (got > 0) {
                if (parser.consume(std::string_view(buffer, static_cast<std::size_t>(got)))
                    == ResponseParser::State::Malformed) {
                    fail("Malformed response");
                    return;
                }
                if (parser.state() == ResponseParser::State::Complete) {
                    complete();
                    return;
                }
                continue;
            }
            if (got == 0) {
                // The peer is done. For a response framed only by the close,
                // that is what finishes it.
                if (parser.finish() == ResponseParser::State::Complete) {
                    complete();
                    return;
                }
                fail("Connection closed before the response was complete");
                return;
            }
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return;
            fail("Read failed: " + systemError(errno));
            return;
        }
    }

    void complete()
    {
        const Response &response = parser.response();

        if (response.status == 401 && !retried
            && !(call.credentials.user.empty() && call.credentials.password.empty())) {
            const Challenge offered = parseChallenge(response.header("WWW-Authenticate"));
            if (offered.usable()) {
                // A new nonce means a new counter; reusing one across
                // challenges is what a replay looks like to a server that
                // checks.
                challenge = offered;
                challengeOrigin = authority;
                nonceCount = 0;
                retried = true;
                closeSocket();
                begin();
                return;
            }
        }

        Result result;
        result.status = response.status;
        result.body = response.body;
        result.headers = response.headers;
        result.ok = response.status > 0 && response.status < 400;
        if (response.status == 401) {
            result.unauthorized = true;
            // The device was reached and said no. Saying that, rather than
            // "HTTP 401", is what lets a caller send somebody to their
            // password instead of to their network.
            result.error = "Invalid credentials";
            // A stale nonce is not a wrong password; the next call gets a
            // fresh challenge rather than reusing the one that was refused.
            challenge = Challenge{};
            challengeOrigin.clear();
        } else if (!result.ok) {
            result.error = "HTTP " + std::to_string(response.status);
        }
        finish(std::move(result));
    }
};

HttpClient::HttpClient(phi::runtime::Loop &loop)
    : m_impl(std::make_unique<Impl>(loop))
{
}

HttpClient::~HttpClient()
{
    cancel();
}

bool HttpClient::busy() const
{
    return m_impl->busy;
}

void HttpClient::cancel()
{
    m_impl->closeSocket();
    m_impl->deadline.reset();
    m_impl->done = nullptr;
    m_impl->busy = false;
}

void HttpClient::forgetAuthentication()
{
    m_impl->challenge = Challenge{};
    m_impl->challengeOrigin.clear();
    m_impl->nonceCount = 0;
}

bool HttpClient::send(Call call, Done done)
{
    m_impl->loop.assertOnLoop("HttpClient::send");
    if (m_impl->busy)
        return false;

    bool secure = false;
    std::string host;
    std::string port;
    std::string target;
    if (!parseUrl(call.url, &secure, &host, &port, &target))
        return false;

    m_impl->call = std::move(call);
    m_impl->done = std::move(done);
    m_impl->host = std::move(host);
    m_impl->port = std::move(port);
    m_impl->target = std::move(target);
    m_impl->authority = m_impl->host + ":" + m_impl->port;
    m_impl->retried = false;
    m_impl->busy = true;

    if (secure) {
        // Stated rather than half-built: nothing that has left Qt needs TLS
        // yet, and a client that pretends to verify a certificate is worse
        // than one that says it cannot.
        m_impl->fail("https is not supported by this client yet");
        return true;
    }

    m_impl->deadline = m_impl->loop.timerAfter(m_impl->call.timeout, [impl = m_impl.get()]() {
        impl->fail("Request timed out");
    });
    m_impl->begin();
    return true;
}

} // namespace phicore::adapter::net
