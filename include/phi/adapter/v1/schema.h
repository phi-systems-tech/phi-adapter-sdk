#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "phi/adapter/v1/types.h"

namespace phicore::adapter::v1 {

struct Adapter {
    std::string name;
    std::string host;
    std::string ip;
    std::uint16_t port = 0;
    std::string user;
    std::string password;
    std::string token;

    std::string pluginType;
    ExternalId externalId;
    JsonText metaJson;
    AdapterFlags flags = AdapterFlag::None;
};

using AdapterList = std::vector<Adapter>;

struct AdapterConfigOption {
    std::string value;
    std::string label;
};

using AdapterConfigOptionList = std::vector<AdapterConfigOption>;

struct AdapterConfigResponsiveInt {
    int xs = 0;
    int sm = 0;
    int md = 0;
    int lg = 0;
    int xl = 0;
    int xxl = 0;
};

struct AdapterConfigFieldVisibility {
    std::string fieldKey;
    ScalarValue value;
    AdapterConfigVisibilityOp op = AdapterConfigVisibilityOp::Equals;
};

struct AdapterConfigFieldLayout {
    AdapterConfigResponsiveInt span;
    int position = 0;
    bool hasLabelPosition = false;
    AdapterConfigLabelPosition labelPosition = AdapterConfigLabelPosition::Left;
    int labelSpan = 0;
    int controlSpan = 0;
    bool hasActionPosition = false;
    AdapterConfigActionPosition actionPosition = AdapterConfigActionPosition::None;
    int actionSpan = 0;
};

struct AdapterConfigField {
    std::string key;
    AdapterConfigFieldType type = AdapterConfigFieldType::String;

    std::string label;
    std::string description;
    std::string actionId;
    std::string actionLabel;

    std::string placeholder;
    ScalarValue defaultValue;

    AdapterConfigFieldVisibility visibility;
    AdapterConfigFieldLayout layout;
    std::string parentActionId;

    AdapterConfigOptionList options;
    JsonText metaJson;
    AdapterConfigFieldFlags flags = AdapterConfigFieldFlag::None;
};

using AdapterConfigFieldList = std::vector<AdapterConfigField>;

struct AdapterConfigSectionLayoutDefaults {
    AdapterConfigResponsiveInt span;
    AdapterConfigLabelPosition labelPosition = AdapterConfigLabelPosition::Left;
    int labelSpan = 8;
    int controlSpan = 16;
    AdapterConfigActionPosition actionPosition = AdapterConfigActionPosition::None;
    int actionSpan = 6;
};

struct AdapterConfigSectionLayout {
    int gridUnits = 24;
    int gutterX = 12;
    int gutterY = 8;
    AdapterConfigSectionLayoutDefaults defaults;
};

struct AdapterConfigSection {
    std::string title;
    std::string description;
    AdapterConfigSectionLayout layout;
    AdapterConfigFieldList fields;
};

struct AdapterConfigSchema {
    AdapterConfigSection factory;
    AdapterConfigSection instance;
};

struct AdapterActionDescriptor {
    std::string id;
    std::string label;
    std::string description;
    bool hasForm = false;
    bool danger = false;
    int cooldownMs = 0;
    JsonText confirmJson;
    JsonText metaJson;
};

using AdapterActionDescriptorList = std::vector<AdapterActionDescriptor>;

struct AdapterCapabilities {
    AdapterRequirements required = AdapterRequirement::None;
    AdapterRequirements optional = AdapterRequirement::None;
    AdapterFlags flags = AdapterFlag::None;
    AdapterActionDescriptorList factoryActions;
    AdapterActionDescriptorList instanceActions;
    JsonText defaultsJson;
};

} // namespace phicore::adapter::v1
