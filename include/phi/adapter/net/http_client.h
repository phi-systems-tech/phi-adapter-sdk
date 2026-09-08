#pragma once

// One HTTP request at a time, driven by a phi::runtime loop.
//
// The Qt-free replacement for what adapters did with QNetworkAccessManager and
// a nested QEventLoop. The nested loop is the part worth being rid of: it
// dispatches whatever is queued for the thread, so any adapter callback could
// run inside any other - a configuration change halfway through a poll, a
// write landing between a value being read and being reported, a teardown
// freeing the very reply the frame below it was waiting on. Nothing here
// blocks, so nothing re-enters.
//
// Authentication is handled rather than exposed: a 401 with a challenge this
// client can answer is answered and the request repeated, and the challenge is
// then kept, so the round trip happens once per connection to a device and not
// once per request.
//
// TLS takes the contract's own settings (`v1::TlsSettings`) and the decisions
// that must not drift - chain always checked, hostname check off is not accept
// anything - live in src/net_tls.h, shared with the blocking fetch().

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "phi/adapter/net/http_auth.h"
#include "phi/adapter/net/http_message.h"
#include "phi/adapter/v1/tlsconfig.h"

namespace phi::runtime {
class Loop;
}

namespace phicore::adapter::net {

class HttpClient
{
public:
    struct Call {
        /// `http://host[:port]/path` or `https://host[:port]/path`.
        std::string url;
        std::string method = "GET";
        std::vector<Header> headers;
        std::string body;
        /// Empty user and password means no authentication is attempted.
        Credentials credentials;
        /// Covers the whole call: resolve, connect, handshake, write, read,
        /// and the repeat after a challenge. For a stream it covers up to
        /// the response head; the stream itself has no deadline.
        std::chrono::milliseconds timeout{5000};
        /// For an https URL: a CA trusted in addition to the system store,
        /// and whether the certificate has to name the host. `enabled` is
        /// not read; the URL scheme already decided.
        v1::TlsSettings tls;
        /**
         * @brief The name the certificate has to carry, when it is not the
         * host in the URL.
         *
         * A Hue bridge is dialled by its IP address and certifies its bridge
         * id, signed by Signify's own root. Naming that id here keeps the
         * hostname check on - the alternative, turning the check off because
         * the URL host can never match, accepts any certificate that root
         * ever signed.
         */
        std::string tlsServerName;
    };

    struct Result {
        /// The exchange completed and the status is below 400 - the reading
        /// `QNetworkReply::error() == NoError` had.
        bool ok = false;
        int status = 0;
        std::string body;
        std::vector<Header> headers;
        /// Empty when `ok`. Says what failed, in words a log can carry.
        std::string error;
        /**
         * @brief A 401 that survived being answered.
         *
         * Separate from a plain failure because it means something different
         * to a person: the device was reached and said no. A caller reports
         * bad credentials for this and a network problem for the rest.
         */
        bool unauthorized = false;
    };

    using Done = std::function<void(Result)>;

    /**
     * @brief What a caller of stream() gets, and when.
     *
     * `head` once the status and headers are in, `chunk` for every piece of
     * body as it arrives - decoded, so a chunked transfer reads like a plain
     * one - and `done` when the stream ends, by the server closing it, by an
     * error, or by cancel(). `done`'s Result carries the status and headers
     * and an empty body; `ok` is false when it ended in an error.
     *
     * For a server-sent event stream, which is a GET that never finishes.
     */
    struct StreamHandlers {
        std::function<void(int status, const std::vector<Header> &headers)> head;
        std::function<void(std::string_view piece)> chunk;
        Done done;
    };

    /// Owned by, and used from, the thread whose loop this is.
    explicit HttpClient(phi::runtime::Loop &loop);
    ~HttpClient();

    HttpClient(const HttpClient &) = delete;
    HttpClient &operator=(const HttpClient &) = delete;

    [[nodiscard]] bool busy() const;

    /// False when a call is already in flight; `done` is then never called.
    /// The callback runs on the loop's thread and may start the next call.
    bool send(Call call, Done done);

    /// False when a call is already in flight. Authentication is not
    /// answered on a stream: a 401 ends it with `unauthorized` set.
    bool stream(Call call, StreamHandlers handlers);

    /// Drops whatever is in flight without calling `done`.
    void cancel();

    /// Forgets any cached challenge, so the next call re-authenticates. For a
    /// credential change: an answer built from the old password would come
    /// back 401 and read as bad credentials rather than as a stale challenge.
    void forgetAuthentication();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace phicore::adapter::net
