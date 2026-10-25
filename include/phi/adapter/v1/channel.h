#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "phi/adapter/v1/schema.h"

namespace phicore::adapter::v1 {

// A channel as the adapter reports it. The data lives behind a d-pointer and
// is reached through the accessors below, which the shared library exports:
// a field added in a later SDK is a new accessor, never a change of size, so
// an adapter built against this release keeps running on that library. Copies
// are deep; a moved-from object is empty and still valid.
//
//     v1::Channel c;
//     c.setExternalId("power").setName("Power").setKind(v1::ChannelKind::Power)
//      .setDataType(v1::ChannelDataType::Bool).setFlags(v1::kChannelFlagDefaultWrite);
class Channel
{
public:
    Channel();
    /// The four every channel has, in one line; flags and the rest follow.
    Channel(ExternalId externalId, Utf8String name, ChannelKind kind, ChannelDataType dataType,
            ChannelFlags flags = ChannelFlag::None);
    Channel(const Channel &other);
    Channel(Channel &&other) noexcept;
    Channel &operator=(const Channel &other);
    Channel &operator=(Channel &&other) noexcept;
    ~Channel();

    const Utf8String &name() const noexcept;
    Channel &setName(Utf8String name);
    const ExternalId &externalId() const noexcept;
    Channel &setExternalId(ExternalId externalId);
    ChannelKind kind() const noexcept;
    Channel &setKind(ChannelKind kind);
    ChannelDataType dataType() const noexcept;
    Channel &setDataType(ChannelDataType dataType);
    ChannelFlags flags() const noexcept;
    Channel &setFlags(ChannelFlags flags);
    /// Ors into what is set.
    Channel &addFlags(ChannelFlags flags);
    const Utf8String &unit() const noexcept;
    Channel &setUnit(Utf8String unit);
    double minValue() const noexcept;
    Channel &setMinValue(double value);
    double maxValue() const noexcept;
    Channel &setMaxValue(double value);
    double stepValue() const noexcept;
    Channel &setStepValue(double value);
    /// min, max and step in one call, the common case for a level.
    Channel &setRange(double minValue, double maxValue, double stepValue = 0.0);
    const JsonText &metaJson() const noexcept;
    Channel &setMetaJson(JsonText metaJson);
    const AdapterConfigOptionList &choices() const noexcept;
    Channel &setChoices(AdapterConfigOptionList choices);
    Channel &addChoice(Utf8String value, Utf8String label);

    // The runtime value, in canonical form for dataType: Bool as bool or
    // int64 0/1, Int and Enum as int64, Float as double, String as Utf8String;
    // Color travels in its own payloads, never here.
    const ScalarValue &lastValue() const noexcept;
    std::int64_t lastUpdateMs() const noexcept;
    bool hasValue() const noexcept;
    /// Sets the value and its time (0: unknown) and marks the channel as having one.
    Channel &setLastValue(ScalarValue value, std::int64_t updateMs = 0);
    /// Back to "no value yet".
    Channel &clearValue();

private:
    struct Private;
    const Private &p() const noexcept;
    Private &m();
    std::unique_ptr<Private> d;
};

using ChannelList = std::vector<Channel>;

} // namespace phicore::adapter::v1
