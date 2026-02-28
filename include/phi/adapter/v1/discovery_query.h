#pragma once

#include <string>
#include <vector>

#include "phi/adapter/v1/types.h"

namespace phicore::adapter::v1 {

struct DiscoveryQuery {
    Utf8String pluginType;
    DiscoveryKind kind = DiscoveryKind::Mdns;
    Utf8String mdnsServiceType;
    Utf8String ssdpSt;
    int defaultPort = 0;
    JsonText hintsJson;

    [[nodiscard]] bool isValid() const noexcept
    {
        if (pluginType.empty())
            return false;
        switch (kind) {
        case DiscoveryKind::Mdns:
            return !mdnsServiceType.empty();
        case DiscoveryKind::Ssdp:
            return !ssdpSt.empty();
        case DiscoveryKind::NetScan:
        case DiscoveryKind::Manual:
            return true;
        }
        return false;
    }
};

using DiscoveryQueryList = std::vector<DiscoveryQuery>;

} // namespace phicore::adapter::v1
