#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

#include "phi/adapter/v1/frame.h"

namespace phicore::adapter::sdk::linuxio {

class UdsEpollServer
{
public:
    using FrameHandler = std::function<void(const phicore::adapter::v1::FrameHeader &, std::span<const std::byte>)>;

    explicit UdsEpollServer(std::string socketPath);
    ~UdsEpollServer();

    UdsEpollServer(const UdsEpollServer &) = delete;
    UdsEpollServer &operator=(const UdsEpollServer &) = delete;

    bool start(std::string *error);
    void stop();

    bool pollOnce(std::chrono::milliseconds timeout,
                  const FrameHandler &onFrame,
                  const std::function<void()> &onConnected,
                  const std::function<void()> &onDisconnected,
                  std::string *error);

    bool send(const phicore::adapter::v1::FrameHeader &header,
              std::span<const std::byte> payload,
              std::string *error);

    /**
     * @brief Interrupt a blocking pollOnce() from any thread.
     *
     * Safe to call concurrently with pollOnce()/send()/stop(); the wake
     * descriptor lives for the lifetime of this object.
     */
    void wakeup() noexcept;

    /// Whether a client connection is currently established.
    bool hasClient() const noexcept { return m_clientFd >= 0; }

    /// epoll descriptor of the started transport (-1 when stopped). Readable
    /// whenever inbound frames or queued outbound work need processing, so it
    /// can drive an external event loop.
    int pollDescriptor() const noexcept { return m_epollFd; }

private:
    bool acceptClient(const std::function<void()> &onDisconnected,
                      bool *newClientOut,
                      std::string *error);
    bool readClient(const FrameHandler &onFrame,
                    const std::function<void()> &onDisconnected,
                    std::string *error);
    bool writeAll(const std::byte *data,
                  std::size_t size,
                  std::chrono::steady_clock::time_point deadline,
                  std::string *error);
    void closeClient(const std::function<void()> &onDisconnected);
    void closeClientDeferred();
    void drainWakeFd();

    std::string m_socketPath;
    int m_serverFd = -1;
    int m_epollFd = -1;
    int m_clientFd = -1;
    int m_wakeFd = -1;
    bool m_notifyDisconnect = false;
    std::vector<std::byte> m_rxBuffer;
    // Read cursor into m_rxBuffer: frames are consumed by advancing it and the
    // buffer is compacted once per read batch instead of memmoving the
    // remainder for every single frame.
    std::size_t m_rxOffset = 0;
};

} // namespace phicore::adapter::sdk::linuxio
