#pragma once

// HTTP authentication, as the devices adapters talk to actually ask for it.
//
// A FRITZ!Box answers an unauthenticated TR-064 call with
//
//   WWW-Authenticate: Digest realm="HTTPS Access",nonce="88B30A35B27FDC00",
//                     algorithm=MD5,qop="auth"
//
// which QNetworkAccessManager used to answer without anyone writing a line of
// it. Everything here is pure - a header string in, a header string out - so
// the arithmetic can be pinned against the worked examples in RFC 7616 and
// RFC 2617 rather than against itself.

#include <cstdint>
#include <string>
#include <string_view>

namespace phicore::adapter::net {

/// What a server said it wants in `WWW-Authenticate`.
struct Challenge {
    enum class Scheme {
        None,   ///< nothing recognisable
        Basic,
        Digest,
    };

    Scheme scheme = Scheme::None;
    std::string realm;
    std::string nonce;
    std::string opaque;
    /// `MD5` or `MD5-sess`; empty means MD5, which is what the RFC says.
    std::string algorithm;
    /// The quality of protection this client picked, or empty for the older
    /// RFC 2069 form. `auth-int` is not offered: it hashes the body, and no
    /// device an adapter talks to has ever asked for it.
    std::string qop;
    /// The server rejected the last response but the credentials may still be
    /// good - the nonce simply expired. A caller retries rather than reporting
    /// bad credentials.
    bool stale = false;

    [[nodiscard]] bool usable() const
    {
        return scheme == Scheme::Basic || (scheme == Scheme::Digest && !nonce.empty());
    }
};

struct Credentials {
    std::string user;
    std::string password;
};

/**
 * @brief Reads one `WWW-Authenticate` header value.
 *
 * Digest wins when a server offers both, because Basic puts the password on
 * the wire. Commas inside quoted values do not split parameters, which is not
 * a nicety: `qop="auth,auth-int"` is the common way to offer both.
 */
Challenge parseChallenge(std::string_view header);

/**
 * @brief The `Authorization` value answering `challenge`, or empty.
 *
 * @param uri         Path and query exactly as it goes in the request line -
 *                    the digest covers it, so a rewritten path fails to
 *                    authenticate rather than fails to route.
 * @param nonceCount  How many times this nonce has been used, from 1. Reusing
 *                    a count is what a replay looks like to a server that
 *                    checks.
 * @param cnonce      The client nonce. Taken as a parameter rather than
 *                    generated inside, so a test can reproduce the RFC's own
 *                    example byte for byte.
 */
std::string buildAuthorization(const Challenge &challenge,
                               const Credentials &credentials,
                               std::string_view method,
                               std::string_view uri,
                               std::uint32_t nonceCount,
                               std::string_view cnonce);

/// A fresh client nonce: 16 hex characters from the system's random source.
std::string makeClientNonce();

/// Lowercase hex MD5 of `data`. Exposed because the digest is defined in terms
/// of it and a test that cannot check the hash cannot check the digest.
std::string md5Hex(std::string_view data);

} // namespace phicore::adapter::net
