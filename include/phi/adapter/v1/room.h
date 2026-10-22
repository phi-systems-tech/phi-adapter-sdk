#pragma once

#include <string>
#include <vector>

#include "phi/adapter/v1/types.h"

namespace phicore::adapter::v1 {

struct Room {
    ExternalId externalId;
    Utf8String name;
    Utf8String zone;
    /// What the room is, when the hub knows (Hue's archetype, Matter's
    /// location tag). Unknown when it does not; core fills a blank from the
    /// adapter and never overwrites what the household chose.
    RoomClass roomClass = RoomClass::Unknown;
    std::vector<ExternalId> deviceExternalIds;
    JsonText metaJson;
};

using RoomList = std::vector<Room>;

} // namespace phicore::adapter::v1
