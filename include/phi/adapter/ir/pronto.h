#pragma once
// Pronto hex, the lingua franca of code collections: four header words and
// then pairs of on/off counted in carrier cycles. Only format 0000 (a learnt
// code at a carrier) is read and written; the predefined-protocol formats
// (5000, 6000, 900a ...) belong to Philips remotes and never travel.

#include <string>
#include <string_view>

#include "phi/adapter/ir/ir_code.h"

namespace phicore::adapter::ir {

/// Reads "0000 006D 0022 0002 015B 00AD ..." (spaces or none between words).
/// A string with both a once and a repeat sequence yields the once sequence
/// when it has one, else the repeat sequence. False with `error` otherwise.
bool fromPronto(std::string_view hex, IrCode *out, std::string *error);

/// Writes the code as a once sequence with no repeat part, upper-case words
/// separated by single spaces. A trailing mark gets a gap of 100 ms, since
/// the format counts pairs.
std::string toPronto(const IrCode &code);

} // namespace phicore::adapter::ir
