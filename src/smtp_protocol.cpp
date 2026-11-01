#include "net/smtp_protocol.h"

#include "net/mime.h"

namespace phicore::adapter::net::smtp {

namespace {

/// RFC 5321 allows 512 octets per reply line; a server that sends more is
/// not one this client wants to keep reading.
constexpr std::size_t kMaxLine = 2048;

std::string upper(std::string_view text)
{
    std::string out(text);
    for (char &c : out)
        c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c);
    return out;
}

std::vector<std::string> words(std::string_view text)
{
    std::vector<std::string> out;
    std::size_t pos = 0;
    while (pos < text.size()) {
        while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t'))
            ++pos;
        const std::size_t start = pos;
        while (pos < text.size() && text[pos] != ' ' && text[pos] != '\t')
            ++pos;
        if (pos > start)
            out.emplace_back(text.substr(start, pos - start));
    }
    return out;
}

} // namespace

std::string Reply::text() const
{
    std::string out;
    for (const std::string &line : lines) {
        if (!out.empty())
            out.push_back('\n');
        out += line;
    }
    return out;
}

ReplyParser::State ReplyParser::consume(std::string_view bytes)
{
    if (m_state != State::NeedMore)
        return m_state;
    m_buffer.append(bytes);
    for (;;) {
        const std::size_t eol = m_buffer.find("\r\n");
        if (eol == std::string::npos) {
            if (m_buffer.size() > kMaxLine)
                m_state = State::Malformed;
            return m_state;
        }
        const std::string line = m_buffer.substr(0, eol);
        m_buffer.erase(0, eol + 2);
        if (line.size() < 3 || line.size() > kMaxLine) {
            m_state = State::Malformed;
            return m_state;
        }
        int code = 0;
        for (int i = 0; i < 3; ++i) {
            const char c = line[static_cast<std::size_t>(i)];
            if (c < '0' || c > '9') {
                m_state = State::Malformed;
                return m_state;
            }
            code = code * 10 + (c - '0');
        }
        const char separator = line.size() > 3 ? line[3] : ' ';
        if (separator != ' ' && separator != '-') {
            m_state = State::Malformed;
            return m_state;
        }
        if (m_reply.code != 0 && m_reply.code != code) {
            m_state = State::Malformed;
            return m_state;
        }
        m_reply.code = code;
        m_reply.lines.push_back(line.size() > 4 ? line.substr(4) : std::string());
        if (separator == ' ') {
            m_state = State::Complete;
            return m_state;
        }
    }
}

void ReplyParser::reset()
{
    m_state = State::NeedMore;
    m_reply = Reply{};
    // Whatever arrived after the last reply stays: a server that pipelines
    // its greeting and a banner, say.
    if (!m_buffer.empty()) {
        const std::string rest = std::move(m_buffer);
        m_buffer.clear();
        consume(rest);
    }
}

Capabilities parseEhlo(const Reply &reply)
{
    Capabilities out;
    for (std::size_t i = 1; i < reply.lines.size(); ++i) {
        std::vector<std::string> parts = words(reply.lines[i]);
        if (parts.empty())
            continue;
        std::string keyword = upper(parts.front());
        // `AUTH=PLAIN LOGIN`: the keyword and its first mechanism in one word.
        if (keyword.size() > 5 && keyword.compare(0, 5, "AUTH=") == 0) {
            parts.insert(parts.begin() + 1, keyword.substr(5));
            keyword = "AUTH";
        }
        if (keyword == "STARTTLS") {
            out.startTls = true;
        } else if (keyword == "8BITMIME") {
            out.eightBitMime = true;
        } else if (keyword == "SMTPUTF8") {
            out.smtpUtf8 = true;
        } else if (keyword == "PIPELINING") {
            out.pipelining = true;
        } else if (keyword == "SIZE") {
            if (parts.size() > 1) {
                std::size_t size = 0;
                for (const char c : parts[1]) {
                    if (c < '0' || c > '9')
                        break;
                    size = size * 10 + static_cast<std::size_t>(c - '0');
                }
                out.maxSize = size;
            }
        } else if (keyword == "AUTH") {
            for (std::size_t m = 1; m < parts.size(); ++m) {
                const std::string mechanism = upper(parts[m]);
                out.authMechanisms.push_back(mechanism);
                if (mechanism == "PLAIN")
                    out.authPlain = true;
                else if (mechanism == "LOGIN")
                    out.authLogin = true;
            }
        }
    }
    return out;
}

std::string authPlainInitialResponse(std::string_view user, std::string_view password)
{
    std::string raw;
    raw.reserve(user.size() + password.size() + 2);
    raw.push_back('\0');
    raw.append(user);
    raw.push_back('\0');
    raw.append(password);
    return mime::base64(raw);
}

std::string authLoginResponse(std::string_view value)
{
    return mime::base64(value);
}

std::string dataBlock(std::string_view message)
{
    std::string out;
    out.reserve(message.size() + message.size() / 32 + 8);
    bool lineStart = true;
    for (std::size_t i = 0; i < message.size(); ++i) {
        const char c = message[i];
        if (c == '\r') {
            // A CR that is not part of CRLF is a stray and becomes a line end
            // like any other; the LF after it is swallowed below.
            out += "\r\n";
            if (i + 1 < message.size() && message[i + 1] == '\n')
                ++i;
            lineStart = true;
            continue;
        }
        if (c == '\n') {
            out += "\r\n";
            lineStart = true;
            continue;
        }
        if (lineStart && c == '.')
            out.push_back('.');
        out.push_back(c);
        lineStart = false;
    }
    if (!lineStart)
        out += "\r\n";
    out += ".\r\n";
    return out;
}

} // namespace phicore::adapter::net::smtp
