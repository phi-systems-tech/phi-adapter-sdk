#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "phi/adapter/v1/types.h"

namespace phicore::adapter::v1 {

// The adapter record, the config forms and the actions an adapter announces.
// Every class here is behind a d-pointer and reached through accessors the
// shared library exports (see channel.h for the rule): the form vocabulary
// grows every few releases, and from 0.26.0 it grows without a rebuild.
// Nested parts have a const reader and a mutable one, so a field's layout is
// written in place: `field.layout().setCells(2)`. Lists have a reader, a
// setter and an `add`.

/// The instance record core sends down: what the household typed for it.
class Adapter
{
public:
    Adapter();
    Adapter(const Adapter &other);
    Adapter(Adapter &&other) noexcept;
    Adapter &operator=(const Adapter &other);
    Adapter &operator=(Adapter &&other) noexcept;
    ~Adapter();

    const Utf8String &name() const noexcept;
    Adapter &setName(Utf8String name);
    const Utf8String &host() const noexcept;
    Adapter &setHost(Utf8String host);
    const Utf8String &ip() const noexcept;
    Adapter &setIp(Utf8String ip);
    std::uint16_t port() const noexcept;
    Adapter &setPort(std::uint16_t port);
    const Utf8String &user() const noexcept;
    Adapter &setUser(Utf8String user);
    const Utf8String &password() const noexcept;
    Adapter &setPassword(Utf8String password);
    const Utf8String &token() const noexcept;
    Adapter &setToken(Utf8String token);
    const Utf8String &pluginType() const noexcept;
    Adapter &setPluginType(Utf8String pluginType);
    const ExternalId &externalId() const noexcept;
    Adapter &setExternalId(ExternalId externalId);
    const JsonText &metaJson() const noexcept;
    Adapter &setMetaJson(JsonText metaJson);
    /// The household's answers for this instance. A plugin's own capabilities
    /// are not here and never were an instance's to carry - they are in the
    /// descriptor it announces (AdapterCapabilities below).
    AdapterOptions options() const noexcept;
    Adapter &setOptions(AdapterOptions options);

private:
    struct Private;
    const Private &p() const noexcept;
    Private &m();
    std::unique_ptr<Private> d;
};

using AdapterList = std::vector<Adapter>;

// ------------------------------------------------------------- config forms
//
// A form says what it means, not how many grid units it takes. The adapter
// picks from the steps below; the client owns the numbers, the same for every
// adapter, and falls back on its own when a wish does not fit: buttons move
// below their control first, then labels move above, then columns collapse.

/// One choice of a select; the label falls back to the value on the wire.
class AdapterConfigOption
{
public:
    AdapterConfigOption();
    AdapterConfigOption(Utf8String value, Utf8String label = {});
    AdapterConfigOption(const AdapterConfigOption &other);
    AdapterConfigOption(AdapterConfigOption &&other) noexcept;
    AdapterConfigOption &operator=(const AdapterConfigOption &other);
    AdapterConfigOption &operator=(AdapterConfigOption &&other) noexcept;
    ~AdapterConfigOption();

    const Utf8String &value() const noexcept;
    AdapterConfigOption &setValue(Utf8String value);
    const Utf8String &label() const noexcept;
    AdapterConfigOption &setLabel(Utf8String label);

private:
    struct Private;
    const Private &p() const noexcept;
    Private &m();
    std::unique_ptr<Private> d;
};

using AdapterConfigOptionList = std::vector<AdapterConfigOption>;

/// A value per choice of another field: `{choice value, value}` pairs.
using AdapterConfigPerChoiceValues = std::vector<std::pair<Utf8String, ScalarValue>>;

/// What one form field holds: a scalar, a list (a multi-select), or one value
/// per choice of the field it names in `perChoiceOf`.
using AdapterFormValueData = std::variant<ScalarValue, ScalarList, AdapterConfigPerChoiceValues>;

/// One field's value, sent with a form rather than declared.
class AdapterFormValue
{
public:
    AdapterFormValue();
    AdapterFormValue(Utf8String key, AdapterFormValueData data);
    AdapterFormValue(const AdapterFormValue &other);
    AdapterFormValue(AdapterFormValue &&other) noexcept;
    AdapterFormValue &operator=(const AdapterFormValue &other);
    AdapterFormValue &operator=(AdapterFormValue &&other) noexcept;
    ~AdapterFormValue();

