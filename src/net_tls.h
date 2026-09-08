#pragma once

// The client side of TLS, in one place.
//
// Not because the code is long - it is a context, a session and a handshake -
// but because the decisions inside it are the ones worth not making twice:
// which minimum version, what a missing close_notify means, and the one that
// actually matters, that turning off the hostname check leaves the chain check
// on. A second copy of this would eventually disagree with the first about
// that, and the disagreement would look like nothing from the outside.
//
// A blocking caller and a loop-driven one differ in how they wait, not in how
// they verify. So what is shared here is the context and the session; each
// caller drives the handshake with its own idea of waiting.

#include <string>

#include <openssl/ssl.h>

#include "phi/adapter/v1/tlsconfig.h"

namespace phicore::adapter::net::detail {

/// The last OpenSSL error, prefixed by `what`; `what` alone when the queue is
/// empty. Always clears the queue, so the next failure is not read as this one.
std::string opensslText(const std::string &what);

/// A verifying client context, or null with `error` set. `tls.enabled` is not
/// read: the caller has already decided to speak TLS.
SSL_CTX *makeClientContext(const v1::TlsSettings &tls, std::string *error);

/// SNI, and - when the hostname is checked - the name the certificate has to
/// carry. The chain is verified either way.
bool prepareSession(SSL *ssl, const std::string &host, bool verifyHostname, std::string *error);

/// True when OpenSSL only wants the socket again; `forRead` then says which
/// way to wait.
bool wantsIo(int reason, bool *forRead);

/// Why an SSL call failed, in words a log can carry. Names a rejected
/// certificate as such rather than as a protocol error, because that is the
/// one failure an operator can do something about.
std::string sessionError(SSL *ssl, int reason, const std::string &what);

} // namespace phicore::adapter::net::detail
