// The HTTP client against a server that demands what a FRITZ!Box demands.
//
// The stub recomputes the digest itself rather than checking that a header is
// present: a client that sends a well-formed Authorization with the wrong hash
// passes the second kind of test and fails against every real device.

#include <phi/adapter/testing/check.h>

#include "phi/adapter/net/http_client.h"
#include "phi/runtime/epollloop.h"

#include "tls_test_server.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using namespace phicore::adapter::net;
using namespace std::chrono_literals;

namespace {

constexpr const char kUser[] = "fritzuser";
constexpr const char kPassword[] = "s3cr3t pass";
constexpr const char kRealm[] = "HTTPS Access";
constexpr const char kNonce[] = "88B30A35B27FDC00";

std::string headerValue(const std::string &head, const std::string &name)
{
    std::size_t pos = 0;
    while (pos < head.size()) {
        const std::size_t eol = head.find("\r\n", pos);
        if (eol == std::string::npos)
            break;
        const std::string line = head.substr(pos, eol - pos);
        if (line.size() > name.size() && line.compare(0, name.size(), name) == 0
            && line[name.size()] == ':') {
            std::size_t v = name.size() + 1;
            while (v < line.size() && line[v] == ' ')
                ++v;
            return line.substr(v);
        }
        pos = eol + 2;
    }
    return {};
}

std::string quotedParam(const std::string &header, const std::string &key)
{
    const std::size_t at = header.find(key + "=\"");
    if (at == std::string::npos)
        return {};
    const std::size_t start = at + key.size() + 2;
    const std::size_t end = header.find('"', start);
    return end == std::string::npos ? std::string() : header.substr(start, end - start);
}

std::string bareParam(const std::string &header, const std::string &key)
{
    const std::size_t at = header.find(key + "=");
    if (at == std::string::npos)
        return {};
    const std::size_t start = at + key.size() + 1;
    std::size_t end = start;
    while (end < header.size() && header[end] != ',' && header[end] != ' ')
        ++end;
    return header.substr(start, end - start);
}

class StubServer
{
public:
    enum class Manner {
        Plain,          ///< 200, no authentication
        DemandsDigest,  ///< 401 with a challenge, then verifies the answer
        AlwaysRejects,  ///< 401 whatever is sent
        Chunked,        ///< 200 with a chunked body
        Silent,         ///< accepts the connection and never answers
    };

