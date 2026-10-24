// Name <-> value round trips for the enum/flag vocabulary in enum_names.h,
// with extra attention to the four families the PowerOnBehavior / fault-flags
// feature added: PowerOnBehavior, ElectricalFaultFlag, DeviceFaultFlag, and
// the ChannelKind/ChannelDataType entries that point at them.

#include <phi/adapter/testing/check.h>

#include "phi/adapter/v1/enum_names.h"
#include "phi/adapter/v1/types.h"

#include <string>
#include <string_view>
#include <vector>

using namespace phicore::adapter::v1;
using namespace phicore::adapter::v1::enum_names;

namespace {

void testPowerOnBehaviorRoundTrips()
{
    static const std::pair<PowerOnBehavior, std::string_view> kCases[] = {
        { PowerOnBehavior::Unknown, "Unknown" },
        { PowerOnBehavior::Off, "Off" },
        { PowerOnBehavior::On, "On" },
        { PowerOnBehavior::Previous, "Previous" },
        { PowerOnBehavior::Toggle, "Toggle" },
    };
    for (const auto &[value, name] : kCases) {
        PHI_CHECK(enumNameFor("PowerOnBehavior", static_cast<int>(value)) == name);
        int parsed = -1;
        PHI_CHECK(parseEnumValueByName("PowerOnBehavior", name, &parsed));
        PHI_CHECK(parsed == static_cast<int>(value));
    }
}

void testElectricalFaultFlagRoundTrips()
{
    static const std::pair<ElectricalFaultFlag, std::string_view> kCases[] = {
        { ElectricalFaultFlag::None, "None" },
        { ElectricalFaultFlag::ShortCircuit, "ShortCircuit" },
        { ElectricalFaultFlag::Leakage, "Leakage" },
        { ElectricalFaultFlag::Overcurrent, "Overcurrent" },
        { ElectricalFaultFlag::Overvoltage, "Overvoltage" },
        { ElectricalFaultFlag::Surge, "Surge" },
        { ElectricalFaultFlag::Overtemperature, "Overtemperature" },
        { ElectricalFaultFlag::Overload, "Overload" },
        { ElectricalFaultFlag::PhaseLoss, "PhaseLoss" },
        { ElectricalFaultFlag::PhaseSequence, "PhaseSequence" },
        { ElectricalFaultFlag::Unbalance, "Unbalance" },
        { ElectricalFaultFlag::Undervoltage, "Undervoltage" },
        { ElectricalFaultFlag::Undercurrent, "Undercurrent" },
        { ElectricalFaultFlag::Underload, "Underload" },
        { ElectricalFaultFlag::Outage, "Outage" },
        { ElectricalFaultFlag::Other, "Other" },
    };
    for (const auto &[value, name] : kCases) {
        PHI_CHECK(enumNameFor("ElectricalFaultFlag", static_cast<int>(value)) == name);
        int parsed = -1;
        PHI_CHECK(parseEnumValueByName("ElectricalFaultFlag", name, &parsed));
        PHI_CHECK(parsed == static_cast<int>(value));
    }
    // Bit values are pinned: the wire format and Tuya's bit mapping both rely
    // on these staying put.
    PHI_CHECK(static_cast<int>(ElectricalFaultFlag::ShortCircuit) == (1 << 0));
    PHI_CHECK(static_cast<int>(ElectricalFaultFlag::Outage) == (1 << 13));
    PHI_CHECK(static_cast<int>(ElectricalFaultFlag::Other) == (1 << 15));
}

void testDeviceFaultFlagRoundTrips()
{
    static const std::pair<DeviceFaultFlag, std::string_view> kCases[] = {
        { DeviceFaultFlag::None, "None" },
        { DeviceFaultFlag::MotorFault, "MotorFault" },
        { DeviceFaultFlag::SensorFault, "SensorFault" },
        { DeviceFaultFlag::WaterShortage, "WaterShortage" },
        { DeviceFaultFlag::BatteryCritical, "BatteryCritical" },
        { DeviceFaultFlag::LowTemperature, "LowTemperature" },
        { DeviceFaultFlag::BatteryLow, "BatteryLow" },
        { DeviceFaultFlag::BatteryDegraded, "BatteryDegraded" },
        { DeviceFaultFlag::Other, "Other" },
    };
    for (const auto &[value, name] : kCases) {
        PHI_CHECK(enumNameFor("DeviceFaultFlag", static_cast<int>(value)) == name);
        int parsed = -1;
        PHI_CHECK(parseEnumValueByName("DeviceFaultFlag", name, &parsed));
        PHI_CHECK(parsed == static_cast<int>(value));
    }
    PHI_CHECK(static_cast<int>(DeviceFaultFlag::MotorFault) == (1 << 0));
    PHI_CHECK(static_cast<int>(DeviceFaultFlag::Other) == (1 << 15));
}

void testChannelKindAndDataTypeEntries()
{
    PHI_CHECK(enumNameFor("ChannelKind", static_cast<int>(ChannelKind::PowerOnBehavior)) == "PowerOnBehavior");
    PHI_CHECK(enumNameFor("ChannelKind", static_cast<int>(ChannelKind::ElectricalFault)) == "ElectricalFault");
    PHI_CHECK(enumNameFor("ChannelKind", static_cast<int>(ChannelKind::DeviceFault)) == "DeviceFault");
    PHI_CHECK(static_cast<int>(ChannelKind::PowerOnBehavior) == 70);
    PHI_CHECK(static_cast<int>(ChannelKind::ElectricalFault) == 71);
    PHI_CHECK(static_cast<int>(ChannelKind::DeviceFault) == 72);

    int parsed = -1;
    PHI_CHECK(parseEnumValueByName("ChannelKind", "PowerOnBehavior", &parsed));
    PHI_CHECK(parsed == 70);
    PHI_CHECK(parseEnumValueByName("ChannelKind", "ElectricalFault", &parsed));
    PHI_CHECK(parsed == 71);
    PHI_CHECK(parseEnumValueByName("ChannelKind", "DeviceFault", &parsed));
    PHI_CHECK(parsed == 72);

    PHI_CHECK(enumNameFor("ChannelDataType", static_cast<int>(ChannelDataType::Flags)) == "Flags");
    PHI_CHECK(static_cast<int>(ChannelDataType::Flags) == 8);
    PHI_CHECK(parseEnumValueByName("ChannelDataType", "Flags", &parsed));
    PHI_CHECK(parsed == 8);
}

void testFlagNamesForOrderAndEmptiness()
{
    // Severity order: lowest set bit first. ShortCircuit (bit 0) then
    // Overcurrent (bit 2) then Other (bit 15), regardless of the order the
    // bits were combined in.
    const int mask = static_cast<int>(ElectricalFaultFlag::Overcurrent)
        | static_cast<int>(ElectricalFaultFlag::ShortCircuit)
        | static_cast<int>(ElectricalFaultFlag::Other);
    const std::vector<std::string> names = flagNamesFor("ElectricalFaultFlag", mask);
    const std::vector<std::string> expected = { "ShortCircuit", "Overcurrent", "Other" };
    PHI_CHECK(names == expected);

    PHI_CHECK(flagNamesFor("ElectricalFaultFlag", 0).empty());
    PHI_CHECK(flagNamesFor("DeviceFaultFlag", 0).empty());

    const int deviceMask = static_cast<int>(DeviceFaultFlag::BatteryLow)
        | static_cast<int>(DeviceFaultFlag::WaterShortage)
        | static_cast<int>(DeviceFaultFlag::LowTemperature);
    const std::vector<std::string> deviceNames = flagNamesFor("DeviceFaultFlag", deviceMask);
    const std::vector<std::string> expectedDevice = { "WaterShortage", "LowTemperature", "BatteryLow" };
    PHI_CHECK(deviceNames == expectedDevice);
}

void testMediaPlayerVocabulary()
{
    PHI_CHECK(enumNameFor("MediaPlayerAction",
                          static_cast<int>(MediaPlayerAction::PlayMedia)) == "PlayMedia");
    PHI_CHECK(enumNameFor("MediaPlayerEnqueue",
                          static_cast<int>(MediaPlayerEnqueue::Next)) == "Next");
    int parsed = -1;
    PHI_CHECK(parseEnumValueByName("MediaPlayerAction", "seekby", &parsed));
    PHI_CHECK(parsed == static_cast<int>(MediaPlayerAction::SeekBy));

    const MediaPlayerFeatures features = MediaPlayerFeature::Play
        | MediaPlayerFeature::Pause | MediaPlayerFeature::PlayMedia
        | MediaPlayerFeature::Enqueue;
    PHI_CHECK(hasFlag(features, MediaPlayerFeature::PlayMedia));
    const std::vector<std::string> names =
        flagNamesFor("MediaPlayerFeature", static_cast<int>(features));
    const std::vector<std::string> expected = { "Play", "Pause", "PlayMedia", "Enqueue" };
    PHI_CHECK(names == expected);

    PHI_CHECK(static_cast<int>(MediaPlayerAction::PlayMedia) == 8);
    PHI_CHECK(static_cast<int>(MediaPlayerFeature::Enqueue) == 0x100);
}

// The voice and camera additions of 0.25.0: every value has a name both ways,
// the numbers are the wire's and must not move.
void testVoiceAndCameraEntries()
{
    const std::vector<std::pair<ChannelKind, int>> kinds = {
        { ChannelKind::Chime, 400 },        { ChannelKind::VoiceLevel, 401 },
        { ChannelKind::Announcement, 402 }, { ChannelKind::VideoStream, 420 },
        { ChannelKind::Snapshot, 421 },     { ChannelKind::DoorbellRing, 422 },
        { ChannelKind::ObjectDetected, 423 }, { ChannelKind::PtzMove, 424 },
        { ChannelKind::PtzPreset, 425 },    { ChannelKind::NightVision, 426 },
        { ChannelKind::PrivacyMode, 427 },  { ChannelKind::Siren, 428 },
    };
    for (const auto &[kind, number] : kinds) {
        PHI_CHECK(static_cast<int>(kind) == number);
        const std::string name = enumNameFor("ChannelKind", number, false);
        PHI_CHECK(!name.empty());
        int parsed = -1;
        PHI_CHECK(parseEnumValueByName("ChannelKind", name, &parsed));
        PHI_CHECK(parsed == number);
    }
    PHI_CHECK(enumNameFor("DeviceClass", static_cast<int>(DeviceClass::Camera)) == "Camera");
    PHI_CHECK(enumNameFor("DeviceClass", static_cast<int>(DeviceClass::Doorbell)) == "Doorbell");
    PHI_CHECK(static_cast<int>(DeviceClass::Doorbell) == 14);
    PHI_CHECK(enumNameFor("ChimePattern", static_cast<int>(ChimePattern::NotUnderstood)) == "NotUnderstood");
    PHI_CHECK(enumNameFor("VideoStreamState", static_cast<int>(VideoStreamState::Live)) == "Live");
    PHI_CHECK(enumNameFor("NightVisionMode", static_cast<int>(NightVisionMode::Auto)) == "Auto");
    PHI_CHECK(enumNameFor("PtzCommand", static_cast<int>(PtzCommand::ZoomOut)) == "ZoomOut");
    PHI_CHECK(static_cast<int>(MediaPlayerFeature::Announce) == 0x200);
    PHI_CHECK(flagNamesFor("MediaPlayerFeature", 0x200) == std::vector<std::string>{ "Announce" });

    const CameraFeatures camera = CameraFeature::Video | CameraFeature::PanTilt | CameraFeature::ObjectDetection;
    PHI_CHECK((flagNamesFor("CameraFeature", static_cast<int>(camera))
               == std::vector<std::string>{ "Video", "PanTilt", "ObjectDetection" }));
    const ObjectClassFlag seen = ObjectClassFlag::Person | ObjectClassFlag::Package;
    PHI_CHECK((flagNamesFor("ObjectClassFlag", static_cast<int>(seen))
               == std::vector<std::string>{ "Person", "Package" }));
}

void testUnknownEnumTypeNameFallsBackGracefully()
{
    // enumNameFor falls back to the number for a type name it does not know;
    // flagNamesFor falls back to an empty list.
    PHI_CHECK(enumNameFor("NoSuchEnum", 42) == "42");
    PHI_CHECK(flagNamesFor("NoSuchEnum", 42).empty());
    int parsed = -1;
    PHI_CHECK(!parseEnumValueByName("NoSuchEnum", "Anything", &parsed));
}

} // namespace

int main()
{
    testPowerOnBehaviorRoundTrips();
    testElectricalFaultFlagRoundTrips();
    testDeviceFaultFlagRoundTrips();
    testChannelKindAndDataTypeEntries();
    testFlagNamesForOrderAndEmptiness();
    testMediaPlayerVocabulary();
    testVoiceAndCameraEntries();
    testUnknownEnumTypeNameFallsBackGracefully();
    return phi::testing::report("sdk_enum_names_tests");
}
