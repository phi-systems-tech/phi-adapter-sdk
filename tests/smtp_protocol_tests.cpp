// SMTP and MIME as text, pinned against the RFCs' own examples and against
// what mx01 answers: a reply in pieces, the EHLO keywords, the AUTH strings
// byte for byte, dot-stuffing, and a message whose subject is in German.

#include <phi/adapter/testing/check.h>

#include "net/mime.h"
#include "net/smtp_protocol.h"
#include "phi/adapter/net/smtp_client.h"

#include <string>
#include <vector>

using namespace phicore::adapter::net;

namespace {

std::string base64Decode(std::string_view text)
{
    auto value = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    std::string out;
    int bits = 0;
    unsigned int accumulator = 0;
    for (const char c : text) {
        const int v = value(c);
        if (v < 0)
            continue;
        accumulator = (accumulator << 6) | static_cast<unsigned int>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((accumulator >> bits) & 0xFF));
        }
    }
    return out;
}

std::string headerOf(const std::string &message, const std::string &name)
{
    const std::string key = name + ": ";
    std::size_t at = message.find(key);
    if (at == std::string::npos || (at != 0 && message[at - 1] != '\n'))
        return {};
    std::string value;
    std::size_t pos = at + key.size();
    for (;;) {
        const std::size_t eol = message.find("\r\n", pos);
        value += message.substr(pos, eol - pos);
        pos = eol + 2;
        if (pos >= message.size() || (message[pos] != ' ' && message[pos] != '\t'))
            break;
    }
    return value;
}

// --- replies -----------------------------------------------------------------

void testReplyInPieces()
{
    smtp::ReplyParser parser;
    PHI_CHECK(parser.consume("250-mx01.phisys.com\r\n250-PIPE") == smtp::ReplyParser::State::NeedMore);
    PHI_CHECK(parser.consume("LINING\r\n250-SIZE 10240000\r") == smtp::ReplyParser::State::NeedMore);
    PHI_CHECK(parser.consume("\n250 CHUNKING\r\n") == smtp::ReplyParser::State::Complete);
    const smtp::Reply &reply = parser.reply();
    PHI_CHECK(reply.code == 250);
    PHI_CHECK(reply.lines.size() == 4);
    PHI_CHECK(reply.firstLine() == "mx01.phisys.com");
    PHI_CHECK(reply.positive());

    // A second reply right behind the first stays for the next round.
    smtp::ReplyParser two;
    PHI_CHECK(two.consume("220 go ahead\r\n235 2.7.0 Authentication successful\r\n")
              == smtp::ReplyParser::State::Complete);
    PHI_CHECK(two.reply().code == 220);
    two.reset();
    PHI_CHECK(two.state() == smtp::ReplyParser::State::Complete);
    PHI_CHECK(two.reply().code == 235);
    PHI_CHECK(two.reply().firstLine() == "2.7.0 Authentication successful");

    // A bare code is a reply too.
    smtp::ReplyParser bare;
    PHI_CHECK(bare.consume("354\r\n") == smtp::ReplyParser::State::Complete);
    PHI_CHECK(bare.reply().code == 354 && bare.reply().lines.size() == 1 && bare.reply().lines[0].empty());
}

void testMalformedReplies()
{
    smtp::ReplyParser noCode;
    PHI_CHECK(noCode.consume("hello\r\n") == smtp::ReplyParser::State::Malformed);

    smtp::ReplyParser codeMoved;
    PHI_CHECK(codeMoved.consume("250-one\r\n550 two\r\n") == smtp::ReplyParser::State::Malformed);

    smtp::ReplyParser endless;
    PHI_CHECK(endless.consume(std::string(3000, 'x')) == smtp::ReplyParser::State::Malformed);
}

