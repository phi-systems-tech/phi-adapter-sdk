#pragma once

#include <array>
#include <charconv>
#include <cstddef>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "phi/adapter/v1/types.h"

namespace phicore::adapter::v1::enum_names {

struct EnumValueName {
    int value = 0;
    std::string_view name{};
};

[[nodiscard]] constexpr char lowerAscii(char c) noexcept
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

[[nodiscard]] constexpr bool equalsIgnoreCaseAscii(std::string_view lhs, std::string_view rhs) noexcept
{
    if (lhs.size() != rhs.size())
        return false;
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        if (lowerAscii(lhs[i]) != lowerAscii(rhs[i]))
            return false;
    }
    return true;
}

template <std::size_t N>
[[nodiscard]] constexpr const EnumValueName *findByValue(
    const std::array<EnumValueName, N> &entries,
    int value) noexcept
{
    for (const EnumValueName &entry : entries) {
        if (entry.value == value)
            return &entry;
    }
    return nullptr;
}

template <std::size_t N>
[[nodiscard]] constexpr const EnumValueName *findByName(
    const std::array<EnumValueName, N> &entries,
    std::string_view name) noexcept
{
    for (const EnumValueName &entry : entries) {
        if (equalsIgnoreCaseAscii(entry.name, name))
            return &entry;
    }
    return nullptr;
}

[[nodiscard]] inline bool parseNumeric(std::string_view text, int *outValue)
{
    if (!outValue || text.empty())
        return false;

    int parsed = 0;
    const char *const begin = text.data();
    const char *const end = begin + text.size();
    const auto result = std::from_chars(begin, end, parsed, 10);
    if (result.ec != std::errc() || result.ptr != end)
        return false;

    *outValue = parsed;
    return true;
}

template <std::size_t N>
[[nodiscard]] inline std::string valueToName(
    const std::array<EnumValueName, N> &entries,
    int value,
    bool fallbackNumber)
{
    if (const EnumValueName *entry = findByValue(entries, value))
        return std::string(entry->name);
    return fallbackNumber ? std::to_string(value) : std::string{};
}

template <std::size_t N>
[[nodiscard]] inline std::vector<std::string> maskToNames(
    const std::array<EnumValueName, N> &entries,
    int mask)
{
    std::vector<std::string> names;
    for (const EnumValueName &entry : entries) {
        if (entry.value == 0)
            continue;
        if ((mask & entry.value) == entry.value)
            names.emplace_back(entry.name);
    }
    return names;
}

template <std::size_t N>
[[nodiscard]] inline bool parseNameToValue(
    const std::array<EnumValueName, N> &entries,
    std::string_view name,
    int *outValue)
{
    if (!outValue)
        return false;

    if (parseNumeric(name, outValue))
        return true;

    const EnumValueName *entry = findByName(entries, name);
    if (!entry)
        return false;

    *outValue = entry->value;
    return true;
}

inline constexpr std::array<EnumValueName, 9> kCmdStatusNames = {{
    { static_cast<int>(CmdStatus::Success), "Success" },
    { static_cast<int>(CmdStatus::Failure), "Failure" },
    { static_cast<int>(CmdStatus::Timeout), "Timeout" },
    { static_cast<int>(CmdStatus::NotSupported), "NotSupported" },
    { static_cast<int>(CmdStatus::InvalidArgument), "InvalidArgument" },
    { static_cast<int>(CmdStatus::Busy), "Busy" },
    { static_cast<int>(CmdStatus::TemporarilyOffline), "TemporarilyOffline" },
    { static_cast<int>(CmdStatus::NotAuthorized), "NotAuthorized" },
    { static_cast<int>(CmdStatus::NotImplemented), "NotImplemented" },
}};

inline constexpr std::array<EnumValueName, 6> kActionResultTypeNames = {{
    { static_cast<int>(ActionResultType::None), "None" },
    { static_cast<int>(ActionResultType::Boolean), "Boolean" },
    { static_cast<int>(ActionResultType::Integer), "Integer" },
    { static_cast<int>(ActionResultType::Float), "Float" },
    { static_cast<int>(ActionResultType::String), "String" },
    { static_cast<int>(ActionResultType::StringList), "StringList" },
}};

