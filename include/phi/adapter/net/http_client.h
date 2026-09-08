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

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "phi/adapter/net/http_auth.h"
#include "phi/adapter/net/http_message.h"

namespace phi::runtime {
class Loop;
}

namespace phicore::adapter::net {

class HttpClient
{
public:
    struct Call {
        /// `http://host[:port]/path`. `https://` is refused for now - see the
        /// note on Result::error - because nothing that has moved off Qt needs
        /// it yet and a half-built TLS is worse than a stated gap.
        std::string url;
        std::string method = "GET";
        std::vector<Header> headers;
        std::string body;
        /// Empty user and password means no authentication is attempted.
        Credentials credentials;
        /// Covers the whole call: resolve, connect, write, read, and the
        /// repeat after a challenge.
        std::chrono::milliseconds timeout{5000};
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

    /// Owned by, and used from, the thread whose loop this is.
    explicit HttpClient(phi::runtime::Loop &loop);
    ~HttpClient();

    HttpClient(const HttpClient &) = delete;
    HttpClient &operator=(const HttpClient &) = delete;

    [[nodiscard]] bool busy() const;

    /// False when a call is already in flight; `done` is then never called.
    /// The callback runs on the loop's thread and may start the next call.
    bool send(Call call, Done done);

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
