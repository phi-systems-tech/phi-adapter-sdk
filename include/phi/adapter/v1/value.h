#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace phicore::adapter::v1 {

// UTF-8 encoded JSON text. Intended for dynamic/extension fields where
// strict first-class members are not practical.
using JsonText = std::string;

// Hot-path value container for command/state payloads.
using ScalarValue = std::variant<std::monostate, bool, std::int64_t, double, std::string>;
using ScalarList = std::vector<ScalarValue>;

} // namespace phicore::adapter::v1
