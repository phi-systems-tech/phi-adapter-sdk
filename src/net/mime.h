#pragma once
// The little of RFC 5322 and MIME a mail from an adapter needs: a header that
// cannot be broken by its value, a subject in any language, a body that
// survives every server on the way, a date and an id. Pure functions, so the
// bytes can be pinned in a test without a socket.

#include <ctime>
#include <string>
#include <string_view>

#include "phi/adapter/net/smtp_client.h"

namespace phicore::adapter::net::mime {

/// Standard alphabet with padding. With `lineBreaks`, CRLF every 76
/// characters, as a body needs.
std::string base64(std::string_view bytes, bool lineBreaks = false);

/// Every byte below 0x80 and none of the controls that end a header line.
bool isPlainHeaderText(std::string_view text);

/**
 * @brief A header value from text in any language.
 *
 * CR and LF are dropped first - a value that could end its own line and
 * start another header is the one injection a mail client has to refuse.
 * Plain ASCII goes as it is; anything else becomes RFC 2047 encoded words,
 * each short enough for a 76-character line, split only between whole UTF-8
 * characters.
 */
std::string headerText(std::string_view text);

/// `"Name" <address>`, the name quoted or encoded as it needs; `<address>`
/// alone without one. The address itself is never encoded.
std::string mailboxText(const Mailbox &mailbox);

/// RFC 5322 date in UTC: `Mon, 02 Nov 2026 10:11:12 +0000`.
std::string dateText(std::time_t when);

/// `<token@domain>`; the domain is the sender's when it has one.
std::string messageId(std::string_view token, std::string_view senderAddress);

/// Whether any address needs SMTPUTF8 (a local part or domain beyond ASCII).
bool needsSmtpUtf8(const SmtpClient::Message &message);

/**
 * @brief The whole message, headers and body, lines ending in CRLF.
 *
 * Text and HTML bodies go base64 in UTF-8: the one encoding every relay
 * passes untouched, at the price of a third more bytes, which a notification
 * does not notice. With an HTML part the message is multipart/alternative
 * with the text first, as the RFC orders the alternatives.
 */
std::string serialize(const SmtpClient::Message &message, std::time_t now, const std::string &messageId,
                      const std::string &boundaryToken);

} // namespace phicore::adapter::net::mime