inline constexpr std::array<EnumValueName, 13> kDeviceClassNames = {{
    { static_cast<int>(DeviceClass::Unknown), "Unknown" },
    { static_cast<int>(DeviceClass::Light), "Light" },
    { static_cast<int>(DeviceClass::Switch), "Switch" },
    { static_cast<int>(DeviceClass::Sensor), "Sensor" },
    { static_cast<int>(DeviceClass::Button), "Button" },
    { static_cast<int>(DeviceClass::Plug), "Plug" },
    { static_cast<int>(DeviceClass::Cover), "Cover" },
    { static_cast<int>(DeviceClass::Thermostat), "Thermostat" },
    { static_cast<int>(DeviceClass::Gateway), "Gateway" },
    { static_cast<int>(DeviceClass::MediaPlayer), "MediaPlayer" },
    { static_cast<int>(DeviceClass::Heater), "Heater" },
    { static_cast<int>(DeviceClass::Gate), "Gate" },
    { static_cast<int>(DeviceClass::Valve), "Valve" },
}};

inline constexpr std::array<EnumValueName, 10> kButtonEventCodeNames = {{
    { static_cast<int>(ButtonEventCode::None), "None" },
    { static_cast<int>(ButtonEventCode::InitialPress), "InitialPress" },
    { static_cast<int>(ButtonEventCode::DoublePress), "DoublePress" },
    { static_cast<int>(ButtonEventCode::TriplePress), "TriplePress" },
    { static_cast<int>(ButtonEventCode::QuadruplePress), "QuadruplePress" },
    { static_cast<int>(ButtonEventCode::QuintuplePress), "QuintuplePress" },
    { static_cast<int>(ButtonEventCode::LongPress), "LongPress" },
    { static_cast<int>(ButtonEventCode::LongPressRelease), "LongPressRelease" },
    { static_cast<int>(ButtonEventCode::ShortPressRelease), "ShortPressRelease" },
    { static_cast<int>(ButtonEventCode::Repeat), "Repeat" },
}};

inline constexpr std::array<EnumValueName, 5> kRockerModeNames = {{
    { static_cast<int>(RockerMode::Unknown), "Unknown" },
    { static_cast<int>(RockerMode::SingleRocker), "SingleRocker" },
    { static_cast<int>(RockerMode::DualRocker), "DualRocker" },
    { static_cast<int>(RockerMode::SinglePush), "SinglePush" },
    { static_cast<int>(RockerMode::DualPush), "DualPush" },
}};

inline constexpr std::array<EnumValueName, 6> kSensitivityLevelNames = {{
    { static_cast<int>(SensitivityLevel::Unknown), "Unknown" },
    { static_cast<int>(SensitivityLevel::Low), "Low" },
    { static_cast<int>(SensitivityLevel::Medium), "Medium" },
    { static_cast<int>(SensitivityLevel::High), "High" },
    { static_cast<int>(SensitivityLevel::VeryHigh), "VeryHigh" },
    { static_cast<int>(SensitivityLevel::Max), "Max" },
}};

inline constexpr std::array<EnumValueName, 6> kOperatingLevelNames = {{
    { static_cast<int>(OperatingLevel::Unknown), "Unknown" },
    { static_cast<int>(OperatingLevel::Off), "Off" },
    { static_cast<int>(OperatingLevel::Low), "Low" },
    { static_cast<int>(OperatingLevel::Medium), "Medium" },
    { static_cast<int>(OperatingLevel::High), "High" },
    { static_cast<int>(OperatingLevel::Auto), "Auto" },
}};

