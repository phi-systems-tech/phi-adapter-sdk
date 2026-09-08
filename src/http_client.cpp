#include "phi/adapter/net/http_client.h"

#include <cerrno>
#include <cstring>
#include <utility>
#include <vector>

#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "net_tls.h"
#include "phi/runtime/loop.h"

namespace phicore::adapter::net {

namespace {

constexpr std::size_t kReadChunk = 8192;

std::string systemError(int code)
{
    return std::string(std::strerror(code));
}

struct Candidate {
    int family = 0;
    int socktype = 0;
    int protocol = 0;
    ::sockaddr_storage addr{};
    ::socklen_t length = 0;
};

/**
 * @brief Resolves a host to the addresses to try, in the resolver's order.
 *
 * A numeric host costs no lookup. A name does, and getaddrinfo blocks the loop
 * for its length - a device on the LAN is reached by address in every
 * configuration that has gone through phi-core, which resolves names in the
 * background, so this is the path before that lands rather than the usual one.
 */
std::vector<Candidate> resolve(const std::string &host, const std::string &port, std::string *error)
{
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
        return {};
    }
    std::vector<Candidate> out;
    for (::addrinfo *entry = resolved; entry != nullptr; entry = entry->ai_next) {
        if (entry->ai_addrlen > sizeof(::sockaddr_storage))
            continue;
        Candidate candidate;
        candidate.family = entry->ai_family;
        candidate.socktype = entry->ai_socktype;
        candidate.protocol = entry->ai_protocol;
        std::memcpy(&candidate.addr, entry->ai_addr, entry->ai_addrlen);
        candidate.length = entry->ai_addrlen;
        out.push_back(candidate);
    }
    ::freeaddrinfo(resolved);
    if (out.empty() && error)
        *error = "No address to connect to";
    return out;
}

/// Decodes a streamed body as it arrives, in whichever framing the head said.
class StreamBodyDecoder
{
public:
    enum class State { NeedMore, Complete, Malformed };

    void configure(bool chunked, bool hasLength, std::size_t length)
    {
        m_chunked = chunked;
        m_hasLength = hasLength;
        m_remaining = length;
        m_state = (hasLength && length == 0) ? State::Complete : State::NeedMore;
    }

    template <typename Deliver>
    State feed(std::string_view bytes, Deliver deliver)
    {
        if (m_state != State::NeedMore)
            return m_state;
        if (m_chunked) {
            m_pending.append(bytes);
            return drainChunked(deliver);
        }
        if (m_hasLength) {
            const std::size_t take = std::min(m_remaining, bytes.size());
            if (take > 0)
                deliver(bytes.substr(0, take));
            m_remaining -= take;
            if (m_remaining == 0)
                m_state = State::Complete;
            return m_state;
        }
        if (!bytes.empty())
            deliver(bytes);
        return m_state;
    }

    [[nodiscard]] State state() const { return m_state; }

    /// The peer closed: what that means depends on the framing.
    State finish()
    {
        if (m_state != State::NeedMore)
            return m_state;
        m_state = (m_chunked || m_hasLength) ? State::Malformed : State::Complete;
        return m_state;
    }

private:
    template <typename Deliver>
    State drainChunked(Deliver deliver)
    {
        for (;;) {
            if (m_chunkLeft > 0) {
                const std::size_t take = std::min(m_chunkLeft, m_pending.size());
                if (take == 0)
                    return m_state;
                deliver(std::string_view(m_pending).substr(0, take));
                m_pending.erase(0, take);
                m_chunkLeft -= take;
                if (m_chunkLeft > 0)
                    return m_state;
                m_afterData = true;
            }
            if (m_afterData) {
                // The CRLF that closes a chunk's data.
                if (m_pending.size() < 2)
                    return m_state;
                if (m_pending.compare(0, 2, "\r\n") != 0) {
                    m_state = State::Malformed;
                    return m_state;
                }
                m_pending.erase(0, 2);
                m_afterData = false;
            }
            if (m_inTrailer) {
                const std::size_t eol = m_pending.find("\r\n");
                if (eol == std::string::npos)
                    return m_state;
                m_pending.erase(0, eol + 2);
                if (eol == 0) {
                    m_state = State::Complete;
                    return m_state;
                }
                continue;
            }
            const std::size_t eol = m_pending.find("\r\n");
            if (eol == std::string::npos)
                return m_state;
            std::size_t size = 0;
            bool any = false;
            for (std::size_t i = 0; i < eol; ++i) {
                const char c = m_pending[i];
                int digit = -1;
                if (c >= '0' && c <= '9')
                    digit = c - '0';
                else if (c >= 'a' && c <= 'f')
                    digit = c - 'a' + 10;
                else if (c >= 'A' && c <= 'F')
                    digit = c - 'A' + 10;
                else
                    break; // a chunk extension, ignored
                size = size * 16 + static_cast<std::size_t>(digit);
                any = true;
            }
            if (!any) {
                m_state = State::Malformed;
                return m_state;
            }
            m_pending.erase(0, eol + 2);
            if (size == 0) {
                m_inTrailer = true;
                continue;
            }
            m_chunkLeft = size;
        }
    }