    const Utf8String &key() const noexcept;
    AdapterFormValue &setKey(Utf8String key);
    const AdapterFormValueData &data() const noexcept;
    AdapterFormValue &setData(AdapterFormValueData data);

private:
    struct Private;
    const Private &p() const noexcept;
    Private &m();
    std::unique_ptr<Private> d;
};

using AdapterFormValues = std::vector<AdapterFormValue>;

/// The choices one select offers, sent with a form rather than declared.
class AdapterFieldChoices
{
public:
    AdapterFieldChoices();
    AdapterFieldChoices(Utf8String key, AdapterConfigOptionList choices);
    AdapterFieldChoices(const AdapterFieldChoices &other);
    AdapterFieldChoices(AdapterFieldChoices &&other) noexcept;
    AdapterFieldChoices &operator=(const AdapterFieldChoices &other);
    AdapterFieldChoices &operator=(AdapterFieldChoices &&other) noexcept;
    ~AdapterFieldChoices();

    const Utf8String &key() const noexcept;
    AdapterFieldChoices &setKey(Utf8String key);
    const AdapterConfigOptionList &choices() const noexcept;
    AdapterFieldChoices &setChoices(AdapterConfigOptionList choices);
    AdapterFieldChoices &addChoice(Utf8String value, Utf8String label = {});

private:
    struct Private;
    const Private &p() const noexcept;
    Private &m();
    std::unique_ptr<Private> d;
};

using AdapterFieldChoicesList = std::vector<AdapterFieldChoices>;

/// Shown only while another field has (or contains) a value; none when
/// fieldKey is empty.
class AdapterConfigFieldVisibility
{
public:
    AdapterConfigFieldVisibility();
    AdapterConfigFieldVisibility(Utf8String fieldKey, ScalarValue value,
                                 AdapterConfigVisibilityOp op = AdapterConfigVisibilityOp::Equals);
    AdapterConfigFieldVisibility(const AdapterConfigFieldVisibility &other);
    AdapterConfigFieldVisibility(AdapterConfigFieldVisibility &&other) noexcept;
    AdapterConfigFieldVisibility &operator=(const AdapterConfigFieldVisibility &other);
    AdapterConfigFieldVisibility &operator=(AdapterConfigFieldVisibility &&other) noexcept;
    ~AdapterConfigFieldVisibility();

    const Utf8String &fieldKey() const noexcept;
    AdapterConfigFieldVisibility &setFieldKey(Utf8String fieldKey);
    const ScalarValue &value() const noexcept;
    AdapterConfigFieldVisibility &setValue(ScalarValue value);
    AdapterConfigVisibilityOp op() const noexcept;
    AdapterConfigFieldVisibility &setOp(AdapterConfigVisibilityOp op);

private:
    struct Private;
    const Private &p() const noexcept;
    Private &m();
    std::unique_ptr<Private> d;
};

/// The dialog a form is shown in.
class AdapterFormLayout
{
public:
    AdapterFormLayout();
    AdapterFormLayout(const AdapterFormLayout &other);
    AdapterFormLayout(AdapterFormLayout &&other) noexcept;
    AdapterFormLayout &operator=(const AdapterFormLayout &other);
    AdapterFormLayout &operator=(AdapterFormLayout &&other) noexcept;
    ~AdapterFormLayout();

    AdapterConfigSize width() const noexcept;
    AdapterFormLayout &setWidth(AdapterConfigSize width);
    /// 1 to 3 columns of cells, each cell a label, a control and its buttons.
    int columns() const noexcept;
    AdapterFormLayout &setColumns(int columns);
    AdapterConfigSize labelWidth() const noexcept;
    AdapterFormLayout &setLabelWidth(AdapterConfigSize width);

private:
    struct Private;
    const Private &p() const noexcept;
    Private &m();
    std::unique_ptr<Private> d;
};

/// Where a field sits in its form.
class AdapterConfigFieldLayout
{
public:
    AdapterConfigFieldLayout();
    AdapterConfigFieldLayout(const AdapterConfigFieldLayout &other);
    AdapterConfigFieldLayout(AdapterConfigFieldLayout &&other) noexcept;
    AdapterConfigFieldLayout &operator=(const AdapterConfigFieldLayout &other);
    AdapterConfigFieldLayout &operator=(AdapterConfigFieldLayout &&other) noexcept;
    ~AdapterConfigFieldLayout();

