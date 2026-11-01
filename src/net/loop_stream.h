#pragma once
// A connected socket on a phi::runtime loop, plain or TLS, with TLS started
// at the beginning or in the middle.
//
// The part of a network client that is the same whatever the protocol:
// resolving, trying the addresses in turn, the non-blocking connect, the TLS
// handshake driven one step per readiness, writes that finish when the socket
// lets them, reads delivered as they come. SMTP needs TLS to begin after a
// few lines of plain text (STARTTLS), which is why the upgrade is a call on
// an open stream and not a flag at connect.
//
// The HTTP client predates this and still drives its own socket; moving it
// here is a change for its own commit.

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <openssl/ssl.h>

#include "phi/adapter/v1/tlsconfig.h"
#include "phi/runtime/loop.h"

namespace phicore::adapter::net::detail {

class LoopStream
{
public:
    struct Handlers {
        /// The connection is up (plain), or the handshake is done (TLS).
        std::function<void()> connected;
        /// The handshake asked for by startTls() is done.
        std::function<void()> secured;
        /// Bytes as they arrive. The handler may close the stream.
        std::function<void(std::string_view bytes)> data;
        /// The peer closed. Not called after a failure or after close().
        std::function<void()> closed;
        /// Something went wrong; the stream is closed when this is called.
        std::function<void(std::string error)> failed;
    };

    explicit LoopStream(phi::runtime::Loop &loop);
    ~LoopStream();
    LoopStream(const LoopStream &) = delete;
    LoopStream &operator=(const LoopStream &) = delete;

    void setHandlers(Handlers handlers);

    /// Resolves and connects; `tls` non-null starts TLS as soon as the
    /// socket is up, checked against `serverName`. The outcome arrives
    /// through the handlers. False only when a stream is already open.
    bool connect(const std::string &host, const std::string &port, const v1::TlsSettings *tls,
                 const std::string &serverName);
    /// Upgrades an open plain stream; `secured` or `failed` follows.
    bool startTls(const v1::TlsSettings &tls, const std::string &serverName);
    /// Queues bytes; they go out as the socket allows.
    void write(std::string bytes);
    /// Closes without a word to the handlers.
    void close();

    [[nodiscard]] bool open() const noexcept { return m_fd >= 0; }
    [[nodiscard]] bool secure() const noexcept { return m_ssl != nullptr && !m_handshaking; }

private:
    struct Candidate;

    void connectNext();
    void onConnectable();
    void connected();
    bool beginTls();
    void continueHandshake();
    bool waitOn(int reason);
    void onWritable();
    void writeOut();
    void onReadable();
    void fail(std::string error);

    phi::runtime::Loop &m_loop;
    Handlers m_handlers;

    int m_fd = -1;
    phi::runtime::FdWatch m_readWatch;
    phi::runtime::FdWatch m_writeWatch;

    std::vector<Candidate> m_candidates;
    std::size_t m_nextCandidate = 0;
    std::string m_lastConnectError;

    v1::TlsSettings m_tls;
    std::string m_serverName;
    bool m_tlsAtConnect = false;
    SSL_CTX *m_sslCtx = nullptr;
    SSL *m_ssl = nullptr;
    bool m_handshaking = false;
    bool m_upgrading = false;

    std::string m_outbound;
    std::size_t m_written = 0;
};

} // namespace phicore::adapter::net::detail
