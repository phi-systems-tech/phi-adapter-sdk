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
//
// Call, Result and StreamHandlers are behind d-pointers (see
// phi/adapter/v1/channel.h for the rule): Call has grown before and will again.

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "phi/adapter/net/http_types.h"
#include "phi/adapter/v1/tlsconfig.h"

namespace phi::runtime {
class Loop;
}

namespace phicore::adapter::net {

class HttpClient
{
public:
    /// One request: where, how, with what.
    class Call
    {
    public:
        Call();
        /// The common case: a GET of a URL.
        explicit Call(std::string url, std::string method = "GET");
        Call(const Call &other);
        Call(Call &&other) noexcept;
        Call &operator=(const Call &other);
        Call &operator=(Call &&other) noexcept;
        ~Call();

        const std::string &url() const noexcept;
        Call &setUrl(std::string url);
        const std::string &method() const noexcept;
        Call &setMethod(std::string method);
        const std::vector<Header> &headers() const noexcept;
        Call &setHeaders(std::vector<Header> headers);
        Call &addHeader(std::string name, std::string value);
        Call &addHeader(Header header);
        const std::string &body() const noexcept;
        Call &setBody(std::string body);
        /// Body and its Content-Type in one call.
        Call &setBody(std::string body, std::string contentType);
        const Credentials &credentials() const noexcept;
        Call &setCredentials(Credentials credentials);
        /// The whole exchange, connect included; 5 s by default.
        std::chrono::milliseconds timeout() const noexcept;
        Call &setTimeout(std::chrono::milliseconds timeout);
        const v1::TlsSettings &tls() const noexcept;
        v1::TlsSettings &tls();
        Call &setTls(v1::TlsSettings tls);
        /// The name the certificate is checked against when it is not the URL's
        /// host (a device dialled by IP whose certificate names it).
        const std::string &tlsServerName() const noexcept;
        Call &setTlsServerName(std::string serverName);

    private:
        struct Private;
        const Private &p() const noexcept;
        Private &m();
        std::unique_ptr<Private> d;
    };

    /// What came back, or why nothing did.
    class Result
    {
    public:
        Result();
        Result(const Result &other);
        Result(Result &&other) noexcept;
        Result &operator=(const Result &other);
        Result &operator=(Result &&other) noexcept;
        ~Result();

        /// A response arrived, whatever its status; false with `error` set otherwise.
        bool ok() const noexcept;
        Result &setOk(bool ok);
        int status() const noexcept;
        Result &setStatus(int status);
        const std::string &body() const noexcept;
        Result &setBody(std::string body);
        const std::vector<Header> &headers() const noexcept;
        Result &setHeaders(std::vector<Header> headers);
        /// The first header of that name, or empty.
        std::string header(std::string_view name) const;
        const std::string &error() const noexcept;
        Result &setError(std::string error);
        /// A 401 the client could not answer: no credentials, or wrong ones.
        bool unauthorized() const noexcept;
        Result &setUnauthorized(bool unauthorized);

    private:
        struct Private;
        const Private &p() const noexcept;
        Private &m();
        std::unique_ptr<Private> d;
    };

    using Done = std::function<void(Result)>;

    /// The pieces of a streamed response as they arrive: the head once, the
    /// body in chunks, and done with the Result's status and error.
    class StreamHandlers
    {
    public:
        using Head = std::function<void(int status, const std::vector<Header> &headers)>;
        using Chunk = std::function<void(std::string_view piece)>;

        StreamHandlers();
        /// All three at once; a null head or chunk is simply not called.
        StreamHandlers(Head head, Chunk chunk, Done done);
        StreamHandlers(const StreamHandlers &other);
        StreamHandlers(StreamHandlers &&other) noexcept;
        StreamHandlers &operator=(const StreamHandlers &other);
        StreamHandlers &operator=(StreamHandlers &&other) noexcept;
        ~StreamHandlers();

        const Head &head() const noexcept;
        StreamHandlers &setHead(Head head);
        const Chunk &chunk() const noexcept;
        StreamHandlers &setChunk(Chunk chunk);
        const Done &done() const noexcept;
        StreamHandlers &setDone(Done done);

    private:
        struct Private;
        const Private &p() const noexcept;
        Private &m();
        std::unique_ptr<Private> d;
    };

    explicit HttpClient(phi::runtime::Loop &loop);
    ~HttpClient();
    HttpClient(const HttpClient &) = delete;
    HttpClient &operator=(const HttpClient &) = delete;

    /// A request is in flight; send() and stream() refuse a second one.
    [[nodiscard]] bool busy() const;

    /// The whole response in one Result, on the loop.
    bool send(Call call, Done done);
    /// The response as it arrives; `done` still comes last, with no body.
    bool stream(Call call, StreamHandlers handlers);
    /// Drops the request in flight; its `done` is not called.
    void cancel();
    /// Forgets the challenge kept from the last 401, so the next request asks again.
    void forgetAuthentication();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace phicore::adapter::net
