#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "phi/adapter/v1/ipc_command.h"
#include "phi/adapter/v1/types.h"
#include "phi/adapter/v1/version.h"

namespace phicore::adapter::v1 {

inline constexpr std::array<std::byte, 4> kFrameMagic{
    std::byte{'P'},
    std::byte{'H'},
    std::byte{'I'},
    std::byte{'A'},
};

// Hard upper bound for one frame payload, enforced by BOTH peers in BOTH
// directions: receivers treat a larger declared payloadSize as a protocol
// violation (disconnect), senders must refuse to emit larger frames.
inline constexpr std::uint32_t kMaxPayloadSize = 2U * 1024U * 1024U;

#pragma pack(push, 1)
struct FrameHeader {
    std::array<std::byte, 4> magic = kFrameMagic;
    std::uint16_t version = kProtocolVersion;
    std::uint8_t type = static_cast<std::uint8_t>(MessageType::Event);
    std::uint8_t flags = 0;
    std::uint32_t payloadSize = 0;
    CorrelationId correlationId = 0;
};
#pragma pack(pop)

inline constexpr std::size_t kFrameHeaderSize = sizeof(FrameHeader);

[[nodiscard]] inline bool isValidFrameHeader(const FrameHeader &header) noexcept
{
    return header.magic == kFrameMagic && header.version == kProtocolVersion
        && header.payloadSize <= kMaxPayloadSize;
}

[[nodiscard]] inline MessageType messageType(const FrameHeader &header) noexcept
{
    return static_cast<MessageType>(header.type);
}

/**
 * @brief Frame class a command must be transported in.
 *
 * `ResponseFactoryDescriptor` is the one command that lives in the
 * adapter->core range but is a correlated response, so it is called out
 * explicitly instead of following the range classification.
 */
[[nodiscard]] constexpr MessageType expectedMessageType(IpcCommand command) noexcept
{
    if (command == IpcCommand::ResponseFactoryDescriptor)
        return MessageType::Response;
    if (isSyncCommand(command) || isCmdCommand(command))
        return MessageType::Request;
    if (isResultCommand(command))
        return MessageType::Response;
    return MessageType::Event;
}

/// Whether a frame header's type matches the frame class required by `command`.
[[nodiscard]] constexpr bool matchesMessageType(MessageType type, IpcCommand command) noexcept
{
    return type == expectedMessageType(command);
}

} // namespace phicore::adapter::v1
