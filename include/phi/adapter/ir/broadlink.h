#pragma once
// The packet a Broadlink RM sends and learns: a frame byte, a repeat count,
// a little-endian length, then durations in ticks of 269/8192 ms (32.84 µs),
// one byte each, or 0x00 and a big-endian word when a duration needs it.
// Base64 of this packet is what SmartIR and Home Assistant users pass around.

#include <cstdint>
#include <string>
#include <string_view>

#include "phi/adapter/ir/ir_code.h"

namespace phicore::adapter::ir {

/// The first byte of the packet names the radio.
enum class BroadlinkFrame : std::uint8_t {
    Ir = 0x26,
    Rf433 = 0xB2,
    Rf315 = 0xD7,
};

/// The packet bytes for `code`, with `repeat` extra transmissions (0 sends
/// once). A code ending in a mark gets the conventional trailing gap. The
/// packet is padded to a multiple of 16 bytes, as the device expects.
std::string toBroadlink(const IrCode &code, BroadlinkFrame frame = BroadlinkFrame::Ir, std::uint8_t repeat = 0);

/// Reads a packet (as learnt from the device or decoded from base64). The
/// carrier is not in the packet; 38 kHz is assumed for IR. `frame` and
/// `repeat`, when given, receive the packet's own. False with `error` for
/// anything that is not such a packet.
bool fromBroadlink(std::string_view packet, IrCode *out, std::string *error, BroadlinkFrame *frame = nullptr,
                   std::uint8_t *repeat = nullptr);

/// Standard base64, as the codes are shared.
std::string base64Encode(std::string_view bytes);
bool base64Decode(std::string_view text, std::string *out);

} // namespace phicore::adapter::ir
