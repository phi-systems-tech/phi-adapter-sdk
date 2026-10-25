#pragma once

#include <memory>
#include <string>
#include <vector>

#include "phi/adapter/v1/types.h"

namespace phicore::adapter::v1 {

// A group of devices as the hub has it. Behind a d-pointer, see channel.h for
// the rule.
class Group
{
public:
    Group();
    Group(const Group &other);
    Group(Group &&other) noexcept;
    Group &operator=(const Group &other);
    Group &operator=(Group &&other) noexcept;
    ~Group();

    const ExternalId &externalId() const noexcept;
    Group &setExternalId(ExternalId externalId);
    const Utf8String &name() const noexcept;
    Group &setName(Utf8String name);
    const Utf8String &zone() const noexcept;
    Group &setZone(Utf8String zone);
    const std::vector<ExternalId> &deviceExternalIds() const noexcept;
    Group &setDeviceExternalIds(std::vector<ExternalId> ids);
    Group &addDeviceExternalId(ExternalId id);
    const JsonText &metaJson() const noexcept;
    Group &setMetaJson(JsonText metaJson);

private:
    struct Private;
    const Private &p() const noexcept;
    Private &m();
    std::unique_ptr<Private> d;
};

using GroupList = std::vector<Group>;

} // namespace phicore::adapter::v1