inline constexpr std::array<EnumValueName, 7> kPresetModeNames = {{
    { static_cast<int>(PresetMode::Unknown), "Unknown" },
    { static_cast<int>(PresetMode::Eco), "Eco" },
    { static_cast<int>(PresetMode::Normal), "Normal" },
    { static_cast<int>(PresetMode::Comfort), "Comfort" },
    { static_cast<int>(PresetMode::Sleep), "Sleep" },
    { static_cast<int>(PresetMode::Away), "Away" },
    { static_cast<int>(PresetMode::Boost), "Boost" },
}};

inline constexpr std::array<EnumValueName, 42> kChannelKindNames = {{
    { static_cast<int>(ChannelKind::Unknown), "Unknown" },
    { static_cast<int>(ChannelKind::PowerOnOff), "PowerOnOff" },
    { static_cast<int>(ChannelKind::ButtonEvent), "ButtonEvent" },
    { static_cast<int>(ChannelKind::Brightness), "Brightness" },
    { static_cast<int>(ChannelKind::ColorTemperature), "ColorTemperature" },
    { static_cast<int>(ChannelKind::ColorRGB), "ColorRGB" },
    { static_cast<int>(ChannelKind::ColorTemperaturePreset), "ColorTemperaturePreset" },
    { static_cast<int>(ChannelKind::Volume), "Volume" },
    { static_cast<int>(ChannelKind::Mute), "Mute" },
    { static_cast<int>(ChannelKind::HdmiInput), "HdmiInput" },
    { static_cast<int>(ChannelKind::PlayPause), "PlayPause" },
    { static_cast<int>(ChannelKind::Temperature), "Temperature" },
    { static_cast<int>(ChannelKind::Humidity), "Humidity" },
    { static_cast<int>(ChannelKind::Illuminance), "Illuminance" },
    { static_cast<int>(ChannelKind::Motion), "Motion" },
    { static_cast<int>(ChannelKind::Battery), "Battery" },
    { static_cast<int>(ChannelKind::CO2), "CO2" },
    { static_cast<int>(ChannelKind::RelativeRotation), "RelativeRotation" },
    { static_cast<int>(ChannelKind::ConnectivityStatus), "ConnectivityStatus" },
    { static_cast<int>(ChannelKind::DeviceSoftwareUpdate), "DeviceSoftwareUpdate" },
    { static_cast<int>(ChannelKind::SignalStrength), "SignalStrength" },
    { static_cast<int>(ChannelKind::Power), "Power" },
    { static_cast<int>(ChannelKind::Voltage), "Voltage" },
    { static_cast<int>(ChannelKind::Current), "Current" },
    { static_cast<int>(ChannelKind::Energy), "Energy" },
    { static_cast<int>(ChannelKind::LinkQuality), "LinkQuality" },
    { static_cast<int>(ChannelKind::Duration), "Duration" },
    { static_cast<int>(ChannelKind::Contact), "Contact" },
    { static_cast<int>(ChannelKind::Tamper), "Tamper" },
    { static_cast<int>(ChannelKind::AmbientLightLevel), "AmbientLightLevel" },
    { static_cast<int>(ChannelKind::MotionSensitivity), "MotionSensitivity" },
    { static_cast<int>(ChannelKind::PhValue), "PhValue" },
    { static_cast<int>(ChannelKind::OrpValue), "OrpValue" },
    { static_cast<int>(ChannelKind::SaltPpm), "SaltPpm" },
    { static_cast<int>(ChannelKind::Conductivity), "Conductivity" },
    { static_cast<int>(ChannelKind::TdsValue), "TdsValue" },
    { static_cast<int>(ChannelKind::SpecificGravity), "SpecificGravity" },
    { static_cast<int>(ChannelKind::WaterHardness), "WaterHardness" },
    { static_cast<int>(ChannelKind::FreeChlorine), "FreeChlorine" },
    { static_cast<int>(ChannelKind::FilterPressure), "FilterPressure" },
    { static_cast<int>(ChannelKind::WaterFlow), "WaterFlow" },
    { static_cast<int>(ChannelKind::SceneTrigger), "SceneTrigger" },
}};

