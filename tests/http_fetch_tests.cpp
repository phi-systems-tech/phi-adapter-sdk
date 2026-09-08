// The blocking exchange, against a server of this test's own: what goes on the
// wire, both body framings, redirects, the deadline, and the TLS checks that
// make an https endpoint mean something - a certificate the system does not
// trust, or one for another name, is refused.
//
// Moved here out of phi-core, which had written all of this a second time.

#include <phi/adapter/testing/check.h>

#include "phi/adapter/net/http_fetch.h"

#include "tls_test_server.h"

#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

#include <arpa/inet.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <mutex>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace phicore::adapter::net;
using phitest::TestCertificate;
using phitest::TestServer;

namespace {

using Clock = std::chrono::steady_clock;

std::int64_t msSince(Clock::time_point start)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
}


/// A self-signed certificate for "localhost", written as PEM for the client's
/// trust store and kept for the server.
std::string plainResponse(int status, const std::string &body, const std::string &extraHeaders = {})
{
    return "HTTP/1.1 " + std::to_string(status) + " Whatever\r\n"
        + "Content-Type: application/json\r\n"
        + "Content-Length: " + std::to_string(body.size()) + "\r\n"
        + extraHeaders + "\r\n" + body;
}

std::string firstLine(const std::string &request)
{
    return request.substr(0, request.find("\r\n"));
}

/// The shape phi-core's translation fetch has, which is what this path exists
/// for: a small JSON POST with a token header.
Fetch translationRequest(const std::string &url)
{
    Fetch request;
    request.url = url;
    request.method = "POST";
    request.timeout = std::chrono::milliseconds(3000);
    request.headers.push_back({"Content-Type", "application/json"});
    request.headers.push_back({"x-phi-token", "secret"});
    request.body = "{\"locale\":\"de\",\"msg\":\"Hello\"}";
    return request;
}

void testWireAndFramings()
{
    TestServer server;
    PHI_CHECK(server.start([](const std::string &request) -> std::string {
        const std::string line = firstLine(request);
        if (line == "POST /api/v1/tr HTTP/1.1")
            return plainResponse(200, "{\"translation\":\"Hallo\"}");
        if (line == "POST /chunked HTTP/1.1")
            return "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                   "5;ext=1\r\nHallo\r\n7\r\n, Welt!\r\n0\r\nX-Trailer: yes\r\n\r\n";
        if (line == "POST /eof HTTP/1.1")
            return "HTTP/1.1 200 OK\r\n\r\nuntil close";
        if (line == "POST /empty HTTP/1.1")
            return "HTTP/1.1 204 No Content\r\n\r\n";
        if (line == "POST /garbage HTTP/1.1")
            return "not http at all\r\n\r\n";
        if (line == "POST /truncated HTTP/1.1")
            return "HTTP/1.1 200 OK\r\nContent-Length: 40\r\n\r\ntoo short";
        return plainResponse(500, "{\"error\":\"boom\"}");
    }));
    const std::string base = "http://127.0.0.1:" + std::to_string(server.port());

    const FetchResult ok = fetch(translationRequest(base + "/api/v1/tr"));
    PHI_CHECK_MSG(ok.ok, "error: %s", ok.error.c_str());
    PHI_CHECK(ok.status == 200);
    PHI_CHECK(ok.body == "{\"translation\":\"Hallo\"}");
    PHI_CHECK(ok.error.empty());
    PHI_CHECK(ok.headers.size() == 2);

    const std::vector<std::string> seen = server.requests();
    PHI_CHECK(seen.size() == 1);
    if (!seen.empty()) {
        const std::string &wire = seen.front();
        PHI_CHECK(wire.rfind("POST /api/v1/tr HTTP/1.1\r\n", 0) == 0);
        PHI_CHECK(wire.find("Host: 127.0.0.1:" + std::to_string(server.port()) + "\r\n")
                  != std::string::npos);
        PHI_CHECK(wire.find("Content-Type: application/json\r\n") != std::string::npos);
        PHI_CHECK(wire.find("x-phi-token: secret\r\n") != std::string::npos);
        PHI_CHECK(wire.find("Content-Length: 29\r\n") != std::string::npos);
        PHI_CHECK(wire.find("Connection: close\r\n") != std::string::npos);
        PHI_CHECK(wire.size() >= 33
                  && wire.compare(wire.size() - 33, 33, "\r\n\r\n{\"locale\":\"de\",\"msg\":\"Hello\"}")
                      == 0);
    }

    const FetchResult chunked = fetch(translationRequest(base + "/chunked"));
    PHI_CHECK_MSG(chunked.ok, "error: %s", chunked.error.c_str());
    PHI_CHECK(chunked.body == "Hallo, Welt!");

    const FetchResult eof = fetch(translationRequest(base + "/eof"));
    PHI_CHECK_MSG(eof.ok, "error: %s", eof.error.c_str());
    PHI_CHECK(eof.body == "until close");

    const FetchResult empty = fetch(translationRequest(base + "/empty"));
    PHI_CHECK(empty.ok && empty.status == 204 && empty.body.empty());

    // A failing status is not ok, but its body still comes through: an
    // endpoint says why in the body, and a caller that only saw the status
    // would have to guess.
    const FetchResult failing = fetch(translationRequest(base + "/other"));
    PHI_CHECK(!failing.ok);
    PHI_CHECK(failing.status == 500);
    PHI_CHECK(failing.error == "HTTP 500");
    PHI_CHECK(failing.body == "{\"error\":\"boom\"}");

    const FetchResult garbage = fetch(translationRequest(base + "/garbage"));
    PHI_CHECK(!garbage.ok);
    PHI_CHECK_MSG(garbage.error == "not an HTTP response", "error: %s", garbage.error.c_str());

    // A body that stopped halfway is a different sentence: the response was
    // read, it just did not finish.
    const FetchResult truncated = fetch(translationRequest(base + "/truncated"));
    PHI_CHECK(!truncated.ok);
    PHI_CHECK_MSG(truncated.error == "the connection closed before the response was complete",
                  "error: %s", truncated.error.c_str());
}

