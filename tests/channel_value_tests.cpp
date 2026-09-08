// Composite channel values, and the field that stands for them.
//
// A firmware update is a status and two versions. Reporting only the status
// loses what a person wants to read; reporting only a version loses the
// answer. So the value is an object - and everything that has to reduce it to
// one comparable thing uses the field the kind names, rather than each side
// picking for itself.

#include <phi/adapter/testing/check.h>

#include "phi/adapter/v1/enum_names.h"
#include "phi/adapter/v1/types.h"
#include "phi/adapter/v1/value.h"

#include <string>
#include <string_view>

using namespace phicore::adapter::v1;

namespace {

void testTheProjectionIsNamedInOnePlace()
{
    // The whole point: the history, the automation conditions and the adapter
    // read this from the same function, so they cannot disagree about what the
    // channel "is".
    static_assert(channelProjectionField(ChannelKind::DeviceSoftwareUpdate) == "status");
    PHI_CHECK(channelProjectionField(ChannelKind::DeviceSoftwareUpdate) == "status");

    // Everything that is one measurement has no projection, because it has
    // nothing to project from.
    PHI_CHECK(channelProjectionField(ChannelKind::Temperature).empty());
    PHI_CHECK(channelProjectionField(ChannelKind::PowerOnOff).empty());
    PHI_CHECK(channelProjectionField(ChannelKind::Unknown).empty());

    // Colour is composite and still has none: it predates this, keeps its own
    // data type and its own transport, and a colour has no scalar meaning to
    // put in a history row or compare in a rule.
    PHI_CHECK(channelProjectionField(ChannelKind::ColorRGB).empty());
}

void testTheDataTypeIsPartOfTheVocabulary()
{
    PHI_CHECK(static_cast<int>(ChannelDataType::Json) == 7);
    // Named, so a descriptor crossing the wire says "Json" rather than a number
    // nobody can look up.
    bool found = false;
    for (const enum_names::EnumValueName &entry : enum_names::kChannelDataTypeNames) {
        if (entry.value == static_cast<int>(ChannelDataType::Json)) {
            found = true;
            PHI_CHECK(std::string_view(entry.name) == "Json");
        }
    }
    PHI_CHECK_MSG(found, "the Json data type has no name");
}

void testTheShapeIsFlatAndScalar()
{
    // Not a general escape hatch. A composite value is named scalars: it is
    // what every composite kind actually needs, it cannot be built into a
    // document that breaks the envelope around it, and a projection field has
    // to name a scalar anyway.
    ChannelValueFields fields;
    fields.emplace_back("status", Utf8String("UpdateAvailable"));
    fields.emplace_back("currentVersion", Utf8String("258.08.25"));
    fields.emplace_back("targetVersion", Utf8String("258.09.01"));
    PHI_CHECK(fields.size() == 3);

    const std::string projection(channelProjectionField(ChannelKind::DeviceSoftwareUpdate));
    bool projectionIsPresent = false;
    for (const auto &[name, value] : fields) {
        if (name != projection)
            continue;
        projectionIsPresent = true;
        // A projection that is not a scalar could not go into a history row.
        PHI_CHECK(std::holds_alternative<Utf8String>(value));
    }
    PHI_CHECK_MSG(projectionIsPresent,
                  "a composite value without its projection field has nothing to compare");
}

} // namespace

int main()
{
    testTheProjectionIsNamedInOnePlace();
    testTheDataTypeIsPartOfTheVocabulary();
    testTheShapeIsFlatAndScalar();
    return phi::testing::report("sdk_channel_value_tests");
}
