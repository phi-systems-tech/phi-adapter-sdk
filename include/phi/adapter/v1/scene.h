#pragma once

#include <memory>
#include <string>
#include <vector>

#include "phi/adapter/v1/types.h"

namespace phicore::adapter::v1 {

// A scene as the hub has it, scoped to a room or a group. Behind a d-pointer,
// see channel.h for the rule.
class Scene
{
public:
    Scene();
    Scene(const Scene &other);
    Scene(Scene &&other) noexcept;
    Scene &operator=(const Scene &other);
    Scene &operator=(Scene &&other) noexcept;
    ~Scene();

    const ExternalId &externalId() const noexcept;
    Scene &setExternalId(ExternalId externalId);
    const Utf8String &name() const noexcept;
    Scene &setName(Utf8String name);
    const Utf8String &description() const noexcept;
    Scene &setDescription(Utf8String description);
    const ExternalId &scopeExternalId() const noexcept;
    Scene &setScopeExternalId(ExternalId scopeExternalId);
    /// "room" or "group": what scopeExternalId names.
    const Utf8String &scopeType() const noexcept;
    Scene &setScopeType(Utf8String scopeType);
    Scene &setScope(Utf8String scopeType, ExternalId scopeExternalId);
    const Utf8String &avatarColor() const noexcept;
    Scene &setAvatarColor(Utf8String avatarColor);
    const Utf8String &image() const noexcept;
    Scene &setImage(Utf8String image);
    const Utf8String &presetTag() const noexcept;
    Scene &setPresetTag(Utf8String presetTag);
    SceneState state() const noexcept;
    Scene &setState(SceneState state);
    SceneFlags flags() const noexcept;
    Scene &setFlags(SceneFlags flags);
    /// Ors into what is set.
    Scene &addFlags(SceneFlags flags);
    const JsonText &metaJson() const noexcept;
    Scene &setMetaJson(JsonText metaJson);

private:
    struct Private;
    const Private &p() const noexcept;
    Private &m();
    std::unique_ptr<Private> d;
};

using SceneList = std::vector<Scene>;

} // namespace phicore::adapter::v1