    explicit StubServer(Manner manner) : m_manner(manner)
    {
        m_listen = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        const int one = 1;
        ::setsockopt(m_listen, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        m_ok = ::bind(m_listen, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0
            && ::listen(m_listen, 8) == 0;
        socklen_t length = sizeof(addr);
        if (m_ok && ::getsockname(m_listen, reinterpret_cast<sockaddr *>(&addr), &length) == 0)
            m_port = ::ntohs(addr.sin_port);
        m_thread = std::thread([this]() { serve(); });
    }

    ~StubServer()
    {
        m_done.store(true);
        ::shutdown(m_listen, SHUT_RDWR);
        ::close(m_listen);
        if (m_thread.joinable())
            m_thread.join();
    }

    bool ok() const { return m_ok; }
    std::uint16_t port() const { return m_port; }
    /// Every request that reached the server, challenges included.
    int requests() const { return m_requests.load(); }
    /// Requests that carried an Authorization the server could verify.
    int authenticated() const { return m_authenticated.load(); }
    std::string lastBody() const { return m_lastBody; }
    std::string lastNonceCount() const { return m_lastNc; }

private:
    /// The digest a correct client must produce, computed here independently.
    static std::string expectedResponse(const std::string &method, const std::string &uri,
                                        const std::string &nc, const std::string &cnonce)
    {
        const std::string ha1 =
            md5Hex(std::string(kUser) + ":" + kRealm + ":" + kPassword);
        const std::string ha2 = md5Hex(method + ":" + uri);
        return md5Hex(ha1 + ":" + kNonce + ":" + nc + ":" + cnonce + ":auth:" + ha2);
    }

    void serve()
    {
        while (!m_done.load()) {
            const int client = ::accept(m_listen, nullptr, nullptr);
            if (client < 0)
                return;

            std::string request;
            std::size_t headEnd = std::string::npos;
            while (!m_done.load()) {
                char chunk[2048];
                const ssize_t got = ::recv(client, chunk, sizeof(chunk), 0);
                if (got <= 0)
                    break;
                request.append(chunk, static_cast<std::size_t>(got));
                headEnd = request.find("\r\n\r\n");
                if (headEnd == std::string::npos)
                    continue;
                const std::string lengthText =
                    headerValue(request.substr(0, headEnd + 2), "Content-Length");
                const std::size_t expected =
                    lengthText.empty() ? 0 : static_cast<std::size_t>(std::stoul(lengthText));
                if (request.size() >= headEnd + 4 + expected)
                    break;
            }
            if (headEnd == std::string::npos) {
                ::close(client);
                continue;
            }

            ++m_requests;
            const std::string head = request.substr(0, headEnd + 2);
            m_lastBody = request.substr(headEnd + 4);
            const std::size_t lineEnd = head.find("\r\n");
            const std::string requestLine = head.substr(0, lineEnd);
            const std::size_t firstSpace = requestLine.find(' ');
            const std::size_t secondSpace = requestLine.find(' ', firstSpace + 1);
            const std::string method = requestLine.substr(0, firstSpace);
            const std::string uri =
                requestLine.substr(firstSpace + 1, secondSpace - firstSpace - 1);

            respond(client, head, method, uri);
            ::close(client);
        }
    }

    void respond(int client, const std::string &head, const std::string &method,
                 const std::string &uri)
    {
        const auto write = [client](const std::string &text) {
            ::send(client, text.data(), text.size(), MSG_NOSIGNAL);
        };

        if (m_manner == Manner::Silent) {
            std::this_thread::sleep_for(3000ms);
            return;
        }
        if (m_manner == Manner::Plain) {
            write("HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok");
            return;
        }
        if (m_manner == Manner::Chunked) {
            write("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n"
                  "5\r\nhello\r\n6\r\n world\r\n0\r\n\r\n");
            return;
        }

        const std::string challenge =
            std::string("HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: Digest realm=\"")
            + kRealm + "\",nonce=\"" + kNonce + "\",algorithm=MD5,qop=\"auth\"\r\n"
            + "Content-Length: 0\r\nConnection: close\r\n\r\n";

        if (m_manner == Manner::AlwaysRejects) {
            write(challenge);
            return;
        }

        const std::string authorization = headerValue(head, "Authorization");
        if (authorization.empty()) {
            write(challenge);
            return;
        }

        const std::string nc = bareParam(authorization, "nc");
        const std::string cnonce = quotedParam(authorization, "cnonce");
        const std::string response = quotedParam(authorization, "response");
        m_lastNc = nc;
        if (quotedParam(authorization, "uri") != uri
            || response != expectedResponse(method, uri, nc, cnonce)) {
            write(challenge);
            return;
        }
        ++m_authenticated;
        write("HTTP/1.1 200 OK\r\nContent-Length: 6\r\nConnection: close\r\n\r\nsecret");
    }

    Manner m_manner;
    int m_listen = -1;
    bool m_ok = false;
    std::uint16_t m_port = 0;
    std::atomic_bool m_done{false};
    std::atomic_int m_requests{0};
    std::atomic_int m_authenticated{0};
    std::string m_lastBody;
    std::string m_lastNc;
    std::thread m_thread;
};

/// Runs calls to completion on a real loop and answers with their results.
std::vector<HttpClient::Result> run(const std::vector<HttpClient::Call> &calls,
                                    std::chrono::milliseconds budget = 10000ms)
{
    phi::runtime::EpollLoop loop;
    HttpClient client(loop);
    std::vector<HttpClient::Result> results;
    std::size_t index = 0;

    std::function<void()> next = [&]() {
        if (index >= calls.size()) {
            loop.stop();
            return;
        }
        const HttpClient::Call call = calls[index++];
        if (!client.send(call, [&](HttpClient::Result result) {
                results.push_back(std::move(result));
                next();
            })) {
            loop.stop();
        }
    };
    next();

    phi::runtime::Timer watchdog = loop.timerAfter(budget, [&loop]() { loop.stop(); });
    loop.run();
    return results;
}

HttpClient::Call callTo(std::uint16_t port, const char *path = "/upnp/control/deviceinfo")
{
    HttpClient::Call call;
    call.url = "http://127.0.0.1:" + std::to_string(port) + path;
    call.method = "POST";
    call.body = "<x/>";
    call.credentials = {kUser, kPassword};
    call.timeout = 4000ms;
    return call;
}

void testAPlainRequest()
{
    StubServer server(StubServer::Manner::Plain);
    PHI_CHECK(server.ok());
    const auto results = run({callTo(server.port())});
    PHI_CHECK(results.size() == 1);
    if (results.empty())
        return;
    PHI_CHECK_MSG(results[0].ok, "%s", results[0].error.c_str());
    PHI_CHECK(results[0].status == 200);
    PHI_CHECK(results[0].body == "ok");
    PHI_CHECK(server.lastBody() == "<x/>");
}

void testTheChallengeIsAnsweredAndThenNotAskedForAgain()
{
    StubServer server(StubServer::Manner::DemandsDigest);
    PHI_CHECK(server.ok());

    const auto results = run({callTo(server.port()), callTo(server.port())});
    PHI_CHECK_MSG(results.size() == 2, "%d calls finished", int(results.size()));
    if (results.size() != 2)
        return;

    for (const HttpClient::Result &result : results) {
        PHI_CHECK_MSG(result.ok, "%s", result.error.c_str());
        PHI_CHECK(result.status == 200);
        PHI_CHECK(result.body == "secret");
        PHI_CHECK(!result.unauthorized);
    }

    // Three requests for two calls: the first was challenged, the second went
    // out already authenticated. A client that forgets the challenge doubles
    // every request a device ever gets.
    PHI_CHECK_MSG(server.requests() == 3, "the server saw %d requests, expected 3",
                  server.requests());
    PHI_CHECK(server.authenticated() == 2);
    // And the count moved, which is what stops the second one being a replay.
    PHI_CHECK_MSG(server.lastNonceCount() == "00000002", "nc was '%s'",
                  server.lastNonceCount().c_str());
}

void testAPasswordThatIsWrongSaysSo()
{
    StubServer server(StubServer::Manner::AlwaysRejects);
    PHI_CHECK(server.ok());

    const auto results = run({callTo(server.port())});
    PHI_CHECK(results.size() == 1);
    if (results.empty())
        return;
    PHI_CHECK(!results[0].ok);
    PHI_CHECK(results[0].status == 401);
    // Reached and refused is a different thing from unreachable, and it sends
    // the user somewhere else.
    PHI_CHECK_MSG(results[0].unauthorized, "a refusal did not read as one");
    PHI_CHECK(results[0].error == "Invalid credentials");
    // Answered once, not forever.
    PHI_CHECK_MSG(server.requests() == 2, "the server saw %d requests, expected 2",
                  server.requests());
}

void testAChunkedBody()
{
    StubServer server(StubServer::Manner::Chunked);
    PHI_CHECK(server.ok());
    const auto results = run({callTo(server.port())});
    PHI_CHECK(results.size() == 1);
    if (!results.empty())
        PHI_CHECK_MSG(results[0].body == "hello world", "got '%s'", results[0].body.c_str());
}

void testASilentServerCostsItsTimeoutAndNoMore()
{
    StubServer server(StubServer::Manner::Silent);
    PHI_CHECK(server.ok());

    HttpClient::Call call = callTo(server.port());
    call.timeout = 300ms;

    const auto before = std::chrono::steady_clock::now();
    const auto results = run({call});
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - before);

    PHI_CHECK(results.size() == 1);
    if (results.empty())
        return;
    PHI_CHECK(!results[0].ok);
    PHI_CHECK(results[0].error == "Request timed out");
    PHI_CHECK_MSG(elapsed < 1500ms, "a silent server cost %lldms",
                  static_cast<long long>(elapsed.count()));
}

void testNothingListening()
{
    std::uint16_t deadPort = 0;
    {
        StubServer probe(StubServer::Manner::Plain);
        deadPort = probe.port();
    }
    const auto results = run({callTo(deadPort)});
    PHI_CHECK(results.size() == 1);
    if (results.empty())
        return;
    PHI_CHECK(!results[0].ok);
    PHI_CHECK(!results[0].unauthorized);
    PHI_CHECK(!results[0].error.empty());
}

void testWhatIsRefusedOutright()
{
    phi::runtime::EpollLoop loop;
    HttpClient client(loop);
    // A URL this client cannot address is refused before anything is opened,
    // and the callback is not called for it.
    PHI_CHECK(!client.send({.url = "ftp://host/x"}, [](HttpClient::Result) {}));
    PHI_CHECK(!client.busy());
}


// --- TLS and streaming -------------------------------------------------------

/// A PEM to trust, written once for the process and removed at exit.
struct CertificateFixture {
    phitest::TestCertificate certificate;
    std::string pemPath;
    bool ok = false;