    /// Order within the form; fields without one keep their declaration order.
    int position() const noexcept;
    AdapterConfigFieldLayout &setPosition(int position);
    /// Cells taken, from 1 to the form's columns.
    int cells() const noexcept;
    AdapterConfigFieldLayout &setCells(int cells);
    /// Start a new row even if the current one has room.
    bool newRow() const noexcept;
    AdapterConfigFieldLayout &setNewRow(bool newRow);
    /// Narrow for a port or a timeout; Wide also takes the button column when
    /// the field has no buttons of its own.
    AdapterConfigSize controlWidth() const noexcept;
    AdapterConfigFieldLayout &setControlWidth(AdapterConfigSize width);
    AdapterConfigLabelPosition labelPosition() const noexcept;
    AdapterConfigFieldLayout &setLabelPosition(AdapterConfigLabelPosition position);
    AdapterConfigActionPosition actionPosition() const noexcept;
    AdapterConfigFieldLayout &setActionPosition(AdapterConfigActionPosition position);

private:
    struct Private;
    const Private &p() const noexcept;
    Private &m();
    std::unique_ptr<Private> d;
};

/// A button that belongs to a field; `id` is an action of the same scope.
class AdapterConfigAction
{
public:
    AdapterConfigAction();
    AdapterConfigAction(Utf8String id, Utf8String label);
    AdapterConfigAction(const AdapterConfigAction &other);
    AdapterConfigAction(AdapterConfigAction &&other) noexcept;
    AdapterConfigAction &operator=(const AdapterConfigAction &other);
    AdapterConfigAction &operator=(AdapterConfigAction &&other) noexcept;
    ~AdapterConfigAction();

    const Utf8String &id() const noexcept;
    AdapterConfigAction &setId(Utf8String id);
    const Utf8String &label() const noexcept;
    AdapterConfigAction &setLabel(Utf8String label);

private:
    struct Private;
    const Private &p() const noexcept;
    Private &m();
    std::unique_ptr<Private> d;
};

using AdapterConfigActionList = std::vector<AdapterConfigAction>;

/// One field of a form.
class AdapterConfigField
{
public:
    AdapterConfigField();
    AdapterConfigField(Utf8String key, AdapterConfigFieldType type, Utf8String label, Utf8String description = {});
    AdapterConfigField(const AdapterConfigField &other);
    AdapterConfigField(AdapterConfigField &&other) noexcept;
    AdapterConfigField &operator=(const AdapterConfigField &other);
    AdapterConfigField &operator=(AdapterConfigField &&other) noexcept;
    ~AdapterConfigField();

    const Utf8String &key() const noexcept;
    AdapterConfigField &setKey(Utf8String key);
    AdapterConfigFieldType type() const noexcept;
    AdapterConfigField &setType(AdapterConfigFieldType type);
    const Utf8String &label() const noexcept;
    AdapterConfigField &setLabel(Utf8String label);
    const Utf8String &description() const noexcept;
    AdapterConfigField &setDescription(Utf8String description);
    const Utf8String &placeholder() const noexcept;
    AdapterConfigField &setPlaceholder(Utf8String placeholder);
    const ScalarValue &defaultValue() const noexcept;
    AdapterConfigField &setDefaultValue(ScalarValue value);

    const AdapterConfigFieldVisibility &visibility() const noexcept;
    AdapterConfigFieldVisibility &visibility();
    AdapterConfigField &setVisibility(AdapterConfigFieldVisibility visibility);
    const AdapterConfigFieldLayout &layout() const noexcept;
    AdapterConfigFieldLayout &layout();
    AdapterConfigField &setLayout(AdapterConfigFieldLayout layout);
    /// The action whose form this field belongs to; empty for the section form.
    const Utf8String &parentActionId() const noexcept;
    AdapterConfigField &setParentActionId(Utf8String actionId);
    const AdapterConfigActionList &actions() const noexcept;
    AdapterConfigField &setActions(AdapterConfigActionList actions);
    AdapterConfigField &addAction(Utf8String id, Utf8String label);

