#pragma once
// The blocking HTTP exchange, for a thread of its own: a probe at adoption, a
// download that may take a while, a test. On an adapter's loop thread the
// HttpClient is the one to use; this one waits.
//
// Fetch and FetchResult are behind d-pointers (see phi/adapter/v1/channel.h).

#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "phi/adapter/net/http_types.h"
#include "phi/adapter/v1/tlsconfig.h"

namespace phicore::adapter::net {

class Fetch
{
public:
    Fetch();
    explicit Fetch(std::string url, std::string method = "GET");
    Fetch(const Fetch &other);
    Fetch(Fetch &&other) noexcept;
    Fetch &operator=(const Fetch &other);
    Fetch &operator=(Fetch &&other) noexcept;
    ~Fetch();

    const std::string &url() const noexcept;
    Fetch &setUrl(std::string url);
    const std::string &method() const noexcept;
    Fetch &setMethod(std::string method);
    const std::vector<Header> &headers() const noexcept;
    Fetch &setHeaders(std::vector<Header> headers);
    Fetch &addHeader(std::string name, std::string value);
    Fetch &addHeader(Header header);
    const std::string &body() const noexcept;
    Fetch &setBody(std::string body);
    /// The whole exchange, redirects included; 10 s by default.
    std::chrono::milliseconds timeout() const noexcept;
    Fetch &setTimeout(std::chrono::milliseconds timeout);
    /// Redirects followed before giving up; 5 by default, 0 follows none.
    int maxRedirects() const noexcept;
    Fetch &setMaxRedirects(int maxRedirects);
    const v1::TlsSettings &tls() const noexcept;
    v1::TlsSettings &tls();
    Fetch &setTls(v1::TlsSettings tls);

private:
    struct Private;
    const Private &p() const noexcept;
    Private &m();
    std::unique_ptr<Private> d;
};

class FetchResult
{
public:
    FetchResult();
    FetchResult(const FetchResult &other);
    FetchResult(FetchResult &&other) noexcept;
    FetchResult &operator=(const FetchResult &other);
    FetchResult &operator=(FetchResult &&other) noexcept;
    ~FetchResult();

    /// A response arrived, whatever its status; false with `error` set otherwise.
    bool ok() const noexcept;
    FetchResult &setOk(bool ok);
    int status() const noexcept;
    FetchResult &setStatus(int status);
    const std::string &body() const noexcept;
    FetchResult &setBody(std::string body);
    const std::vector<Header> &headers() const noexcept;
    FetchResult &setHeaders(std::vector<Header> headers);
    /// The first header of that name, or empty.
    std::string header(std::string_view name) const;
    const std::string &error() const noexcept;
    FetchResult &setError(std::string error);

private:
    struct Private;
    const Private &p() const noexcept;
    Private &m();
    std::unique_ptr<Private> d;
};

[[nodiscard]] FetchResult fetch(const Fetch &request);

} // namespace phicore::adapter::net