inline constexpr std::array<EnumValueName, 7> kChannelDataTypeNames = {{
    { static_cast<int>(ChannelDataType::Unknown), "Unknown" },
    { static_cast<int>(ChannelDataType::Bool), "Bool" },
    { static_cast<int>(ChannelDataType::Int), "Int" },
    { static_cast<int>(ChannelDataType::Float), "Float" },
    { static_cast<int>(ChannelDataType::String), "String" },
    { static_cast<int>(ChannelDataType::Color), "Color" },
    { static_cast<int>(ChannelDataType::Enum), "Enum" },
}};

inline constexpr std::array<EnumValueName, 4> kConnectivityStatusNames = {{
    { static_cast<int>(ConnectivityStatus::Unknown), "Unknown" },
    { static_cast<int>(ConnectivityStatus::Connected), "Connected" },
    { static_cast<int>(ConnectivityStatus::Limited), "Limited" },
    { static_cast<int>(ConnectivityStatus::Disconnected), "Disconnected" },
}};

inline constexpr std::array<EnumValueName, 9> kAdapterConfigFieldTypeNames = {{
    { static_cast<int>(AdapterConfigFieldType::String), "String" },
    { static_cast<int>(AdapterConfigFieldType::Password), "Password" },
    { static_cast<int>(AdapterConfigFieldType::Integer), "Integer" },
    { static_cast<int>(AdapterConfigFieldType::Boolean), "Boolean" },
    { static_cast<int>(AdapterConfigFieldType::Hostname), "Hostname" },
    { static_cast<int>(AdapterConfigFieldType::Port), "Port" },
    { static_cast<int>(AdapterConfigFieldType::QrCode), "QrCode" },
    { static_cast<int>(AdapterConfigFieldType::Select), "Select" },
    { static_cast<int>(AdapterConfigFieldType::Action), "Action" },
}};

inline constexpr std::array<EnumValueName, 3> kAdapterConfigLabelPositionNames = {{
    { static_cast<int>(AdapterConfigLabelPosition::Top), "Top" },
    { static_cast<int>(AdapterConfigLabelPosition::Left), "Left" },
    { static_cast<int>(AdapterConfigLabelPosition::Right), "Right" },
}};

inline constexpr std::array<EnumValueName, 3> kAdapterConfigActionPositionNames = {{
    { static_cast<int>(AdapterConfigActionPosition::None), "None" },
    { static_cast<int>(AdapterConfigActionPosition::Inline), "Inline" },
    { static_cast<int>(AdapterConfigActionPosition::Below), "Below" },
}};

inline constexpr std::array<EnumValueName, 2> kAdapterConfigVisibilityOpNames = {{
    { static_cast<int>(AdapterConfigVisibilityOp::Equals), "Equals" },
    { static_cast<int>(AdapterConfigVisibilityOp::Contains), "Contains" },
}};

inline constexpr std::array<EnumValueName, 8> kChannelFlagNames = {{
    { static_cast<int>(ChannelFlag::None), "None" },
    { static_cast<int>(ChannelFlag::Readable), "Readable" },
    { static_cast<int>(ChannelFlag::Writable), "Writable" },
    { static_cast<int>(ChannelFlag::Reportable), "Reportable" },
    { static_cast<int>(ChannelFlag::Retained), "Retained" },
    { static_cast<int>(ChannelFlag::Inactive), "Inactive" },
    { static_cast<int>(ChannelFlag::NoTrigger), "NoTrigger" },
    { static_cast<int>(ChannelFlag::Suppress), "Suppress" },
}};

inline constexpr std::array<EnumValueName, 5> kDeviceFlagNames = {{
    { static_cast<int>(DeviceFlag::None), "None" },
    { static_cast<int>(DeviceFlag::Wireless), "Wireless" },
    { static_cast<int>(DeviceFlag::Battery), "Battery" },
    { static_cast<int>(DeviceFlag::Flushable), "Flushable" },
    { static_cast<int>(DeviceFlag::Ble), "Ble" },
}};

