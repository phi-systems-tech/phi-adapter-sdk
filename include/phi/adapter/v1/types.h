#pragma once

#include <cstdint>
#include <string>
#include <string_view>
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
    /// `ActionResponse::display`: a text, a code to copy, a QR payload.
    Display = 6,
    /// `ActionResponse::run`: a run that streams its progress.
    Run = 7,
    /// `ActionResponse::dataJson`: a machine-readable answer for tools, not for a person.
    Data = 8,
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

struct AdapterConfigOption {
    Utf8String value;
    Utf8String label;
};

using AdapterConfigOptionList = std::vector<AdapterConfigOption>;

/// A value per choice of another field: `{choice value, value}` pairs.
using AdapterConfigPerChoiceValues = std::vector<std::pair<Utf8String, ScalarValue>>;

/// What one form field holds: a scalar, a list (a multi-select), or one value
/// per choice of the field it names in `perChoiceOf`.
using AdapterFormValueData = std::variant<ScalarValue, ScalarList, AdapterConfigPerChoiceValues>;

struct AdapterFormValue {
    Utf8String key;
    AdapterFormValueData data;
};

using AdapterFormValues = std::vector<AdapterFormValue>;

/// The choices one select offers, sent with a form rather than declared.
struct AdapterFieldChoices {
    Utf8String key;
    AdapterConfigOptionList choices;
};

using AdapterFieldChoicesList = std::vector<AdapterFieldChoices>;

/// What a person is shown for an action's result.
struct AdapterResultDisplay {
    Utf8String text;
    /// Something to copy or type elsewhere: a pairing code, a dataset.
    Utf8String code;
    /// A payload to show as a QR code.
    Utf8String qr;
};

/// A long action that runs on and streams what it does.
struct AdapterRunHandle {
    Utf8String runId;
    Utf8String streamKind;
    ChannelValueFields streamParams;
    /// The action that stops the run, with its params.
    Utf8String abortActionId;
    ChannelValueFields abortParams;
    bool batch = false;
};

struct ActionResponse {
    CmdId id = 0;
    CmdStatus status = CmdStatus::Success;
    Utf8String error;
    ScalarList errorParams;
    Utf8String errorContext;
    ActionResultType resultType = ActionResultType::None;
    /// The result for the scalar result types.
    ScalarValue resultValue;
    /// The result for `ActionResultType::Display`.
    AdapterResultDisplay display;
    /// The result for `ActionResultType::Run`.
    AdapterRunHandle run;
    /// The result for `ActionResultType::Data`: JSON a tool reads.
    JsonText dataJson;
    AdapterFormValues formValues;
    AdapterFieldChoicesList fieldChoices;
    bool reloadLayout = false;
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
    // [0..100] percent
    Brightness = 10,
    // mired (micro reciprocal kelvin), typically [153..500]
    ColorTemperature = 11,
    // RGB color (sRGB), value stored in ChannelDataType::Color payload
    ColorRGB = 12,
    ColorTemperaturePreset = 13,
    // [0..100] percent
    Volume = 30,
    Mute = 31,
    HdmiInput = 32,
    PlayPause = 33,
    // Celsius
    Temperature = 50,
    // [0..100] percent
    Humidity = 51,
    // lux
    Illuminance = 52,
    Motion = 53,
    // [0..100] percent
    Battery = 54,
    // ppm
    CO2 = 55,
    // signed step delta (clockwise > 0, counter-clockwise < 0)
    RelativeRotation = 56,
    // enum ConnectivityStatus
    ConnectivityStatus = 57,
    // enum-style firmware/update state
    DeviceSoftwareUpdate = 58,
    // dBm
    SignalStrength = 59,
    // watt
    Power = 60,
    // volt
    Voltage = 61,
    // ampere
    Current = 62,
    // kWh
    Energy = 63,
    // [0..100] percent
    LinkQuality = 64,
    // seconds
    Duration = 65,
    Contact = 66,
    Tamper = 67,
    // enum ambient light bucket (dark/dim/bright/...)
    AmbientLightLevel = 68,
    // enum SensitivityLevel, canonical range [1..5], no unit
    MotionSensitivity = 69,
    // pH [0.00..14.00]
    PhValue = 200,
    // mV
    OrpValue = 201,
    // ppm
    SaltPpm = 202,
    // us/cm or ms/cm (adapter-specific scaling via unit/meta)
    Conductivity = 203,
    // ppm
    TdsValue = 204,
    // SG (dimensionless), typically [1.000..1.035]
    SpecificGravity = 205,
    // dH or ppm CaCO3 (adapter-specific via unit/meta)
    WaterHardness = 206,
    // ppm
    FreeChlorine = 207,
    // bar
    FilterPressure = 208,
    // L/min
    WaterFlow = 209,
    SceneTrigger = 300,
};

