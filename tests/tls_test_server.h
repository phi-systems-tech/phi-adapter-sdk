#pragma once

// A throwaway certificate and a small HTTP server, plain or TLS, for the
// tests of both HTTP clients. The server reads one request per connection,
// hands it to a handler, writes the answer - all at once, or in pieces with a
// pause between them for a client that streams - and closes.

#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

#include <arpa/inet.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <mutex>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace phitest {

inline std::string loweredText(std::string text)
{
    for (char &c : text)
        c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    return text;
}

struct TestCertificate {
    EVP_PKEY *key = nullptr;
    X509 *cert = nullptr;

    bool create(const std::string &pemPath)
    {
        key = EVP_RSA_gen(2048);
        cert = X509_new();
        if (!key || !cert)
            return false;
        X509_set_version(cert, 2);
        ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);
        X509_gmtime_adj(X509_getm_notBefore(cert), -60);
        X509_gmtime_adj(X509_getm_notAfter(cert), 3600);
        X509_set_pubkey(cert, key);
        X509_NAME *name = X509_get_subject_name(cert);
        X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                                   reinterpret_cast<const unsigned char *>("localhost"), -1, -1, 0);
        X509_set_issuer_name(cert, name);

        X509V3_CTX ctx;
        X509V3_set_ctx_nodb(&ctx);
        X509V3_set_ctx(&ctx, cert, cert, nullptr, nullptr, 0);
        for (const auto &[nid, value] : {std::pair{NID_subject_alt_name, "DNS:localhost"},
                                         std::pair{NID_basic_constraints, "critical,CA:TRUE"}}) {
            X509_EXTENSION *ext = X509V3_EXT_conf_nid(nullptr, &ctx, nid, value);
            if (!ext)
                return false;
            X509_add_ext(cert, ext, -1);
            X509_EXTENSION_free(ext);
        }
        if (X509_sign(cert, key, EVP_sha256()) <= 0)
            return false;

        FILE *pem = std::fopen(pemPath.c_str(), "w");
        if (!pem)
            return false;
        const bool written = PEM_write_X509(pem, cert) == 1;
        std::fclose(pem);
        return written;
    }

    ~TestCertificate()
    {
        if (cert)
            X509_free(cert);
        if (key)
            EVP_PKEY_free(key);
    }
};

/// Accepts on one thread and serves each connection on its own, so a slow
/// handler does not hold the next request back: reads a request, hands it to
/// the handler, writes what comes back, closes.
class TestServer
{
public:
    using Handler = std::function<std::string(const std::string &request)>;
    /// Pieces written one after another, `pieceGapMs` apart, then close.
    using PieceHandler = std::function<std::vector<std::string>(const std::string &request)>;

    ~TestServer() { stop(); }

    bool start(PieceHandler handler, int pieceGapMs, const TestCertificate *tls = nullptr)
    {
        m_pieces = std::move(handler);
        m_pieceGapMs = pieceGapMs;
        return start(Handler{}, tls);
    }

    bool start(Handler handler, const TestCertificate *tls = nullptr)
    {
        if (handler)
            m_handler = std::move(handler);
        if (tls) {
            m_ctx = SSL_CTX_new(TLS_server_method());
            if (!m_ctx || SSL_CTX_use_certificate(m_ctx, tls->cert) != 1
                || SSL_CTX_use_PrivateKey(m_ctx, tls->key) != 1)
                return false;
        }
        m_listenFd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (m_listenFd < 0)
            return false;
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::bind(m_listenFd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0
            || ::listen(m_listenFd, 8) < 0)
            return false;
        socklen_t len = sizeof(addr);
        ::getsockname(m_listenFd, reinterpret_cast<sockaddr *>(&addr), &len);
        m_port = ntohs(addr.sin_port);
        m_thread = std::thread([this] { serve(); });
        return true;
    }

    void stop()
    {
        if (m_listenFd >= 0) {
            ::shutdown(m_listenFd, SHUT_RDWR);
            ::close(m_listenFd);
            m_listenFd = -1;
        }
        if (m_thread.joinable())
            m_thread.join();
        for (std::thread &worker : m_workers)
            worker.join();
        m_workers.clear();
        if (m_ctx) {
            SSL_CTX_free(m_ctx);
            m_ctx = nullptr;
        }
    }

    [[nodiscard]] std::uint16_t port() const { return m_port; }

    std::vector<std::string> requests()
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        return m_requests;
    }

private:
    void serve()
    {
        for (;;) {
            const int fd = ::accept4(m_listenFd, nullptr, nullptr, SOCK_CLOEXEC);
            if (fd < 0)
                return;
            m_workers.emplace_back([this, fd] { serveOne(fd); });
        }
    }

    void serveOne(int fd)
    {
        SSL *ssl = nullptr;
        if (m_ctx) {
            ssl = SSL_new(m_ctx);
            SSL_set_fd(ssl, fd);
            if (SSL_accept(ssl) != 1) {
                SSL_free(ssl);
                ::close(fd);
                return;
            }
        }
        const auto readSome = [&](char *buf, int cap) -> int {
            return ssl ? SSL_read(ssl, buf, cap) : static_cast<int>(::recv(fd, buf, cap, 0));
        };
        std::string request;
        std::ptrdiff_t expected = -1;
        char buf[4096];
        for (;;) {
            const std::size_t headerEnd = request.find("\r\n\r\n");
            if (headerEnd != std::string::npos) {
                if (expected < 0) {
                    expected = 0;
                    const std::size_t at = loweredText(request).find("content-length:");
                    if (at != std::string::npos && at < headerEnd) {
                        const std::size_t eol = request.find("\r\n", at);
                        expected = std::strtoll(request.c_str() + at + 15, nullptr, 10);
                        (void)eol;
                    }
                }
                if (request.size() >= headerEnd + 4 + static_cast<std::size_t>(expected))
                    break;
            }
            const int n = readSome(buf, sizeof(buf));
            if (n <= 0)
                break;
            request.append(buf, static_cast<std::size_t>(n));
        }
        {
            const std::lock_guard<std::mutex> lock(m_mutex);
            m_requests.push_back(request);
        }
        std::vector<std::string> pieces;
        if (m_pieces)
            pieces = m_pieces(request);
        else
            pieces.push_back(m_handler(request));
        for (std::size_t i = 0; i < pieces.size(); ++i) {
            const std::string &response = pieces[i];
            if (!response.empty()) {
                if (ssl)
                    SSL_write(ssl, response.data(), static_cast<int>(response.size()));
                else
                    ::send(fd, response.data(), response.size(), MSG_NOSIGNAL);
            }
            if (m_pieces && i + 1 < pieces.size())
                std::this_thread::sleep_for(std::chrono::milliseconds(m_pieceGapMs));
        }
        if (ssl) {
            SSL_shutdown(ssl);
            SSL_free(ssl);
        }
        ::close(fd);
    }

    Handler m_handler;
    PieceHandler m_pieces;
    int m_pieceGapMs = 0;
    int m_listenFd = -1;
    std::uint16_t m_port = 0;
    std::thread m_thread;
    std::vector<std::thread> m_workers;
    SSL_CTX *m_ctx = nullptr;
    std::mutex m_mutex;
    std::vector<std::string> m_requests;
};


} // namespace phitest
