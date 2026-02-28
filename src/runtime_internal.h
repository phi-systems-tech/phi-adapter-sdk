#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>

#include "phi/adapter/v1/frame.h"

namespace phicore::adapter::sdk {

struct RuntimeCallbacks {
    std::function<void()> onConnected;
    std::function<void()> onDisconnected;
    std::function<void(const phicore::adapter::v1::FrameHeader &, std::span<const std::byte>)> onFrame;
};

class SidecarRuntime
{
public:
    explicit SidecarRuntime(phicore::adapter::v1::Utf8String socketPath);
    ~SidecarRuntime();

    SidecarRuntime(const SidecarRuntime &) = delete;
    SidecarRuntime &operator=(const SidecarRuntime &) = delete;

    void setCallbacks(RuntimeCallbacks callbacks);

    bool start(phicore::adapter::v1::Utf8String *error = nullptr);
    void stop();

    bool pollOnce(std::chrono::milliseconds timeout, phicore::adapter::v1::Utf8String *error = nullptr);

    bool send(phicore::adapter::v1::MessageType type,
              phicore::adapter::v1::CorrelationId correlationId,
              std::span<const std::byte> payload,
              phicore::adapter::v1::Utf8String *error = nullptr);

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace phicore::adapter::sdk
