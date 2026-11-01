#pragma once
// SMTP as text: the reply a server sends, what its EHLO answer offers, and
// the two strings a client has to get byte-exact. Everything here is pure -
// bytes in, a value out - so it is pinned against examples from RFC 5321 and
// RFC 4954 rather than against itself.

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace phicore::adapter::net::smtp {

/// One reply: a three-digit code and one line or several.
struct Reply {
    int code = 0;
    std::vector<std::string> lines;

    /// 2xx and 3xx: the server agreed, or asked for the next piece (354, 334).
    [[nodiscard]] bool positive() const noexcept { return code >= 200 && code < 400; }
    /// The first line, which carries the sentence a person reads.
    [[nodiscard]] std::string firstLine() const { return lines.empty() ? std::string() : lines.front(); }
    /// The lines joined with newlines, for a log.
    [[nodiscard]] std::string text() const;
};

/**
 * @brief Reads replies out of a byte stream as it arrives.
 *
 * A reply ends with a line whose fourth character is a space (`250 ok`);
 * a hyphen there (`250-STARTTLS`) continues it. The code has to agree across
 * the lines; a line without a code, or one longer than a server may send, is
 * malformed and ends the exchange rather than being guessed at.
 */
class ReplyParser
{
public:
    enum class State { NeedMore, Complete, Malformed };

    State consume(std::string_view bytes);
    [[nodiscard]] State state() const noexcept { return m_state; }
    [[nodiscard]] const Reply &reply() const noexcept { return m_reply; }
    /// Ready for the next reply; bytes after the last one are kept.
    void reset();

private:
    State m_state = State::NeedMore;
    Reply m_reply;
    std::string m_buffer;
};

/// What the EHLO answer offered, of the things this client acts on.
struct Capabilities {
    bool startTls = false;
    bool authPlain = false;
    bool authLogin = false;
    bool eightBitMime = false;
    bool smtpUtf8 = false;
    bool pipelining = false;
    /// SIZE, 0 when not announced.
    std::size_t maxSize = 0;
    /// The AUTH mechanisms as announced, for an error that names them.
    std::vector<std::string> authMechanisms;
};

/// Reads an EHLO reply. The first line is the greeting and carries no
/// keyword. `AUTH=PLAIN LOGIN`, the pre-standard form some servers still
/// send beside the standard one, is read like `AUTH PLAIN LOGIN`.
Capabilities parseEhlo(const Reply &reply);

/// The initial response of AUTH PLAIN (RFC 4616): `\0user\0password`, base64.
std::string authPlainInitialResponse(std::string_view user, std::string_view password);

/// A line of the AUTH LOGIN dialogue: the user or the password, base64.
std::string authLoginResponse(std::string_view value);

/**
 * @brief The message as it goes after DATA.
 *
 * Every line ends in CRLF whatever it ended in before, a line that starts
 * with a dot gets a second one (the receiver removes it), and the whole
 * thing ends with the lone dot that means "that was the message".
 */
std::string dataBlock(std::string_view message);

} // namespace phicore::adapter::net::smtp
