#pragma once
// The Flipper Zero `.ir` file: the format of the largest open collection of
// remote codes (Flipper-IRDB, CC0). A file is a header and then records of
// `name:` and either a protocol with address and command, or raw timings at
// a carrier. Read here, never written: the collection is a source.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "phi/adapter/ir/ir_code.h"
#include "phi/adapter/ir/protocols.h"

namespace phicore::adapter::ir {

struct FlipperRecord {
    std::string name;
    /// "parsed" (protocol + address + command) or "raw".
    bool raw = false;
    /// Parsed records: the protocol as named, and the numbers as the file
    /// writes them (little-endian byte lists, read into host order).
    std::string protocolName;
    Protocol protocol = Protocol::Unknown;
    std::uint32_t address = 0;
    std::uint32_t command = 0;
    /// Raw records: the signal as it is.
    IrCode code;
};

/// Every record in the file, in order. Lines that do not belong to a record
/// are skipped; a record missing what its type needs is left out. The
/// result is empty for a file that is not an IR signals file.
std::vector<FlipperRecord> parseFlipperFile(std::string_view text);

/// The record as light: a raw record's own code, a parsed record encoded.
/// False with `error` when the protocol is not one encoded here.
bool codeOf(const FlipperRecord &record, IrCode *out, std::string *error);

} // namespace phicore::adapter::ir
