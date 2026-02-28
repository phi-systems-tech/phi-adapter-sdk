#pragma once

#include <string>
#include <vector>

#include "phi/adapter/v1/types.h"

namespace phicore::adapter::v1 {

struct DeviceEffectDescriptor {
    DeviceEffect effect = DeviceEffect::None;
    Utf8String id;
    Utf8String label;
    Utf8String description;
    bool requiresParams = false;
    JsonText metaJson;
};

using DeviceEffectDescriptorList = std::vector<DeviceEffectDescriptor>;

struct Device {
    Utf8String name;
    DeviceClass deviceClass = DeviceClass::Unknown;
    DeviceFlags flags = DeviceFlag::None;
    ExternalId externalId;
    Utf8String manufacturer;
    Utf8String firmware;
    Utf8String model;
    JsonText metaJson;
    DeviceEffectDescriptorList effects;
};

using DeviceList = std::vector<Device>;

} // namespace phicore::adapter::v1
