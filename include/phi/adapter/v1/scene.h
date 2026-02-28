#pragma once

#include <string>
#include <vector>

#include "phi/adapter/v1/types.h"

namespace phicore::adapter::v1 {

struct Scene {
    ExternalId externalId;
    std::string name;
    std::string description;
    ExternalId scopeExternalId;
    std::string scopeType;
    std::string avatarColor;
    std::string image;
    std::string presetTag;
    SceneState state = SceneState::Unknown;
    SceneFlags flags = SceneFlag::None;
    JsonText metaJson;
};

using SceneList = std::vector<Scene>;

} // namespace phicore::adapter::v1
