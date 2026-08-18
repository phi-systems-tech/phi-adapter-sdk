// Minimal test support for phi-adapter-sdk tests: assertion helpers and a
// raw Unix-domain-socket client that speaks the v1 frame protocol so tests
// can exercise the sidecar runtime exactly like phi-core does on the wire.
#pragma once

#include "phi/adapter/v1/frame.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <vector>

namespace phitest {

inline int g_failures = 0;

#define CHECK(cond)                                                                             \
    do {                                                                                        \
        if (!(cond)) {                                                                          \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                         \
            ++::phitest::g_failures;                                                            \
        }                                                                                       \
    } while (false)

#define CHECK_MSG(cond, ...)                                                                    \
    do {                                                                                        \
        if (!(cond)) {                                                                          \
            std::printf("FAIL %s:%d: %s (", __FILE__, __LINE__, #cond);                         \
            std::printf(__VA_ARGS__);                                                           \
            std::printf(")\n");                                                                 \
            ++::phitest::g_failures;                                                            \
        }                                                                                       \
    } while (false)

#define REQUIRE(cond)                                                                           \
    do {                                                                                        \
        if (!(cond)) {                                                                          \
            std::printf("FATAL %s:%d: %s\n", __FILE__, __LINE__, #cond);                        \
            ++::phitest::g_failures;                                                            \
            return;                                                                             \
        }                                                                                       \
    } while (false)

inline std::string uniqueSocketPath(const char *tag)
{
    return "/tmp/phi-sdk-test-" + std::to_string(::getpid()) + "-" + tag + ".sock";
}

inline long msSince(std::chrono::steady_clock::time_point t0)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0)
        .count();
}

// Raw frame-protocol client (the "core" side of the socket).
class TestClient
{
public:
    ~TestClient() { close(); }

    bool connectTo(const std::string &path)
    {
        m_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (m_fd < 0)
            return false;
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
        if (::connect(m_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
            close();
            return false;
        }
        return true;
    }

    void close()
    {
        if (m_fd >= 0) {
            ::close(m_fd);
            m_fd = -1;
        }
    }

    int fd() const { return m_fd; }

    bool sendFrame(phicore::adapter::v1::MessageType type,
                   std::uint64_t correlationId,
                   const std::string &json)
    {
        phicore::adapter::v1::FrameHeader header;
        header.type = static_cast<std::uint8_t>(type);
        header.correlationId = correlationId;
        header.payloadSize = static_cast<std::uint32_t>(json.size());
        return writeAll(&header, sizeof(header)) && writeAll(json.data(), json.size());
    }

    bool sendRaw(const void *data, std::size_t size) { return writeAll(data, size); }

    /// Read one complete frame within timeoutMs. Returns false on timeout/EOF/error.
    bool readFrame(int timeoutMs,
                   phicore::adapter::v1::FrameHeader *headerOut,
                   std::string *payloadOut,
                   bool *eofOut = nullptr)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        if (eofOut)
            *eofOut = false;
        for (;;) {
            if (tryParseFrame(headerOut, payloadOut))
                return true;
            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline)
                return false;
            const int waitMs = static_cast<int>(
                std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
            pollfd pfd{};
            pfd.fd = m_fd;
            pfd.events = POLLIN;
            const int rv = ::poll(&pfd, 1, waitMs > 50 ? 50 : waitMs);
            if (rv < 0 && errno != EINTR)
                return false;
            if (rv <= 0)
                continue;
            char tmp[4096];
            const ssize_t n = ::read(m_fd, tmp, sizeof(tmp));
            if (n == 0) {
                if (eofOut)
                    *eofOut = true;
                return false; // EOF
            }
            if (n < 0) {
                if (errno == EINTR || errno == EAGAIN)
                    continue;
                return false;
            }
            m_buffer.insert(m_buffer.end(), tmp, tmp + n);
        }
    }

    /// Wait for EOF (peer closed) within timeoutMs; drains and discards data.
    bool waitForEof(int timeoutMs)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        for (;;) {
            if (std::chrono::steady_clock::now() >= deadline)
                return false;
            pollfd pfd{};
            pfd.fd = m_fd;
            pfd.events = POLLIN;
            const int rv = ::poll(&pfd, 1, 50);
            if (rv <= 0)
                continue;
            char tmp[4096];
            const ssize_t n = ::read(m_fd, tmp, sizeof(tmp));
            if (n == 0)
                return true;
            if (n < 0 && errno != EINTR && errno != EAGAIN)
                return true; // reset counts as closed
        }
    }

private:
    bool writeAll(const void *data, std::size_t size)
    {
        const char *p = static_cast<const char *>(data);
        std::size_t written = 0;
        while (written < size) {
            const ssize_t n = ::write(m_fd, p + written, size - written);
            if (n < 0) {
                if (errno == EINTR)
                    continue;
                return false;
            }
            written += static_cast<std::size_t>(n);
        }
        return true;
    }

    bool tryParseFrame(phicore::adapter::v1::FrameHeader *headerOut, std::string *payloadOut)
    {
        using phicore::adapter::v1::FrameHeader;
        using phicore::adapter::v1::kFrameHeaderSize;
        if (m_buffer.size() < kFrameHeaderSize)
            return false;
        FrameHeader header{};
        std::memcpy(&header, m_buffer.data(), kFrameHeaderSize);
        const std::size_t frameSize = kFrameHeaderSize + header.payloadSize;
        if (m_buffer.size() < frameSize)
            return false;
        if (headerOut)
            *headerOut = header;
        if (payloadOut)
            payloadOut->assign(m_buffer.data() + kFrameHeaderSize, header.payloadSize);
        m_buffer.erase(m_buffer.begin(), m_buffer.begin() + static_cast<std::ptrdiff_t>(frameSize));
        return true;
    }

    int m_fd = -1;
    std::vector<char> m_buffer;
};

inline bool contains(const std::string &haystack, const std::string &needle)
{
    return haystack.find(needle) != std::string::npos;
}

} // namespace phitest
