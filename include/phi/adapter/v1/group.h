#pragma once

#include <string>
#include <vector>

#include "phi/adapter/v1/types.h"

namespace phicore::adapter::v1 {

struct Group {
    ExternalId externalId;
    std::string name;
    std::string zone;
    std::vector<ExternalId> deviceExternalIds;
    JsonText metaJson;
};

using GroupList = std::vector<Group>;

} // namespace phicore::adapter::v1
