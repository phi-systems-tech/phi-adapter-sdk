#pragma once
// The remote-control protocols a code database names instead of spelling
// out: a protocol, an address and a command stand for a frame of marks and
// spaces. Encoding them here is what lets a Flipper or IRDB entry play on
// any blaster without a learning step.
//
// Only the common consumer protocols: NEC and its 16-bit and 42-bit cousins,
// Samsung's 32-bit frame, Sony SIRC in its three lengths, Philips RC5 and
// RC6. Air conditioners are not here - they send their whole state in one
// frame and need an encoder per brand.

#include <cstdint>
#include <string>
#include <string_view>

#include "phi/adapter/ir/ir_code.h"

namespace phicore::adapter::ir {

enum class Protocol : std::uint8_t {
    Unknown = 0,
    /// 8-bit address and command, each followed by its inverse; 38 kHz.
    Nec = 1,
    /// 16-bit address, 8-bit command with inverse ("extended NEC").
    NecExt = 2,
    /// 13-bit address and 8-bit command, each with inverse (42 bits).
    Nec42 = 3,
    /// 26-bit address, 8-bit command with inverse.
    Nec42Ext = 4,
    /// 8-bit address sent twice, 8-bit command with inverse; 4.5 ms header.
    Samsung32 = 5,
    /// 7-bit command, 5-bit address; 40 kHz, sent three times.
    Sirc = 6,
    /// 7-bit command, 8-bit address.
    Sirc15 = 7,
    /// 7-bit command, 5-bit address, 8-bit extended address.
    Sirc20 = 8,
    /// 14 bits Manchester at 889 µs, 5-bit address, 6-bit command; 36 kHz.
    Rc5 = 9,
    /// RC5 with the second start bit as the command's seventh bit.
    Rc5X = 10,
    /// Mode 0: 8-bit address and command, 444 µs half-bits; 36 kHz.
    Rc6 = 11,
};

/// The name a Flipper file uses (NEC, NECext, NEC42, NEC42ext, Samsung32,
/// SIRC, SIRC15, SIRC20, RC5, RC5X, RC6), case-insensitively.
Protocol protocolFromName(std::string_view name);
std::string_view protocolName(Protocol protocol);

/// One key press: the frame (with repeats where the protocol needs them),
/// ending in the gap that separates presses. `address` and `command` are
/// the values as the protocol defines them, in host order. False with
/// `error` when the protocol is not encoded here or a value does not fit.
bool encode(Protocol protocol, std::uint32_t address, std::uint32_t command, IrCode *out, std::string *error);

} // namespace phicore::adapter::ir