/**
 * @brief The field of a composite channel value that carries its scalar meaning.
 *
 * A `Json` channel's value is an object, and everything that has to reduce it
 * to one comparable thing - a history sample, an automation condition - uses
 * this field. Naming it here rather than letting each side pick means the
 * history, the rules and the adapter cannot disagree about what the channel
 * "is".
 *
 * Empty for every kind with no composite form, which is all but one of them.
 */
[[nodiscard]] constexpr std::string_view channelProjectionField(ChannelKind kind) noexcept
{
    switch (kind) {
    case ChannelKind::DeviceSoftwareUpdate:
        // status: UpToDate, UpdateAvailable, Downloading, Installing, ...
        // The versions beside it are for a person to read, not to compare.
        return "status";
    default:
        return {};
    }
}


enum class ChannelDataType : std::uint8_t {
    Unknown = 0,
    Bool = 1,
    Int = 2,
    Float = 3,
    String = 4,
    Color = 5,
    Enum = 6,
    /**
     * @brief A composite value: a flat set of named scalars.
     *
     * For the kinds whose meaning genuinely does not fit in one number - a
     * firmware update is a status *and* two versions, and reporting only one
     * of the three loses the other two. Not a general escape hatch: the fields
     * are scalars, there is no nesting, and every kind that uses this names
     * one field as its scalar meaning through channelProjectionField().
     *
     * Colour predates this and keeps its own type and its own transport.
     */
    Json = 7,
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

// Frame classes implemented by the v1 sidecar IPC. The values 1, 2, 6 and 7
// are reserved: they previously declared Hello/Heartbeat/Error/Goodbye, none of
// which were ever implemented on either side. Re-introducing a frame class
// later is an additive change; liveness currently relies on socket state plus
// core-side process supervision.
enum class MessageType : std::uint8_t {
    Request = 3,
    Response = 4,
    Event = 5,
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
    /// No control of its own: the field's `actions` as one row of buttons.
    Actions = 8,
    /// A heading that opens a group of fields; label and description only.
    Section = 9,
};

/// One size scale for everything in a form that is a width: the dialog, the
/// label column, a control. What a step measures is the client's to decide.
enum class AdapterConfigSize : std::uint8_t {
    Normal = 0,
    Narrow = 1,
    Wide = 2,
};

/// Where a field's label goes. `Auto` beside the control while both fit, above
/// it otherwise - the client decides, from the widths it knows.
enum class AdapterConfigLabelPosition : std::uint8_t {
    Auto = 0,
    Top = 1,
    None = 2,
};

/// Where a field's buttons go. `Auto` beside the control while they fit, below
/// it otherwise.
enum class AdapterConfigActionPosition : std::uint8_t {
    Auto = 0,
    Below = 1,
};

/// Where a client offers an action.
enum class AdapterActionPlacement : std::uint8_t {
    /// On the adapter's card.
    Card = 0,
    /// Only as a button of a form field that lists it in `actions`.
    Field = 1,
    /// In a device's menu; the params carry deviceId and externalId.
    Device = 2,
    /// Nowhere in the UI: for tools and tests.
    Hidden = 3,
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
