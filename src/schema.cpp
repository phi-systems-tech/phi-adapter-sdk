// The adapter record, the form and action vocabulary and the answers, each
// behind its d-pointer. Same rules as entities.cpp: deep copies, a moved-from
// object reads as empty and takes a new Private on its first setter, every
// field an exported accessor.

#include "phi/adapter/v1/response.h"
#include "phi/adapter/v1/schema.h"
#include "phi/adapter/v1/tlsconfig.h"

#include <type_traits>
#include <utility>

namespace phicore::adapter::v1 {

#define PHI_V1_ENTITY_MEMBERS(Class)                                                                    \
    Class::Class() : d(std::make_unique<Private>()) {}                                                  \
    Class::Class(const Class &other) : d(other.d ? std::make_unique<Private>(*other.d) : nullptr) {}    \
    Class::Class(Class &&other) noexcept : d(std::move(other.d)) {}                                     \
    Class &Class::operator=(const Class &other)                                                         \
    {                                                                                                   \
        if (this != &other)                                                                             \
            d = other.d ? std::make_unique<Private>(*other.d) : nullptr;                                \
        return *this;                                                                                   \
    }                                                                                                   \
    Class &Class::operator=(Class &&other) noexcept                                                     \
    {                                                                                                   \
        d = std::move(other.d);                                                                         \
        return *this;                                                                                   \
    }                                                                                                   \
    Class::~Class() = default;                                                                          \
    const Class::Private &Class::p() const noexcept                                                     \
    {                                                                                                   \
        static const Private empty;                                                                     \
        return d ? *d : empty;                                                                          \
    }                                                                                                   \
    Class::Private &Class::m()                                                                          \
    {                                                                                                   \
        if (!d)                                                                                         \
            d = std::make_unique<Private>();                                                            \
        return *d;                                                                                      \
    }

#define PHI_V1_FIELD(Class, Type, name, Setter)                                                         \
    Type Class::name() const noexcept { return p().name; }                                              \
    Class &Class::Setter(std::remove_cvref_t<Type> value)                                               \
    {                                                                                                   \
        m().name = std::move(value);                                                                    \
        return *this;                                                                                   \
    }

// A nested part: a const reader, a mutable reader and a setter.
#define PHI_V1_PART(Class, Type, name, Setter)                                                          \
    const Type &Class::name() const noexcept { return p().name; }                                       \
    Type &Class::name() { return m().name; }                                                            \
    Class &Class::Setter(Type value)                                                                    \
    {                                                                                                   \
        m().name = std::move(value);                                                                    \
        return *this;                                                                                   \
    }

// --- Adapter -----------------------------------------------------------------

struct Adapter::Private {
    Utf8String name;
    Utf8String host;
    Utf8String ip;
    std::uint16_t port = 0;
    Utf8String user;
    Utf8String password;
    Utf8String token;
    Utf8String pluginType;
    ExternalId externalId;
    JsonText metaJson;
    AdapterOptions options = AdapterOption::None;
};

PHI_V1_ENTITY_MEMBERS(Adapter)
PHI_V1_FIELD(Adapter, const Utf8String &, name, setName)
PHI_V1_FIELD(Adapter, const Utf8String &, host, setHost)
PHI_V1_FIELD(Adapter, const Utf8String &, ip, setIp)
PHI_V1_FIELD(Adapter, std::uint16_t, port, setPort)
PHI_V1_FIELD(Adapter, const Utf8String &, user, setUser)
PHI_V1_FIELD(Adapter, const Utf8String &, password, setPassword)
PHI_V1_FIELD(Adapter, const Utf8String &, token, setToken)
PHI_V1_FIELD(Adapter, const Utf8String &, pluginType, setPluginType)
PHI_V1_FIELD(Adapter, const ExternalId &, externalId, setExternalId)
PHI_V1_FIELD(Adapter, const JsonText &, metaJson, setMetaJson)
PHI_V1_FIELD(Adapter, AdapterOptions, options, setOptions)

// --- AdapterConfigOption -----------------------------------------------------

struct AdapterConfigOption::Private {
    Utf8String value;
    Utf8String label;
};

PHI_V1_ENTITY_MEMBERS(AdapterConfigOption)
PHI_V1_FIELD(AdapterConfigOption, const Utf8String &, value, setValue)
PHI_V1_FIELD(AdapterConfigOption, const Utf8String &, label, setLabel)

AdapterConfigOption::AdapterConfigOption(Utf8String value, Utf8String label) : d(std::make_unique<Private>())
{
    d->value = std::move(value);
    d->label = std::move(label);
}

// --- AdapterFormValue --------------------------------------------------------

struct AdapterFormValue::Private {
    Utf8String key;
    AdapterFormValueData data;
};

PHI_V1_ENTITY_MEMBERS(AdapterFormValue)
PHI_V1_FIELD(AdapterFormValue, const Utf8String &, key, setKey)
PHI_V1_FIELD(AdapterFormValue, const AdapterFormValueData &, data, setData)

AdapterFormValue::AdapterFormValue(Utf8String key, AdapterFormValueData data) : d(std::make_unique<Private>())
{
    d->key = std::move(key);
    d->data = std::move(data);
}

// --- AdapterFieldChoices -----------------------------------------------------

struct AdapterFieldChoices::Private {
    Utf8String key;
    AdapterConfigOptionList choices;
};

PHI_V1_ENTITY_MEMBERS(AdapterFieldChoices)
PHI_V1_FIELD(AdapterFieldChoices, const Utf8String &, key, setKey)
PHI_V1_FIELD(AdapterFieldChoices, const AdapterConfigOptionList &, choices, setChoices)

AdapterFieldChoices::AdapterFieldChoices(Utf8String key, AdapterConfigOptionList choices) : d(std::make_unique<Private>())
{
    d->key = std::move(key);
    d->choices = std::move(choices);
}

AdapterFieldChoices &AdapterFieldChoices::addChoice(Utf8String value, Utf8String label)
{
    m().choices.emplace_back(std::move(value), std::move(label));
    return *this;
}

// --- AdapterConfigFieldVisibility --------------------------------------------

struct AdapterConfigFieldVisibility::Private {
    Utf8String fieldKey;
    ScalarValue value;
    AdapterConfigVisibilityOp op = AdapterConfigVisibilityOp::Equals;
};

PHI_V1_ENTITY_MEMBERS(AdapterConfigFieldVisibility)
PHI_V1_FIELD(AdapterConfigFieldVisibility, const Utf8String &, fieldKey, setFieldKey)
PHI_V1_FIELD(AdapterConfigFieldVisibility, const ScalarValue &, value, setValue)
PHI_V1_FIELD(AdapterConfigFieldVisibility, AdapterConfigVisibilityOp, op, setOp)

AdapterConfigFieldVisibility::AdapterConfigFieldVisibility(Utf8String fieldKey, ScalarValue value,
                                                           AdapterConfigVisibilityOp op)
    : d(std::make_unique<Private>())
{
    d->fieldKey = std::move(fieldKey);
    d->value = std::move(value);
    d->op = op;
}

// --- AdapterFormLayout -------------------------------------------------------

struct AdapterFormLayout::Private {
    AdapterConfigSize width = AdapterConfigSize::Normal;
    int columns = 1;
    AdapterConfigSize labelWidth = AdapterConfigSize::Normal;
};

PHI_V1_ENTITY_MEMBERS(AdapterFormLayout)
PHI_V1_FIELD(AdapterFormLayout, AdapterConfigSize, width, setWidth)
PHI_V1_FIELD(AdapterFormLayout, int, columns, setColumns)
PHI_V1_FIELD(AdapterFormLayout, AdapterConfigSize, labelWidth, setLabelWidth)

// --- AdapterConfigFieldLayout ------------------------------------------------

struct AdapterConfigFieldLayout::Private {
    int position = 0;
    int cells = 1;
    bool newRow = false;
    AdapterConfigSize controlWidth = AdapterConfigSize::Normal;
    AdapterConfigLabelPosition labelPosition = AdapterConfigLabelPosition::Auto;
    AdapterConfigActionPosition actionPosition = AdapterConfigActionPosition::Auto;
};

PHI_V1_ENTITY_MEMBERS(AdapterConfigFieldLayout)
PHI_V1_FIELD(AdapterConfigFieldLayout, int, position, setPosition)
PHI_V1_FIELD(AdapterConfigFieldLayout, int, cells, setCells)
PHI_V1_FIELD(AdapterConfigFieldLayout, bool, newRow, setNewRow)
PHI_V1_FIELD(AdapterConfigFieldLayout, AdapterConfigSize, controlWidth, setControlWidth)
PHI_V1_FIELD(AdapterConfigFieldLayout, AdapterConfigLabelPosition, labelPosition, setLabelPosition)
PHI_V1_FIELD(AdapterConfigFieldLayout, AdapterConfigActionPosition, actionPosition, setActionPosition)

// --- AdapterConfigAction -----------------------------------------------------

struct AdapterConfigAction::Private {
    Utf8String id;
    Utf8String label;
};

PHI_V1_ENTITY_MEMBERS(AdapterConfigAction)
PHI_V1_FIELD(AdapterConfigAction, const Utf8String &, id, setId)
PHI_V1_FIELD(AdapterConfigAction, const Utf8String &, label, setLabel)

AdapterConfigAction::AdapterConfigAction(Utf8String id, Utf8String label) : d(std::make_unique<Private>())
{
    d->id = std::move(id);
    d->label = std::move(label);
}

// --- AdapterConfigField ------------------------------------------------------

struct AdapterConfigField::Private {
    Utf8String key;
    AdapterConfigFieldType type = AdapterConfigFieldType::String;
    Utf8String label;
    Utf8String description;
    Utf8String placeholder;
    ScalarValue defaultValue;
    AdapterConfigFieldVisibility visibility;
    AdapterConfigFieldLayout layout;
    Utf8String parentActionId;
    AdapterConfigActionList actions;
    AdapterConfigOptionList options;
    Utf8String choicesFrom;
    Utf8String perChoiceOf;
    std::optional<double> minValue;
    std::optional<double> maxValue;
    std::optional<double> step;
    Utf8String appendTo;
    bool reloadsForm = false;
    AdapterConfigFieldFlags flags = AdapterConfigFieldFlag::None;
};

PHI_V1_ENTITY_MEMBERS(AdapterConfigField)
PHI_V1_FIELD(AdapterConfigField, const Utf8String &, key, setKey)
PHI_V1_FIELD(AdapterConfigField, AdapterConfigFieldType, type, setType)
PHI_V1_FIELD(AdapterConfigField, const Utf8String &, label, setLabel)
PHI_V1_FIELD(AdapterConfigField, const Utf8String &, description, setDescription)
PHI_V1_FIELD(AdapterConfigField, const Utf8String &, placeholder, setPlaceholder)
PHI_V1_FIELD(AdapterConfigField, const ScalarValue &, defaultValue, setDefaultValue)
PHI_V1_PART(AdapterConfigField, AdapterConfigFieldVisibility, visibility, setVisibility)
PHI_V1_PART(AdapterConfigField, AdapterConfigFieldLayout, layout, setLayout)
PHI_V1_FIELD(AdapterConfigField, const Utf8String &, parentActionId, setParentActionId)
PHI_V1_FIELD(AdapterConfigField, const AdapterConfigActionList &, actions, setActions)
PHI_V1_FIELD(AdapterConfigField, const AdapterConfigOptionList &, options, setOptions)
PHI_V1_FIELD(AdapterConfigField, const Utf8String &, choicesFrom, setChoicesFrom)
PHI_V1_FIELD(AdapterConfigField, const Utf8String &, perChoiceOf, setPerChoiceOf)
PHI_V1_FIELD(AdapterConfigField, std::optional<double>, minValue, setMinValue)
PHI_V1_FIELD(AdapterConfigField, std::optional<double>, maxValue, setMaxValue)
PHI_V1_FIELD(AdapterConfigField, std::optional<double>, step, setStep)
PHI_V1_FIELD(AdapterConfigField, const Utf8String &, appendTo, setAppendTo)
PHI_V1_FIELD(AdapterConfigField, bool, reloadsForm, setReloadsForm)
PHI_V1_FIELD(AdapterConfigField, AdapterConfigFieldFlags, flags, setFlags)

AdapterConfigField::AdapterConfigField(Utf8String key, AdapterConfigFieldType type, Utf8String label)
    : d(std::make_unique<Private>())
{
    d->key = std::move(key);
    d->type = type;
    d->label = std::move(label);
}

AdapterConfigField &AdapterConfigField::addAction(Utf8String id, Utf8String label)
{
    m().actions.emplace_back(std::move(id), std::move(label));
    return *this;
}

AdapterConfigField &AdapterConfigField::addOption(Utf8String value, Utf8String label)
{
    m().options.emplace_back(std::move(value), std::move(label));
    return *this;
}

// --- AdapterConfigSection ----------------------------------------------------

struct AdapterConfigSection::Private {
    Utf8String title;
    Utf8String description;
    AdapterFormLayout layout;
    AdapterConfigFieldList fields;
};

PHI_V1_ENTITY_MEMBERS(AdapterConfigSection)
PHI_V1_FIELD(AdapterConfigSection, const Utf8String &, title, setTitle)
PHI_V1_FIELD(AdapterConfigSection, const Utf8String &, description, setDescription)
PHI_V1_PART(AdapterConfigSection, AdapterFormLayout, layout, setLayout)
PHI_V1_PART(AdapterConfigSection, AdapterConfigFieldList, fields, setFields)

AdapterConfigSection &AdapterConfigSection::addField(AdapterConfigField field)
{
    m().fields.push_back(std::move(field));
    return *this;
}

// --- AdapterConfigSchema -----------------------------------------------------

struct AdapterConfigSchema::Private {
    AdapterConfigSection factory;
    AdapterConfigSection instance;
};

PHI_V1_ENTITY_MEMBERS(AdapterConfigSchema)
PHI_V1_PART(AdapterConfigSchema, AdapterConfigSection, factory, setFactory)
PHI_V1_PART(AdapterConfigSchema, AdapterConfigSection, instance, setInstance)

// --- AdapterActionConfirm ----------------------------------------------------

struct AdapterActionConfirm::Private {
    Utf8String title;
    Utf8String message;
    Utf8String okLabel;
    Utf8String cancelLabel;
};

PHI_V1_ENTITY_MEMBERS(AdapterActionConfirm)
PHI_V1_FIELD(AdapterActionConfirm, const Utf8String &, title, setTitle)
PHI_V1_FIELD(AdapterActionConfirm, const Utf8String &, message, setMessage)
PHI_V1_FIELD(AdapterActionConfirm, const Utf8String &, okLabel, setOkLabel)
PHI_V1_FIELD(AdapterActionConfirm, const Utf8String &, cancelLabel, setCancelLabel)

AdapterActionConfirm::AdapterActionConfirm(Utf8String title, Utf8String message, Utf8String okLabel,
                                           Utf8String cancelLabel)
    : d(std::make_unique<Private>())
{
    d->title = std::move(title);
    d->message = std::move(message);
    d->okLabel = std::move(okLabel);
    d->cancelLabel = std::move(cancelLabel);
}

// --- AdapterActionDescriptor -------------------------------------------------

struct AdapterActionDescriptor::Private {
    Utf8String id;
    Utf8String label;
    Utf8String description;
    AdapterActionPlacement placement = AdapterActionPlacement::Card;
    bool hasForm = false;
    bool danger = false;
    int cooldownMs = 0;
    int timeoutMs = 0;
    AdapterFormLayout formLayout;
    bool loadFormOnOpen = false;
    Utf8String submitLabel;
    Utf8String resultField;
    AdapterActionConfirm confirm;
};

PHI_V1_ENTITY_MEMBERS(AdapterActionDescriptor)
PHI_V1_FIELD(AdapterActionDescriptor, const Utf8String &, id, setId)
PHI_V1_FIELD(AdapterActionDescriptor, const Utf8String &, label, setLabel)
PHI_V1_FIELD(AdapterActionDescriptor, const Utf8String &, description, setDescription)
PHI_V1_FIELD(AdapterActionDescriptor, AdapterActionPlacement, placement, setPlacement)
PHI_V1_FIELD(AdapterActionDescriptor, bool, hasForm, setHasForm)
PHI_V1_FIELD(AdapterActionDescriptor, bool, danger, setDanger)
PHI_V1_FIELD(AdapterActionDescriptor, int, cooldownMs, setCooldownMs)
PHI_V1_FIELD(AdapterActionDescriptor, int, timeoutMs, setTimeoutMs)
PHI_V1_PART(AdapterActionDescriptor, AdapterFormLayout, formLayout, setFormLayout)
PHI_V1_FIELD(AdapterActionDescriptor, bool, loadFormOnOpen, setLoadFormOnOpen)
PHI_V1_FIELD(AdapterActionDescriptor, const Utf8String &, submitLabel, setSubmitLabel)
PHI_V1_FIELD(AdapterActionDescriptor, const Utf8String &, resultField, setResultField)
PHI_V1_PART(AdapterActionDescriptor, AdapterActionConfirm, confirm, setConfirm)

AdapterActionDescriptor::AdapterActionDescriptor(Utf8String id, Utf8String label, Utf8String description)
    : d(std::make_unique<Private>())
{
    d->id = std::move(id);
    d->label = std::move(label);
    d->description = std::move(description);
}

// --- AdapterCapabilities -----------------------------------------------------

struct AdapterCapabilities::Private {
    AdapterRequirements required = AdapterRequirement::None;
    AdapterRequirements optional = AdapterRequirement::None;
    AdapterCapabilityFlags flags = AdapterCapability::None;
    AdapterActionDescriptorList factoryActions;
    AdapterActionDescriptorList instanceActions;
};

PHI_V1_ENTITY_MEMBERS(AdapterCapabilities)
PHI_V1_FIELD(AdapterCapabilities, AdapterRequirements, required, setRequired)
PHI_V1_FIELD(AdapterCapabilities, AdapterRequirements, optional, setOptional)
PHI_V1_FIELD(AdapterCapabilities, AdapterCapabilityFlags, flags, setFlags)
PHI_V1_PART(AdapterCapabilities, AdapterActionDescriptorList, factoryActions, setFactoryActions)
PHI_V1_PART(AdapterCapabilities, AdapterActionDescriptorList, instanceActions, setInstanceActions)

AdapterCapabilities &AdapterCapabilities::addFactoryAction(AdapterActionDescriptor action)
{
    m().factoryActions.push_back(std::move(action));
    return *this;
}

AdapterCapabilities &AdapterCapabilities::addInstanceAction(AdapterActionDescriptor action)
{
    m().instanceActions.push_back(std::move(action));
    return *this;
}

// --- CmdResponse -------------------------------------------------------------

struct CmdResponse::Private {
    CmdId id = 0;
    CmdStatus status = CmdStatus::Success;
    Utf8String error;
    ScalarList errorParams;
    Utf8String errorContext;
    ScalarValue finalValue;
    std::int64_t tsMs = 0;
};

PHI_V1_ENTITY_MEMBERS(CmdResponse)
PHI_V1_FIELD(CmdResponse, CmdId, id, setId)
PHI_V1_FIELD(CmdResponse, CmdStatus, status, setStatus)
PHI_V1_FIELD(CmdResponse, const Utf8String &, error, setError)
PHI_V1_FIELD(CmdResponse, const ScalarList &, errorParams, setErrorParams)
PHI_V1_FIELD(CmdResponse, const Utf8String &, errorContext, setErrorContext)
PHI_V1_FIELD(CmdResponse, const ScalarValue &, finalValue, setFinalValue)
PHI_V1_FIELD(CmdResponse, std::int64_t, tsMs, setTsMs)

CmdResponse CmdResponse::success(CmdId id, ScalarValue finalValue)
{
    CmdResponse out;
    out.d->id = id;
    out.d->finalValue = std::move(finalValue);
    return out;
}

CmdResponse CmdResponse::failure(CmdId id, CmdStatus status, Utf8String error, ScalarList errorParams,
                                 Utf8String errorContext)
{
    CmdResponse out;
    out.d->id = id;
    out.d->status = status;
    out.d->error = std::move(error);
    out.d->errorParams = std::move(errorParams);
    out.d->errorContext = std::move(errorContext);
    return out;
}

// --- AdapterResultDisplay ----------------------------------------------------

struct AdapterResultDisplay::Private {
    Utf8String text;
    Utf8String code;
    Utf8String qr;
};

PHI_V1_ENTITY_MEMBERS(AdapterResultDisplay)
PHI_V1_FIELD(AdapterResultDisplay, const Utf8String &, text, setText)
PHI_V1_FIELD(AdapterResultDisplay, const Utf8String &, code, setCode)
PHI_V1_FIELD(AdapterResultDisplay, const Utf8String &, qr, setQr)

AdapterResultDisplay::AdapterResultDisplay(Utf8String text, Utf8String code, Utf8String qr)
    : d(std::make_unique<Private>())
{
    d->text = std::move(text);
    d->code = std::move(code);
    d->qr = std::move(qr);
}

// --- AdapterRunHandle --------------------------------------------------------

struct AdapterRunHandle::Private {
    Utf8String runId;
    Utf8String streamKind;
    ChannelValueFields streamParams;
    Utf8String abortActionId;
    ChannelValueFields abortParams;
    bool batch = false;
};

PHI_V1_ENTITY_MEMBERS(AdapterRunHandle)
PHI_V1_FIELD(AdapterRunHandle, const Utf8String &, runId, setRunId)
PHI_V1_FIELD(AdapterRunHandle, const Utf8String &, streamKind, setStreamKind)
PHI_V1_FIELD(AdapterRunHandle, const ChannelValueFields &, streamParams, setStreamParams)
PHI_V1_FIELD(AdapterRunHandle, const Utf8String &, abortActionId, setAbortActionId)
PHI_V1_FIELD(AdapterRunHandle, const ChannelValueFields &, abortParams, setAbortParams)
PHI_V1_FIELD(AdapterRunHandle, bool, batch, setBatch)

// --- ActionResponse ----------------------------------------------------------

struct ActionResponse::Private {
    CmdId id = 0;
    CmdStatus status = CmdStatus::Success;
    Utf8String error;
    ScalarList errorParams;
    Utf8String errorContext;
    ActionResultType resultType = ActionResultType::None;
    ScalarValue resultValue;
    AdapterResultDisplay display;
    AdapterRunHandle run;
    JsonText dataJson;
    AdapterFormValues formValues;
    AdapterFieldChoicesList fieldChoices;
    bool reloadLayout = false;
    std::int64_t tsMs = 0;
};

PHI_V1_ENTITY_MEMBERS(ActionResponse)
PHI_V1_FIELD(ActionResponse, CmdId, id, setId)
PHI_V1_FIELD(ActionResponse, CmdStatus, status, setStatus)
PHI_V1_FIELD(ActionResponse, const Utf8String &, error, setError)
PHI_V1_FIELD(ActionResponse, const ScalarList &, errorParams, setErrorParams)
PHI_V1_FIELD(ActionResponse, const Utf8String &, errorContext, setErrorContext)
PHI_V1_FIELD(ActionResponse, ActionResultType, resultType, setResultType)
PHI_V1_FIELD(ActionResponse, const ScalarValue &, resultValue, setResultValue)
PHI_V1_FIELD(ActionResponse, const AdapterFormValues &, formValues, setFormValues)
PHI_V1_FIELD(ActionResponse, const AdapterFieldChoicesList &, fieldChoices, setFieldChoices)
PHI_V1_FIELD(ActionResponse, bool, reloadLayout, setReloadLayout)
PHI_V1_FIELD(ActionResponse, std::int64_t, tsMs, setTsMs)

const AdapterResultDisplay &ActionResponse::display() const noexcept { return p().display; }
const AdapterRunHandle &ActionResponse::run() const noexcept { return p().run; }
const JsonText &ActionResponse::dataJson() const noexcept { return p().dataJson; }

ActionResponse ActionResponse::success(CmdId id)
{
    ActionResponse out;
    out.d->id = id;
    return out;
}

ActionResponse ActionResponse::failure(CmdId id, CmdStatus status, Utf8String error, ScalarList errorParams,
                                       Utf8String errorContext)
{
    ActionResponse out;
    out.d->id = id;
    out.d->status = status;
    out.d->error = std::move(error);
    out.d->errorParams = std::move(errorParams);
    out.d->errorContext = std::move(errorContext);
    return out;
}

ActionResponse &ActionResponse::setResult(ScalarValue value, ActionResultType type)
{
    Private &priv = m();
    priv.resultValue = std::move(value);
    priv.resultType = type;
    return *this;
}

ActionResponse &ActionResponse::setDisplay(AdapterResultDisplay display)
{
    Private &priv = m();
    priv.display = std::move(display);
    priv.resultType = ActionResultType::Display;
    return *this;
}

ActionResponse &ActionResponse::setRun(AdapterRunHandle run)
{
    Private &priv = m();
    priv.run = std::move(run);
    priv.resultType = ActionResultType::Run;
    return *this;
}

ActionResponse &ActionResponse::setData(JsonText dataJson)
{
    Private &priv = m();
    priv.dataJson = std::move(dataJson);
    priv.resultType = ActionResultType::Data;
    return *this;
}

ActionResponse &ActionResponse::addFormValue(Utf8String key, AdapterFormValueData data)
{
    m().formValues.emplace_back(std::move(key), std::move(data));
    return *this;
}

ActionResponse &ActionResponse::addFieldChoices(Utf8String key, AdapterConfigOptionList choices)
{
    m().fieldChoices.emplace_back(std::move(key), std::move(choices));
    return *this;
}

// --- TlsSettings -------------------------------------------------------------

struct TlsSettings::Private {
    bool enabled = false;
    Utf8String caFile;
    bool verifyHostname = true;
};

PHI_V1_ENTITY_MEMBERS(TlsSettings)
PHI_V1_FIELD(TlsSettings, bool, enabled, setEnabled)
PHI_V1_FIELD(TlsSettings, const Utf8String &, caFile, setCaFile)
PHI_V1_FIELD(TlsSettings, bool, verifyHostname, setVerifyHostname)

#undef PHI_V1_PART
#undef PHI_V1_FIELD
#undef PHI_V1_ENTITY_MEMBERS

} // namespace phicore::adapter::v1
