#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "phi/adapter/v1/discovery_query.h"

namespace phicore::adapter::v1 {

struct Discovery {
    Utf8String pluginType;
    ExternalId discoveredExternalId;
    Utf8String label;
    Utf8String hostname;
    Utf8String ip;
    std::uint16_t port = 0;
    DiscoveryKind kind = DiscoveryKind::Mdns;
    Utf8String serviceType;
    Utf8String signal;
    JsonText metaJson;
};

using DiscoveryList = std::vector<Discovery>;

} // namespace phicore::adapter::v1
