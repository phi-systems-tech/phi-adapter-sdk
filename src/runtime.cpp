#include "runtime_internal.h"

#include <utility>

#include "linux/uds_epoll_transport.h"

namespace phicore::adapter::sdk {

class SidecarRuntime::Impl
{
public:
    explicit Impl(phicore::adapter::v1::Utf8String socketPath)
        : transport(std::move(socketPath))
    {
    }

    RuntimeCallbacks callbacks;
    linuxio::UdsEpollServer transport;
};

SidecarRuntime::SidecarRuntime(phicore::adapter::v1::Utf8String socketPath)
    : m_impl(std::make_unique<Impl>(std::move(socketPath)))
{
}

SidecarRuntime::~SidecarRuntime() = default;

void SidecarRuntime::setCallbacks(RuntimeCallbacks callbacks)
{
    m_impl->callbacks = std::move(callbacks);
}

bool SidecarRuntime::start(phicore::adapter::v1::Utf8String *error)
{
    return m_impl->transport.start(error);
}

void SidecarRuntime::stop()
{
    m_impl->transport.stop();
}

bool SidecarRuntime::pollOnce(std::chrono::milliseconds timeout, phicore::adapter::v1::Utf8String *error)
{
    return m_impl->transport.pollOnce(
        timeout,
        m_impl->callbacks.onFrame,
        m_impl->callbacks.onConnected,
        m_impl->callbacks.onDisconnected,
        error);
}

bool SidecarRuntime::send(phicore::adapter::v1::MessageType type,
                          phicore::adapter::v1::CorrelationId correlationId,
                          std::span<const std::byte> payload,
                          phicore::adapter::v1::Utf8String *error)
{
    phicore::adapter::v1::FrameHeader header;
    header.type = static_cast<std::uint8_t>(type);
    header.correlationId = correlationId;
    return m_impl->transport.send(header, payload, error);
}

} // namespace phicore::adapter::sdk
