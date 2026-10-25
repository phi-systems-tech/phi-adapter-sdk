// The HTTP value types behind their d-pointers: Credentials, the client's Call,
// Result and StreamHandlers, the blocking Fetch and FetchResult. Same rules as
// entities.cpp.

#include "phi/adapter/net/http_client.h"
#include "phi/adapter/net/http_fetch.h"
#include "phi/adapter/net/http_types.h"

#include <algorithm>
#include <cctype>
#include <type_traits>
#include <utility>

namespace phicore::adapter::net {

namespace {

bool sameHeaderName(std::string_view a, std::string_view b)
{
    return a.size() == b.size()
        && std::equal(a.begin(), a.end(), b.begin(), [](unsigned char x, unsigned char y) {
               return std::tolower(x) == std::tolower(y);
           });
}

std::string firstHeader(const std::vector<Header> &headers, std::string_view name)
{
    for (const Header &header : headers)
        if (sameHeaderName(header.first, name))
            return header.second;
    return {};
}

} // namespace

#define PHI_NET_MEMBERS(Q, Class)                                                                          \
    Q::Class() : d(std::make_unique<Private>()) {}                                                      \
    Q::Class(const Q &other) : d(other.d ? std::make_unique<Private>(*other.d) : nullptr) {}            \
    Q::Class(Q &&other) noexcept : d(std::move(other.d)) {}                                             \
    Q &Q::operator=(const Q &other)                                                                     \
    {                                                                                                   \
        if (this != &other)                                                                             \
            d = other.d ? std::make_unique<Private>(*other.d) : nullptr;                                \
        return *this;                                                                                   \
    }                                                                                                   \
    Q &Q::operator=(Q &&other) noexcept                                                                 \
    {                                                                                                   \
        d = std::move(other.d);                                                                         \
        return *this;                                                                                   \
    }                                                                                                   \
    Q::~Class() = default;                                                                              \
    const Q::Private &Q::p() const noexcept                                                             \
    {                                                                                                   \
        static const Private empty;                                                                     \
        return d ? *d : empty;                                                                          \
    }                                                                                                   \
    Q::Private &Q::m()                                                                                  \
    {                                                                                                   \
        if (!d)                                                                                         \
            d = std::make_unique<Private>();                                                            \
        return *d;                                                                                      \
    }

#define PHI_NET_FIELD(Class, Type, name, Setter)                                                        \
    Type Class::name() const noexcept { return p().name; }                                              \
    Class &Class::Setter(std::remove_cvref_t<Type> value)                                               \
    {                                                                                                   \
        m().name = std::move(value);                                                                    \
        return *this;                                                                                   \
    }

// --- Credentials -------------------------------------------------------------

struct Credentials::Private {
    std::string user;
    std::string password;
};

PHI_NET_MEMBERS(Credentials, Credentials)
PHI_NET_FIELD(Credentials, const std::string &, user, setUser)
PHI_NET_FIELD(Credentials, const std::string &, password, setPassword)

Credentials::Credentials(std::string user, std::string password) : d(std::make_unique<Private>())
{
    d->user = std::move(user);
    d->password = std::move(password);
}

bool Credentials::empty() const noexcept { return p().user.empty() && p().password.empty(); }

// --- HttpClient::Call --------------------------------------------------------

struct HttpClient::Call::Private {
    std::string url;
    std::string method = "GET";
    std::vector<Header> headers;
    std::string body;
    Credentials credentials;
    std::chrono::milliseconds timeout{5000};
    v1::TlsSettings tls;
    std::string tlsServerName;
};

PHI_NET_MEMBERS(HttpClient::Call, Call)
PHI_NET_FIELD(HttpClient::Call, const std::string &, url, setUrl)
PHI_NET_FIELD(HttpClient::Call, const std::string &, method, setMethod)
PHI_NET_FIELD(HttpClient::Call, const std::vector<Header> &, headers, setHeaders)
PHI_NET_FIELD(HttpClient::Call, const Credentials &, credentials, setCredentials)
PHI_NET_FIELD(HttpClient::Call, std::chrono::milliseconds, timeout, setTimeout)
PHI_NET_FIELD(HttpClient::Call, const v1::TlsSettings &, tls, setTls)
PHI_NET_FIELD(HttpClient::Call, const std::string &, tlsServerName, setTlsServerName)

HttpClient::Call::Call(std::string url, std::string method) : d(std::make_unique<Private>())
{
    d->url = std::move(url);
    d->method = std::move(method);
}

const std::string &HttpClient::Call::body() const noexcept { return p().body; }

HttpClient::Call &HttpClient::Call::setBody(std::string body)
{
    m().body = std::move(body);
    return *this;
}

HttpClient::Call &HttpClient::Call::setBody(std::string body, std::string contentType)
{
    Private &priv = m();
    priv.body = std::move(body);
    std::erase_if(priv.headers, [](const Header &h) { return sameHeaderName(h.first, "Content-Type"); });
    priv.headers.emplace_back("Content-Type", std::move(contentType));
    return *this;
}

HttpClient::Call &HttpClient::Call::addHeader(std::string name, std::string value)
{
    m().headers.emplace_back(std::move(name), std::move(value));
    return *this;
}

HttpClient::Call &HttpClient::Call::addHeader(Header header)
{
    m().headers.push_back(std::move(header));
    return *this;
}

v1::TlsSettings &HttpClient::Call::tls() { return m().tls; }

// --- HttpClient::Result ------------------------------------------------------

struct HttpClient::Result::Private {
    bool ok = false;
    int status = 0;
    std::string body;
    std::vector<Header> headers;
    std::string error;
    bool unauthorized = false;
};

PHI_NET_MEMBERS(HttpClient::Result, Result)
PHI_NET_FIELD(HttpClient::Result, bool, ok, setOk)
PHI_NET_FIELD(HttpClient::Result, int, status, setStatus)
PHI_NET_FIELD(HttpClient::Result, const std::string &, body, setBody)
PHI_NET_FIELD(HttpClient::Result, const std::vector<Header> &, headers, setHeaders)
PHI_NET_FIELD(HttpClient::Result, const std::string &, error, setError)
PHI_NET_FIELD(HttpClient::Result, bool, unauthorized, setUnauthorized)

std::string HttpClient::Result::header(std::string_view name) const { return firstHeader(p().headers, name); }

// --- HttpClient::StreamHandlers ----------------------------------------------

struct HttpClient::StreamHandlers::Private {
    Head head;
    Chunk chunk;
    Done done;
};

PHI_NET_MEMBERS(HttpClient::StreamHandlers, StreamHandlers)
PHI_NET_FIELD(HttpClient::StreamHandlers, const HttpClient::StreamHandlers::Head &, head, setHead)
PHI_NET_FIELD(HttpClient::StreamHandlers, const HttpClient::StreamHandlers::Chunk &, chunk, setChunk)
PHI_NET_FIELD(HttpClient::StreamHandlers, const HttpClient::Done &, done, setDone)

HttpClient::StreamHandlers::StreamHandlers(Head head, Chunk chunk, Done done) : d(std::make_unique<Private>())
{
    d->head = std::move(head);
    d->chunk = std::move(chunk);
    d->done = std::move(done);
}

// --- Fetch -------------------------------------------------------------------

struct Fetch::Private {
    std::string url;
    std::string method = "GET";
    std::vector<Header> headers;
    std::string body;
    std::chrono::milliseconds timeout{10000};
    int maxRedirects = 5;
    v1::TlsSettings tls;
};

PHI_NET_MEMBERS(Fetch, Fetch)
PHI_NET_FIELD(Fetch, const std::string &, url, setUrl)
PHI_NET_FIELD(Fetch, const std::string &, method, setMethod)
PHI_NET_FIELD(Fetch, const std::vector<Header> &, headers, setHeaders)
PHI_NET_FIELD(Fetch, const std::string &, body, setBody)
PHI_NET_FIELD(Fetch, std::chrono::milliseconds, timeout, setTimeout)
PHI_NET_FIELD(Fetch, int, maxRedirects, setMaxRedirects)
PHI_NET_FIELD(Fetch, const v1::TlsSettings &, tls, setTls)

Fetch::Fetch(std::string url, std::string method) : d(std::make_unique<Private>())
{
    d->url = std::move(url);
    d->method = std::move(method);
}

Fetch &Fetch::addHeader(std::string name, std::string value)
{
    m().headers.emplace_back(std::move(name), std::move(value));
    return *this;
}

Fetch &Fetch::addHeader(Header header)
{
    m().headers.push_back(std::move(header));
    return *this;
}

v1::TlsSettings &Fetch::tls() { return m().tls; }

// --- FetchResult -------------------------------------------------------------

struct FetchResult::Private {
    bool ok = false;
    int status = 0;
    std::string body;
    std::vector<Header> headers;
    std::string error;
};

PHI_NET_MEMBERS(FetchResult, FetchResult)
PHI_NET_FIELD(FetchResult, bool, ok, setOk)
PHI_NET_FIELD(FetchResult, int, status, setStatus)
PHI_NET_FIELD(FetchResult, const std::string &, body, setBody)
PHI_NET_FIELD(FetchResult, const std::vector<Header> &, headers, setHeaders)
PHI_NET_FIELD(FetchResult, const std::string &, error, setError)

std::string FetchResult::header(std::string_view name) const { return firstHeader(p().headers, name); }

#undef PHI_NET_FIELD
#undef PHI_NET_MEMBERS

} // namespace phicore::adapter::net
