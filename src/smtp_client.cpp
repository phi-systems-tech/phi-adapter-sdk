#include "phi/adapter/net/smtp_client.h"

#include "net/http_auth.h"
#include "net/loop_stream.h"
#include "net/mime.h"
#include "net/smtp_protocol.h"
#include "phi/runtime/loop.h"

#include <ctime>
#include <unistd.h>

namespace phicore::adapter::net {

// --- the value types -----------------------------------------------------------

#define PHI_SMTP_MEMBERS(Q, Class)                                                                         \
    Q::Class() : d(std::make_unique<Private>()) {}                                                      \
    Q::Class(const Q &other) : d(other.d ? std::make_unique<Private>(*other.d) : nullptr) {}            \
    Q::Class(Q &&other) noexcept : d(std::move(other.d)) {}                                             \
    Q &Q::operator=(const Q &other)                                                                     \
    {                                                                                                   \
        if (this != &other)                                                                             \
            d = other.d ? std::make_unique<Private>(*other.d) : nullptr;                                \
        return *this;                                                                                   \
    }                                                                                                   \
    Q &Q::operator=(Q &&other) noexcept                                                                 \
    {                                                                                                   \
        d = std::move(other.d);                                                                         \
        return *this;                                                                                   \
    }                                                                                                   \
    Q::~Class() = default;                                                                              \
    const Q::Private &Q::p() const noexcept                                                             \
    {                                                                                                   \
        static const Private empty;                                                                     \
        return d ? *d : empty;                                                                          \
    }                                                                                                   \
    Q::Private &Q::m()                                                                                  \
    {                                                                                                   \
        if (!d)                                                                                         \
            d = std::make_unique<Private>();                                                            \
        return *d;                                                                                      \
    }

#define PHI_SMTP_FIELD(Q, Type, name, setter)                                                              \
    Type Q::name() const noexcept { return p().name; }                                                  \
    Q &Q::setter(std::remove_cv_t<std::remove_reference_t<Type>> value)                                 \
    {                                                                                                   \
        m().name = std::move(value);                                                                    \
        return *this;                                                                                   \
    }

struct SmtpClient::Account::Private {
    std::string host;
    std::uint16_t port = 0;
    Security security = Security::StartTls;
    Credentials credentials;
    v1::TlsSettings tls;
    std::string clientName;
    std::chrono::milliseconds timeout{30000};
};
PHI_SMTP_MEMBERS(SmtpClient::Account, Account)
PHI_SMTP_FIELD(SmtpClient::Account, const std::string &, host, setHost)
PHI_SMTP_FIELD(SmtpClient::Account, std::uint16_t, port, setPort)
PHI_SMTP_FIELD(SmtpClient::Account, SmtpClient::Security, security, setSecurity)
PHI_SMTP_FIELD(SmtpClient::Account, const Credentials &, credentials, setCredentials)
PHI_SMTP_FIELD(SmtpClient::Account, const std::string &, clientName, setClientName)
PHI_SMTP_FIELD(SmtpClient::Account, std::chrono::milliseconds, timeout, setTimeout)
const v1::TlsSettings &SmtpClient::Account::tls() const noexcept { return p().tls; }
v1::TlsSettings &SmtpClient::Account::tls() { return m().tls; }
SmtpClient::Account &SmtpClient::Account::setTls(v1::TlsSettings tls)
{
    m().tls = std::move(tls);
    return *this;
}

struct SmtpClient::Message::Private {
    Mailbox from;
    std::vector<Mailbox> to;
    Mailbox replyTo;
    std::string subject;
    std::string text;
    std::string html;
    std::vector<Header> headers;
};
PHI_SMTP_MEMBERS(SmtpClient::Message, Message)
PHI_SMTP_FIELD(SmtpClient::Message, const Mailbox &, from, setFrom)
PHI_SMTP_FIELD(SmtpClient::Message, const std::vector<Mailbox> &, to, setTo)
PHI_SMTP_FIELD(SmtpClient::Message, const Mailbox &, replyTo, setReplyTo)
PHI_SMTP_FIELD(SmtpClient::Message, const std::string &, subject, setSubject)
PHI_SMTP_FIELD(SmtpClient::Message, const std::string &, text, setText)
PHI_SMTP_FIELD(SmtpClient::Message, const std::string &, html, setHtml)
PHI_SMTP_FIELD(SmtpClient::Message, const std::vector<Header> &, headers, setHeaders)
SmtpClient::Message &SmtpClient::Message::addTo(Mailbox to)
{
    m().to.push_back(std::move(to));
    return *this;
}
SmtpClient::Message &SmtpClient::Message::addHeader(std::string name, std::string value)
{
    m().headers.emplace_back(std::move(name), std::move(value));
    return *this;
}

struct SmtpClient::Result::Private {
    bool ok = false;
    std::string error;
    int code = 0;
    std::string reply;
    bool authenticationFailed = false;
    std::vector<std::string> rejectedRecipients;
    std::string messageId;
};
PHI_SMTP_MEMBERS(SmtpClient::Result, Result)
PHI_SMTP_FIELD(SmtpClient::Result, bool, ok, setOk)
PHI_SMTP_FIELD(SmtpClient::Result, const std::string &, error, setError)
PHI_SMTP_FIELD(SmtpClient::Result, int, code, setCode)
PHI_SMTP_FIELD(SmtpClient::Result, const std::string &, reply, setReply)
PHI_SMTP_FIELD(SmtpClient::Result, bool, authenticationFailed, setAuthenticationFailed)
PHI_SMTP_FIELD(SmtpClient::Result, const std::vector<std::string> &, rejectedRecipients,
               setRejectedRecipients)
PHI_SMTP_FIELD(SmtpClient::Result, const std::string &, messageId, setMessageId)

#undef PHI_SMTP_FIELD
#undef PHI_SMTP_MEMBERS

// --- the exchange ------------------------------------------------------------------

namespace {

std::string machineName()
{
    char name[256] = {};
    if (::gethostname(name, sizeof(name) - 1) != 0 || name[0] == '\0')
        return "phi.local";
    std::string out(name);
    // EHLO wants a domain name; a bare host is refused by a server that
    // checks (reject_non_fqdn_helo_hostname), and .local is what the box
    // answers to on the LAN anyway.
    if (out.find('.') == std::string::npos)
        out += ".local";
    return out;
}

std::string join(const std::vector<std::string> &items)
{
    std::string out;
    for (const std::string &item : items) {
        if (!out.empty())
            out += ", ";
        out += item;
    }
    return out;
}

} // namespace

struct SmtpClient::Impl {
    enum class Step {
        Idle,
        Greeting,
        Ehlo,
        StartTls,
        EhloAgain,
        AuthPlain,
        AuthLoginUser,
        AuthLoginPassword,
        MailFrom,
        RcptTo,
        Data,
        Body,
        Quit,
    };

