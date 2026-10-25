#pragma once

#include <memory>
#include <string>
#include <vector>

#include "phi/adapter/v1/types.h"

namespace phicore::adapter::v1 {

// A room as the hub knows it. Behind a d-pointer, see channel.h for the rule.
class Room
{
public:
    Room();
    Room(const Room &other);
    Room(Room &&other) noexcept;
    Room &operator=(const Room &other);
    Room &operator=(Room &&other) noexcept;
    ~Room();

    const ExternalId &externalId() const noexcept;
    Room &setExternalId(ExternalId externalId);
    const Utf8String &name() const noexcept;
    Room &setName(Utf8String name);
    const Utf8String &zone() const noexcept;
    Room &setZone(Utf8String zone);
    /// What the room is, when the hub knows (Hue's archetype, Matter's
    /// location tag). Unknown when it does not; core fills a blank from the
    /// adapter and never overwrites what the household chose.
    RoomClass roomClass() const noexcept;
    Room &setRoomClass(RoomClass roomClass);
    const std::vector<ExternalId> &deviceExternalIds() const noexcept;
    Room &setDeviceExternalIds(std::vector<ExternalId> ids);
    Room &addDeviceExternalId(ExternalId id);
    const JsonText &metaJson() const noexcept;
    Room &setMetaJson(JsonText metaJson);

private:
    struct Private;
    const Private &p() const noexcept;
    Private &m();
    std::unique_ptr<Private> d;
};

using RoomList = std::vector<Room>;

} // namespace phicore::adapter::v1
