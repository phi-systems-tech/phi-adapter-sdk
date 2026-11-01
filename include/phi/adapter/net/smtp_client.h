#pragma once
// One mail at a time, handed to a server that delivers it, driven by a
// phi::runtime loop.
//
// Submission, not delivery: the client talks to one server the account names
// (port 587 with STARTTLS, 465 with TLS from the first byte, or a relay in the
// house), authenticates, hands the message over and is done. It never looks up
// the recipient's MX. A box on a home connection could not deliver a mail that
// way if it tried - the address is on every blocklist, port 25 is often shut,
// SPF for the sender's domain does not name the house, and greylisting would
// want a queue - and the server the account names does all of that already.
//
// There is no "try TLS". An account that asks for STARTTLS fails when the
// server does not offer it, because the alternative is an adapter that says
// "TLS" in its form while the password crosses the wire in clear. The
// certificate checks are the contract's (`v1::TlsSettings`), decided in one
// place with the HTTP clients.
//
// Account, Message and Result are behind d-pointers (see
// phi/adapter/v1/channel.h for the rule): a client certificate, an attachment
// and a delivery receipt are the obvious next fields.

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "phi/adapter/net/http_types.h"
#include "phi/adapter/v1/tlsconfig.h"

namespace phi::runtime {
class Loop;
}

namespace phicore::adapter::net {

/// An address and the name a person sees beside it; the name may be empty.
struct Mailbox {
    std::string address;
    std::string name;
};

class SmtpClient
{
public:
    /// How the connection is protected.
    enum class Security : std::uint8_t {
        /// Nothing: a relay in the house that offers no TLS. Credentials then
        /// travel in clear, which is why the mail adapter's form does not
        /// offer it.
        None = 0,
        /// Plain connection, upgraded with STARTTLS before anything else is
        /// said; refused when the server does not offer the upgrade.
        StartTls = 1,
        /// TLS from the first byte (RFC 8314's "submissions").
        Tls = 2,
    };

    /// Where to submit, and as whom.
    class Account
    {
    public:
        Account();
        Account(const Account &other);
        Account(Account &&other) noexcept;
        Account &operator=(const Account &other);
        Account &operator=(Account &&other) noexcept;
        ~Account();

        const std::string &host() const noexcept;
        Account &setHost(std::string host);
        /// 0 picks the port the security implies: 587, 465, or 25 for none.
        std::uint16_t port() const noexcept;
        Account &setPort(std::uint16_t port);
        Security security() const noexcept;
        Account &setSecurity(Security security);
        /// Empty user: no AUTH, for a relay that trusts the address it sees.
        const Credentials &credentials() const noexcept;
        Account &setCredentials(Credentials credentials);
        /// The certificate checks; `enabled()` is not read, `security()` decides.
        const v1::TlsSettings &tls() const noexcept;
        v1::TlsSettings &tls();
        Account &setTls(v1::TlsSettings tls);
        /// What the client calls itself in EHLO; the machine's hostname when empty.
        const std::string &clientName() const noexcept;
        Account &setClientName(std::string name);
        /// The whole exchange, connect included; 30 s by default. A mail
        /// server is slower than a device on the LAN, and a greylisting one
        /// thinks before it answers.
        std::chrono::milliseconds timeout() const noexcept;
        Account &setTimeout(std::chrono::milliseconds timeout);

    private:
        struct Private;
        const Private &p() const noexcept;
        Private &m();
        std::unique_ptr<Private> d;
    };

    /// What is sent. The client writes the message itself (Date, Message-ID,
    /// encoded subject, UTF-8 body), so an adapter hands over text, never
    /// MIME.
    class Message
    {
    public:
        Message();
        Message(const Message &other);
        Message(Message &&other) noexcept;
        Message &operator=(const Message &other);
        Message &operator=(Message &&other) noexcept;
        ~Message();

        const Mailbox &from() const noexcept;
        Message &setFrom(Mailbox from);
        const std::vector<Mailbox> &to() const noexcept;
        Message &setTo(std::vector<Mailbox> to);
        Message &addTo(Mailbox to);
        /// Where an answer goes when not to `from`; empty address means none.
        const Mailbox &replyTo() const noexcept;
        Message &setReplyTo(Mailbox replyTo);
        const std::string &subject() const noexcept;
        Message &setSubject(std::string subject);
        /// The text, UTF-8; sent as text/plain.
        const std::string &text() const noexcept;
        Message &setText(std::string text);
        /// Optional; with it the mail is multipart/alternative, text first.
        const std::string &html() const noexcept;
        Message &setHtml(std::string html);
        /// Further headers, verbatim names; CR and LF in a value are dropped.
        const std::vector<Header> &headers() const noexcept;
        Message &setHeaders(std::vector<Header> headers);
        Message &addHeader(std::string name, std::string value);

    private:
        struct Private;
        const Private &p() const noexcept;
        Private &m();
        std::unique_ptr<Private> d;
    };

    /// What the server said, or why it was never asked.
    class Result
    {
    public:
        Result();
        Result(const Result &other);
        Result(Result &&other) noexcept;
        Result &operator=(const Result &other);
        Result &operator=(Result &&other) noexcept;
        ~Result();

        /// The message was accepted for at least one recipient (a probe: the
        /// account works). False with `error` set otherwise.
        bool ok() const noexcept;
        Result &setOk(bool ok);
        const std::string &error() const noexcept;
        Result &setError(std::string error);
        /// The last reply's code, 0 when the server never answered.
        int code() const noexcept;
        Result &setCode(int code);
        /// The last reply's text, the server's own words: what an operator
        /// reads to know whether it was the password or the sender address.
        const std::string &reply() const noexcept;
        Result &setReply(std::string reply);
        /// The server was reached and refused the credentials.
        bool authenticationFailed() const noexcept;
        Result &setAuthenticationFailed(bool failed);
        /// Recipients the server refused while accepting the others; the
        /// message went to the rest. All refused is not ok().
        const std::vector<std::string> &rejectedRecipients() const noexcept;
        Result &setRejectedRecipients(std::vector<std::string> rejected);
        /// The Message-ID the client wrote, for a log or a thread.
        const std::string &messageId() const noexcept;
        Result &setMessageId(std::string messageId);

    private:
        struct Private;
        const Private &p() const noexcept;
        Private &m();
        std::unique_ptr<Private> d;
    };

    using Done = std::function<void(Result)>;

    explicit SmtpClient(phi::runtime::Loop &loop);
    ~SmtpClient();
    SmtpClient(const SmtpClient &) = delete;
    SmtpClient &operator=(const SmtpClient &) = delete;

    /// An exchange is in flight; send() and probe() refuse a second one.
    [[nodiscard]] bool busy() const;

    /// Connects, submits `message`, quits; `done` on the loop. False when
    /// busy or when the message has no sender or no recipient.
    bool send(Account account, Message message, Done done);
    /// Connects, greets, secures and authenticates, then quits without
    /// sending: the test of an account, for a form's "test" action.
    bool probe(Account account, Done done);
    /// Drops the exchange in flight; its `done` is not called.
    void cancel();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace phicore::adapter::net
