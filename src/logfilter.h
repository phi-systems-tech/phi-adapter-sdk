// What of an adapter's logging reaches core: the library's own, not the
// adapter's. Out of the public headers so its shape is nobody's contract.
#pragma once

#include "phi/adapter/sdk/sidecar.h"

#include <cstdint>

namespace phicore::adapter::sdk {

struct LogFilterCache {
    /// Effective config received (without it, everything is forwarded).
    bool hasConfig = false;
    /// AdapterOption::EnableLogs on the effective config.
    bool forwardingEnabled = false;
    /// Minimum forwarded level as priority (Trace=0 ... Error=4).
    int minLevelPriority = 1;
    bool allowAllCategories = true;
    /// Bit per LogCategory index when allowAllCategories is false.
    std::uint16_t categoryMask = 0;
};

[[nodiscard]] bool shouldForwardLog(const LogFilterCache &cache, LogLevel level, LogCategory category);

} // namespace phicore::adapter::sdk
