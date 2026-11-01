// The SMTP client against a stub that behaves like a submission server:
// greets first, offers what it is told to offer, checks the credentials it is
// given rather than that AUTH was said, upgrades to TLS in the middle of the
// dialogue when asked, and keeps what it received.

#include <phi/adapter/testing/check.h>

#include "phi/adapter/net/smtp_client.h"
#include "phi/runtime/epollloop.h"

#include "tls_test_server.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

using namespace phicore::adapter::net;
using namespace std::chrono_literals;

namespace {

constexpr const char kUser[] = "box-17";
constexpr const char kPassword[] = "pa ss:wörd";

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

std::string upper(std::string text)
{
    for (char &c : text)
        c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c);
    return text;
}

/// A PEM to trust, written once for the process and removed at exit.
struct CertificateFixture {
    phitest::TestCertificate certificate;
    std::string pemPath;
    bool ok = false;

    CertificateFixture()
    {
        char dir[] = "/tmp/phi-sdk-smtp-XXXXXX";
        if (::mkdtemp(dir) == nullptr)
            return;
        pemPath = std::string(dir) + "/ca.pem";
        ok = certificate.create(pemPath);
    }
    ~CertificateFixture()
    {
        if (!pemPath.empty()) {
            ::unlink(pemPath.c_str());
            ::rmdir(pemPath.substr(0, pemPath.rfind('/')).c_str());
        }
    }
};

struct StubOptions {
    /// TLS from the first byte (port 465 behaviour).
    const phitest::TestCertificate *tlsAtConnect = nullptr;
    /// STARTTLS offered and served with this certificate.
    const phitest::TestCertificate *startTls = nullptr;
    /// The AUTH line, empty for none.
    std::string authMechanisms = "PLAIN LOGIN";
    /// Credentials the stub accepts.
    std::string user = kUser;
    std::string password = kPassword;
    /// Recipients answered with 550.
    std::set<std::string> rejectRecipients;
    /// Never greets, for the deadline.
    bool silent = false;
    /// Answers AUTH PLAIN's initial response with 334 instead of using it.
    bool ignoreInitialResponse = false;
    bool smtpUtf8 = true;
};

/// One connection, one dialogue, recorded.
class SmtpStub
{
public:
    ~SmtpStub() { stop(); }

    bool start(StubOptions options)
    {
        m_options = std::move(options);
        const phitest::TestCertificate *cert = m_options.tlsAtConnect ? m_options.tlsAtConnect
                                                                       : m_options.startTls;
        if (cert) {
            m_ctx = SSL_CTX_new(TLS_server_method());
            if (!m_ctx || SSL_CTX_use_certificate(m_ctx, cert->cert) != 1
                || SSL_CTX_use_PrivateKey(m_ctx, cert->key) != 1)
                return false;
        }
        m_listenFd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (m_listenFd < 0)
            return false;
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::bind(m_listenFd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0
            || ::listen(m_listenFd, 4) < 0)
            return false;
        socklen_t len = sizeof(addr);
        ::getsockname(m_listenFd, reinterpret_cast<sockaddr *>(&addr), &len);
        m_port = ntohs(addr.sin_port);
        m_thread = std::thread([this] { serve(); });
        return true;
    }

    void stop()
    {
        if (m_listenFd >= 0) {
            ::shutdown(m_listenFd, SHUT_RDWR);
            ::close(m_listenFd);
            m_listenFd = -1;
        }
        if (m_thread.joinable())
            m_thread.join();
        if (m_ctx) {
            SSL_CTX_free(m_ctx);
            m_ctx = nullptr;
        }
    }

    [[nodiscard]] std::uint16_t port() const { return m_port; }

