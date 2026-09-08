#include "net_tls.h"

#include <cerrno>
#include <cstring>

#include <openssl/err.h>
#include <openssl/x509v3.h>

namespace phicore::adapter::net::detail {

std::string opensslText(const std::string &what)
{
    char buffer[256] = {};
    const unsigned long code = ERR_get_error();
    ERR_clear_error();
    if (code == 0)
        return what;
    ERR_error_string_n(code, buffer, sizeof(buffer));
    return what + ": " + buffer;
}

SSL_CTX *makeClientContext(const v1::TlsSettings &tls, std::string *error)
{
    SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) {
        *error = opensslText("TLS context");
        return nullptr;
    }
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    // A server that closes without close_notify ends the stream instead of
    // failing it; the Content-Length and chunk framing decide completeness.
    SSL_CTX_set_options(ctx, SSL_OP_IGNORE_UNEXPECTED_EOF);
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, nullptr);

    // The named certificate is trusted *in addition* to the system store, so
    // an endpoint with a private authority does not cost the operator every
    // public one.
    bool loaded = SSL_CTX_set_default_verify_paths(ctx) == 1;
    if (!tls.caFile.empty())
        loaded = SSL_CTX_load_verify_locations(ctx, tls.caFile.c_str(), nullptr) == 1;
    if (!loaded) {
        *error = opensslText("TLS trust store");
        SSL_CTX_free(ctx);
        return nullptr;
    }
    return ctx;
}

bool prepareSession(SSL *ssl, const std::string &host, bool verifyHostname, std::string *error)
{
    // SNI names the host either way: it is what the server needs to pick a
    // certificate, not part of checking one.
    SSL_set_tlsext_host_name(ssl, host.c_str());
    if (verifyHostname) {
        if (SSL_set1_host(ssl, host.c_str()) != 1) {
            *error = opensslText("TLS hostname");
            return false;
        }
        SSL_set_hostflags(ssl, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
    }
    return true;
}

bool wantsIo(int reason, bool *forRead)
{
    if (reason != SSL_ERROR_WANT_READ && reason != SSL_ERROR_WANT_WRITE)
        return false;
    if (forRead)
        *forRead = reason == SSL_ERROR_WANT_READ;
    return true;
}

std::string sessionError(SSL *ssl, int reason, const std::string &what)
{
    if (reason == SSL_ERROR_SSL) {
        const long verify = SSL_get_verify_result(ssl);
        if (verify != X509_V_OK) {
            ERR_clear_error();
            return std::string("certificate verification failed: ")
                + X509_verify_cert_error_string(verify);
        }
    }
    if (reason == SSL_ERROR_SYSCALL && errno != 0)
        return what + " failed: " + std::strerror(errno);
    return opensslText(what + " failed");
}

} // namespace phicore::adapter::net::detail
