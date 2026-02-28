#include "linux/uds_epoll_transport.h"

#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace phicore::adapter::sdk::linuxio {

namespace {

std::string errnoString(const char *prefix)
{
    return std::string(prefix) + ": " + std::strerror(errno);
}

bool setNonBlocking(int fd)
{
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0)
        return false;
    return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

} // namespace

UdsEpollServer::UdsEpollServer(std::string socketPath)
    : m_socketPath(std::move(socketPath))
{
}

UdsEpollServer::~UdsEpollServer()
{
    stop();
}

bool UdsEpollServer::start(std::string *error)
{
    stop();

    m_serverFd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (m_serverFd < 0) {
        if (error)
            *error = errnoString("socket");
        return false;
    }

    ::unlink(m_socketPath.c_str());

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (m_socketPath.size() >= sizeof(addr.sun_path)) {
        if (error)
            *error = "socket path too long";
        stop();
        return false;
    }
    std::strncpy(addr.sun_path, m_socketPath.c_str(), sizeof(addr.sun_path) - 1);

    if (::bind(m_serverFd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        if (error)
            *error = errnoString("bind");
        stop();
        return false;
    }

    if (::listen(m_serverFd, 8) < 0) {
        if (error)
            *error = errnoString("listen");
        stop();
        return false;
    }

    m_epollFd = ::epoll_create1(EPOLL_CLOEXEC);
    if (m_epollFd < 0) {
        if (error)
            *error = errnoString("epoll_create1");
        stop();
        return false;
    }

    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = m_serverFd;
    if (::epoll_ctl(m_epollFd, EPOLL_CTL_ADD, m_serverFd, &ev) < 0) {
        if (error)
            *error = errnoString("epoll_ctl add server");
        stop();
        return false;
    }

    m_rxBuffer.clear();
    return true;
}

void UdsEpollServer::stop()
{
    if (m_clientFd >= 0) {
        ::close(m_clientFd);
        m_clientFd = -1;
    }
    if (m_epollFd >= 0) {
        ::close(m_epollFd);
        m_epollFd = -1;
    }
    if (m_serverFd >= 0) {
        ::close(m_serverFd);
        m_serverFd = -1;
    }
    if (!m_socketPath.empty())
        ::unlink(m_socketPath.c_str());
    m_rxBuffer.clear();
}

bool UdsEpollServer::acceptClient(std::string *error)
{
    const int fd = ::accept4(m_serverFd, nullptr, nullptr, SOCK_NONBLOCK);
    if (fd < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return true;
        if (error)
            *error = errnoString("accept4");
        return false;
    }

    if (!setNonBlocking(fd)) {
        if (error)
            *error = errnoString("fcntl nonblock");
        ::close(fd);
        return false;
    }

    if (m_clientFd >= 0)
        ::close(m_clientFd);
    m_clientFd = fd;

    epoll_event ev{};
    ev.events = EPOLLIN | EPOLLRDHUP | EPOLLHUP;
    ev.data.fd = m_clientFd;
    if (::epoll_ctl(m_epollFd, EPOLL_CTL_ADD, m_clientFd, &ev) < 0) {
        if (error)
            *error = errnoString("epoll_ctl add client");
        ::close(m_clientFd);
        m_clientFd = -1;
        return false;
    }

    m_rxBuffer.clear();
    return true;
}

bool UdsEpollServer::readClient(const FrameHandler &onFrame,
                                const std::function<void()> &onDisconnected,
                                std::string *error)
{
    std::byte tmp[4096];
    for (;;) {
        const ssize_t n = ::read(m_clientFd, tmp, sizeof(tmp));
        if (n > 0) {
            m_rxBuffer.insert(m_rxBuffer.end(), tmp, tmp + n);
            continue;
        }
        if (n == 0) {
            closeClient(onDisconnected);
            return true;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            break;
        if (errno == EINTR)
            continue;
        if (error)
            *error = errnoString("read");
        closeClient(onDisconnected);
        return false;
    }

    while (m_rxBuffer.size() >= phicore::adapter::v1::kFrameHeaderSize) {
        phicore::adapter::v1::FrameHeader header{};
        std::memcpy(&header, m_rxBuffer.data(), phicore::adapter::v1::kFrameHeaderSize);

        if (!phicore::adapter::v1::isValidFrameHeader(header)) {
            if (error)
                *error = "invalid frame header";
            closeClient(onDisconnected);
            return false;
        }

        const std::size_t frameSize = phicore::adapter::v1::kFrameHeaderSize + header.payloadSize;
        if (m_rxBuffer.size() < frameSize)
            break;

        const std::byte *payloadStart = m_rxBuffer.data() + phicore::adapter::v1::kFrameHeaderSize;
        const std::span<const std::byte> payload(payloadStart, header.payloadSize);
        if (onFrame)
            onFrame(header, payload);

        m_rxBuffer.erase(m_rxBuffer.begin(), m_rxBuffer.begin() + static_cast<std::ptrdiff_t>(frameSize));
    }

    return true;
}

bool UdsEpollServer::writeAll(const std::byte *data, std::size_t size, std::string *error)
{
    std::size_t written = 0;
    while (written < size) {
        const ssize_t n = ::write(m_clientFd, data + written, size - written);
        if (n > 0) {
            written += static_cast<std::size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            continue;
        if (error)
            *error = errnoString("write");
        return false;
    }
    return true;
}

bool UdsEpollServer::send(const phicore::adapter::v1::FrameHeader &header,
                          std::span<const std::byte> payload,
                          std::string *error)
{
    if (m_clientFd < 0) {
        if (error)
            *error = "no connected client";
        return false;
    }

    phicore::adapter::v1::FrameHeader wireHeader = header;
    wireHeader.payloadSize = static_cast<std::uint32_t>(payload.size());

    if (!writeAll(reinterpret_cast<const std::byte *>(&wireHeader),
                  phicore::adapter::v1::kFrameHeaderSize,
                  error)) {
        return false;
    }

    if (!payload.empty() && !writeAll(payload.data(), payload.size(), error))
        return false;

    return true;
}

void UdsEpollServer::closeClient(const std::function<void()> &onDisconnected)
{
    if (m_clientFd >= 0) {
        ::epoll_ctl(m_epollFd, EPOLL_CTL_DEL, m_clientFd, nullptr);
        ::close(m_clientFd);
        m_clientFd = -1;
    }
    m_rxBuffer.clear();
    if (onDisconnected)
        onDisconnected();
}

bool UdsEpollServer::pollOnce(std::chrono::milliseconds timeout,
                              const FrameHandler &onFrame,
                              const std::function<void()> &onConnected,
                              const std::function<void()> &onDisconnected,
                              std::string *error)
{
    if (m_epollFd < 0) {
        if (error)
            *error = "transport not started";
        return false;
    }

    epoll_event events[8]{};
    const int timeoutMs = static_cast<int>(timeout.count());
    const int n = ::epoll_wait(m_epollFd, events, 8, timeoutMs);
    if (n < 0) {
        if (errno == EINTR)
            return true;
        if (error)
            *error = errnoString("epoll_wait");
        return false;
    }

    for (int i = 0; i < n; ++i) {
        const int fd = events[i].data.fd;
        const std::uint32_t ev = events[i].events;

        if (fd == m_serverFd) {
            const bool hadClient = m_clientFd >= 0;
            if (!acceptClient(error))
                return false;
            if (!hadClient && m_clientFd >= 0 && onConnected)
                onConnected();
            continue;
        }

        if (fd == m_clientFd) {
            if ((ev & (EPOLLRDHUP | EPOLLHUP)) != 0U) {
                closeClient(onDisconnected);
                continue;
            }
            if ((ev & EPOLLIN) != 0U) {
                if (!readClient(onFrame, onDisconnected, error))
                    return false;
            }
        }
    }

    return true;
}

} // namespace phicore::adapter::sdk::linuxio