    explicit Impl(phi::runtime::Loop &loopRef) : loop(loopRef), stream(loopRef)
    {
        detail::LoopStream::Handlers handlers;
        handlers.connected = [this]() { onConnected(); };
        handlers.secured = [this]() { onSecured(); };
        handlers.data = [this](std::string_view bytes) { onData(bytes); };
        handlers.closed = [this]() { onClosed(); };
        handlers.failed = [this](std::string error) { onFailed(std::move(error)); };
        stream.setHandlers(std::move(handlers));
    }

    phi::runtime::Loop &loop;
    detail::LoopStream stream;
    phi::runtime::Timer deadline;

    Account account;
    Message message;
    bool probing = false;
    Done done;
    bool busy = false;
    Step step = Step::Idle;

    smtp::ReplyParser parser;
    smtp::Capabilities caps;
    std::string lastReply;
    int lastCode = 0;

    std::string messageId;
    std::string body;
    std::size_t rcptIndex = 0;
    std::size_t accepted = 0;
    std::vector<std::string> rejected;
    /// What send() ends with once QUIT has been said; the server's answer to
    /// QUIT, or its silence, changes nothing about it.
    Result pending;

    bool start(Account newAccount, Message newMessage, bool probe, Done newDone)
    {
        loop.assertOnLoop("SmtpClient::send");
        if (busy || newAccount.host().empty())
            return false;
        if (!probe) {
            if (newMessage.from().address.empty())
                return false;
            bool anyRecipient = false;
            for (const Mailbox &box : newMessage.to())
                anyRecipient = anyRecipient || !box.address.empty();
            if (!anyRecipient)
                return false;
        }
        account = std::move(newAccount);
        message = std::move(newMessage);
        probing = probe;
        done = std::move(newDone);
        busy = true;
        step = Step::Greeting;
        parser = smtp::ReplyParser{};
        caps = smtp::Capabilities{};
        lastReply.clear();
        lastCode = 0;
        messageId.clear();
        body.clear();
        rcptIndex = 0;
        accepted = 0;
        rejected.clear();
        pending = Result{};

        std::uint16_t port = account.port();
        if (port == 0) {
            port = account.security() == Security::Tls ? 465
                : account.security() == Security::StartTls ? 587
                                                            : 25;
        }
        deadline = loop.timerAfter(account.timeout(), [this]() { onDeadline(); });
        const bool tlsAtConnect = account.security() == Security::Tls;
        stream.connect(account.host(), std::to_string(port), tlsAtConnect ? &account.tls() : nullptr,
                       account.host());
        return true;
    }