    /// The command lines the client sent, in order, AUTH arguments included.
    std::vector<std::string> commands()
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        return m_commands;
    }
    /// The message between DATA and the lone dot, as received.
    std::string message()
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        return m_message;
    }
    bool sawCommand(const std::string &prefix)
    {
        for (const std::string &command : commands())
            if (upper(command).rfind(upper(prefix), 0) == 0)
                return true;
        return false;
    }
    bool authenticatedOverTls()
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        return m_authenticatedOverTls;
    }

private:
    void serve()
    {
        const int fd = ::accept4(m_listenFd, nullptr, nullptr, SOCK_CLOEXEC);
        if (fd < 0)
            return;
        serveOne(fd);
    }

    struct Line {
        int fd = -1;
        SSL *ssl = nullptr;
        std::string buffer;

        bool read(std::string *line)
        {
            for (;;) {
                const std::size_t eol = buffer.find("\r\n");
                if (eol != std::string::npos) {
                    *line = buffer.substr(0, eol);
                    buffer.erase(0, eol + 2);
                    return true;
                }
                char chunk[4096];
                const int got = ssl ? SSL_read(ssl, chunk, sizeof(chunk))
                                    : static_cast<int>(::recv(fd, chunk, sizeof(chunk), 0));
                if (got <= 0)
                    return false;
                buffer.append(chunk, static_cast<std::size_t>(got));
            }
        }
        void write(const std::string &text)
        {
            if (ssl)
                SSL_write(ssl, text.data(), static_cast<int>(text.size()));
            else
                ::send(fd, text.data(), text.size(), MSG_NOSIGNAL);
        }
        bool upgrade(SSL_CTX *ctx)
        {
            ssl = SSL_new(ctx);
            SSL_set_fd(ssl, fd);
            if (SSL_accept(ssl) == 1)
                return true;
            SSL_free(ssl);
            ssl = nullptr;
            return false;
        }
    };

    void record(const std::string &command)
    {
        const std::lock_guard<std::mutex> lock(m_mutex);
        m_commands.push_back(command);
    }

    std::string ehloReply() const
    {
        std::string out = "250-stub.example\r\n250-SIZE 1048576\r\n";
        if (m_options.startTls && !m_secured)
            out += "250-STARTTLS\r\n";
        if (!m_options.authMechanisms.empty())
            out += "250-AUTH " + m_options.authMechanisms + "\r\n";
        out += "250-8BITMIME\r\n";
        if (m_options.smtpUtf8)
            out += "250-SMTPUTF8\r\n";
        out += "250 OK\r\n";
        return out;
    }

    bool credentialsMatch(const std::string &user, const std::string &password) const
    {
        return user == m_options.user && password == m_options.password;
    }

    void serveOne(int fd)
    {
        Line line;
        line.fd = fd;
        if (m_options.tlsAtConnect) {
            if (!line.upgrade(m_ctx)) {
                ::close(fd);
                return;
            }
            m_secured = true;
        }
        if (m_options.silent) {
            std::string ignored;
            line.read(&ignored);
            ::close(fd);
            return;
        }
        line.write("220 stub.example ESMTP ready\r\n");
        std::string command;
        while (line.read(&command)) {
            record(command);
            const std::string verb = upper(command.substr(0, command.find(' ')));
            if (verb == "EHLO") {
                line.write(ehloReply());
            } else if (verb == "STARTTLS") {
                if (!m_options.startTls || m_secured) {
                    line.write("454 TLS not available\r\n");
                    continue;
                }
                line.write("220 2.0.0 Ready to start TLS\r\n");
                if (!line.upgrade(m_ctx))
                    break;
                m_secured = true;
            } else if (verb == "AUTH") {
                handleAuth(line, command);
            } else if (verb == "MAIL") {
                line.write("250 2.1.0 Ok\r\n");
            } else if (verb == "RCPT") {
                const std::size_t open = command.find('<');
                const std::size_t close = command.find('>');
                const std::string address = open != std::string::npos && close != std::string::npos
                    ? command.substr(open + 1, close - open - 1)
                    : std::string();
                if (m_options.rejectRecipients.count(address))
                    line.write("550 5.1.1 <" + address + ">: Recipient address rejected: User unknown\r\n");
                else
                    line.write("250 2.1.5 Ok\r\n");
            } else if (verb == "DATA") {
                line.write("354 End data with <CR><LF>.<CR><LF>\r\n");
                std::string body;
                std::string piece;
                while (line.read(&piece)) {
                    if (piece == ".")
                        break;
                    if (!piece.empty() && piece[0] == '.')
                        piece.erase(0, 1);
                    body += piece + "\r\n";
                }
                {
                    const std::lock_guard<std::mutex> lock(m_mutex);
                    m_message = body;
                }
                line.write("250 2.0.0 Ok: queued as 4ZxY1\r\n");
            } else if (verb == "QUIT") {
                line.write("221 2.0.0 Bye\r\n");
                break;
            } else {
                line.write("502 5.5.2 Error: command not recognized\r\n");
            }
        }
        if (line.ssl) {
            SSL_shutdown(line.ssl);
            SSL_free(line.ssl);
        }
        ::close(fd);
    }

    void handleAuth(Line &line, const std::string &command)
    {
        std::string rest = command.size() > 5 ? command.substr(5) : std::string();
        const std::size_t space = rest.find(' ');
        const std::string mechanism = upper(rest.substr(0, space));
        std::string initial = space == std::string::npos ? std::string() : rest.substr(space + 1);
        if (mechanism == "PLAIN") {
            if (initial.empty() || m_options.ignoreInitialResponse) {
                line.write("334 \r\n");
                if (!line.read(&initial))
                    return;
                record("<plain> " + initial);
            }
            const std::string decoded = base64Decode(initial);
            const std::size_t first = decoded.find('\0');
            const std::size_t second = first == std::string::npos ? first : decoded.find('\0', first + 1);
            const bool ok = first == 0 && second != std::string::npos
                && credentialsMatch(decoded.substr(1, second - 1), decoded.substr(second + 1));
            finishAuth(line, ok);
        } else if (mechanism == "LOGIN") {
            line.write("334 VXNlcm5hbWU6\r\n");
            std::string user;
            if (!line.read(&user))
                return;
            record("<login-user> " + user);
            line.write("334 UGFzc3dvcmQ6\r\n");
            std::string password;
            if (!line.read(&password))
                return;
            record("<login-password> " + password);
            finishAuth(line, credentialsMatch(base64Decode(user), base64Decode(password)));
        } else {
            line.write("504 5.5.4 Unrecognized authentication type\r\n");
        }
    }

    void finishAuth(Line &line, bool ok)
    {
        if (ok) {
            const std::lock_guard<std::mutex> lock(m_mutex);
            m_authenticatedOverTls = line.ssl != nullptr;
        }
        line.write(ok ? "235 2.7.0 Authentication successful\r\n"
                      : "535 5.7.8 Error: authentication failed: UGFzc3dvcmQ6\r\n");
    }

    StubOptions m_options;
    int m_listenFd = -1;
    std::uint16_t m_port = 0;
    SSL_CTX *m_ctx = nullptr;
    bool m_secured = false;
    std::thread m_thread;
    std::mutex m_mutex;
    std::vector<std::string> m_commands;
    std::string m_message;
    bool m_authenticatedOverTls = false;
};

