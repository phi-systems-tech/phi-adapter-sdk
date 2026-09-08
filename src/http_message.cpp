#include "phi/adapter/net/http_message.h"

#include <algorithm>
#include <cctype>
#include <charconv>

namespace phicore::adapter::net {

namespace {

constexpr std::size_t kMaxHeadSize = 64 * 1024;
constexpr std::size_t kMaxBodySize = 32 * 1024 * 1024;

bool isWs(char c)
{
    return c == ' ' || c == '\t';
}

std::string_view trim(std::string_view s)
{
    while (!s.empty() && (isWs(s.front()) || s.front() == '\r' || s.front() == '\n'))
        s.remove_prefix(1);
    while (!s.empty() && (isWs(s.back()) || s.back() == '\r' || s.back() == '\n'))
        s.remove_suffix(1);
    return s;
}

bool equalsIgnoreCase(std::string_view lhs, std::string_view rhs)
{
    if (lhs.size() != rhs.size())
        return false;
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(lhs[i]))
            != std::tolower(static_cast<unsigned char>(rhs[i])))
            return false;
    }
    return true;
}

bool hasHeader(const std::vector<Header> &headers, std::string_view name)
{
    return std::any_of(headers.begin(), headers.end(), [name](const Header &header) {
        return equalsIgnoreCase(header.first, name);
    });
}

/// A hex chunk size, ignoring any `;ext=...` the sender appended.
bool parseChunkSize(std::string_view line, std::size_t *size)
{
    const std::size_t semicolon = line.find(';');
    std::string_view digits = trim(semicolon == std::string_view::npos ? line
                                                                      : line.substr(0, semicolon));
    if (digits.empty())
        return false;
    std::size_t value = 0;
    for (const char c : digits) {
        int digit = 0;
        if (c >= '0' && c <= '9')
            digit = c - '0';
        else if (c >= 'a' && c <= 'f')
            digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            digit = c - 'A' + 10;
        else
            return false;
        if (value > (kMaxBodySize >> 4))
            return false;
        value = value * 16 + static_cast<std::size_t>(digit);
    }
    *size = value;
    return true;
}

/// Bodies these statuses never have, whatever the headers claim.
bool statusHasNoBody(int status)
{
    return status == 204 || status == 304 || (status >= 100 && status < 200);
}

} // namespace

std::string Response::header(std::string_view name) const
{
    for (const Header &entry : headers) {
        if (equalsIgnoreCase(entry.first, name))
            return entry.second;
    }
    return {};
}

std::string serializeRequest(const Request &request)
{
    std::string out;
    out.reserve(256 + request.body.size());
    out += request.method;
    out += ' ';
    out += request.target.empty() ? "/" : request.target;
    out += " HTTP/1.1\r\n";

    if (!hasHeader(request.headers, "Host")) {
        out += "Host: ";
        out += request.authority;
        out += "\r\n";
    }
    for (const Header &header : request.headers) {
        out += header.first;
        out += ": ";
        out += header.second;
        out += "\r\n";
    }
    if (!hasHeader(request.headers, "Content-Length")) {
        // Always, including zero: a POST without it makes a server wait for a
        // body that is not coming.
        out += "Content-Length: ";
        out += std::to_string(request.body.size());
        out += "\r\n";
    }
    if (!hasHeader(request.headers, "Connection")) {
        // One request per connection. Keep-alive would save the handshake, and
        // would also mean owning a connection pool and its idle timeouts; a
        // device on the LAN is not worth that.
        out += "Connection: close\r\n";
    }
    out += "\r\n";
    out += request.body;
    return out;
}

void ResponseParser::reset()
{
    m_buffer.clear();
    m_response = Response{};
    m_state = State::NeedMore;
    m_headComplete = false;
    m_chunked = false;
    m_hasContentLength = false;
    m_closed = false;
    m_contentLength = 0;
    m_bodyStart = 0;
}

ResponseParser::State ResponseParser::consume(std::string_view bytes)
{
    if (m_state != State::NeedMore)
        return m_state;
    if (m_buffer.size() + bytes.size() > kMaxBodySize) {
        m_state = State::Malformed;
        return m_state;
    }
    m_buffer.append(bytes);
    return parse();
}

ResponseParser::State ResponseParser::finish()
{
    if (m_state != State::NeedMore)
        return m_state;
    m_closed = true;
    return parse();
}

ResponseParser::State ResponseParser::parse()
{
    if (!m_headComplete) {
        const State head = parseHead();
        if (head != State::NeedMore || !m_headComplete)
            return head;
    }
    return parseBody();
}

