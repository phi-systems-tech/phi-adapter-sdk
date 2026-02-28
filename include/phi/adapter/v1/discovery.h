#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "phi/adapter/v1/discovery_query.h"

namespace phicore::adapter::v1 {

struct Discovery {
    std::string pluginType;
    ExternalId discoveredExternalId;
    std::string label;
    std::string hostname;
    std::string ip;
    std::uint16_t port = 0;
    DiscoveryKind kind = DiscoveryKind::Mdns;
    std::string serviceType;
    std::string signal;
    JsonText metaJson;
};

using DiscoveryList = std::vector<Discovery>;

} // namespace phicore::adapter::v1