    bool m_chunked = false;
    bool m_hasLength = false;
    std::size_t m_remaining = 0;
    std::size_t m_chunkLeft = 0;
    bool m_afterData = false;
    bool m_inTrailer = false;
    std::string m_pending;
    State m_state = State::NeedMore;
};

} // namespace

struct HttpClient::Impl {
    explicit Impl(phi::runtime::Loop &loopRef) : loop(loopRef) {}

    ~Impl() { closeSocket(); }

    phi::runtime::Loop &loop;
    Done done;
    StreamHandlers stream;
    bool streaming = false;
    Call call;

    // Where the call is going, once the URL has been taken apart.
    bool secure = false;
    std::string host;
    std::string port;
    std::string target;
    std::string authority;

    int fd = -1;
    phi::runtime::FdWatch readWatch;
    phi::runtime::FdWatch writeWatch;
    phi::runtime::Timer deadline;

    /// The addresses the host resolved to, and how far along them we are. A
    /// name with an IPv6 address the peer does not listen on is not a dead
    /// peer; the next address is tried, as a browser would.
    std::vector<Candidate> candidates;
    std::size_t nextCandidate = 0;
    std::string lastConnectError;

    SSL_CTX *sslCtx = nullptr;
    SSL *ssl = nullptr;
    bool handshaking = false;