SmtpClient::Account accountFor(const SmtpStub &stub, SmtpClient::Security security,
                               const std::string &caFile = {})
{
    SmtpClient::Account account;
    account.setHost("localhost");
    account.setPort(stub.port());
    account.setSecurity(security);
    account.setCredentials(Credentials(kUser, kPassword));
    account.setClientName("box.phi.local");
    account.setTimeout(4000ms);
    if (!caFile.empty())
        account.tls().setCaFile(caFile);
    return account;
}

SmtpClient::Message sampleMessage()
{
    SmtpClient::Message message;
    message.setFrom({"phi@haus.example", "phi"});
    message.addTo({"marius@example.org", "Marius"});
    message.setSubject("Waschmaschine");
    message.setText("ist fertig (12,4 kWh)\n.\nzweite Zeile mit Punkt");
    return message;
}

SmtpClient::Result runSend(const SmtpClient::Account &account, const SmtpClient::Message &message,
                           bool *accepted = nullptr)
{
    phi::runtime::EpollLoop loop;
    SmtpClient client(loop);
    std::optional<SmtpClient::Result> ended;
    const bool started = client.send(account, message, [&](SmtpClient::Result r) {
        ended = std::move(r);
        loop.stop();
    });
    if (accepted)
        *accepted = started;
    if (!started)
        return SmtpClient::Result{};
    phi::runtime::Timer watchdog = loop.timerAfter(8s, [&loop]() { loop.stop(); });
    loop.run();
    PHI_CHECK_MSG(ended.has_value(), "send never finished");
    return ended.value_or(SmtpClient::Result{});
}

