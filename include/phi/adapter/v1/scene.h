#pragma once

#include <string>
#include <vector>

#include "phi/adapter/v1/types.h"

namespace phicore::adapter::v1 {

struct Scene {
    ExternalId externalId;
    Utf8String name;
    Utf8String description;
    ExternalId scopeExternalId;
    Utf8String scopeType;
    Utf8String avatarColor;
    Utf8String image;
    Utf8String presetTag;
    SceneState state = SceneState::Unknown;
    SceneFlags flags = SceneFlag::None;
    JsonText metaJson;
};

using SceneList = std::vector<Scene>;

} // namespace phicore::adapter::v1
