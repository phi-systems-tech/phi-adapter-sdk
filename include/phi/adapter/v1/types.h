#pragma once

#include <cstdint>
#include <string>
#include <type_traits>

#include "phi/adapter/v1/value.h"

namespace phicore::adapter::v1 {

using CmdId = std::uint64_t;
using CorrelationId = std::uint64_t;
using ExternalId = Utf8String;

enum class CmdStatus : std::uint8_t {
    Success = 0,
    Failure = 1,
    Timeout = 2,
    NotSupported = 3,
    InvalidArgument = 4,
    Busy = 5,
    TemporarilyOffline = 6,
    NotAuthorized = 7,
    NotImplemented = 8,
    InternalError = 255,
};

enum class ActionResultType : std::uint8_t {
    None = 0,
    Boolean = 1,
    Integer = 2,
    Float = 3,
    String = 4,
    StringList = 5,
};

struct CmdResponse {
    CmdId id = 0;
    CmdStatus status = CmdStatus::Success;
    Utf8String error;
    ScalarList errorParams;
    Utf8String errorContext;
    ScalarValue finalValue;
    std::int64_t tsMs = 0;
};

struct ActionResponse {
    CmdId id = 0;
    CmdStatus status = CmdStatus::Success;
    Utf8String error;
    ScalarList errorParams;
    Utf8String errorContext;
    ActionResultType resultType = ActionResultType::None;
    ScalarValue resultValue;
    std::int64_t tsMs = 0;
};

enum class DeviceClass : std::uint8_t {
    Unknown = 0,
    Light = 1,
    Switch = 2,
    Sensor = 3,
    Button = 4,
    Plug = 5,
    Cover = 6,
    Thermostat = 7,
    Gateway = 8,
    MediaPlayer = 9,
    Heater = 10,
    Gate = 11,
    Valve = 12,
};

enum class DeviceEffect : std::uint16_t {
    None = 0,
    Candle,
    Fireplace,
    Sparkle,
    ColorLoop,
    Alarm,
    Relax,
    Concentrate,
    CustomVendor,
};

enum class ButtonEventCode : std::uint8_t {
    None = 0,
    InitialPress = 1,
    DoublePress = 2,
    TriplePress = 3,
    QuadruplePress = 4,
    QuintuplePress = 5,
    LongPress = 10,
    LongPressRelease = 11,
    ShortPressRelease = 12,
    Repeat = 20,
};

enum class RockerMode : std::uint8_t {
    Unknown = 0,
    SingleRocker = 1,
    DualRocker = 2,
    SinglePush = 3,
    DualPush = 4,
};

enum class SensitivityLevel : std::uint8_t {
    Unknown = 0,
    Low = 1,
    Medium = 2,
    High = 3,
    VeryHigh = 4,
    Max = 5,
};

enum class OperatingLevel : std::uint8_t {
    Unknown = 0,
    Off = 1,
    Low = 2,
    Medium = 3,
    High = 4,
    Auto = 5,
};

enum class PresetMode : std::uint8_t {
    Unknown = 0,
    Eco = 1,
    Normal = 2,
    Comfort = 3,
    Sleep = 4,
    Away = 5,
    Boost = 6,
};

enum class ChannelKind : std::uint16_t {
    Unknown = 0,
    PowerOnOff = 1,
    ButtonEvent = 2,
    Brightness = 10,
    ColorTemperature = 11,
    ColorRGB = 12,
    ColorTemperaturePreset = 13,
    Volume = 30,
    Mute = 31,
    HdmiInput = 32,
    PlayPause = 33,
    Temperature = 50,
    Humidity = 51,
    Illuminance = 52,
    Motion = 53,
    Battery = 54,
    CO2 = 55,
    RelativeRotation = 56,
    ConnectivityStatus = 57,
    DeviceSoftwareUpdate = 58,
    SignalStrength = 59,
    Power = 60,
    Voltage = 61,
    Current = 62,
    Energy = 63,
    LinkQuality = 64,
    Duration = 65,
    Contact = 66,
    Tamper = 67,
    AmbientLightLevel = 68,
    PhValue = 200,
    OrpValue = 201,
    SaltPpm = 202,
    Conductivity = 203,
    TdsValue = 204,
    SpecificGravity = 205,
    WaterHardness = 206,
    FreeChlorine = 207,
    FilterPressure = 208,
    WaterFlow = 209,
    SceneTrigger = 300,
};

enum class ChannelDataType : std::uint8_t {
    Unknown = 0,
    Bool = 1,
    Int = 2,
    Float = 3,
    String = 4,
    Color = 5,
    Enum = 6,
};

enum class ConnectivityStatus : std::uint8_t {
    Unknown = 0,
    Connected = 1,
    Limited = 2,
    Disconnected = 3,
};

enum class SceneState : std::uint8_t {
    Unknown = 0,
    Inactive = 1,
    ActiveStatic = 2,
    ActiveDynamic = 3,
};

enum class SceneAction : std::uint8_t {
    Activate = 0,
    Deactivate = 1,
    Dynamic = 2,
};

enum class DiscoveryKind : std::uint8_t {
    Mdns = 0,
    Ssdp = 1,
    NetScan = 2,
    Manual = 3,
};

enum class MessageType : std::uint8_t {
    Hello = 1,
    Heartbeat = 2,
    Request = 3,
    Response = 4,
    Event = 5,
    Error = 6,
    Goodbye = 7,
};

enum class ChannelFlag : std::uint32_t {
    None = 0x00000000,
    Readable = 0x00000001,
    Writable = 0x00000002,
    Reportable = 0x00000004,
    Retained = 0x00000008,
    Inactive = 0x00000010,
    NoTrigger = 0x00000020,
    Suppress = 0x00000040,
};

enum class DeviceFlag : std::uint32_t {
    None = 0x00000000,
    Wireless = 0x00000001,
    Battery = 0x00000002,
    Flushable = 0x00000004,
    Ble = 0x00000008,
};

enum class SceneFlag : std::uint32_t {
    None = 0x00000000,
    OriginAdapter = 0x00000001,
    SupportsDynamic = 0x00000002,
    SupportsDeactivate = 0x00000004,
};

enum class AdapterFlag : std::uint32_t {
    None = 0x00000000,
    UseTls = 0x00000001,
    CloudServices = 0x00000002,
    EnableLogs = 0x00000004,
    RequiresPolling = 0x00000008,
    SupportsDiscovery = 0x00000010,
    SupportsProbe = 0x00000020,
    SupportsRename = 0x00000040,
};

enum class AdapterConfigFieldType : std::uint8_t {
    String = 0,
    Password = 1,
    Integer = 2,
    Boolean = 3,
    Hostname = 4,
    Port = 5,
    QrCode = 6,
    Select = 7,
    Action = 8,
};

enum class AdapterConfigLabelPosition : std::uint8_t {
    Top = 0,
    Left = 1,
    Right = 2,
};

enum class AdapterConfigActionPosition : std::uint8_t {
    None = 0,
    Inline = 1,
    Below = 2,
};

enum class AdapterConfigVisibilityOp : std::uint8_t {
    Equals = 0,
    Contains = 1,
};

enum class AdapterConfigFieldFlag : std::uint8_t {
    None = 0x00,
    Required = 0x01,
    Secret = 0x02,
    ReadOnly = 0x04,
    Transient = 0x08,
    Multi = 0x10,
    InstanceOnly = 0x20,
};

enum class AdapterRequirement : std::uint32_t {
    None = 0x00000000,
    Host = 0x00000001,
    Port = 0x00000002,
    Username = 0x00000004,
    Password = 0x00000008,
    AppKey = 0x00000010,
    Token = 0x00000020,
    QrCode = 0x00000040,
    SupportsTls = 0x00000080,
    ManualConfirm = 0x00000100,
    UsesRetryInterval = 0x00000200,
};

template <typename Enum>
struct EnableBitMaskOperators : std::false_type {};

template <typename Enum>
concept BitMaskEnum = std::is_enum_v<Enum> && EnableBitMaskOperators<Enum>::value;

template <BitMaskEnum Enum>
[[nodiscard]] constexpr Enum operator|(Enum lhs, Enum rhs) noexcept
{
    using U = std::underlying_type_t<Enum>;
    return static_cast<Enum>(static_cast<U>(lhs) | static_cast<U>(rhs));
}

template <BitMaskEnum Enum>
[[nodiscard]] constexpr Enum operator&(Enum lhs, Enum rhs) noexcept
{
    using U = std::underlying_type_t<Enum>;
    return static_cast<Enum>(static_cast<U>(lhs) & static_cast<U>(rhs));
}

template <BitMaskEnum Enum>
constexpr Enum &operator|=(Enum &lhs, Enum rhs) noexcept
{
    lhs = lhs | rhs;
    return lhs;
}

template <BitMaskEnum Enum>
[[nodiscard]] constexpr bool hasFlag(Enum value, Enum flag) noexcept
{
    using U = std::underlying_type_t<Enum>;
    return (static_cast<U>(value) & static_cast<U>(flag)) == static_cast<U>(flag);
}

template <>
struct EnableBitMaskOperators<ChannelFlag> : std::true_type {};
template <>
struct EnableBitMaskOperators<DeviceFlag> : std::true_type {};
template <>
struct EnableBitMaskOperators<SceneFlag> : std::true_type {};
template <>
struct EnableBitMaskOperators<AdapterFlag> : std::true_type {};
template <>
struct EnableBitMaskOperators<AdapterConfigFieldFlag> : std::true_type {};
template <>
struct EnableBitMaskOperators<AdapterRequirement> : std::true_type {};

using ChannelFlags = ChannelFlag;
using DeviceFlags = DeviceFlag;
using SceneFlags = SceneFlag;
using AdapterFlags = AdapterFlag;
using AdapterConfigFieldFlags = AdapterConfigFieldFlag;
using AdapterRequirements = AdapterRequirement;

inline constexpr ChannelFlags kChannelFlagDefaultWrite =
    ChannelFlag::Readable | ChannelFlag::Writable | ChannelFlag::Reportable | ChannelFlag::Retained;

inline constexpr ChannelFlags kChannelFlagDefaultRead =
    ChannelFlag::Readable | ChannelFlag::Reportable | ChannelFlag::Retained;

} // namespace phicore::adapter::v1
