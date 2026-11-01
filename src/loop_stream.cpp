#include "net/loop_stream.h"

#include "net_tls.h"

#include <cerrno>
#include <cstring>

#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <openssl/err.h>
#include <sys/socket.h>
#include <unistd.h>

namespace phicore::adapter::net::detail {

namespace {

constexpr std::size_t kReadChunk = 8192;

std::string systemError(int code)
{
    return std::string(std::strerror(code));
}

} // namespace

struct LoopStream::Candidate {
    int family = 0;
    int socktype = 0;
    int protocol = 0;
    ::sockaddr_storage addr{};
    ::socklen_t length = 0;
};

LoopStream::LoopStream(phi::runtime::Loop &loop) : m_loop(loop) {}

LoopStream::~LoopStream()
{
    close();
}

void LoopStream::setHandlers(Handlers handlers)
{
    m_handlers = std::move(handlers);
}

bool LoopStream::connect(const std::string &host, const std::string &port, const v1::TlsSettings *tls,
                         const std::string &serverName)
{
    if (m_fd >= 0)
        return false;
    m_tlsAtConnect = tls != nullptr;
    if (tls)
        m_tls = *tls;
    m_serverName = serverName.empty() ? host : serverName;
    m_outbound.clear();
    m_written = 0;

    // A numeric host costs no lookup; a name blocks the loop for its length,
    // as in the HTTP client. A mail server is named once per account and
    // the lookup is cached by the resolver after the first send.
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
    m_candidates.clear();
    m_nextCandidate = 0;
    if (rc != 0 || resolved == nullptr) {
        fail(std::string("Cannot resolve host: ") + ::gai_strerror(rc));
        return true;
    }
    for (::addrinfo *entry = resolved; entry != nullptr; entry = entry->ai_next) {
        if (entry->ai_addrlen > sizeof(::sockaddr_storage))
            continue;
        Candidate candidate;
        candidate.family = entry->ai_family;
        candidate.socktype = entry->ai_socktype;
        candidate.protocol = entry->ai_protocol;
        std::memcpy(&candidate.addr, entry->ai_addr, entry->ai_addrlen);
        candidate.length = entry->ai_addrlen;
        m_candidates.push_back(candidate);
    }
    ::freeaddrinfo(resolved);
    m_lastConnectError = "No address to connect to";
    connectNext();
    return true;
}

void LoopStream::connectNext()
{
    while (m_nextCandidate < m_candidates.size()) {
        const Candidate &candidate = m_candidates[m_nextCandidate++];
        m_fd = ::socket(candidate.family, candidate.socktype | SOCK_NONBLOCK | SOCK_CLOEXEC,
                        candidate.protocol);
        if (m_fd < 0) {
            m_lastConnectError = systemError(errno);
            continue;
        }
        const int one = 1;
        ::setsockopt(m_fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        const ::sockaddr *addr = reinterpret_cast<const ::sockaddr *>(&candidate.addr);
        if (::connect(m_fd, addr, candidate.length) == 0) {
            connected();
            return;
        }
        if (errno == EINPROGRESS) {
            m_writeWatch = m_loop.watchFd(m_fd, phi::runtime::Loop::FdEvent::Write,
                                          [this]() { onConnectable(); });
            return;
        }
        m_lastConnectError = systemError(errno);
        ::close(m_fd);
        m_fd = -1;
    }
    fail(m_lastConnectError);
}

void LoopStream::onConnectable()
{
    int soError = 0;
    ::socklen_t length = sizeof(soError);
    if (::getsockopt(m_fd, SOL_SOCKET, SO_ERROR, &soError, &length) != 0)
        soError = errno;
    if (soError != 0) {
        // A name with an address the peer does not listen on is not a dead
        // peer; the next address is tried, as a browser would.
        m_lastConnectError = systemError(soError);
        m_writeWatch.reset();
        ::close(m_fd);
        m_fd = -1;
        connectNext();
        return;
    }
    connected();
}

void LoopStream::connected()
{
    m_writeWatch.reset();
    m_readWatch = m_loop.watchFd(m_fd, phi::runtime::Loop::FdEvent::Read, [this]() { onReadable(); });
    if (m_tlsAtConnect) {
        m_upgrading = false;
        if (beginTls())
            continueHandshake();
        return;
    }
    if (m_handlers.connected)
        m_handlers.connected();
}

bool LoopStream::startTls(const v1::TlsSettings &tls, const std::string &serverName)
{
    if (m_fd < 0 || m_ssl)
        return false;
    m_tls = tls;
    if (!serverName.empty())
        m_serverName = serverName;
    m_upgrading = true;
    if (!beginTls())
        return true;
    continueHandshake();
    return true;
}

bool LoopStream::beginTls()
{
    std::string error;
    m_sslCtx = makeClientContext(m_tls, &error);
    if (!m_sslCtx) {
        fail(error);
        return false;
    }
    m_ssl = SSL_new(m_sslCtx);
    if (!m_ssl) {
        fail(opensslText("TLS session"));
        return false;
    }
    if (!prepareSession(m_ssl, m_serverName, m_tls.verifyHostname(), &error)) {
        fail(error);
        return false;
    }
    if (SSL_set_fd(m_ssl, m_fd) != 1) {
        fail(opensslText("TLS socket"));
        return false;
    }
    m_handshaking = true;
    return true;
}

void LoopStream::continueHandshake()
{
    ERR_clear_error();
    const int rc = SSL_connect(m_ssl);
    if (rc == 1) {
        m_handshaking = false;
        m_writeWatch.reset();
        const bool upgraded = m_upgrading;
        m_upgrading = false;
        if (upgraded) {
            if (m_handlers.secured)
                m_handlers.secured();
        } else if (m_handlers.connected) {
            m_handlers.connected();
        }
        if (m_fd >= 0 && m_written < m_outbound.size())
            writeOut();
        return;
    }
    const int reason = SSL_get_error(m_ssl, rc);
    if (!waitOn(reason))
        fail(sessionError(m_ssl, reason, "TLS handshake"));
}

bool LoopStream::waitOn(int reason)
{
    bool forRead = false;
    if (!wantsIo(reason, &forRead))
        return false;
    if (forRead) {
        m_writeWatch.reset();
    } else if (!m_writeWatch) {
        m_writeWatch = m_loop.watchFd(m_fd, phi::runtime::Loop::FdEvent::Write,
                                      [this]() { onWritable(); });
    }
    return true;
}

void LoopStream::write(std::string bytes)
{
    if (m_fd < 0)
        return;
    m_outbound.append(bytes);
    if (!m_handshaking)
        writeOut();
}

void LoopStream::onWritable()
{
    if (m_handshaking) {
        continueHandshake();
        return;
    }
    if (m_written < m_outbound.size()) {
        writeOut();
        return;
    }
    // Nothing left to write: a TLS read was waiting for the socket to
    // become writable.
    m_writeWatch.reset();
    onReadable();
}

void LoopStream::writeOut()
{
    while (m_written < m_outbound.size()) {
        const std::size_t left = m_outbound.size() - m_written;
        if (m_ssl) {
            ERR_clear_error();
            const int sent = SSL_write(m_ssl, m_outbound.data() + m_written, static_cast<int>(left));
            if (sent > 0) {
                m_written += static_cast<std::size_t>(sent);
                continue;
            }
            const int reason = SSL_get_error(m_ssl, sent);
            if (!waitOn(reason))
                fail(sessionError(m_ssl, reason, "TLS write"));
            return;
        }
        const ssize_t sent = ::send(m_fd, m_outbound.data() + m_written, left, MSG_NOSIGNAL);
        if (sent > 0) {
            m_written += static_cast<std::size_t>(sent);
            continue;
        }
        if (sent < 0 && errno == EINTR)
            continue;
        if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (!m_writeWatch)
                m_writeWatch = m_loop.watchFd(m_fd, phi::runtime::Loop::FdEvent::Write,
                                              [this]() { onWritable(); });
            return;
        }
        fail("Write failed: " + systemError(errno));
        return;
    }
    m_outbound.clear();
    m_written = 0;
    m_writeWatch.reset();
}

void LoopStream::onReadable()
{
    if (m_handshaking) {
        continueHandshake();
        return;
    }
    if (m_written < m_outbound.size() && m_ssl) {
        // A TLS write that waited for the socket to be readable.
        writeOut();
        if (m_fd < 0)
            return;
    }
    for (;;) {
        char buffer[kReadChunk];
        ssize_t got = 0;
        if (m_ssl) {
            ERR_clear_error();
            const int n = SSL_read(m_ssl, buffer, sizeof(buffer));
            if (n > 0) {
                got = n;
            } else {
                const int reason = SSL_get_error(m_ssl, n);
                if (reason == SSL_ERROR_ZERO_RETURN || (reason == SSL_ERROR_SYSCALL && n == 0)) {
                    got = 0;
                } else if (waitOn(reason)) {
                    return;
                } else {
                    fail(sessionError(m_ssl, reason, "TLS read"));
                    return;
                }
            }
        } else {
            got = ::recv(m_fd, buffer, sizeof(buffer), 0);
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
            close();
            if (m_handlers.closed)
                m_handlers.closed();
            return;
        }
        if (m_handlers.data)
            m_handlers.data(std::string_view(buffer, static_cast<std::size_t>(got)));
        if (m_fd < 0 || m_handshaking)
            return; // closed, or STARTTLS began, inside the handler
    }
}

void LoopStream::fail(std::string error)
{
    close();
    if (m_handlers.failed)
        m_handlers.failed(std::move(error));
}

void LoopStream::close()
{
    m_readWatch.reset();
    m_writeWatch.reset();
    if (m_ssl) {
        // A close_notify is not worth waiting for on a non-blocking socket.
        // Not before the handshake is done, though: a shutdown then leaves
        // an error on OpenSSL's thread-local queue that the next client on
        // this thread would read as its own.
        if (SSL_is_init_finished(m_ssl))
            SSL_shutdown(m_ssl);
        SSL_free(m_ssl);
        m_ssl = nullptr;
        ERR_clear_error();
    }
    if (m_sslCtx) {
        SSL_CTX_free(m_sslCtx);
        m_sslCtx = nullptr;
    }
    m_handshaking = false;
    m_upgrading = false;
    if (m_fd >= 0) {
        // Half-close so what has been written leaves with the FIN, then take
        // what is waiting off the queue: close() with unread data makes the
        // kernel send a reset, and a reset discards our own unacknowledged
        // bytes - the QUIT, typically.
        ::shutdown(m_fd, SHUT_WR);
        char discard[1024];
        for (int round = 0; round < 32; ++round) {
            const ssize_t got = ::recv(m_fd, discard, sizeof(discard), MSG_DONTWAIT);
            if (got > 0)
                continue;
            if (got < 0 && errno == EINTR)
                continue;
            break;
        }
        ::close(m_fd);
        m_fd = -1;
    }
    m_outbound.clear();
    m_written = 0;
}

} // namespace phicore::adapter::net::detail