inline constexpr std::array<EnumValueName, 4> kSceneFlagNames = {{
    { static_cast<int>(SceneFlag::None), "None" },
    { static_cast<int>(SceneFlag::OriginAdapter), "OriginAdapter" },
    { static_cast<int>(SceneFlag::SupportsDynamic), "SupportsDynamic" },
    { static_cast<int>(SceneFlag::SupportsDeactivate), "SupportsDeactivate" },
}};

inline constexpr std::array<EnumValueName, 8> kAdapterFlagNames = {{
    { static_cast<int>(AdapterFlag::None), "None" },
    { static_cast<int>(AdapterFlag::UseTls), "UseTls" },
    { static_cast<int>(AdapterFlag::CloudServices), "CloudServices" },
    { static_cast<int>(AdapterFlag::EnableLogs), "EnableLogs" },
    { static_cast<int>(AdapterFlag::RequiresPolling), "RequiresPolling" },
    { static_cast<int>(AdapterFlag::SupportsDiscovery), "SupportsDiscovery" },
    { static_cast<int>(AdapterFlag::SupportsProbe), "SupportsProbe" },
    { static_cast<int>(AdapterFlag::SupportsRename), "SupportsRename" },
}};

inline constexpr std::array<EnumValueName, 7> kAdapterConfigFieldFlagNames = {{
    { static_cast<int>(AdapterConfigFieldFlag::None), "None" },
    { static_cast<int>(AdapterConfigFieldFlag::Required), "Required" },
    { static_cast<int>(AdapterConfigFieldFlag::Secret), "Secret" },
    { static_cast<int>(AdapterConfigFieldFlag::ReadOnly), "ReadOnly" },
    { static_cast<int>(AdapterConfigFieldFlag::Transient), "Transient" },
    { static_cast<int>(AdapterConfigFieldFlag::Multi), "Multi" },
    { static_cast<int>(AdapterConfigFieldFlag::InstanceOnly), "InstanceOnly" },
}};

inline constexpr std::array<EnumValueName, 11> kAdapterRequirementNames = {{
    { static_cast<int>(AdapterRequirement::None), "None" },
    { static_cast<int>(AdapterRequirement::Host), "Host" },
    { static_cast<int>(AdapterRequirement::Port), "Port" },
    { static_cast<int>(AdapterRequirement::Username), "Username" },
    { static_cast<int>(AdapterRequirement::Password), "Password" },
    { static_cast<int>(AdapterRequirement::AppKey), "AppKey" },
    { static_cast<int>(AdapterRequirement::Token), "Token" },
    { static_cast<int>(AdapterRequirement::QrCode), "QrCode" },
    { static_cast<int>(AdapterRequirement::SupportsTls), "SupportsTls" },
    { static_cast<int>(AdapterRequirement::ManualConfirm), "ManualConfirm" },
    { static_cast<int>(AdapterRequirement::UsesRetryInterval), "UsesRetryInterval" },
}};

[[nodiscard]] inline std::string cmdStatusName(CmdStatus status)
{
    return valueToName(kCmdStatusNames, static_cast<int>(status), true);
}

[[nodiscard]] inline std::string actionResultTypeName(ActionResultType resultType)
{
    return valueToName(kActionResultTypeNames, static_cast<int>(resultType), true);
}