    const AdapterConfigOptionList &options() const noexcept;
    AdapterConfigField &setOptions(AdapterConfigOptionList options);
    AdapterConfigField &addOption(Utf8String value, Utf8String label = {});
    /// A select offering those choices of the named multi-select that are
    /// selected there.
    const Utf8String &choicesFrom() const noexcept;
    AdapterConfigField &setChoicesFrom(Utf8String fieldKey);
    /// The field holds one value per choice of the named select and shows the
    /// one for the choice selected there.
    const Utf8String &perChoiceOf() const noexcept;
    AdapterConfigField &setPerChoiceOf(Utf8String fieldKey);
    /// Bounds and step of a number; nullopt leaves one out.
    std::optional<double> minValue() const noexcept;
    AdapterConfigField &setMinValue(std::optional<double> value);
    std::optional<double> maxValue() const noexcept;
    AdapterConfigField &setMaxValue(std::optional<double> value);
    std::optional<double> step() const noexcept;
    AdapterConfigField &setStep(std::optional<double> step);
    /// The result of this field's action is also added to the named multi-select.
    const Utf8String &appendTo() const noexcept;
    AdapterConfigField &setAppendTo(Utf8String fieldKey);
    /// A change of this field asks the adapter for the form again.
    bool reloadsForm() const noexcept;
    AdapterConfigField &setReloadsForm(bool reloads);
    AdapterConfigFieldFlags flags() const noexcept;
    AdapterConfigField &setFlags(AdapterConfigFieldFlags flags);

private:
    struct Private;
    const Private &p() const noexcept;
    Private &m();
    std::unique_ptr<Private> d;
};

using AdapterConfigFieldList = std::vector<AdapterConfigField>;

/// The factory form or the instance form.
class AdapterConfigSection
{
public:
    AdapterConfigSection();
    AdapterConfigSection(const AdapterConfigSection &other);
    AdapterConfigSection(AdapterConfigSection &&other) noexcept;
    AdapterConfigSection &operator=(const AdapterConfigSection &other);
    AdapterConfigSection &operator=(AdapterConfigSection &&other) noexcept;
    ~AdapterConfigSection();

    const Utf8String &title() const noexcept;
    AdapterConfigSection &setTitle(Utf8String title);
    const Utf8String &description() const noexcept;
    AdapterConfigSection &setDescription(Utf8String description);
    const AdapterFormLayout &layout() const noexcept;
    AdapterFormLayout &layout();
    AdapterConfigSection &setLayout(AdapterFormLayout layout);
    const AdapterConfigFieldList &fields() const noexcept;
    AdapterConfigFieldList &fields();
    AdapterConfigSection &setFields(AdapterConfigFieldList fields);
    AdapterConfigSection &addField(AdapterConfigField field);

private:
    struct Private;
    const Private &p() const noexcept;
    Private &m();
    std::unique_ptr<Private> d;
};

class AdapterConfigSchema
{
public:
    AdapterConfigSchema();
    AdapterConfigSchema(const AdapterConfigSchema &other);
    AdapterConfigSchema(AdapterConfigSchema &&other) noexcept;
    AdapterConfigSchema &operator=(const AdapterConfigSchema &other);
    AdapterConfigSchema &operator=(AdapterConfigSchema &&other) noexcept;
    ~AdapterConfigSchema();

    const AdapterConfigSection &factory() const noexcept;
    AdapterConfigSection &factory();
    AdapterConfigSchema &setFactory(AdapterConfigSection section);
    const AdapterConfigSection &instance() const noexcept;
    AdapterConfigSection &instance();
    AdapterConfigSchema &setInstance(AdapterConfigSection section);

private:
    struct Private;
    const Private &p() const noexcept;
    Private &m();
    std::unique_ptr<Private> d;
};

/// The question asked before an action runs; none when `title` is empty.
class AdapterActionConfirm
{
public:
    AdapterActionConfirm();
    AdapterActionConfirm(Utf8String title, Utf8String message, Utf8String okLabel = {}, Utf8String cancelLabel = {});
    AdapterActionConfirm(const AdapterActionConfirm &other);
    AdapterActionConfirm(AdapterActionConfirm &&other) noexcept;
    AdapterActionConfirm &operator=(const AdapterActionConfirm &other);
    AdapterActionConfirm &operator=(AdapterActionConfirm &&other) noexcept;
    ~AdapterActionConfirm();

    const Utf8String &title() const noexcept;
    AdapterActionConfirm &setTitle(Utf8String title);
    const Utf8String &message() const noexcept;
    AdapterActionConfirm &setMessage(Utf8String message);
    const Utf8String &okLabel() const noexcept;
    AdapterActionConfirm &setOkLabel(Utf8String label);
    const Utf8String &cancelLabel() const noexcept;
    AdapterActionConfirm &setCancelLabel(Utf8String label);

private:
    struct Private;
    const Private &p() const noexcept;
    Private &m();
    std::unique_ptr<Private> d;
};

/// An action the adapter offers on its card or on a device.
class AdapterActionDescriptor
{
public:
    AdapterActionDescriptor();
    AdapterActionDescriptor(Utf8String id, Utf8String label, Utf8String description = {});
    AdapterActionDescriptor(const AdapterActionDescriptor &other);
    AdapterActionDescriptor(AdapterActionDescriptor &&other) noexcept;
    AdapterActionDescriptor &operator=(const AdapterActionDescriptor &other);
    AdapterActionDescriptor &operator=(AdapterActionDescriptor &&other) noexcept;
    ~AdapterActionDescriptor();

