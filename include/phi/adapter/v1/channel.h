#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "phi/adapter/v1/schema.h"

namespace phicore::adapter::v1 {

struct Channel {
    Utf8String name;
    ExternalId externalId;
    ChannelKind kind = ChannelKind::Unknown;
    ChannelDataType dataType = ChannelDataType::Unknown;
    ChannelFlags flags = ChannelFlag::None;
    Utf8String unit;
    double minValue = 0.0;
    double maxValue = 0.0;
    double stepValue = 0.0;
    JsonText metaJson;
    AdapterConfigOptionList choices;

    // Runtime value in canonical form for dataType:
    // - Bool: bool(true/false) or int64(0/1)
    // - Int/Enum: int64
    // - Float: double
    // - String: Utf8String
    // - Color: transported via dedicated Color payloads (not ScalarValue)
    ScalarValue lastValue;
    std::int64_t lastUpdateMs = 0;
    bool hasValue = false;
};

using ChannelList = std::vector<Channel>;

} // namespace phicore::adapter::v1