    void cancel()
    {
        stream.close();
        deadline.reset();
        busy = false;
        done = nullptr;
        step = Step::Idle;
    }

    // --- outcomes ----------------------------------------------------------

    void finish(Result result)
    {
        deadline.reset();
        stream.close();
        busy = false;
        step = Step::Idle;
        result.setRejectedRecipients(rejected);
        if (!messageId.empty())
            result.setMessageId(messageId);
        // Moved out first: the callback may start the next exchange from
        // inside itself and must find an idle client.
        Done callback = std::move(done);
        done = nullptr;
        if (callback)
            callback(std::move(result));
    }

    void fail(std::string error)
    {
        Result result;
        result.setOk(false);
        result.setError(std::move(error));
        result.setCode(lastCode);
        result.setReply(lastReply);
        finish(std::move(result));
    }

    void failWith(const char *what, const smtp::Reply &reply)
    {
        fail(std::string(what) + ": " + std::to_string(reply.code) + " " + reply.firstLine());
    }

    void authenticationFailed(const smtp::Reply &reply)
    {
        Result result;
        result.setOk(false);
        result.setAuthenticationFailed(true);
        // The server was reached and said no to the credentials. Saying that,
        // rather than "535", is what sends somebody to their password rather
        // than to their network.
        result.setError("Authentication failed: " + std::to_string(reply.code) + " " + reply.firstLine());
        result.setCode(reply.code);
        result.setReply(reply.text());
        finish(std::move(result));
    }

    void succeed()
    {
        pending = Result{};
        pending.setOk(true);
        pending.setCode(lastCode);
        pending.setReply(lastReply);
        step = Step::Quit;
        stream.write("QUIT\r\n");
    }

    void onDeadline()
    {
        if (step == Step::Quit) {
            finish(pending);
            return;
        }
        fail(step == Step::Greeting && !stream.open() ? "Connection timed out" : "Server did not answer in time");
    }

    // --- the stream --------------------------------------------------------

    void onConnected()
    {
        // The server speaks first (220); the greeting is awaited, not asked for.
    }

    void onSecured()
    {
        // Everything said before STARTTLS is forgotten, the capabilities
        // included: a server may offer more once the line is private.
        parser = smtp::ReplyParser{};
        caps = smtp::Capabilities{};
        sendEhlo(Step::EhloAgain);
    }

    void onClosed()
    {
        if (step == Step::Quit) {
            finish(pending);
            return;
        }
        fail(lastReply.empty() ? std::string("Connection closed by the server")
                               : "Connection closed by the server after: " + lastReply);
    }

    void onFailed(std::string error)
    {
        if (step == Step::Quit) {
            finish(pending);
            return;
        }
        fail(std::move(error));
    }

    void onData(std::string_view bytes)
    {
        if (parser.consume(bytes) == smtp::ReplyParser::State::Malformed) {
            fail("Malformed reply from the server");
            return;
        }
        while (busy && parser.state() == smtp::ReplyParser::State::Complete) {
            const smtp::Reply reply = parser.reply();
            parser.reset();
            lastCode = reply.code;
            lastReply = std::to_string(reply.code) + " " + reply.firstLine();
            handle(reply);
        }
    }

    // --- the dialogue ------------------------------------------------------

    void sendEhlo(Step next)
    {
        step = next;
        const std::string name = account.clientName().empty() ? machineName() : account.clientName();
        stream.write("EHLO " + name + "\r\n");
    }

