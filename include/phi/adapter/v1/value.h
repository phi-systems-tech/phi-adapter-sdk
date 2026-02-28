#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace phicore::adapter::v1 {

// Canonical text type for the contract. All values MUST be UTF-8.
using Utf8String = std::string;

// UTF-8 encoded JSON text. Intended for dynamic/extension fields where
// strict first-class members are not practical.
using JsonText = Utf8String;

// Hot-path value container for command/state payloads.
using ScalarValue = std::variant<std::monostate, bool, std::int64_t, double, Utf8String>;
using ScalarList = std::vector<ScalarValue>;

} // namespace phicore::adapter::v1