    CertificateFixture()
    {
        char dir[] = "/tmp/phi-sdk-tls-XXXXXX";
        if (::mkdtemp(dir) == nullptr)
            return;
        pemPath = std::string(dir) + "/ca.pem";
        ok = certificate.create(pemPath);
    }
    ~CertificateFixture()
    {
        if (!pemPath.empty()) {
            ::unlink(pemPath.c_str());
            ::rmdir(pemPath.substr(0, pemPath.rfind('/')).c_str());
        }
    }
};

HttpClient::Result runOne(HttpClient::Call call)
{
    phi::runtime::EpollLoop loop;
    HttpClient client(loop);
    std::optional<HttpClient::Result> result;
    client.send(std::move(call), [&](HttpClient::Result r) {
        result = std::move(r);
        loop.stop();
    });
    phi::runtime::Timer watchdog = loop.timerAfter(6s, [&loop]() { loop.stop(); });
    loop.run();
    return result.value_or(HttpClient::Result{.error = "no result"});
}

std::string okResponse(const std::string &body)
{
    return "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(body.size())
        + "\r\nConnection: close\r\n\r\n" + body;
}

/// A certificate for `localhost` is accepted when the bundle names its
/// issuer, refused when the name does not match, refused by the system store,
/// and - the case the Hue bridge needs - accepted for an address that is not
/// its name when the caller says which name to expect.
void testTlsIsVerified()
{
    CertificateFixture fixture;
    PHI_CHECK(fixture.ok);
    if (!fixture.ok)
        return;
    phitest::TestServer server;
    PHI_CHECK(server.start([](const std::string &) { return okResponse("secure"); },
                           &fixture.certificate));

    HttpClient::Call byName;
    byName.url = "https://localhost:" + std::to_string(server.port()) + "/x";
    byName.tls.caFile = fixture.pemPath;
    byName.timeout = 4000ms;
    HttpClient::Result result = runOne(byName);
    PHI_CHECK_MSG(result.ok, "%s", result.error.c_str());
    PHI_CHECK(result.body == "secure");

    // Dialled by address: the certificate names localhost, not 127.0.0.1.
    HttpClient::Call byAddress = byName;
    byAddress.url = "https://127.0.0.1:" + std::to_string(server.port()) + "/x";
    result = runOne(byAddress);
    PHI_CHECK_MSG(!result.ok, "a certificate for another name was accepted");
    PHI_CHECK_MSG(result.error.find("certificate") != std::string::npos, "error was: %s",
                  result.error.c_str());

    // ...unless the caller says which name the certificate has to carry.
    HttpClient::Call expectingName = byAddress;
    expectingName.tlsServerName = "localhost";
    result = runOne(expectingName);
    PHI_CHECK_MSG(result.ok, "%s", result.error.c_str());

    // The wrong expected name is still wrong.
    HttpClient::Call wrongName = byAddress;
    wrongName.tlsServerName = "bridge-0123";
    result = runOne(wrongName);
    PHI_CHECK(!result.ok);

    // Nothing trusts a self-signed certificate on its own.
    HttpClient::Call systemStore = byName;
    systemStore.tls.caFile.clear();
    result = runOne(systemStore);
    PHI_CHECK_MSG(!result.ok, "a self-signed certificate was accepted by the system store");

    // Off is not accept anything: with no bundle the chain still fails.
    HttpClient::Call noCheckNoBundle = byAddress;
    noCheckNoBundle.tls.caFile.clear();
    noCheckNoBundle.tls.verifyHostname = false;
    result = runOne(noCheckNoBundle);
    PHI_CHECK(!result.ok);
}

/// Pieces arrive as they are sent, decoded; the end of the stream ends it.
void testAStreamDeliversAsItGoes()
{
    phitest::TestServer server;
    PHI_CHECK(server.start(
        [](const std::string &) {
            return std::vector<std::string>{
                "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n"
                "Transfer-Encoding: chunked\r\n\r\n"
                "b\r\ndata: one\n\n\r\n",
                "b\r\ndata: two\n\n\r\n",
                "5\r\ndata:\r\n",
                "8\r\n three\n\n\r\n0\r\n\r\n",
            };
        },
        60));

    phi::runtime::EpollLoop loop;
    HttpClient client(loop);
    int headStatus = 0;
    std::vector<std::string> pieces;
    std::vector<long long> pieceAt;
    std::optional<HttpClient::Result> ended;
    const auto started = std::chrono::steady_clock::now();
    HttpClient::Call call;
    call.url = "http://127.0.0.1:" + std::to_string(server.port()) + "/eventstream";
    call.headers.emplace_back("Accept", "text/event-stream");
    call.timeout = 300ms; // covers the head only
    PHI_CHECK(client.stream(call, {
        .head = [&](int status, const std::vector<Header> &) { headStatus = status; },
        .chunk = [&](std::string_view piece) {
            pieces.emplace_back(piece);
            pieceAt.push_back(std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::steady_clock::now() - started).count());
        },
        .done = [&](HttpClient::Result r) { ended = std::move(r); loop.stop(); },
    }));
    PHI_CHECK(client.busy());
    phi::runtime::Timer watchdog = loop.timerAfter(6s, [&loop]() { loop.stop(); });
    loop.run();