SmtpClient::Result runProbe(const SmtpClient::Account &account)
{
    phi::runtime::EpollLoop loop;
    SmtpClient client(loop);
    std::optional<SmtpClient::Result> ended;
    PHI_CHECK(client.probe(account, [&](SmtpClient::Result r) {
        ended = std::move(r);
        loop.stop();
    }));
    phi::runtime::Timer watchdog = loop.timerAfter(8s, [&loop]() { loop.stop(); });
    loop.run();
    PHI_CHECK_MSG(ended.has_value(), "probe never finished");
    return ended.value_or(SmtpClient::Result{});
}

// --- tests ------------------------------------------------------------------------

void testPlainSubmission()
{
    SmtpStub stub;
    PHI_CHECK(stub.start(StubOptions{}));
    const SmtpClient::Result result = runSend(accountFor(stub, SmtpClient::Security::None), sampleMessage());
    PHI_CHECK_MSG(result.ok(), "%s", result.error().c_str());
    PHI_CHECK(result.code() == 250);
    PHI_CHECK(result.reply().find("queued as 4ZxY1") != std::string::npos);
    PHI_CHECK(result.messageId().find("@haus.example>") != std::string::npos);
    PHI_CHECK(result.rejectedRecipients().empty());

    const std::vector<std::string> commands = stub.commands();
    PHI_CHECK(commands.size() >= 6);
    PHI_CHECK(commands.size() > 0 && commands[0] == "EHLO box.phi.local");
    PHI_CHECK(stub.sawCommand("AUTH PLAIN "));
    PHI_CHECK(stub.sawCommand("MAIL FROM:<phi@haus.example>"));
    PHI_CHECK(stub.sawCommand("RCPT TO:<marius@example.org>"));
    PHI_CHECK(stub.sawCommand("DATA"));
    PHI_CHECK(stub.sawCommand("QUIT"));
    PHI_CHECK(!stub.sawCommand("STARTTLS"));

    const std::string message = stub.message();
    PHI_CHECK(message.find("Subject: Waschmaschine\r\n") != std::string::npos);
    PHI_CHECK(message.find("From: phi <phi@haus.example>\r\n") != std::string::npos);
    PHI_CHECK(message.find("To: Marius <marius@example.org>\r\n") != std::string::npos);
    PHI_CHECK(message.find("Message-ID: " + result.messageId() + "\r\n") != std::string::npos);
    const std::size_t bodyAt = message.find("\r\n\r\n");
    PHI_CHECK(bodyAt != std::string::npos);
    PHI_CHECK(base64Decode(message.substr(bodyAt + 4)) == "ist fertig (12,4 kWh)\n.\nzweite Zeile mit Punkt");
}

