#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <utility>
#include <vector>

namespace phicore::adapter::v1 {

// Canonical text type for the contract. All values MUST be UTF-8.
using Utf8String = std::string;

// UTF-8 encoded JSON text. Intended for dynamic/extension fields where
// strict first-class members are not practical.
using JsonText = Utf8String;

// Hot-path value container for command/state payloads.
// Runtime comparison policy is defined by channel data type in core:
// bool comparisons are lenient (string/int aliases), while adapters should
// still emit canonical values per ChannelDataType.
using ScalarValue = std::variant<std::monostate, bool, std::int64_t, double, Utf8String>;
using ScalarList = std::vector<ScalarValue>;

/**
 * @brief A composite channel value: named scalars, no nesting.
 *
 * The shape a `ChannelDataType::Json` channel is transported in. Flat and
 * scalar on purpose - it is what every composite kind actually needs, it makes
 * an invalid document impossible to build, and a projection field has to name
 * a scalar anyway.
 */
using ChannelValueFields = std::vector<std::pair<Utf8String, ScalarValue>>;

} // namespace phicore::adapter::v1