    PHI_CHECK(headStatus == 200);
    std::string all;
    for (const std::string &piece : pieces)
        all += piece;
    PHI_CHECK_MSG(all == "data: one\n\ndata: two\n\ndata: three\n\n", "got '%s'", all.c_str());
    // Three pauses of 60 ms in the server: the first piece must not have
    // waited for the last, and the head timeout of 300 ms must not have
    // ended a stream that outlived it.
    PHI_CHECK(pieces.size() >= 2);
    if (pieceAt.size() >= 2)
        PHI_CHECK_MSG(pieceAt.back() - pieceAt.front() >= 100, "pieces were held back until the end");
    PHI_CHECK(ended.has_value());
    if (ended) {
        PHI_CHECK_MSG(ended->ok, "%s", ended->error.c_str());
        PHI_CHECK(ended->status == 200);
    }
    PHI_CHECK(!client.busy());
}

/// A stream cancelled from a chunk callback stops there, with no `done`.
void testAStreamCanBeCancelledFromInside()
{
    phitest::TestServer server;
    PHI_CHECK(server.start(
        [](const std::string &) {
            return std::vector<std::string>{
                "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n4\r\nabcd\r\n",
                "4\r\nefgh\r\n0\r\n\r\n",
            };
        },
        60));
    phi::runtime::EpollLoop loop;
    HttpClient client(loop);
    int chunks = 0;
    bool doneCalled = false;
    HttpClient::Call call;
    call.url = "http://127.0.0.1:" + std::to_string(server.port()) + "/s";
    call.timeout = 2000ms;
    client.stream(call, {
        .head = nullptr,
        .chunk = [&](std::string_view) { ++chunks; client.cancel(); loop.stop(); },
        .done = [&](HttpClient::Result) { doneCalled = true; },
    });
    phi::runtime::Timer watchdog = loop.timerAfter(4s, [&loop]() { loop.stop(); });
    loop.run();
    PHI_CHECK(chunks == 1);
    PHI_CHECK(!doneCalled);
    PHI_CHECK(!client.busy());

    // And it can be used again.
    std::optional<HttpClient::Result> again;
    HttpClient::Call plain = call;
    client.send(plain, [&](HttpClient::Result r) { again = std::move(r); loop.stop(); });
    phi::runtime::Timer watchdog2 = loop.timerAfter(4s, [&loop]() { loop.stop(); });
    loop.run();
    PHI_CHECK(again.has_value() && again->ok && again->body == "abcdefgh");
}

/// A 4xx head ends a stream at once, and a 401 is named as such.
void testAStreamRefusedIsNamed()
{
    phitest::TestServer server;
    PHI_CHECK(server.start([](const std::string &) {
        return std::string("HTTP/1.1 401 Unauthorized\r\nContent-Length: 0\r\n\r\n");
    }));
    phi::runtime::EpollLoop loop;
    HttpClient client(loop);
    std::optional<HttpClient::Result> ended;
    bool anyChunk = false;
    HttpClient::Call call;
    call.url = "http://127.0.0.1:" + std::to_string(server.port()) + "/s";
    call.timeout = 2000ms;
    client.stream(call, {
        .head = nullptr,
        .chunk = [&](std::string_view) { anyChunk = true; },
        .done = [&](HttpClient::Result r) { ended = std::move(r); loop.stop(); },
    });
    phi::runtime::Timer watchdog = loop.timerAfter(4s, [&loop]() { loop.stop(); });
    loop.run();
    PHI_CHECK(!anyChunk);
    PHI_CHECK(ended.has_value() && !ended->ok && ended->unauthorized && ended->status == 401);
}

} // namespace

int main()
{
    testAPlainRequest();
    testTheChallengeIsAnsweredAndThenNotAskedForAgain();
    testAPasswordThatIsWrongSaysSo();
    testAChunkedBody();
    testASilentServerCostsItsTimeoutAndNoMore();
    testNothingListening();
    testWhatIsRefusedOutright();
    testTlsIsVerified();
    testAStreamDeliversAsItGoes();
    testAStreamCanBeCancelledFromInside();
    testAStreamRefusedIsNamed();
    return phi::testing::report("sdk_http_client_tests");
}