void testStartTls()
{
    CertificateFixture fixture;
    PHI_CHECK(fixture.ok);
    if (!fixture.ok)
        return;
    StubOptions options;
    options.startTls = &fixture.certificate;
    SmtpStub stub;
    PHI_CHECK(stub.start(options));

    const SmtpClient::Result result =
        runSend(accountFor(stub, SmtpClient::Security::StartTls, fixture.pemPath), sampleMessage());
    PHI_CHECK_MSG(result.ok(), "%s", result.error().c_str());
    PHI_CHECK(stub.sawCommand("STARTTLS"));
    PHI_CHECK_MSG(stub.authenticatedOverTls(), "the password went before the upgrade");
    // EHLO twice: once in clear, once on the secured line.
    int ehlos = 0;
    for (const std::string &command : stub.commands())
        ehlos += upper(command).rfind("EHLO", 0) == 0 ? 1 : 0;
    PHI_CHECK(ehlos == 2);
    PHI_CHECK(!stub.message().empty());
}

void testStartTlsIsNotDowngraded()
{
    // A server that offers no STARTTLS gets no password.
    SmtpStub stub;
    PHI_CHECK(stub.start(StubOptions{}));
    const SmtpClient::Result result = runSend(accountFor(stub, SmtpClient::Security::StartTls), sampleMessage());
    PHI_CHECK(!result.ok());
    PHI_CHECK_MSG(result.error() == "Server offers no STARTTLS", "error was: %s", result.error().c_str());
    PHI_CHECK(!stub.sawCommand("AUTH"));
    PHI_CHECK(!stub.sawCommand("STARTTLS"));
    PHI_CHECK(stub.message().empty());
}

void testImplicitTls()
{
    CertificateFixture fixture;
    PHI_CHECK(fixture.ok);
    if (!fixture.ok)
        return;
    StubOptions options;
    options.tlsAtConnect = &fixture.certificate;
    SmtpStub stub;
    PHI_CHECK(stub.start(options));

    const SmtpClient::Result result =
        runSend(accountFor(stub, SmtpClient::Security::Tls, fixture.pemPath), sampleMessage());
    PHI_CHECK_MSG(result.ok(), "%s", result.error().c_str());
    PHI_CHECK(!stub.sawCommand("STARTTLS"));
    PHI_CHECK(stub.authenticatedOverTls());
}

void testCertificateIsChecked()
{
    CertificateFixture fixture;
    PHI_CHECK(fixture.ok);
    if (!fixture.ok)
        return;
    StubOptions options;
    options.tlsAtConnect = &fixture.certificate;

    // Nothing trusts a self-signed certificate on its own.
    {
        SmtpStub stub;
        PHI_CHECK(stub.start(options));
        const SmtpClient::Result result = runSend(accountFor(stub, SmtpClient::Security::Tls), sampleMessage());
        PHI_CHECK_MSG(!result.ok(), "a self-signed certificate was accepted by the system store");
        PHI_CHECK_MSG(result.error().find("certificate") != std::string::npos, "error was: %s",
                      result.error().c_str());
    }
    // Dialled by address: the certificate names localhost, not 127.0.0.1.
    {
        SmtpStub stub;
        PHI_CHECK(stub.start(options));
        SmtpClient::Account account = accountFor(stub, SmtpClient::Security::Tls, fixture.pemPath);
        account.setHost("127.0.0.1");
        const SmtpClient::Result result = runSend(account, sampleMessage());
        PHI_CHECK_MSG(!result.ok(), "a certificate for another name was accepted");
    }
}

void testAuthenticationFailure()
{
    StubOptions options;
    options.password = "something else";
    SmtpStub stub;
    PHI_CHECK(stub.start(options));
    const SmtpClient::Result result = runSend(accountFor(stub, SmtpClient::Security::None), sampleMessage());
    PHI_CHECK(!result.ok());
    PHI_CHECK(result.authenticationFailed());
    PHI_CHECK(result.code() == 535);
    PHI_CHECK_MSG(result.error().rfind("Authentication failed: 535", 0) == 0, "error was: %s",
                  result.error().c_str());
    PHI_CHECK(!stub.sawCommand("MAIL"));
}

