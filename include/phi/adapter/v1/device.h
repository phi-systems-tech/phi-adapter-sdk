#pragma once

#include <string>
#include <vector>

#include "phi/adapter/v1/types.h"

namespace phicore::adapter::v1 {

struct DeviceEffectDescriptor {
    DeviceEffect effect = DeviceEffect::None;
    std::string id;
    std::string label;
    std::string description;
    bool requiresParams = false;
    JsonText metaJson;
};

using DeviceEffectDescriptorList = std::vector<DeviceEffectDescriptor>;

struct Device {
    std::string name;
    DeviceClass deviceClass = DeviceClass::Unknown;
    DeviceFlags flags = DeviceFlag::None;
    ExternalId externalId;
    std::string manufacturer;
    std::string firmware;
    std::string model;
    JsonText metaJson;
    DeviceEffectDescriptorList effects;
};

using DeviceList = std::vector<Device>;

} // namespace phicore::adapter::v1