void testEhloCapabilities()
{
    // What mx01.phisys.com answers on 587, 2026-11-02.
    smtp::ReplyParser parser;
    parser.consume("250-mx01.phisys.com\r\n250-PIPELINING\r\n250-SIZE 10240000\r\n250-VRFY\r\n"
                   "250-ETRN\r\n250-STARTTLS\r\n250-AUTH PLAIN LOGIN\r\n250-AUTH=PLAIN LOGIN\r\n"
                   "250-ENHANCEDSTATUSCODES\r\n250-8BITMIME\r\n250-DSN\r\n250-SMTPUTF8\r\n"
                   "250 CHUNKING\r\n");
    const smtp::Capabilities caps = smtp::parseEhlo(parser.reply());
    PHI_CHECK(caps.startTls);
    PHI_CHECK(caps.authPlain);
    PHI_CHECK(caps.authLogin);
    PHI_CHECK(caps.eightBitMime);
    PHI_CHECK(caps.smtpUtf8);
    PHI_CHECK(caps.pipelining);
    PHI_CHECK(caps.maxSize == 10240000);

    // Only the old form, lower case, and a mechanism we do not speak.
    smtp::ReplyParser old;
    old.consume("250-relay\r\n250 auth=cram-md5 login\r\n");
    const smtp::Capabilities oldCaps = smtp::parseEhlo(old.reply());
    PHI_CHECK(!oldCaps.authPlain);
    PHI_CHECK(oldCaps.authLogin);
    PHI_CHECK(oldCaps.authMechanisms.size() == 2 && oldCaps.authMechanisms[0] == "CRAM-MD5");
    PHI_CHECK(!oldCaps.startTls);

    // The greeting line never counts as a keyword.
    smtp::ReplyParser greetingOnly;
    greetingOnly.consume("250 STARTTLS\r\n");
    PHI_CHECK(!smtp::parseEhlo(greetingOnly.reply()).startTls);
}

void testAuthStrings()
{
    // RFC 4616, section 4: tim / tanstaaftanstaaf.
    PHI_CHECK(smtp::authPlainInitialResponse("tim", "tanstaaftanstaaf") == "AHRpbQB0YW5zdGFhZnRhbnN0YWFm");
    PHI_CHECK(smtp::authLoginResponse("Username:") == "VXNlcm5hbWU6");
    PHI_CHECK(smtp::authLoginResponse("") == "");
}

void testDataBlock()
{
    PHI_CHECK(smtp::dataBlock("a\r\nb") == "a\r\nb\r\n.\r\n");
    PHI_CHECK(smtp::dataBlock("a\nb\n") == "a\r\nb\r\n.\r\n");
    PHI_CHECK(smtp::dataBlock(".hidden\r\n..twice\r\n") == "..hidden\r\n...twice\r\n.\r\n");
    PHI_CHECK(smtp::dataBlock("") == ".\r\n");
    PHI_CHECK(smtp::dataBlock("x\r.\n") == "x\r\n..\r\n.\r\n");
}

// --- mime ----------------------------------------------------------------------

void testBase64()
{
    // RFC 4648, section 10.
    PHI_CHECK(mime::base64("") == "");
    PHI_CHECK(mime::base64("f") == "Zg==");
    PHI_CHECK(mime::base64("fo") == "Zm8=");
    PHI_CHECK(mime::base64("foo") == "Zm9v");
    PHI_CHECK(mime::base64("foobar") == "Zm9vYmFy");
    const std::string body(100, 'a');
    const std::string wrapped = mime::base64(body, true);
    PHI_CHECK(wrapped.find("\r\n") == 76);
    PHI_CHECK(wrapped.size() == 136 + 4);
    PHI_CHECK(base64Decode(wrapped) == body);
}

void testHeaderText()
{
    PHI_CHECK(mime::headerText("Washing machine") == "Washing machine");
    PHI_CHECK(mime::headerText("one\r\nBcc: x\n") == "oneBcc: x");
    const std::string german = mime::headerText("Waschmaschine fertig – 12,4 kWh");
    PHI_CHECK(german.rfind("=?UTF-8?B?", 0) == 0);
    PHI_CHECK(german.size() >= 2 && german.compare(german.size() - 2, 2, "?=") == 0);
    PHI_CHECK(base64Decode(german.substr(10, german.size() - 12)) == "Waschmaschine fertig – 12,4 kWh");

    // Long and non-ASCII: several words, none over 75, split between characters.
    std::string umlauts;
    for (int i = 0; i < 40; ++i)
        umlauts += "äö";
    const std::string words = mime::headerText(umlauts);
    std::string decoded;
    std::size_t pos = 0;
    int count = 0;
    while ((pos = words.find("=?UTF-8?B?", pos)) != std::string::npos) {
        const std::size_t end = words.find("?=", pos);
        PHI_CHECK(end - pos + 2 <= 75);
        decoded += base64Decode(words.substr(pos + 10, end - pos - 10));
        pos = end + 2;
        ++count;
    }
    PHI_CHECK(count >= 2);
    PHI_CHECK(decoded == umlauts);
}