void testAuthLoginAndIgnoredInitialResponse()
{
    // LOGIN only: the two-step dialogue.
    {
        StubOptions options;
        options.authMechanisms = "LOGIN";
        SmtpStub stub;
        PHI_CHECK(stub.start(options));
        const SmtpClient::Result result = runSend(accountFor(stub, SmtpClient::Security::None), sampleMessage());
        PHI_CHECK_MSG(result.ok(), "%s", result.error().c_str());
        PHI_CHECK(stub.sawCommand("AUTH LOGIN"));
        PHI_CHECK(stub.sawCommand("<login-user> "));
        PHI_CHECK(stub.sawCommand("<login-password> "));
    }
    // PLAIN whose initial response the server ignores: asked again, answered again.
    {
        StubOptions options;
        options.ignoreInitialResponse = true;
        SmtpStub stub;
        PHI_CHECK(stub.start(options));
        const SmtpClient::Result result = runSend(accountFor(stub, SmtpClient::Security::None), sampleMessage());
        PHI_CHECK_MSG(result.ok(), "%s", result.error().c_str());
        PHI_CHECK(stub.sawCommand("<plain> "));
    }
    // A mechanism we do not speak is named in the error.
    {
        StubOptions options;
        options.authMechanisms = "CRAM-MD5 XOAUTH2";
        SmtpStub stub;
        PHI_CHECK(stub.start(options));
        const SmtpClient::Result result = runSend(accountFor(stub, SmtpClient::Security::None), sampleMessage());
        PHI_CHECK(!result.ok());
        PHI_CHECK_MSG(result.error().find("CRAM-MD5, XOAUTH2") != std::string::npos, "error was: %s",
                      result.error().c_str());
    }
    // No credentials: no AUTH at all, for a relay that trusts the address.
    {
        StubOptions options;
        options.authMechanisms.clear();
        SmtpStub stub;
        PHI_CHECK(stub.start(options));
        SmtpClient::Account account = accountFor(stub, SmtpClient::Security::None);
        account.setCredentials(Credentials{});
        const SmtpClient::Result result = runSend(account, sampleMessage());
        PHI_CHECK_MSG(result.ok(), "%s", result.error().c_str());
        PHI_CHECK(!stub.sawCommand("AUTH"));
    }
}

void testRecipients()
{
    // One of two refused: the mail goes to the other, and the refusal is named.
    {
        StubOptions options;
        options.rejectRecipients = {"nobody@example.org"};
        SmtpStub stub;
        PHI_CHECK(stub.start(options));
        SmtpClient::Message message = sampleMessage();
        message.addTo({"nobody@example.org", ""});
        const SmtpClient::Result result = runSend(accountFor(stub, SmtpClient::Security::None), message);
        PHI_CHECK_MSG(result.ok(), "%s", result.error().c_str());
        PHI_CHECK(result.rejectedRecipients() == std::vector<std::string>{"nobody@example.org"});
        PHI_CHECK(!stub.message().empty());
    }
    // All refused: nothing sent, the server's line in the error.
    {
        StubOptions options;
        options.rejectRecipients = {"marius@example.org"};
        SmtpStub stub;
        PHI_CHECK(stub.start(options));
        const SmtpClient::Result result = runSend(accountFor(stub, SmtpClient::Security::None), sampleMessage());
        PHI_CHECK(!result.ok());
        PHI_CHECK(result.code() == 550);
        PHI_CHECK_MSG(result.error().find("Recipient refused: 550") == 0, "error was: %s", result.error().c_str());
        PHI_CHECK(result.rejectedRecipients().size() == 1);
        PHI_CHECK(!stub.sawCommand("DATA"));
    }
    // No sender or no recipient is refused before anything is dialled.
    {
        SmtpStub stub;
        PHI_CHECK(stub.start(StubOptions{}));
        bool started = true;
        SmtpClient::Message noRecipient = sampleMessage();
        noRecipient.setTo({});
        runSend(accountFor(stub, SmtpClient::Security::None), noRecipient, &started);
        PHI_CHECK(!started);
        SmtpClient::Message noSender = sampleMessage();
        noSender.setFrom({});
        runSend(accountFor(stub, SmtpClient::Security::None), noSender, &started);
        PHI_CHECK(!started);
    }
}