    const Utf8String &id() const noexcept;
    AdapterActionDescriptor &setId(Utf8String id);
    const Utf8String &label() const noexcept;
    AdapterActionDescriptor &setLabel(Utf8String label);
    const Utf8String &description() const noexcept;
    AdapterActionDescriptor &setDescription(Utf8String description);
    AdapterActionPlacement placement() const noexcept;
    AdapterActionDescriptor &setPlacement(AdapterActionPlacement placement);
    bool hasForm() const noexcept;
    AdapterActionDescriptor &setHasForm(bool hasForm);
    bool danger() const noexcept;
    AdapterActionDescriptor &setDanger(bool danger);
    int cooldownMs() const noexcept;
    AdapterActionDescriptor &setCooldownMs(int cooldownMs);
    /// How long the action may take before core answers in the adapter's
    /// place; 0 means the adapter's command timeout.
    int timeoutMs() const noexcept;
    AdapterActionDescriptor &setTimeoutMs(int timeoutMs);
    /// The dialog of an action with a form.
    const AdapterFormLayout &formLayout() const noexcept;
    AdapterFormLayout &formLayout();
    AdapterActionDescriptor &setFormLayout(AdapterFormLayout layout);
    /// Ask the adapter for the form's values and choices before showing it.
    bool loadFormOnOpen() const noexcept;
    AdapterActionDescriptor &setLoadFormOnOpen(bool load);
    /// The label of the dialog's submit button; the client's own when empty.
    const Utf8String &submitLabel() const noexcept;
    AdapterActionDescriptor &setSubmitLabel(Utf8String label);
    /// The form field the action's result is written into.
    const Utf8String &resultField() const noexcept;
    AdapterActionDescriptor &setResultField(Utf8String fieldKey);
    const AdapterActionConfirm &confirm() const noexcept;
    AdapterActionConfirm &confirm();
    AdapterActionDescriptor &setConfirm(AdapterActionConfirm confirm);

private:
    struct Private;
    const Private &p() const noexcept;
    Private &m();
    std::unique_ptr<Private> d;
};

using AdapterActionDescriptorList = std::vector<AdapterActionDescriptor>;

/// What a plugin declares about itself: what it needs typed, what it can do,
/// and the actions it offers.
class AdapterCapabilities
{
public:
    AdapterCapabilities();
    AdapterCapabilities(const AdapterCapabilities &other);
    AdapterCapabilities(AdapterCapabilities &&other) noexcept;
    AdapterCapabilities &operator=(const AdapterCapabilities &other);
    AdapterCapabilities &operator=(AdapterCapabilities &&other) noexcept;
    ~AdapterCapabilities();

    AdapterRequirements required() const noexcept;
    AdapterCapabilities &setRequired(AdapterRequirements required);
    AdapterRequirements optional() const noexcept;
    AdapterCapabilities &setOptional(AdapterRequirements optional);
    AdapterCapabilityFlags flags() const noexcept;
    AdapterCapabilities &setFlags(AdapterCapabilityFlags flags);
    /// The ways the plugin can reach its devices; at least one, or the
    /// descriptor is refused (see Topology).
    Topologies topologies() const noexcept;
    AdapterCapabilities &setTopologies(Topologies topologies);
    /// What a CloudStandIn needs; None exactly when CloudStandIn is not declared.
    StandInRedirect standInRedirect() const noexcept;
    AdapterCapabilities &setStandInRedirect(StandInRedirect redirect);
    /// How devices join; None for a plugin whose devices are not paired (a
    /// router listing its clients). See PairingStyle.
    PairingStyles pairingStyles() const noexcept;
    AdapterCapabilities &setPairingStyles(PairingStyles styles);
    /// The setup codes the plugin takes; set exactly when PairingStyle::SetupCode is.
    SetupCodeSchemes setupCodeSchemes() const noexcept;
    AdapterCapabilities &setSetupCodeSchemes(SetupCodeSchemes schemes);
    const AdapterActionDescriptorList &factoryActions() const noexcept;
    AdapterActionDescriptorList &factoryActions();
    AdapterCapabilities &setFactoryActions(AdapterActionDescriptorList actions);
    AdapterCapabilities &addFactoryAction(AdapterActionDescriptor action);
    const AdapterActionDescriptorList &instanceActions() const noexcept;
    AdapterActionDescriptorList &instanceActions();
    AdapterCapabilities &setInstanceActions(AdapterActionDescriptorList actions);
    AdapterCapabilities &addInstanceAction(AdapterActionDescriptor action);

private:
    struct Private;
    const Private &p() const noexcept;
    Private &m();
    std::unique_ptr<Private> d;
};

} // namespace phicore::adapter::v1