    void handle(const smtp::Reply &reply)
    {
        switch (step) {
        case Step::Idle:
            return;
        case Step::Greeting:
            if (reply.code != 220) {
                failWith("Server refused the connection", reply);
                return;
            }
            sendEhlo(Step::Ehlo);
            return;
        case Step::Ehlo:
        case Step::EhloAgain:
            if (!reply.positive()) {
                failWith("EHLO refused", reply);
                return;
            }
            caps = smtp::parseEhlo(reply);
            if (step == Step::Ehlo && account.security() == Security::StartTls) {
                if (!caps.startTls) {
                    // Not downgraded: the form says TLS, so TLS it is or nothing.
                    fail("Server offers no STARTTLS");
                    return;
                }
                step = Step::StartTls;
                stream.write("STARTTLS\r\n");
                return;
            }
            authenticate();
            return;
        case Step::StartTls:
            if (reply.code != 220) {
                failWith("STARTTLS refused", reply);
                return;
            }
            stream.startTls(account.tls(), account.host());
            return;
        case Step::AuthPlain:
            if (reply.code == 235) {
                afterAuthentication();
            } else if (reply.code == 334) {
                // The server ignored the initial response and asks; the same
                // bytes again, on their own line.
                stream.write(smtp::authPlainInitialResponse(account.credentials().user(),
                                                            account.credentials().password())
                             + "\r\n");
            } else {
                authenticationFailed(reply);
            }
            return;
        case Step::AuthLoginUser:
            if (reply.code != 334) {
                authenticationFailed(reply);
                return;
            }
            step = Step::AuthLoginPassword;
            stream.write(smtp::authLoginResponse(account.credentials().user()) + "\r\n");
            return;
        case Step::AuthLoginPassword:
            if (reply.code == 334) {
                stream.write(smtp::authLoginResponse(account.credentials().password()) + "\r\n");
            } else if (reply.code == 235) {
                afterAuthentication();
            } else {
                authenticationFailed(reply);
            }
            return;
        case Step::MailFrom:
            if (!reply.positive()) {
                failWith("Sender refused", reply);
                return;
            }
            rcptIndex = 0;
            sendRecipient();
            return;
        case Step::RcptTo:
            if (reply.positive()) {
                ++accepted;
            } else {
                rejected.push_back(message.to()[rcptIndex].address);
            }
            ++rcptIndex;
            if (rcptIndex < message.to().size()) {
                sendRecipient();
                return;
            }
            if (accepted == 0) {
                failWith("Recipient refused", reply);
                return;
            }
            step = Step::Data;
            stream.write("DATA\r\n");
            return;
        case Step::Data:
            if (reply.code != 354) {
                failWith("DATA refused", reply);
                return;
            }
            step = Step::Body;
            stream.write(smtp::dataBlock(body));
            return;
        case Step::Body:
            if (!reply.positive()) {
                failWith("Message refused", reply);
                return;
            }
            succeed();
            return;
        case Step::Quit:
            finish(pending);
            return;
        }
    }

    void authenticate()
    {
        const Credentials &credentials = account.credentials();
        if (credentials.empty()) {
            afterAuthentication();
            return;
        }
        if (caps.authPlain) {
            step = Step::AuthPlain;
            stream.write("AUTH PLAIN " + smtp::authPlainInitialResponse(credentials.user(), credentials.password())
                         + "\r\n");
            return;
        }
        if (caps.authLogin) {
            step = Step::AuthLoginUser;
            stream.write("AUTH LOGIN\r\n");
            return;
        }
        fail(caps.authMechanisms.empty()
                 ? std::string("Server offers no authentication")
                 : "Server offers no authentication this client speaks (" + join(caps.authMechanisms) + ")");
    }

    void afterAuthentication()
    {
        if (probing) {
            succeed();
            return;
        }
        const bool utf8 = mime::needsSmtpUtf8(message);
        if (utf8 && !caps.smtpUtf8) {
            fail("Server does not accept international addresses (no SMTPUTF8)");
            return;
        }
        messageId = mime::messageId(makeClientNonce(), message.from().address);
        body = mime::serialize(message, std::time(nullptr), messageId, makeClientNonce());
        step = Step::MailFrom;
        stream.write("MAIL FROM:<" + message.from().address + ">" + (utf8 ? " SMTPUTF8" : "") + "\r\n");
    }

    void sendRecipient()
    {
        // A recipient without an address was skipped by start()'s check only
        // as a whole; one among several is simply not asked for.
        while (rcptIndex < message.to().size() && message.to()[rcptIndex].address.empty())
            ++rcptIndex;
        if (rcptIndex >= message.to().size()) {
            if (accepted == 0) {
                fail("No recipient");
                return;
            }
            step = Step::Data;
            stream.write("DATA\r\n");
            return;
        }
        step = Step::RcptTo;
        stream.write("RCPT TO:<" + message.to()[rcptIndex].address + ">\r\n");
    }
};

SmtpClient::SmtpClient(phi::runtime::Loop &loop) : m_impl(std::make_unique<Impl>(loop)) {}

SmtpClient::~SmtpClient()
{
    m_impl->cancel();
}

bool SmtpClient::busy() const
{
    return m_impl->busy;
}

bool SmtpClient::send(Account account, Message message, Done done)
{
    return m_impl->start(std::move(account), std::move(message), false, std::move(done));
}

bool SmtpClient::probe(Account account, Done done)
{
    return m_impl->start(std::move(account), Message{}, true, std::move(done));
}

void SmtpClient::cancel()
{
    m_impl->cancel();
}

} // namespace phicore::adapter::net