void testMailboxText()
{
    PHI_CHECK(mime::mailboxText({"a@b.de", ""}) == "<a@b.de>");
    PHI_CHECK(mime::mailboxText({"a@b.de", "phi"}) == "phi <a@b.de>");
    PHI_CHECK(mime::mailboxText({"a@b.de", "Haus Müller"}) == "=?UTF-8?B?SGF1cyBNw7xsbGVy?= <a@b.de>");
    PHI_CHECK(mime::mailboxText({"a@b.de", "Smith, John"}) == "\"Smith, John\" <a@b.de>");
    PHI_CHECK(mime::mailboxText({"a@b.de", "say \"hi\""}) == "\"say \\\"hi\\\"\" <a@b.de>");
    PHI_CHECK(mime::mailboxText({"a@b.de\r\nBcc: x", "n\r\n"}) == "n <a@b.deBcc: x>");
}

void testDateAndId()
{
    PHI_CHECK(mime::dateText(0) == "Thu, 01 Jan 1970 00:00:00 +0000");
    PHI_CHECK(mime::dateText(1793614272) == "Mon, 02 Nov 2026 10:11:12 +0000");
    PHI_CHECK(mime::messageId("abc", "phi@example.org") == "<abc@example.org>");
    PHI_CHECK(mime::messageId("abc", "") == "<abc@phi.invalid>");
}

void testSerialize()
{
    SmtpClient::Message message;
    message.setFrom({"phi@haus.example", "phi"});
    message.addTo({"marius@example.org", "Marius"});
    message.addTo({"anna@example.org", ""});
    message.setReplyTo({"admin@haus.example", ""});
    message.setSubject("Küche: Waschmaschine");
    message.setText("ist fertig (12,4 kWh)\n");
    message.addHeader("X-Phi-Severity", "Info");
    message.addHeader("Evil: injected\r\n", "x");

    const std::string plain = mime::serialize(message, 1793614272, "<id1@haus.example>", "tok");
    PHI_CHECK(plain.rfind("Date: Mon, 02 Nov 2026 10:11:12 +0000\r\n", 0) == 0);
    PHI_CHECK(headerOf(plain, "From") == "phi <phi@haus.example>");
    PHI_CHECK(headerOf(plain, "To") == "Marius <marius@example.org>, <anna@example.org>");
    PHI_CHECK(headerOf(plain, "Reply-To") == "<admin@haus.example>");
    PHI_CHECK(headerOf(plain, "Subject") == "=?UTF-8?B?S8O8Y2hlOiBXYXNjaG1hc2NoaW5l?=");
    PHI_CHECK(headerOf(plain, "Message-ID") == "<id1@haus.example>");
    PHI_CHECK(headerOf(plain, "X-Phi-Severity") == "Info");
    PHI_CHECK(plain.find("Evil") == std::string::npos);
    PHI_CHECK(headerOf(plain, "MIME-Version") == "1.0");
    PHI_CHECK(headerOf(plain, "Content-Type") == "text/plain; charset=UTF-8");
    PHI_CHECK(headerOf(plain, "Content-Transfer-Encoding") == "base64");
    const std::size_t bodyAt = plain.find("\r\n\r\n");
    PHI_CHECK(bodyAt != std::string::npos);
    PHI_CHECK(base64Decode(plain.substr(bodyAt + 4)) == "ist fertig (12,4 kWh)\n");
    PHI_CHECK(plain.find("\n\n") == std::string::npos || plain.find("\r\n\r\n") == bodyAt);
    PHI_CHECK(!mime::needsSmtpUtf8(message));

    message.setHtml("<p>ist fertig</p>");
    const std::string alternative = mime::serialize(message, 1793614272, "<id2@haus.example>", "tok");
    PHI_CHECK(headerOf(alternative, "Content-Type") == "multipart/alternative; boundary=\"=_phi_tok\"");
    const std::size_t textPart = alternative.find("--=_phi_tok\r\nContent-Type: text/plain; charset=UTF-8\r\n");
    const std::size_t htmlPart = alternative.find("--=_phi_tok\r\nContent-Type: text/html; charset=UTF-8\r\n");
    PHI_CHECK(textPart != std::string::npos && htmlPart != std::string::npos && textPart < htmlPart);
    PHI_CHECK(alternative.compare(alternative.size() - 16, 16, "--=_phi_tok--\r\n") == 0
              || alternative.find("--=_phi_tok--\r\n") != std::string::npos);

    SmtpClient::Message international;
    international.setFrom({"phi@haus.example", ""});
    international.addTo({"jürgen@example.org", ""});
    PHI_CHECK(mime::needsSmtpUtf8(international));
}

} // namespace

int main()
{
    testReplyInPieces();
    testMalformedReplies();
    testEhloCapabilities();
    testAuthStrings();
    testDataBlock();
    testBase64();
    testHeaderText();
    testMailboxText();
    testDateAndId();
    testSerialize();
    return phi::testing::report("sdk_smtp_protocol_tests");
}