void testProbe()
{
    SmtpStub stub;
    PHI_CHECK(stub.start(StubOptions{}));
    const SmtpClient::Result result = runProbe(accountFor(stub, SmtpClient::Security::None));
    PHI_CHECK_MSG(result.ok(), "%s", result.error().c_str());
    PHI_CHECK(stub.sawCommand("AUTH"));
    PHI_CHECK(stub.sawCommand("QUIT"));
    PHI_CHECK(!stub.sawCommand("MAIL"));
    PHI_CHECK(stub.message().empty());

    StubOptions wrong;
    wrong.password = "other";
    SmtpStub refusing;
    PHI_CHECK(refusing.start(wrong));
    const SmtpClient::Result refused = runProbe(accountFor(refusing, SmtpClient::Security::None));
    PHI_CHECK(!refused.ok());
    PHI_CHECK(refused.authenticationFailed());
}

void testDeadlineAndRefusedConnection()
{
    {
        StubOptions options;
        options.silent = true;
        SmtpStub stub;
        PHI_CHECK(stub.start(options));
        SmtpClient::Account account = accountFor(stub, SmtpClient::Security::None);
        account.setTimeout(300ms);
        const SmtpClient::Result result = runSend(account, sampleMessage());
        PHI_CHECK(!result.ok());
        PHI_CHECK_MSG(result.error() == "Server did not answer in time", "error was: %s", result.error().c_str());
        PHI_CHECK(result.code() == 0);
    }
    {
        // A port nobody listens on.
        int probe = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ::bind(probe, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
        socklen_t len = sizeof(addr);
        ::getsockname(probe, reinterpret_cast<sockaddr *>(&addr), &len);
        const std::uint16_t freePort = ntohs(addr.sin_port);
        ::close(probe);

        SmtpClient::Account account;
        account.setHost("127.0.0.1");
        account.setPort(freePort);
        account.setSecurity(SmtpClient::Security::None);
        account.setTimeout(2000ms);
        const SmtpClient::Result result = runSend(account, sampleMessage());
        PHI_CHECK(!result.ok());
        PHI_CHECK_MSG(result.error().find("refused") != std::string::npos, "error was: %s", result.error().c_str());
    }
}

void testBusyAndCancel()
{
    StubOptions options;
    options.silent = true;
    SmtpStub stub;
    PHI_CHECK(stub.start(options));

    phi::runtime::EpollLoop loop;
    SmtpClient client(loop);
    bool called = false;
    PHI_CHECK(client.send(accountFor(stub, SmtpClient::Security::None), sampleMessage(),
                          [&](SmtpClient::Result) { called = true; }));
    PHI_CHECK(client.busy());
    PHI_CHECK(!client.send(accountFor(stub, SmtpClient::Security::None), sampleMessage(),
                           [&](SmtpClient::Result) { called = true; }));
    phi::runtime::Timer later = loop.timerAfter(100ms, [&]() {
        client.cancel();
        loop.stop();
    });
    loop.run();
    PHI_CHECK(!client.busy());
    PHI_CHECK(!called);
}

} // namespace

int main()
{
    testPlainSubmission();
    testStartTls();
    testStartTlsIsNotDowngraded();
    testImplicitTls();
    testCertificateIsChecked();
    testAuthenticationFailure();
    testAuthLoginAndIgnoredInitialResponse();
    testRecipients();
    testProbe();
    testDeadlineAndRefusedConnection();
    testBusyAndCancel();
    return phi::testing::report("sdk_smtp_client_tests");
}