void testRedirects()
{
    TestServer server;
    PHI_CHECK(server.start([](const std::string &request) -> std::string {
        const std::string line = firstLine(request);
        if (line == "POST /start HTTP/1.1")
            return plainResponse(307, "", "Location: /again\r\n");
        if (line == "POST /again HTTP/1.1")
            return plainResponse(302, "", "Location: final\r\n");
        if (line == "GET /final HTTP/1.1")
            return plainResponse(200, "done");
        if (line == "POST /loop HTTP/1.1")
            return plainResponse(308, "", "Location: /loop\r\n");
        return plainResponse(404, "?");
    }));
    const std::string base = "http://127.0.0.1:" + std::to_string(server.port());

    // 307 keeps the POST and its body; 302 turns it into a GET without one,
    // and its relative Location resolves against the path it came from.
    const FetchResult chain = fetch(translationRequest(base + "/start"));
    PHI_CHECK_MSG(chain.ok, "error: %s", chain.error.c_str());
    PHI_CHECK(chain.status == 200 && chain.body == "done");
    const std::vector<std::string> seen = server.requests();
    PHI_CHECK(seen.size() == 3);
    if (seen.size() == 3) {
        PHI_CHECK(seen[1].find("{\"locale\":\"de\",\"msg\":\"Hello\"}") != std::string::npos);
        PHI_CHECK(seen[2].rfind("GET /final HTTP/1.1\r\n", 0) == 0);
        PHI_CHECK(seen[2].find("Content-Length: 0\r\n") != std::string::npos);
        PHI_CHECK(seen[2].size() >= 4 && seen[2].compare(seen[2].size() - 4, 4, "\r\n\r\n") == 0);
    }

    // Redirects are bounded; past the limit the last redirect is the answer,
    // and it is a failure rather than an empty body somebody reads as one.
    const FetchResult loop = fetch(translationRequest(base + "/loop"));
    PHI_CHECK(!loop.ok);
    PHI_CHECK(loop.status == 308);
    PHI_CHECK(loop.error == "HTTP 308 redirect not followed");
    PHI_CHECK(server.requests().size() == 3 + 6);

    Fetch none = translationRequest(base + "/start");
    none.maxRedirects = 0;
    const FetchResult unfollowed = fetch(none);
    PHI_CHECK(!unfollowed.ok && unfollowed.status == 307);
}

void testFailures()
{
    TestServer slow;
    PHI_CHECK(slow.start([](const std::string &) -> std::string {
        std::this_thread::sleep_for(std::chrono::milliseconds(700));
        return plainResponse(200, "late");
    }));

    Fetch request = translationRequest("http://127.0.0.1:" + std::to_string(slow.port()) + "/slow");
    request.timeout = std::chrono::milliseconds(200);
    Clock::time_point started = Clock::now();
    const FetchResult timedOut = fetch(request);
    PHI_CHECK(!timedOut.ok);
    PHI_CHECK_MSG(timedOut.error.find("timed out") != std::string::npos, "error: %s",
                  timedOut.error.c_str());
    PHI_CHECK_MSG(msSince(started) < 600, "took %lld ms", (long long)msSince(started));
    const std::uint16_t deadPort = slow.port();
    slow.stop();

    // Nobody listening: a refusal, not a wait.
    started = Clock::now();
    const FetchResult refused =
        fetch(translationRequest("http://127.0.0.1:" + std::to_string(deadPort) + "/"));
    PHI_CHECK(!refused.ok);
    PHI_CHECK(refused.status == 0);
    PHI_CHECK_MSG(refused.error.find("could not connect") != std::string::npos, "error: %s",
                  refused.error.c_str());
    PHI_CHECK(msSince(started) < 1000);

    const FetchResult scheme = fetch(translationRequest("ftp://127.0.0.1/x"));
    PHI_CHECK(!scheme.ok && scheme.error.find("not an http(s) URL") != std::string::npos);
    const FetchResult nohost = fetch(translationRequest("https://"));
    PHI_CHECK(!nohost.ok);
    const FetchResult unresolved = fetch(translationRequest("http://no-such-host.invalid/"));
    PHI_CHECK(!unresolved.ok && unresolved.error.find("could not resolve") != std::string::npos);
}

