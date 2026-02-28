#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "phi/adapter/v1/types.h"
#include "phi/adapter/v1/version.h"

namespace phicore::adapter::v1 {

inline constexpr std::array<std::byte, 4> kFrameMagic{
    std::byte{'P'},
    std::byte{'H'},
    std::byte{'I'},
    std::byte{'A'},
};

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
    return header.magic == kFrameMagic && header.version == kProtocolVersion;
}

[[nodiscard]] inline MessageType messageType(const FrameHeader &header) noexcept
{
    return static_cast<MessageType>(header.type);
}

} // namespace phicore::adapter::v1
