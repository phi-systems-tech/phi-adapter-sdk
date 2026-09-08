#pragma once

// A whole HTTP exchange on the calling thread, from connect to last byte.
//
// The other half of this directory, and the counterpart to HttpClient. That
// one never blocks, because an adapter's thread also carries its timers, its
// socket and every callback phi-core sends it. This one blocks, because its
// caller is a thread that exists for one request and has nothing else to do
// until it is answered: phi-core's translation fetches, and a factory probe,
// which is somebody looking at a form.
//
// What it has and the loop client does not is what a caller on the open
// internet needs and a caller on the LAN does not: TLS, and redirects. What it
// does not have is authentication - the endpoints that ask for a digest are on
// the LAN, and are reached through the loop client.
//
// The framing is not written twice. The request comes out of
// serializeRequest() and the answer goes through ResponseParser, the same two
// the loop client uses, so a body split across three reads, a chunked
// transfer and a head that never ends mean the same thing on both paths.

#include <chrono>
#include <string>
#include <vector>

#include "phi/adapter/net/http_message.h"
#include "phi/adapter/v1/tlsconfig.h"

namespace phicore::adapter::net {

struct Fetch {
    /// `http://host[:port]/path` or `https://...`.
    std::string url;
    std::string method = "GET";
    std::vector<Header> headers;
    std::string body;
    /// Covers the whole call: resolve, connect, handshake, write, read, and
    /// every redirect after it. One deadline, so a chain of slow hops cannot
    /// add up past what the caller allowed.
    std::chrono::milliseconds timeout{10000};
    /**
     * @brief How many redirects to follow; 0 follows none.
     *
     * 307 and 308 are re-sent as they are, 301/302/303 become a GET without a
     * body - what every client has done since long before it was written down.
     * A redirect past the limit is a failure rather than an answer: the empty
     * body of a 302 is not the endpoint's reply, and a caller that read it as
     * one would find nothing wrong with it.
     */
    int maxRedirects = 5;
    /// `caFile` and `verifyHostname` apply when the URL is https. `enabled` is
    /// not read here - the scheme has already said.
    v1::TlsSettings tls;
};

struct FetchResult {
    /// The exchange completed and the status is below 400.
    bool ok = false;
    int status = 0;
    /// Present for a failing status too: an endpoint says why in the body.
    std::string body;
    std::vector<Header> headers;
    /// Empty when `ok`. Says what failed, in words a log can carry.
    std::string error;
};

[[nodiscard]] FetchResult fetch(const Fetch &request);

} // namespace phicore::adapter::net