void testTls(const std::string &dir)
{
    TestCertificate certificate;
    const std::string pem = dir + "/ca.pem";
    PHI_CHECK(certificate.create(pem));

    TestServer server;
    PHI_CHECK(server.start([](const std::string &request) {
        if (firstLine(request) == "POST /downgrade HTTP/1.1")
            return plainResponse(301, "", "Location: http://127.0.0.1:1/\r\n");
        return plainResponse(200, "{\"translation\":\"sicher\"}");
    }, &certificate));
    const std::string port = std::to_string(server.port());

    // The name the certificate carries, verified against the bundle it is in.
    Fetch trusted = translationRequest("https://localhost:" + port + "/api/v1/tr");
    trusted.tls.caFile = pem;
    const FetchResult ok = fetch(trusted);
    PHI_CHECK_MSG(ok.ok, "error: %s", ok.error.c_str());
    PHI_CHECK(ok.body == "{\"translation\":\"sicher\"}");
    PHI_CHECK(server.requests().size() == 1);
    if (!server.requests().empty())
        PHI_CHECK(server.requests().front().find("Host: localhost:") != std::string::npos);

    // Same certificate, another name: refused before a byte of the request
    // leaves.
    Fetch wrongName = translationRequest("https://127.0.0.1:" + port + "/api/v1/tr");
    wrongName.tls.caFile = pem;
    const FetchResult mismatch = fetch(wrongName);
    PHI_CHECK(!mismatch.ok);
    PHI_CHECK_MSG(mismatch.error.find("certificate") != std::string::npos, "error: %s",
                  mismatch.error.c_str());
    PHI_CHECK(server.requests().size() == 1);

    // The same wrong name with the hostname check turned off: the operator
    // said the certificate names a host they reach by address. The chain is
    // still checked, which is why the bundle still has to be named.
    Fetch byAddress = wrongName;
    byAddress.tls.verifyHostname = false;
    const FetchResult accepted = fetch(byAddress);
    PHI_CHECK_MSG(accepted.ok, "error: %s", accepted.error.c_str());
    PHI_CHECK(server.requests().size() == 2);

    // ... and with the check off but nothing trusting the certificate, it is
    // still refused. Off is not "accept anything".
    Fetch unverifiable = translationRequest("https://127.0.0.1:" + port + "/api/v1/tr");
    unverifiable.tls.verifyHostname = false;
    const FetchResult stillRefused = fetch(unverifiable);
    PHI_CHECK(!stillRefused.ok);
    PHI_CHECK_MSG(stillRefused.error.find("certificate") != std::string::npos, "error: %s",
                  stillRefused.error.c_str());

    // The system's trust store does not know this certificate: the production
    // path refuses it.
    const FetchResult selfSigned =
        fetch(translationRequest("https://localhost:" + port + "/api/v1/tr"));
    PHI_CHECK(!selfSigned.ok);
    PHI_CHECK_MSG(selfSigned.error.find("certificate") != std::string::npos, "error: %s",
                  selfSigned.error.c_str());

    // A redirect off the protected connection is refused rather than
    // followed: the caller asked for https and only the redirect said
    // otherwise.
    Fetch downgrade = translationRequest("https://localhost:" + port + "/downgrade");
    downgrade.tls.caFile = pem;
    const FetchResult refusedDowngrade = fetch(downgrade);
    PHI_CHECK(!refusedDowngrade.ok);
    PHI_CHECK_MSG(refusedDowngrade.error == "redirect from https to a plain connection refused",
                  "error: %s", refusedDowngrade.error.c_str());

    // Plain http to a TLS port is a transport failure, not a hang.
    Fetch plain = translationRequest("http://localhost:" + port + "/api/v1/tr");
    plain.timeout = std::chrono::milliseconds(500);
    const FetchResult confused = fetch(plain);
    PHI_CHECK(!confused.ok);
}

} // namespace

int main()
{
    char templ[] = "/tmp/phi-http-fetch-XXXXXX";
    const char *dir = ::mkdtemp(templ);
    PHI_CHECK(dir != nullptr);
    if (!dir)
        return phi::testing::report("sdk_http_fetch_tests");

    testWireAndFramings();
    testRedirects();
    testFailures();
    testTls(dir);

    ::unlink((std::string(dir) + "/ca.pem").c_str());
    ::rmdir(dir);
    return phi::testing::report("sdk_http_fetch_tests");
}
