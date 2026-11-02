#include "phi/adapter/net/udp_probe.h"

#include "phi/runtime/loop.h"

#include <cerrno>
#include <cstring>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace phicore::adapter::net {

struct UdpProbe::Private {
    explicit Private(phi::runtime::Loop &l) : loop(l) {}

    phi::runtime::Loop &loop;
    int fd = -1;
    phi::runtime::FdWatch watch;
    phi::runtime::Timer deadline;
    OnReply onReply;
    OnDone onDone;

    void onReadable();
    void close();
    void finish(std::string error);
};

namespace {

std::string systemError()
{
    return std::strerror(errno);
}

bool resolveIpv4(const std::string &host, std::uint16_t port, ::sockaddr_in *out, std::string *error)
{
    ::addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    ::addrinfo *resolved = nullptr;
    const int rc = ::getaddrinfo(host.c_str(), nullptr, &hints, &resolved);
    if (rc != 0 || !resolved) {
        *error = "Cannot resolve " + host + ": " + ::gai_strerror(rc);
        return false;
    }
    std::memcpy(out, resolved->ai_addr, sizeof(::sockaddr_in));
    out->sin_port = htons(port);
    ::freeaddrinfo(resolved);
    return true;
}

} // namespace

UdpProbe::UdpProbe(phi::runtime::Loop &loop) : d(std::make_unique<Private>(loop)) {}

UdpProbe::~UdpProbe()
{
    cancel();
}

bool UdpProbe::running() const
{
    return d->fd >= 0;
}

bool UdpProbe::start(const std::string &target, std::uint16_t port, std::string payload,
                     std::chrono::milliseconds window, OnReply onReply, OnDone onDone)
{
    if (d->fd >= 0)
        return false;
    d->onReply = std::move(onReply);
    d->onDone = std::move(onDone);
    d->fd = ::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (d->fd < 0) {
        d->finish("socket: " + systemError());
        return true;
    }
    const int one = 1;
    ::setsockopt(d->fd, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one));
    ::sockaddr_in any{};
    any.sin_family = AF_INET;
    any.sin_addr.s_addr = htonl(INADDR_ANY);
    if (::bind(d->fd, reinterpret_cast<const ::sockaddr *>(&any), sizeof(any)) != 0) {
        d->finish("bind: " + systemError());
        return true;
    }

    ::sockaddr_in to{};
    if (target.empty()) {
        to.sin_family = AF_INET;
        to.sin_port = htons(port);
        to.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    } else {
        std::string error;
        if (!resolveIpv4(target, port, &to, &error)) {
            d->finish(error);
            return true;
        }
    }
    if (::sendto(d->fd, payload.data(), payload.size(), 0, reinterpret_cast<const ::sockaddr *>(&to), sizeof(to))
        < 0) {
        d->finish("sendto: " + systemError());
        return true;
    }
    d->watch = d->loop.watchFd(d->fd, phi::runtime::Loop::FdEvent::Read, [this]() { d->onReadable(); });
    d->deadline = d->loop.timerAfter(window, [this]() { d->finish({}); });
    return true;
}

void UdpProbe::cancel()
{
    d->close();
    d->onReply = nullptr;
    d->onDone = nullptr;
}

void UdpProbe::Private::onReadable()
{
    for (;;) {
        char buffer[4096];
        ::sockaddr_in from{};
        ::socklen_t length = sizeof(from);
        const ssize_t got =
            ::recvfrom(fd, buffer, sizeof(buffer), 0, reinterpret_cast<::sockaddr *>(&from), &length);
        if (got < 0) {
            if (errno == EINTR)
                continue;
            return;
        }
        char text[INET_ADDRSTRLEN] = {};
        ::inet_ntop(AF_INET, &from.sin_addr, text, sizeof(text));
        // A copy: the callback may cancel() the run, which drops the member.
        const OnReply reply = onReply;
        if (reply)
            reply(text, ntohs(from.sin_port), std::string(buffer, static_cast<std::size_t>(got)));
        if (fd < 0)
            return;
    }
}

void UdpProbe::Private::close()
{
    watch.reset();
    deadline.reset();
    if (fd >= 0) {
        ::close(fd);
        fd = -1;
    }
}

void UdpProbe::Private::finish(std::string error)
{
    OnDone done = std::move(onDone);
    onReply = nullptr;
    onDone = nullptr;
    close();
    if (done)
        done(std::move(error));
}

} // namespace phicore::adapter::net