    std::string outbound;
    std::size_t written = 0;
    ResponseParser parser;
    /// Raw bytes kept only until the head is in, for a stream.
    std::string inbound;
    bool headDelivered = false;
    StreamBodyDecoder bodyDecoder;

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
        if (ssl) {
            // A close_notify is polite but not worth waiting for on a
            // non-blocking socket; the peer sees the FIN either way. Not
            // before the handshake is done, though: a shutdown then leaves
            // "shutdown while in init" on OpenSSL's thread-local error
            // queue, and SSL_get_error() in the *other* client on this
            // thread - the event stream next to the poll - reports it as
            // its own failure.
            if (SSL_is_init_finished(ssl))
                SSL_shutdown(ssl);
            SSL_free(ssl);
            ssl = nullptr;
            ERR_clear_error();
        }
        if (sslCtx) {
            SSL_CTX_free(sslCtx);
            sslCtx = nullptr;
        }
        handshaking = false;
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
        inbound.clear();
        headDelivered = false;
        bodyDecoder = StreamBodyDecoder{};
    }

    void finish(Result result)
    {
        closeSocket();
        deadline.reset();
        busy = false;
        // Moved out first: the callback may start the next call from inside
        // itself and must find an idle client.
        Done callback = streaming ? std::move(stream.done) : std::move(done);
        done = nullptr;
        stream = StreamHandlers{};
        streaming = false;
        if (callback)
            callback(std::move(result));
    }

    void fail(std::string error)
    {
        Result result;
        result.error = std::move(error);
        if (streaming && parser.headComplete()) {
            result.status = parser.response().status;
            result.headers = parser.response().headers;
        }
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
        inbound.clear();
        headDelivered = false;

        std::string error;
        candidates = resolve(host, port, &error);
        nextCandidate = 0;
        lastConnectError = "No address to connect to";
        if (candidates.empty()) {
            fail(error);
            return;
        }
        connectNext();
    }

    /// Starts a connection to the next address; fails when none is left.
    void connectNext()
    {
        while (nextCandidate < candidates.size()) {
            const Candidate &candidate = candidates[nextCandidate++];
            fd = ::socket(candidate.family, candidate.socktype | SOCK_NONBLOCK | SOCK_CLOEXEC,
                          candidate.protocol);
            if (fd < 0) {
                lastConnectError = systemError(errno);
                continue;
            }
            const int one = 1;
            ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
            const ::sockaddr *addr = reinterpret_cast<const ::sockaddr *>(&candidate.addr);
            if (::connect(fd, addr, candidate.length) == 0) {
                connected();
                return;
            }
            if (errno == EINPROGRESS) {
                writeWatch = loop.watchFd(fd, phi::runtime::Loop::FdEvent::Write,
                                          [this]() { onConnectable(); });
                return;
            }
            lastConnectError = systemError(errno);
            ::close(fd);
            fd = -1;
        }
        fail(lastConnectError);
    }

    void onConnectable()
    {
        int soError = 0;
        ::socklen_t length = sizeof(soError);
        if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &soError, &length) != 0)
            soError = errno;
        if (soError != 0) {
            lastConnectError = systemError(soError);
            writeWatch.reset();
            ::close(fd);
            fd = -1;
            connectNext();
            return;
        }
        connected();
    }

    void connected()
    {
        writeWatch.reset();
        readWatch = loop.watchFd(fd, phi::runtime::Loop::FdEvent::Read, [this]() { onReadable(); });
        if (secure) {
            if (!startTls())
                return;
            continueHandshake();
            return;
        }
        writeOut();
    }

    // --- TLS ---------------------------------------------------------------

    bool startTls()
    {
        std::string error;
        sslCtx = detail::makeClientContext(call.tls, &error);
        if (!sslCtx) {
            fail(error);
            return false;
        }
        ssl = SSL_new(sslCtx);
        if (!ssl) {
            fail(detail::opensslText("TLS session"));
            return false;
        }
        const std::string &name = call.tlsServerName.empty() ? host : call.tlsServerName;
        if (!detail::prepareSession(ssl, name, call.tls.verifyHostname, &error)) {
            fail(error);
            return false;
        }
        if (SSL_set_fd(ssl, fd) != 1) {
            fail(detail::opensslText("TLS socket"));
            return false;
        }
        handshaking = true;
        return true;
    }

    /// One step of the handshake; the socket says when the next is possible.
    void continueHandshake()
    {
        ERR_clear_error();
        const int rc = SSL_connect(ssl);
        if (rc == 1) {
            handshaking = false;
            writeWatch.reset();
            writeOut();
            return;
        }
        const int reason = SSL_get_error(ssl, rc);
        if (!waitOn(reason))
            fail(detail::sessionError(ssl, reason, "TLS handshake"));
    }

    /// Arms the watch the pending TLS operation is waiting on. False when it
    /// is not waiting but failed.
    bool waitOn(int reason)
    {
        bool forRead = false;
        if (!detail::wantsIo(reason, &forRead))
            return false;
        if (forRead) {
            writeWatch.reset();
        } else if (!writeWatch) {
            writeWatch = loop.watchFd(fd, phi::runtime::Loop::FdEvent::Write,
                                      [this]() { onWritable(); });
        }
        return true;
    }

    // --- writing -----------------------------------------------------------

    void onWritable()
    {
        if (handshaking) {
            continueHandshake();
            return;
        }
        if (written < outbound.size()) {
            writeOut();
            return;
        }
        // Nothing left to write: a TLS read was waiting for the socket to
        // become writable.
        writeWatch.reset();
        onReadable();
    }

    void writeOut()
    {
        while (written < outbound.size()) {
            const std::size_t left = outbound.size() - written;
            if (ssl) {
                ERR_clear_error();
                const int sent = SSL_write(ssl, outbound.data() + written, static_cast<int>(left));
                if (sent > 0) {
                    written += static_cast<std::size_t>(sent);
                    continue;
                }
                const int reason = SSL_get_error(ssl, sent);
                if (!waitOn(reason))
                    fail(detail::sessionError(ssl, reason, "TLS write"));
                return;
            }
            const ssize_t sent = ::send(fd, outbound.data() + written, left, MSG_NOSIGNAL);
            if (sent > 0) {
                written += static_cast<std::size_t>(sent);
                continue;
            }
            if (sent < 0 && errno == EINTR)
                continue;
            if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                if (!writeWatch)
                    writeWatch = loop.watchFd(fd, phi::runtime::Loop::FdEvent::Write,
                                              [this]() { onWritable(); });
                return;
            }
            fail("Write failed: " + systemError(errno));
            return;
        }
        writeWatch.reset();
    }

    // --- reading -----------------------------------------------------------

    void onReadable()
    {
        if (handshaking) {
            continueHandshake();
            return;
        }
        if (written < outbound.size() && ssl) {
            // A TLS write that waited for the socket to be readable.
            writeOut();
            if (fd < 0)
                return;
        }
        for (;;) {
            char buffer[kReadChunk];
            ssize_t got = 0;
            if (ssl) {
                ERR_clear_error();
                const int n = SSL_read(ssl, buffer, sizeof(buffer));
                if (n > 0) {
                    got = n;
                } else {
                    const int reason = SSL_get_error(ssl, n);
                    if (reason == SSL_ERROR_ZERO_RETURN
                        || (reason == SSL_ERROR_SYSCALL && n == 0)) {
                        got = 0;
                    } else if (waitOn(reason)) {
                        return;
                    } else {
                        fail(detail::sessionError(ssl, reason, "TLS read"));
                        return;
                    }
                }
            } else {
                got = ::recv(fd, buffer, sizeof(buffer), 0);
                if (got < 0) {
                    if (errno == EINTR)
                        continue;
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                        return;
                    fail("Read failed: " + systemError(errno));
                    return;
                }
            }
            if (got == 0) {
                onClosed();
                return;
            }
            if (!consume(std::string_view(buffer, static_cast<std::size_t>(got))))
                return;
        }
    }

    /// False when the call ended inside.
    bool consume(std::string_view bytes)
    {
        if (streaming)
            return consumeStream(bytes);
        if (parser.consume(bytes) == ResponseParser::State::Malformed) {
            fail("Malformed response");
            return false;
        }
        if (parser.state() == ResponseParser::State::Complete) {
            complete();
            return false;
        }
        return true;
    }

    bool consumeStream(std::string_view bytes)
    {
        if (!headDelivered) {
            inbound.append(bytes);
            if (parser.consume(bytes) == ResponseParser::State::Malformed) {
                fail("Malformed response");
                return false;
            }
            if (!parser.headComplete())
                return true;
            headDelivered = true;
            deadline.reset();
            const Response &response = parser.response();
            if (response.status == 401 || response.status >= 400) {
                Result result;
                result.status = response.status;
                result.headers = response.headers;
                result.unauthorized = response.status == 401;
                result.error = response.status == 401 ? "Invalid credentials"
                                                      : "HTTP " + std::to_string(response.status);
                finish(std::move(result));
                return false;
            }
            bodyDecoder.configure(parser.chunked(), parser.hasContentLength(),
                                  parser.contentLength());
            if (stream.head)
                stream.head(response.status, response.headers);
            if (fd < 0)
                return false; // the head callback cancelled
            const std::string rest = inbound.substr(parser.bodyStart());
            inbound.clear();
            if (rest.empty())
                return finishIfDecoderDone();
            return feedBody(rest);
        }
        return feedBody(bytes);
    }

    bool feedBody(std::string_view bytes)
    {
        const StreamBodyDecoder::State state =
            bodyDecoder.feed(bytes, [this](std::string_view piece) {
                if (stream.chunk && fd >= 0)
                    stream.chunk(piece);
            });
        if (fd < 0)
            return false; // cancelled from inside a chunk callback
        if (state == StreamBodyDecoder::State::Malformed) {
            fail("Malformed response");
            return false;
        }
        return finishIfDecoderDone();
    }

    bool finishIfDecoderDone()
    {
        if (bodyDecoder.state() != StreamBodyDecoder::State::Complete)
            return true;
        endStream();
        return false;
    }

    void onClosed()
    {
        if (streaming) {
            if (!headDelivered) {
                fail("Connection closed before the response was complete");
                return;
            }
            if (bodyDecoder.finish() == StreamBodyDecoder::State::Malformed) {
                fail("Connection closed before the response was complete");
                return;
            }
            endStream();
            return;
        }
        // The peer is done. For a response framed only by the close, that is
        // what finishes it.
        if (parser.finish() == ResponseParser::State::Complete) {
            complete();
            return;
        }
        fail("Connection closed before the response was complete");
    }

    void endStream()
    {
        Result result;
        result.status = parser.response().status;
        result.headers = parser.response().headers;
        result.ok = true;
        finish(std::move(result));
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

    bool start(Call newCall)
    {
        loop.assertOnLoop("HttpClient::send");
        if (busy)
            return false;

        std::string newHost;
        std::string newPort;
        std::string newTarget;
        bool https = false;
        if (!parseUrl(newCall.url, &https, &newHost, &newPort, &newTarget))
            return false;

        call = std::move(newCall);
        secure = https;
        host = std::move(newHost);
        port = std::move(newPort);
        target = std::move(newTarget);
        authority = host + ":" + port;
        retried = false;
        busy = true;

        deadline = loop.timerAfter(call.timeout, [this]() { fail("Request timed out"); });
        begin();
        return true;
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
    m_impl->stream = StreamHandlers{};
    m_impl->streaming = false;
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
    if (m_impl->busy)
        return false;
    m_impl->streaming = false;
    m_impl->done = std::move(done);
    if (!m_impl->start(std::move(call))) {
        m_impl->done = nullptr;
        return false;
    }
    return true;
}

bool HttpClient::stream(Call call, StreamHandlers handlers)
{
    if (m_impl->busy)
        return false;
    m_impl->streaming = true;
    m_impl->stream = std::move(handlers);
    if (!m_impl->start(std::move(call))) {
        m_impl->stream = StreamHandlers{};
        m_impl->streaming = false;
        return false;
    }
    return true;
}

} // namespace phicore::adapter::net
