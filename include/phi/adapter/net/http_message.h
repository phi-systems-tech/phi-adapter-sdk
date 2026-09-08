#pragma once

// HTTP/1.1 on the wire: what goes out, and how to read what comes back.
//
// Pure over bytes, and incremental, because a response arrives in whatever
// pieces the network chose. The parser is separate from the connection for the
// same reason the eISCP scanner is: a body split across three reads, a chunked
// transfer, and a header line that never ends are all things worth handing to
// something on purpose, and none of them need a socket.

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace phicore::adapter::net {

using Header = std::pair<std::string, std::string>;

struct Request {
    std::string method = "GET";
    /// Path and query, exactly as it goes in the request line. The digest
    /// covers this string, so it must be the one that was signed.
    std::string target = "/";
    /// `host` or `host:port`, as the Host header requires it.
    std::string authority;
    std::vector<Header> headers;
    std::string body;
};

/// The request as bytes. Host, Content-Length and Connection are added here so
/// no caller has to remember them; anything already in `headers` is left alone.
std::string serializeRequest(const Request &request);

struct Response {
    int status = 0;
    std::vector<Header> headers;
    std::string body;

    /// First header with this name, case-insensitively; empty when absent.
    [[nodiscard]] std::string header(std::string_view name) const;
};

/**
 * @brief Feeds bytes in, answers whether a whole response is there yet.
 *
 * Understands the two framings a device uses: `Content-Length`, and `chunked`.
 * A response with neither and no body-forbidding status is read to the close of
 * the connection, which is what HTTP/1.0 servers still do.
 */
class ResponseParser
{
public:
    enum class State {
        NeedMore,   ///< nothing wrong, just not finished
        Complete,
        Malformed,  ///< no way to find the end; the caller drops the connection
    };

    /// Adds bytes and re-examines. Safe to call with an empty view.
    State consume(std::string_view bytes);

    /**
     * @brief Tells the parser the peer closed.
     *
     * For a response framed only by the close this is what completes it; for
     * one that was still expecting bytes it is a truncation.
     */
    State finish();

    [[nodiscard]] const Response &response() const { return m_response; }
    [[nodiscard]] State state() const { return m_state; }
    void reset();

    /// The head has been read: status and headers are final, and the framing
    /// below is known. A caller streaming a body that never ends takes over
    /// from here rather than letting the parser hold the whole stream.
    [[nodiscard]] bool headComplete() const { return m_headComplete; }
    /// Offset of the first body byte in everything consumed so far.
    [[nodiscard]] std::size_t bodyStart() const { return m_bodyStart; }
    [[nodiscard]] bool chunked() const { return m_chunked; }
    [[nodiscard]] bool hasContentLength() const { return m_hasContentLength; }
    [[nodiscard]] std::size_t contentLength() const { return m_contentLength; }

private:
    State parse();
    State parseHead();
    State parseBody();

    std::string m_buffer;
    Response m_response;
    State m_state = State::NeedMore;
    bool m_headComplete = false;
    bool m_chunked = false;
    bool m_hasContentLength = false;
    bool m_closed = false;
    std::size_t m_contentLength = 0;
    std::size_t m_bodyStart = 0;
};

/// Splits `http://host:port/path` into its parts. False for anything this
/// client cannot address.
bool parseUrl(std::string_view url,
              bool *secure,
              std::string *host,
              std::string *port,
              std::string *target);

} // namespace phicore::adapter::net