[[nodiscard]] inline std::string enumNameFor(
    std::string_view enumTypeName,
    int value,
    bool fallbackNumber = true)
{
    if (equalsIgnoreCaseAscii(enumTypeName, "CmdStatus"))
        return valueToName(kCmdStatusNames, value, fallbackNumber);
    if (equalsIgnoreCaseAscii(enumTypeName, "ActionResultType"))
        return valueToName(kActionResultTypeNames, value, fallbackNumber);
    if (equalsIgnoreCaseAscii(enumTypeName, "DeviceClass"))
        return valueToName(kDeviceClassNames, value, fallbackNumber);
    if (equalsIgnoreCaseAscii(enumTypeName, "ButtonEventCode"))
        return valueToName(kButtonEventCodeNames, value, fallbackNumber);
    if (equalsIgnoreCaseAscii(enumTypeName, "RockerMode"))
        return valueToName(kRockerModeNames, value, fallbackNumber);
    if (equalsIgnoreCaseAscii(enumTypeName, "SensitivityLevel"))
        return valueToName(kSensitivityLevelNames, value, fallbackNumber);
    if (equalsIgnoreCaseAscii(enumTypeName, "OperatingLevel"))
        return valueToName(kOperatingLevelNames, value, fallbackNumber);
    if (equalsIgnoreCaseAscii(enumTypeName, "PresetMode"))
        return valueToName(kPresetModeNames, value, fallbackNumber);
    if (equalsIgnoreCaseAscii(enumTypeName, "ChannelKind"))
        return valueToName(kChannelKindNames, value, fallbackNumber);
    if (equalsIgnoreCaseAscii(enumTypeName, "ChannelDataType"))
        return valueToName(kChannelDataTypeNames, value, fallbackNumber);
    if (equalsIgnoreCaseAscii(enumTypeName, "ConnectivityStatus"))
        return valueToName(kConnectivityStatusNames, value, fallbackNumber);
    if (equalsIgnoreCaseAscii(enumTypeName, "AdapterConfigFieldType"))
        return valueToName(kAdapterConfigFieldTypeNames, value, fallbackNumber);
    if (equalsIgnoreCaseAscii(enumTypeName, "AdapterConfigLabelPosition"))
        return valueToName(kAdapterConfigLabelPositionNames, value, fallbackNumber);
    if (equalsIgnoreCaseAscii(enumTypeName, "AdapterConfigActionPosition"))
        return valueToName(kAdapterConfigActionPositionNames, value, fallbackNumber);
    if (equalsIgnoreCaseAscii(enumTypeName, "AdapterConfigVisibilityOp"))
        return valueToName(kAdapterConfigVisibilityOpNames, value, fallbackNumber);
    if (equalsIgnoreCaseAscii(enumTypeName, "AdapterConfigFieldFlag"))
        return valueToName(kAdapterConfigFieldFlagNames, value, fallbackNumber);
    if (equalsIgnoreCaseAscii(enumTypeName, "AdapterRequirement"))
        return valueToName(kAdapterRequirementNames, value, fallbackNumber);
    if (equalsIgnoreCaseAscii(enumTypeName, "AdapterFlag"))
        return valueToName(kAdapterFlagNames, value, fallbackNumber);
    if (equalsIgnoreCaseAscii(enumTypeName, "DeviceFlag"))
        return valueToName(kDeviceFlagNames, value, fallbackNumber);
    if (equalsIgnoreCaseAscii(enumTypeName, "SceneFlag"))
        return valueToName(kSceneFlagNames, value, fallbackNumber);
    if (equalsIgnoreCaseAscii(enumTypeName, "ChannelFlag"))
        return valueToName(kChannelFlagNames, value, fallbackNumber);
    return fallbackNumber ? std::to_string(value) : std::string{};
}

[[nodiscard]] inline std::vector<std::string> flagNamesFor(std::string_view enumTypeName, int mask)
{
    if (equalsIgnoreCaseAscii(enumTypeName, "ChannelFlag"))
        return maskToNames(kChannelFlagNames, mask);
    if (equalsIgnoreCaseAscii(enumTypeName, "DeviceFlag"))
        return maskToNames(kDeviceFlagNames, mask);
    if (equalsIgnoreCaseAscii(enumTypeName, "SceneFlag"))
        return maskToNames(kSceneFlagNames, mask);
    if (equalsIgnoreCaseAscii(enumTypeName, "AdapterFlag"))
        return maskToNames(kAdapterFlagNames, mask);
    if (equalsIgnoreCaseAscii(enumTypeName, "AdapterConfigFieldFlag"))
        return maskToNames(kAdapterConfigFieldFlagNames, mask);
    if (equalsIgnoreCaseAscii(enumTypeName, "AdapterRequirement"))
        return maskToNames(kAdapterRequirementNames, mask);
    return {};
}

