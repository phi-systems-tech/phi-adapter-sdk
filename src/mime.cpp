#include "net/mime.h"

#include <cstdint>
#include <cstdio>

namespace phicore::adapter::net::mime {

namespace {

constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/// RFC 2047 keeps an encoded word to 75 characters; `=?UTF-8?B?` and `?=`
/// take 12, which leaves 63 for base64, which is 45 bytes of text.
constexpr std::size_t kEncodedWordBytes = 45;

bool isAscii(std::string_view text)
{
    for (const char c : text) {
        if (static_cast<unsigned char>(c) >= 0x80)
            return false;
    }
    return true;
}

std::string stripLineBreaks(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        if (c == '\r' || c == '\n')
            continue;
        out.push_back(c);
    }
    return out;
}

/// Whether the byte starts a UTF-8 character (not a continuation byte).
bool startsCharacter(unsigned char byte)
{
    return (byte & 0xC0) != 0x80;
}

std::string encodedWords(std::string_view text)
{
    std::string out;
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t end = pos + kEncodedWordBytes;
        if (end >= text.size()) {
            end = text.size();
        } else {
            while (end > pos && !startsCharacter(static_cast<unsigned char>(text[end])))
                --end;
            if (end == pos)
                end = pos + kEncodedWordBytes; // a run of continuation bytes: not UTF-8, cut anyway
        }
        if (!out.empty())
            out += " "; // folding white space between words, which readers drop
        out += "=?UTF-8?B?";
        out += base64(text.substr(pos, end - pos));
        out += "?=";
        pos = end;
    }
    return out;
}

/// A display name that RFC 5322 lets stand bare: letters, digits and a few
/// marks, no quotes or specials.
bool isAtomText(std::string_view text)
{
    if (text.empty())
        return false;
    for (const char c : text) {
        const bool atom = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
            || c == ' ' || c == '!' || c == '#' || c == '$' || c == '%' || c == '&' || c == '\''
            || c == '*' || c == '+' || c == '-' || c == '/' || c == '=' || c == '?' || c == '^'
            || c == '_' || c == '`' || c == '{' || c == '|' || c == '}' || c == '~';
        if (!atom)
            return false;
    }
    return true;
}

std::string quotedName(std::string_view text)
{
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\')
            out.push_back('\\');
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

std::string addressList(const std::vector<Mailbox> &boxes)
{
    std::string out;
    for (const Mailbox &box : boxes) {
        if (box.address.empty())
            continue;
        if (!out.empty())
            out += ",\r\n ";
        out += mailboxText(box);
    }
    return out;
}

void header(std::string &out, std::string_view name, std::string_view value)
{
    if (value.empty())
        return;
    out.append(name);
    out += ": ";
    out.append(value);
    out += "\r\n";
}

void bodyPart(std::string &out, std::string_view contentType, std::string_view body)
{
    out += "Content-Type: ";
    out.append(contentType);
    out += "; charset=UTF-8\r\n";
    out += "Content-Transfer-Encoding: base64\r\n\r\n";
    out += base64(body, true);
}

} // namespace

std::string base64(std::string_view bytes, bool lineBreaks)
{
    std::string out;
    out.reserve(((bytes.size() + 2) / 3) * 4 + (lineBreaks ? bytes.size() / 57 * 2 + 2 : 0));
    std::size_t column = 0;
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const unsigned char b0 = static_cast<unsigned char>(bytes[i]);
        const bool has1 = i + 1 < bytes.size();
        const bool has2 = i + 2 < bytes.size();
        const unsigned char b1 = has1 ? static_cast<unsigned char>(bytes[i + 1]) : 0;
        const unsigned char b2 = has2 ? static_cast<unsigned char>(bytes[i + 2]) : 0;
        out.push_back(kAlphabet[b0 >> 2]);
        out.push_back(kAlphabet[((b0 & 0x03) << 4) | (b1 >> 4)]);
        out.push_back(has1 ? kAlphabet[((b1 & 0x0F) << 2) | (b2 >> 6)] : '=');
        out.push_back(has2 ? kAlphabet[b2 & 0x3F] : '=');
        if (lineBreaks) {
            column += 4;
            if (column >= 76) {
                out += "\r\n";
                column = 0;
            }
        }
    }
    if (lineBreaks && column != 0)
        out += "\r\n";
    return out;
}

bool isPlainHeaderText(std::string_view text)
{
    for (const char c : text) {
        const unsigned char byte = static_cast<unsigned char>(c);
        if (byte >= 0x80 || byte == '\r' || byte == '\n')
            return false;
    }
    return true;
}

std::string headerText(std::string_view text)
{
    const std::string clean = stripLineBreaks(text);
    if (isAscii(clean))
        return clean;
    return encodedWords(clean);
}

std::string mailboxText(const Mailbox &mailbox)
{
    const std::string address = stripLineBreaks(mailbox.address);
    const std::string name = stripLineBreaks(mailbox.name);
    if (name.empty())
        return "<" + address + ">";
    std::string shown;
    if (!isAscii(name))
        shown = encodedWords(name);
    else if (isAtomText(name))
        shown = name;
    else
        shown = quotedName(name);
    return shown + " <" + address + ">";
}

std::string dateText(std::time_t when)
{
    static constexpr const char *kDays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static constexpr const char *kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                              "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    std::tm parts{};
    gmtime_r(&when, &parts);
    char out[48];
    std::snprintf(out, sizeof(out), "%s, %02d %s %04d %02d:%02d:%02d +0000", kDays[parts.tm_wday],
                  parts.tm_mday, kMonths[parts.tm_mon], parts.tm_year + 1900, parts.tm_hour,
                  parts.tm_min, parts.tm_sec);
    return out;
}

std::string messageId(std::string_view token, std::string_view senderAddress)
{
    std::string domain = "phi.invalid";
    const std::size_t at = senderAddress.rfind('@');
    if (at != std::string_view::npos && at + 1 < senderAddress.size())
        domain = std::string(senderAddress.substr(at + 1));
    return "<" + std::string(token) + "@" + domain + ">";
}

bool needsSmtpUtf8(const SmtpClient::Message &message)
{
    if (!isAscii(message.from().address))
        return true;
    for (const Mailbox &box : message.to()) {
        if (!isAscii(box.address))
            return true;
    }
    return false;
}

std::string serialize(const SmtpClient::Message &message, std::time_t now, const std::string &messageId,
                      const std::string &boundaryToken)
{
    std::string out;
    header(out, "Date", dateText(now));
    header(out, "From", mailboxText(message.from()));
    header(out, "To", addressList(message.to()));
    if (!message.replyTo().address.empty())
        header(out, "Reply-To", mailboxText(message.replyTo()));
    header(out, "Subject", headerText(message.subject()));
    header(out, "Message-ID", messageId);
    for (const Header &extra : message.headers()) {
        const std::string name = stripLineBreaks(extra.first);
        if (name.empty() || name.find(':') != std::string::npos)
            continue;
        header(out, name, headerText(extra.second));
    }
    header(out, "MIME-Version", "1.0");
    if (message.html().empty()) {
        bodyPart(out, "text/plain", message.text());
        return out;
    }
    const std::string boundary = "=_phi_" + boundaryToken;
    out += "Content-Type: multipart/alternative; boundary=\"" + boundary + "\"\r\n\r\n";
    out += "--" + boundary + "\r\n";
    bodyPart(out, "text/plain", message.text());
    out += "--" + boundary + "\r\n";
    bodyPart(out, "text/html", message.html());
    out += "--" + boundary + "--\r\n";
    return out;
}

} // namespace phicore::adapter::net::mime
