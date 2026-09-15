#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "phi/adapter/v1/types.h"

namespace phicore::adapter::v1 {

struct Adapter {
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
    AdapterFlags flags = AdapterFlag::None;
};

using AdapterList = std::vector<Adapter>;

// ------------------------------------------------------------- config forms
//
// A form says what it means, not how many grid units it takes. The adapter
// picks from the steps below; the client owns the numbers, the same for every
// adapter, and falls back on its own when a wish does not fit: buttons move
// below their control first, then labels move above, then columns collapse.

struct AdapterConfigFieldVisibility {
    Utf8String fieldKey;
    ScalarValue value;
    AdapterConfigVisibilityOp op = AdapterConfigVisibilityOp::Equals;
};

/// The dialog a form is shown in.
struct AdapterFormLayout {
    AdapterConfigSize width = AdapterConfigSize::Normal;
    /// 1 to 3 columns of cells, each cell a label, a control and its buttons.
    int columns = 1;
    AdapterConfigSize labelWidth = AdapterConfigSize::Normal;
};

struct AdapterConfigFieldLayout {
    /// Order within the form; fields without one keep their declaration order.
    int position = 0;
    /// Cells taken, from 1 to the form's columns.
    int cells = 1;
    /// Start a new row even if the current one has room.
    bool newRow = false;
    /// Narrow for a port or a timeout; Wide also takes the button column when
    /// the field has no buttons of its own.
    AdapterConfigSize controlWidth = AdapterConfigSize::Normal;
    AdapterConfigLabelPosition labelPosition = AdapterConfigLabelPosition::Auto;
    AdapterConfigActionPosition actionPosition = AdapterConfigActionPosition::Auto;
};

/// A button that belongs to a field; `id` is an action of the same scope.
struct AdapterConfigAction {
    Utf8String id;
    Utf8String label;
};

using AdapterConfigActionList = std::vector<AdapterConfigAction>;

struct AdapterConfigField {
    Utf8String key;
    AdapterConfigFieldType type = AdapterConfigFieldType::String;

    Utf8String label;
    Utf8String description;

    Utf8String placeholder;
    ScalarValue defaultValue;

    AdapterConfigFieldVisibility visibility;
    AdapterConfigFieldLayout layout;
    /// The action whose form this field belongs to; empty for the section form.
    Utf8String parentActionId;
    AdapterConfigActionList actions;

    AdapterConfigOptionList options;
    /// A select offering those choices of the named multi-select that are
    /// selected there.
    Utf8String choicesFrom;
    /// The field holds one value per choice of the named select and shows the
    /// one for the choice selected there.
    Utf8String perChoiceOf;
    /// Bounds and step of a number.
    std::optional<double> minValue;
    std::optional<double> maxValue;
    std::optional<double> step;
    /// The result of this field's action is also added to the named multi-select.
    Utf8String appendTo;
    /// A change of this field asks the adapter for the form again.
    bool reloadsForm = false;
    AdapterConfigFieldFlags flags = AdapterConfigFieldFlag::None;
};

using AdapterConfigFieldList = std::vector<AdapterConfigField>;

struct AdapterConfigSection {
    Utf8String title;
    Utf8String description;
    AdapterFormLayout layout;
    AdapterConfigFieldList fields;
};

struct AdapterConfigSchema {
    AdapterConfigSection factory;
    AdapterConfigSection instance;
};

/// The question asked before an action runs; none when `title` is empty.
struct AdapterActionConfirm {
    Utf8String title;
    Utf8String message;
    Utf8String okLabel;
    Utf8String cancelLabel;
};

struct AdapterActionDescriptor {
    Utf8String id;
    Utf8String label;
    Utf8String description;
    AdapterActionPlacement placement = AdapterActionPlacement::Card;
    AdapterActionKind kind = AdapterActionKind::Command;
    bool requiresAck = true;
    bool hasForm = false;
    bool danger = false;
    int cooldownMs = 0;
    /// How long the action may take before core answers in the adapter's
    /// place; 0 means the adapter's command timeout.
    int timeoutMs = 0;
    /// The dialog of an action with a form.
    AdapterFormLayout formLayout;
    /// Ask the adapter for the form's values and choices before showing it.
    bool loadFormOnOpen = false;
    /// The label of the dialog's submit button; the client's own when empty.
    Utf8String submitLabel;
    /// The form field the action's result is written into.
    Utf8String resultField;
    AdapterActionConfirm confirm;
};

using AdapterActionDescriptorList = std::vector<AdapterActionDescriptor>;

struct AdapterCapabilities {
    AdapterRequirements required = AdapterRequirement::None;
    AdapterRequirements optional = AdapterRequirement::None;
    AdapterFlags flags = AdapterFlag::None;
    AdapterActionDescriptorList factoryActions;
    AdapterActionDescriptorList instanceActions;
};

} // namespace phicore::adapter::v1
