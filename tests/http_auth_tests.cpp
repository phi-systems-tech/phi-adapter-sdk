// HTTP authentication, checked against somebody else's arithmetic.
//
// A digest that is only consistent with itself is worthless: it will agree
// with every wrong answer it produces. Both worked examples the RFCs print are
// reproduced here byte for byte, which is why the client nonce is a parameter
// rather than something generated inside.

#include <phi/adapter/testing/check.h>

#include "phi/adapter/net/http_auth.h"

#include <string>

using namespace phicore::adapter::net;

namespace {

void testMd5AgreesWithPublishedVectors()
{
    // RFC 1321's own vectors. If this is wrong nothing below means anything.
    PHI_CHECK(md5Hex("") == "d41d8cd98f00b204e9800998ecf8427e");
    PHI_CHECK(md5Hex("abc") == "900150983cd24fb0d6963f7d28e17f72");
    PHI_CHECK(md5Hex("message digest") == "f96b697d7cb7938d525a2f31aaf161d0");
}

void testTheWorkedExampleFromRfc2617()
{
    // RFC 2617 section 3.5, printed in full there including the response.
    Challenge challenge;
    challenge.scheme = Challenge::Scheme::Digest;
    challenge.realm = "testrealm@host.com";
    challenge.nonce = "dcd98b7102dd2f0e8b11d0f600bfb0c093";
    challenge.opaque = "5ccc069c403ebaf9f0171e9517f40e41";
    challenge.qop = "auth";

    const std::string header = buildAuthorization(challenge,
                                                  {"Mufasa", "Circle Of Life"},
                                                  "GET",
                                                  "/dir/index.html",
                                                  1,
                                                  "0a4f113b");
    PHI_CHECK_MSG(header.find("response=\"6629fae49393a05397450978507c4ef1\"") != std::string::npos,
                  "digest does not match RFC 2617: %s", header.c_str());
    PHI_CHECK(header.find("nc=00000001") != std::string::npos);
    PHI_CHECK(header.find("qop=auth") != std::string::npos);
    PHI_CHECK(header.find("cnonce=\"0a4f113b\"") != std::string::npos);
    PHI_CHECK(header.find("opaque=\"5ccc069c403ebaf9f0171e9517f40e41\"") != std::string::npos);
    PHI_CHECK(header.rfind("Digest ", 0) == 0);
}

void testTheWorkedExampleFromRfc7616()
{
    // RFC 7616 section 3.9.1. Same scheme, a longer nonce, and a password with
    // a lower-case "of" - the two examples differ in that one letter, which is
    // exactly the kind of thing a self-consistent test would never catch.
    const Challenge challenge = parseChallenge(
        R"(Digest realm="http-auth@example.org", qop="auth, auth-int", algorithm=MD5, )"
        R"(nonce="7ypf/xlj9XXwfDPEoM4URrv/xwf94BcCAzFZH4GiTo0v", )"
        R"(opaque="FQhe/qaU925kfnzjCev0ciny7QMkPqMAFRtzCUYo5tdS")");

    PHI_CHECK(challenge.scheme == Challenge::Scheme::Digest);
    PHI_CHECK(challenge.realm == "http-auth@example.org");
    PHI_CHECK(challenge.algorithm == "MD5");
    // `qop="auth, auth-int"` is one parameter with two values. A split on every
    // comma reads the second as a parameter named `auth-int"`.
    PHI_CHECK_MSG(challenge.qop == "auth", "picked qop '%s'", challenge.qop.c_str());

    const std::string header = buildAuthorization(challenge,
                                                  {"Mufasa", "Circle of Life"},
                                                  "GET",
                                                  "/dir/index.html",
                                                  1,
                                                  "f2/wE4q74E6zIJEtWaHKaf5wv/H5QzzpXusqGemxURZJ");
    PHI_CHECK_MSG(header.find("response=\"8ca523f5e9506fed4657c9700eebdbec\"") != std::string::npos,
                  "digest does not match RFC 7616: %s", header.c_str());
}

void testTheChallengeAFritzBoxActuallySends()
{
    // Captured from a FRITZ!Box on 192.168.1.1:49000 by sending GetInfo with no
    // credentials. Note the parameters run together with no space after the
    // commas, and that qop is quoted while algorithm is not.
    const Challenge challenge = parseChallenge(
        R"(Digest realm="HTTPS Access",nonce="88B30A35B27FDC00",algorithm=MD5,qop="auth")");

    PHI_CHECK(challenge.scheme == Challenge::Scheme::Digest);
    PHI_CHECK(challenge.realm == "HTTPS Access");
    PHI_CHECK(challenge.nonce == "88B30A35B27FDC00");
    PHI_CHECK(challenge.algorithm == "MD5");
    PHI_CHECK(challenge.qop == "auth");
    PHI_CHECK(challenge.opaque.empty());
    PHI_CHECK(!challenge.stale);
    PHI_CHECK(challenge.usable());

    const std::string header = buildAuthorization(challenge, {"admin", "secret"},
                                                  "POST", "/upnp/control/deviceinfo",
                                                  1, "0123456789abcdef");
    PHI_CHECK(header.rfind("Digest ", 0) == 0);
    PHI_CHECK(header.find("uri=\"/upnp/control/deviceinfo\"") != std::string::npos);
    // No opaque was offered, so none is sent back. Inventing one is how a
    // server that checks decides the response is not its own.
    PHI_CHECK_MSG(header.find("opaque") == std::string::npos, "%s", header.c_str());
    PHI_CHECK(header.find("secret") == std::string::npos);
}

void testWhatTheParserHasToSurvive()
{
    // Nothing offered, or nothing understood.
    PHI_CHECK(parseChallenge("").scheme == Challenge::Scheme::None);
    PHI_CHECK(parseChallenge("Negotiate").scheme == Challenge::Scheme::None);
    PHI_CHECK(!parseChallenge("Digest").usable());

    // Digest wins over Basic wherever both appear: Basic puts the password on
    // the wire in something that is not encryption.
    const Challenge both = parseChallenge(
        R"(Digest realm="r", nonce="n", qop="auth")");
    PHI_CHECK(both.scheme == Challenge::Scheme::Digest);
    PHI_CHECK(parseChallenge(R"(Basic realm="r")").scheme == Challenge::Scheme::Basic);

    // An expired nonce is not a wrong password, and the caller has to be able
    // to tell them apart or it reports the wrong thing to the user.
    const Challenge stale = parseChallenge(
        R"(Digest realm="r", nonce="n2", qop="auth", stale=TRUE)");
    PHI_CHECK_MSG(stale.stale, "a stale nonce read as bad credentials");

    // The older form: no qop, so no count and no client nonce in the answer.
    Challenge legacy;
    legacy.scheme = Challenge::Scheme::Digest;
    legacy.realm = "r";
    legacy.nonce = "n";
    const std::string header = buildAuthorization(legacy, {"u", "p"}, "GET", "/", 1, "cn");
    PHI_CHECK(header.find("qop") == std::string::npos);
    PHI_CHECK(header.find("nc=") == std::string::npos);
    PHI_CHECK(header.find("cnonce") == std::string::npos);
    // RFC 2069: MD5(HA1:nonce:HA2), and nothing else.
    const std::string expected = md5Hex(md5Hex("u:r:p") + ":n:" + md5Hex("GET:/"));
    PHI_CHECK(header.find("response=\"" + expected + "\"") != std::string::npos);

    // A hash this client cannot compute is refused rather than answered wrongly.
    Challenge sha256;
    sha256.scheme = Challenge::Scheme::Digest;
    sha256.realm = "r";
    sha256.nonce = "n";
    sha256.algorithm = "SHA-256";
    PHI_CHECK(buildAuthorization(sha256, {"u", "p"}, "GET", "/", 1, "cn").empty());
}

void testBasicIsStillBasic()
{
    Challenge basic;
    basic.scheme = Challenge::Scheme::Basic;
    basic.realm = "r";
    // RFC 7617's own example.
    PHI_CHECK(buildAuthorization(basic, {"Aladdin", "open sesame"}, "GET", "/", 1, "")
              == "Basic QWxhZGRpbjpvcGVuIHNlc2FtZQ==");
    // Padding, both lengths.
    PHI_CHECK(buildAuthorization(basic, {"a", "b"}, "GET", "/", 1, "") == "Basic YTpi");
    PHI_CHECK(buildAuthorization(basic, {"a", "bc"}, "GET", "/", 1, "") == "Basic YTpiYw==");
}

void testTheCountAndTheNonceMove()
{
    Challenge challenge;
    challenge.scheme = Challenge::Scheme::Digest;
    challenge.realm = "r";
    challenge.nonce = "n";
    challenge.qop = "auth";

    // Reusing a count is what a replay looks like to a server that checks, so
    // the same request at a different count must not hash the same.
    const std::string first = buildAuthorization(challenge, {"u", "p"}, "GET", "/", 1, "cn");
    const std::string second = buildAuthorization(challenge, {"u", "p"}, "GET", "/", 2, "cn");
    PHI_CHECK(first != second);
    PHI_CHECK(second.find("nc=00000002") != std::string::npos);

    // The digest covers the path, so a rewritten one must not authenticate.
    PHI_CHECK(buildAuthorization(challenge, {"u", "p"}, "GET", "/other", 1, "cn") != first);
    // And the method.
    PHI_CHECK(buildAuthorization(challenge, {"u", "p"}, "POST", "/", 1, "cn") != first);

    const std::string nonce = makeClientNonce();
    PHI_CHECK(nonce.size() == 16);
    PHI_CHECK(nonce != makeClientNonce());
}

} // namespace

int main()
{
    testMd5AgreesWithPublishedVectors();
    testTheWorkedExampleFromRfc2617();
    testTheWorkedExampleFromRfc7616();
    testTheChallengeAFritzBoxActuallySends();
    testWhatTheParserHasToSurvive();
    testBasicIsStillBasic();
    testTheCountAndTheNonceMove();
    return phi::testing::report("sdk_http_auth_tests");
}
