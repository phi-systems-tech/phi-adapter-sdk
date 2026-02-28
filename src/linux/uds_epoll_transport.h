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

private:
    bool acceptClient(std::string *error);
    bool readClient(const FrameHandler &onFrame,
                    const std::function<void()> &onDisconnected,
                    std::string *error);
    bool writeAll(const std::byte *data, std::size_t size, std::string *error);
    void closeClient(const std::function<void()> &onDisconnected);

    std::string m_socketPath;
    int m_serverFd = -1;
    int m_epollFd = -1;
    int m_clientFd = -1;
    std::vector<std::byte> m_rxBuffer;
};

} // namespace phicore::adapter::sdk::linuxio