[[nodiscard]] inline bool parseEnumValueByName(
    std::string_view enumTypeName,
    std::string_view name,
    int *outValue)
{
    if (equalsIgnoreCaseAscii(enumTypeName, "CmdStatus"))
        return parseNameToValue(kCmdStatusNames, name, outValue);
    if (equalsIgnoreCaseAscii(enumTypeName, "ActionResultType"))
        return parseNameToValue(kActionResultTypeNames, name, outValue);
    if (equalsIgnoreCaseAscii(enumTypeName, "DeviceClass"))
        return parseNameToValue(kDeviceClassNames, name, outValue);
    if (equalsIgnoreCaseAscii(enumTypeName, "ButtonEventCode"))
        return parseNameToValue(kButtonEventCodeNames, name, outValue);
    if (equalsIgnoreCaseAscii(enumTypeName, "RockerMode"))
        return parseNameToValue(kRockerModeNames, name, outValue);
    if (equalsIgnoreCaseAscii(enumTypeName, "SensitivityLevel"))
        return parseNameToValue(kSensitivityLevelNames, name, outValue);
    if (equalsIgnoreCaseAscii(enumTypeName, "OperatingLevel"))
        return parseNameToValue(kOperatingLevelNames, name, outValue);
    if (equalsIgnoreCaseAscii(enumTypeName, "PresetMode"))
        return parseNameToValue(kPresetModeNames, name, outValue);
    if (equalsIgnoreCaseAscii(enumTypeName, "ChannelKind"))
        return parseNameToValue(kChannelKindNames, name, outValue);
    if (equalsIgnoreCaseAscii(enumTypeName, "ChannelDataType"))
        return parseNameToValue(kChannelDataTypeNames, name, outValue);
    if (equalsIgnoreCaseAscii(enumTypeName, "ConnectivityStatus"))
        return parseNameToValue(kConnectivityStatusNames, name, outValue);
    if (equalsIgnoreCaseAscii(enumTypeName, "AdapterConfigFieldType"))
        return parseNameToValue(kAdapterConfigFieldTypeNames, name, outValue);
    if (equalsIgnoreCaseAscii(enumTypeName, "AdapterConfigLabelPosition"))
        return parseNameToValue(kAdapterConfigLabelPositionNames, name, outValue);
    if (equalsIgnoreCaseAscii(enumTypeName, "AdapterConfigActionPosition"))
        return parseNameToValue(kAdapterConfigActionPositionNames, name, outValue);
    if (equalsIgnoreCaseAscii(enumTypeName, "AdapterConfigVisibilityOp"))
        return parseNameToValue(kAdapterConfigVisibilityOpNames, name, outValue);
    if (equalsIgnoreCaseAscii(enumTypeName, "AdapterConfigFieldFlag"))
        return parseNameToValue(kAdapterConfigFieldFlagNames, name, outValue);
    if (equalsIgnoreCaseAscii(enumTypeName, "AdapterRequirement"))
        return parseNameToValue(kAdapterRequirementNames, name, outValue);
    if (equalsIgnoreCaseAscii(enumTypeName, "AdapterFlag"))
        return parseNameToValue(kAdapterFlagNames, name, outValue);
    if (equalsIgnoreCaseAscii(enumTypeName, "DeviceFlag"))
        return parseNameToValue(kDeviceFlagNames, name, outValue);
    if (equalsIgnoreCaseAscii(enumTypeName, "SceneFlag"))
        return parseNameToValue(kSceneFlagNames, name, outValue);
    if (equalsIgnoreCaseAscii(enumTypeName, "ChannelFlag"))
        return parseNameToValue(kChannelFlagNames, name, outValue);
    return false;
}

} // namespace phicore::adapter::v1::enum_names
