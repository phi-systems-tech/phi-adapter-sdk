#pragma once
// An infrared code as light: a carrier and the times it is on and off.
//
// Every blaster speaks its own dialect - Broadlink counts ticks of 32.84 µs,
// Tuya compresses little-endian microseconds, a Pronto string counts carrier
// cycles, a Flipper file names a protocol and two numbers - and every code
// database ships in one of them. What they all describe is the same thing:
// marks and spaces in microseconds at a carrier frequency. That is the one
// form codes are kept in, and each dialect is a conversion to or from it, so
// a code learnt on one blaster plays on another and a database is imported
// once.
//
// Timings alternate mark, space, mark, ... and start with a mark. The last
// entry may be a mark (the trailing gap is the blaster's business) or a
// space (an explicit gap before a repeat). Repeated frames are part of the
// timings: a Sony remote sends every key three times, so a Sony code is
// three frames with their gaps.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace phicore::adapter::ir {

struct IrCode {
    /// Hz; 38000 is what most remotes use and what a learnt code is taken as.
    std::uint32_t carrierHz = 38000;
    /// Microseconds, mark first, alternating.
    std::vector<std::uint32_t> timingsUs;

    [[nodiscard]] bool empty() const noexcept { return timingsUs.empty(); }
    /// The whole transmission, gaps included, in microseconds.
    [[nodiscard]] std::uint64_t durationUs() const noexcept;
};

/// Adjacent entries of the same state folded into one, zero-length entries
/// dropped, a leading space removed: what a decoder or an encoder may
/// produce, made into the shape above.
IrCode normalised(IrCode code);

/// Whether two codes are the same signal within `toleranceUs` per timing and
/// 5 % on the carrier - a learnt code against a database entry.
bool similar(const IrCode &a, const IrCode &b, std::uint32_t toleranceUs = 150);

/// The timings as space-separated microseconds, for a log or a file.
std::string timingsText(const IrCode &code);
/// The inverse; false with `error` when a token is not a number.
bool timingsFromText(std::string_view text, std::vector<std::uint32_t> *out, std::string *error);

} // namespace phicore::adapter::ir
