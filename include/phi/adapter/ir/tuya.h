#pragma once
// The code string of Tuya blasters, over Wi-Fi (DP 201) and over Zigbee
// (Moes ZS06, UFO-R11, TS1201 through Zigbee2MQTT): little-endian 16-bit
// microseconds, mark first, through an LZ77 compressor with an 8 KiB window
// (FastLZ's grammar), then base64. The format was documented by the
// community (mildsunrise's gist, zigbee-herdsman-converters' zosung); what
// is here is written from that description.

#include <string>
#include <string_view>

#include "phi/adapter/ir/ir_code.h"

namespace phicore::adapter::ir {

/// The code string for `code`. Written as literal blocks only, which every
/// device reads and which the Zigbee converters send too; it costs 3 % over
/// the raw bytes and nothing over a bad match. A timing above 65535 µs is
/// clamped: the format cannot say more.
std::string toTuya(const IrCode &code);

/// Reads a code string, compressed or not: a stream that does not parse as
/// blocks but has an even length is taken as the plain 16-bit form some
/// Wi-Fi blasters emit. The carrier is not in the string; 38 kHz is
/// assumed. False with `error` when it is neither.
bool fromTuya(std::string_view text, IrCode *out, std::string *error);

} // namespace phicore::adapter::ir