ResponseParser::State ResponseParser::parseHead()
{
    const std::size_t end = m_buffer.find("\r\n\r\n");
    if (end == std::string::npos) {
        if (m_buffer.size() > kMaxHeadSize) {
            // A head that never ends is not a slow server, it is a stream this
            // parser has no way to finish.
            m_state = State::Malformed;
        } else if (m_closed) {
            m_state = State::Malformed;
        }
        return m_state;
    }

    std::string_view head(m_buffer.data(), end);
    const std::size_t statusEnd = head.find("\r\n");
    const std::string_view statusLine =
        statusEnd == std::string_view::npos ? head : head.substr(0, statusEnd);

    // "HTTP/1.1 200 OK"
    const std::size_t firstSpace = statusLine.find(' ');
    if (firstSpace == std::string_view::npos || statusLine.rfind("HTTP/", 0) != 0) {
        m_state = State::Malformed;
        return m_state;
    }
    const std::string_view rest = statusLine.substr(firstSpace + 1);
    int status = 0;
    const auto parsed = std::from_chars(rest.data(), rest.data() + std::min<std::size_t>(3, rest.size()),
                                        status);
    if (parsed.ec != std::errc() || status < 100 || status > 599) {
        m_state = State::Malformed;
        return m_state;
    }
    m_response.status = status;

    std::size_t pos = statusEnd == std::string_view::npos ? head.size() : statusEnd + 2;
    while (pos < head.size()) {
        const std::size_t eol = head.find("\r\n", pos);
        const std::string_view line =
            head.substr(pos, (eol == std::string_view::npos ? head.size() : eol) - pos);
        pos = eol == std::string_view::npos ? head.size() : eol + 2;
        if (line.empty())
            continue;
        const std::size_t colon = line.find(':');
        if (colon == std::string_view::npos)
            continue;
        m_response.headers.emplace_back(std::string(trim(line.substr(0, colon))),
                                        std::string(trim(line.substr(colon + 1))));
    }

    const std::string transferEncoding = m_response.header("Transfer-Encoding");
    m_chunked = !transferEncoding.empty()
        && transferEncoding.find("chunked") != std::string::npos;

    const std::string contentLength = m_response.header("Content-Length");
    if (!m_chunked && !contentLength.empty()) {
        std::size_t value = 0;
        const std::string_view view = contentLength;
        const auto lengthParsed =
            std::from_chars(view.data(), view.data() + view.size(), value);
        if (lengthParsed.ec != std::errc() || value > kMaxBodySize) {
            m_state = State::Malformed;
            return m_state;
        }
        m_hasContentLength = true;
        m_contentLength = value;
    }

    m_bodyStart = end + 4;
    m_headComplete = true;
    return State::NeedMore;
}

ResponseParser::State ResponseParser::parseBody()
{
    if (statusHasNoBody(m_response.status)) {
        m_state = State::Complete;
        return m_state;
    }

    std::string_view available(m_buffer.data() + m_bodyStart, m_buffer.size() - m_bodyStart);

    if (m_chunked) {
        std::string body;
        std::size_t pos = 0;
        for (;;) {
            const std::size_t eol = available.find("\r\n", pos);
            if (eol == std::string_view::npos) {
                if (m_closed)
                    m_state = State::Malformed;
                return m_state;
            }
            std::size_t chunkSize = 0;
            if (!parseChunkSize(available.substr(pos, eol - pos), &chunkSize)) {
                m_state = State::Malformed;
                return m_state;
            }
            const std::size_t dataStart = eol + 2;
            if (chunkSize == 0) {
                // The trailer, and then the empty line that ends it. Trailers
                // are not surfaced: nothing an adapter talks to sends one.
                const std::size_t trailerEnd = available.find("\r\n", dataStart);
                if (trailerEnd == std::string_view::npos && !m_closed)
                    return m_state;
                m_response.body = std::move(body);
                m_state = State::Complete;
                return m_state;
            }
            if (dataStart + chunkSize + 2 > available.size()) {
                if (m_closed)
                    m_state = State::Malformed;
                return m_state;
            }
            body.append(available.substr(dataStart, chunkSize));
            pos = dataStart + chunkSize + 2;
        }
    }

    if (m_hasContentLength) {
        if (available.size() < m_contentLength) {
            if (m_closed)
                m_state = State::Malformed;
            return m_state;
        }
        m_response.body.assign(available.substr(0, m_contentLength));
        m_state = State::Complete;
        return m_state;
    }

    // Neither framing: the body is whatever arrives until the peer closes.
    if (!m_closed)
        return m_state;
    m_response.body.assign(available);
    m_state = State::Complete;
    return m_state;
}

bool parseUrl(std::string_view url,
              bool *secure,
              std::string *host,
              std::string *port,
              std::string *target)
{
    bool https = false;
    if (url.rfind("http://", 0) == 0) {
        url.remove_prefix(7);
    } else if (url.rfind("https://", 0) == 0) {
        url.remove_prefix(8);
        https = true;
    } else {
        return false;
    }

    const std::size_t slash = url.find('/');
    std::string_view authority = slash == std::string_view::npos ? url : url.substr(0, slash);
    const std::string_view path = slash == std::string_view::npos ? std::string_view("/")
                                                                  : url.substr(slash);
    if (authority.empty())
        return false;

    std::string hostPart(authority);
    std::string portPart = https ? "443" : "80";
    // An IPv6 literal is bracketed, and the colons inside it are not a port.
    if (!hostPart.empty() && hostPart.front() == '[') {
        const std::size_t close = hostPart.find(']');
        if (close == std::string::npos)
            return false;
        if (close + 1 < hostPart.size() && hostPart[close + 1] == ':')
            portPart = hostPart.substr(close + 2);
        hostPart = hostPart.substr(1, close - 1);
    } else {
        const std::size_t colon = hostPart.rfind(':');
        if (colon != std::string::npos) {
            portPart = hostPart.substr(colon + 1);
            hostPart = hostPart.substr(0, colon);
        }
    }
    if (hostPart.empty() || portPart.empty())
        return false;

    if (secure)
        *secure = https;
    if (host)
        *host = hostPart;
    if (port)
        *port = portPart;
    if (target)
        *target = std::string(path);
    return true;
}

} // namespace phicore::adapter::net
