// HTTP/1.1 framing: the request that goes out, and reading what comes back
// out of whatever pieces the network chose to deliver it in.

#include <phi/adapter/testing/check.h>

#include "phi/adapter/net/http_message.h"

#include <string>

using namespace phicore::adapter::net;

namespace {

Response parseWhole(const std::string &bytes, ResponseParser::State expected)
{
    ResponseParser parser;
    const ResponseParser::State state = parser.consume(bytes);
    PHI_CHECK_MSG(state == expected, "state %d, expected %d", int(state), int(expected));
    return parser.response();
}

void testTheRequestThatGoesOut()
{
    Request request;
    request.method = "POST";
    request.target = "/upnp/control/deviceinfo";
    request.authority = "192.168.1.1:49000";
    request.headers = {{"SOAPAction", "\"urn:x#GetInfo\""}};
    request.body = "<x/>";

    const std::string wire = serializeRequest(request);
    PHI_CHECK(wire.rfind("POST /upnp/control/deviceinfo HTTP/1.1\r\n", 0) == 0);
    PHI_CHECK(wire.find("Host: 192.168.1.1:49000\r\n") != std::string::npos);
    PHI_CHECK(wire.find("SOAPAction: \"urn:x#GetInfo\"\r\n") != std::string::npos);
    PHI_CHECK(wire.find("Content-Length: 4\r\n") != std::string::npos);
    PHI_CHECK(wire.find("\r\n\r\n<x/>") != std::string::npos);

    // Zero length is still stated. A POST without it makes a server wait for a
    // body that is not coming.
    Request empty;
    empty.method = "POST";
    empty.authority = "h";
    PHI_CHECK(serializeRequest(empty).find("Content-Length: 0\r\n") != std::string::npos);

    // What the caller set is what is sent - the digest signs a header, and a
    // second copy of it is not the same request.
    Request explicitLength;
    explicitLength.authority = "h";
    explicitLength.headers = {{"Content-Length", "0"}, {"Host", "other"}};
    const std::string custom = serializeRequest(explicitLength);
    PHI_CHECK(custom.find("Host: other") != std::string::npos);
    PHI_CHECK(custom.find("Host: h") == std::string::npos);
}

void testAResponseWithALength()
{
    const Response response = parseWhole(
        "HTTP/1.1 200 OK\r\nContent-Type: text/xml\r\nContent-Length: 5\r\n\r\nhello",
        ResponseParser::State::Complete);
    PHI_CHECK(response.status == 200);
    PHI_CHECK(response.body == "hello");
    PHI_CHECK(response.header("content-type") == "text/xml");   // names are not case
    PHI_CHECK(response.header("Content-Type") == "text/xml");
    PHI_CHECK(response.header("absent").empty());
}

void testAResponseArrivingInPieces()
{
    // The network splits where it likes, including inside the status line and
    // between the head and the body.
    const std::string whole =
        "HTTP/1.1 401 Unauthorized\r\n"
        "WWW-Authenticate: Digest realm=\"HTTPS Access\",nonce=\"AB\",algorithm=MD5,qop=\"auth\"\r\n"
        "Content-Length: 3\r\n\r\nno!";

    for (std::size_t split = 1; split < whole.size(); ++split) {
        ResponseParser parser;
        PHI_CHECK(parser.consume(whole.substr(0, split)) != ResponseParser::State::Malformed);
        const ResponseParser::State state = parser.consume(whole.substr(split));
        PHI_CHECK_MSG(state == ResponseParser::State::Complete,
                      "split at %zu did not complete", split);
        PHI_CHECK(parser.response().status == 401);
        PHI_CHECK(parser.response().body == "no!");
        PHI_CHECK(!parser.response().header("WWW-Authenticate").empty());
    }
}

void testChunked()
{
    const Response response = parseWhole(
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
        "4\r\nWiki\r\n5\r\npedia\r\ne\r\n in\r\n\r\nchunks.\r\n0\r\n\r\n",
        ResponseParser::State::Complete);
    PHI_CHECK_MSG(response.body == "Wikipedia in\r\n\r\nchunks.", "got '%s'",
                  response.body.c_str());

    // A chunk extension is not part of the size.
    PHI_CHECK(parseWhole("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                         "3;name=value\r\nabc\r\n0\r\n\r\n",
                         ResponseParser::State::Complete)
                  .body == "abc");

    // Half a chunked body is not an error, it is not finished.
    ResponseParser partial;
    PHI_CHECK(partial.consume("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n4\r\nWi")
              == ResponseParser::State::NeedMore);
    // Until the connection goes away under it, and then it is.
    PHI_CHECK(partial.finish() == ResponseParser::State::Malformed);
}

void testTheFramingsThatAreNotFramings()
{
    // No length and no chunking: the body is what arrives before the close.
    // HTTP/1.0 servers still do this, and a parser that waits for a length
    // waits forever.
    ResponseParser parser;
    PHI_CHECK(parser.consume("HTTP/1.1 200 OK\r\nServer: old\r\n\r\nbody bytes")
              == ResponseParser::State::NeedMore);
    PHI_CHECK(parser.finish() == ResponseParser::State::Complete);
    PHI_CHECK(parser.response().body == "body bytes");

    // 204 has no body whatever else the headers claim.
    PHI_CHECK(parseWhole("HTTP/1.1 204 No Content\r\nContent-Length: 5\r\n\r\n",
                         ResponseParser::State::Complete)
                  .body.empty());

    // Truncated before the head is finished.
    ResponseParser truncated;
    PHI_CHECK(truncated.consume("HTTP/1.1 200 OK\r\nContent-Len")
              == ResponseParser::State::NeedMore);
    PHI_CHECK(truncated.finish() == ResponseParser::State::Malformed);

    // Not HTTP at all.
    PHI_CHECK(parseWhole("<html>hello</html>\r\n\r\n", ResponseParser::State::Malformed).status == 0);
    PHI_CHECK(parseWhole("HTTP/1.1 wat OK\r\n\r\n", ResponseParser::State::Malformed).status == 0);
    // A length nobody could mean. parseWhole() asserts the state; the fields of
    // a response that was refused are not something a caller may read.
    (void)parseWhole("HTTP/1.1 200 OK\r\nContent-Length: 99999999999999\r\n\r\n",
                     ResponseParser::State::Malformed);
}

void testUrls()
{
    bool secure = true;
    std::string host, port, target;
    PHI_CHECK(parseUrl("http://192.168.1.1:49000/upnp/control/hosts", &secure, &host, &port, &target));
    PHI_CHECK(!secure && host == "192.168.1.1" && port == "49000"
              && target == "/upnp/control/hosts");

    PHI_CHECK(parseUrl("https://fritz.box/x", &secure, &host, &port, &target));
    PHI_CHECK(secure && host == "fritz.box" && port == "443" && target == "/x");

    PHI_CHECK(parseUrl("http://fritz.box", &secure, &host, &port, &target));
    PHI_CHECK(port == "80" && target == "/");

    // The colons in an address are not a port.
    PHI_CHECK(parseUrl("http://[fe80::1]:8080/a", &secure, &host, &port, &target));
    PHI_CHECK_MSG(host == "fe80::1", "host came out as '%s'", host.c_str());
    PHI_CHECK(port == "8080");

    PHI_CHECK(!parseUrl("ftp://host/x", &secure, &host, &port, &target));
    PHI_CHECK(!parseUrl("http:///x", &secure, &host, &port, &target));
    PHI_CHECK(!parseUrl("192.168.1.1", &secure, &host, &port, &target));
}

} // namespace

int main()
{
    testTheRequestThatGoesOut();
    testAResponseWithALength();
    testAResponseArrivingInPieces();
    testChunked();
    testTheFramingsThatAreNotFramings();
    testUrls();
    return phi::testing::report("sdk_http_message_tests");
}
