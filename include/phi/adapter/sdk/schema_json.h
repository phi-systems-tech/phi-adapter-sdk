#pragma once

#include "phi/adapter/v1/contract.h"

namespace phicore::adapter::sdk {

/**
 * @brief The wire JSON of a config schema, the one spelling core reads.
 *
 * Adapters declare their forms as `AdapterConfigSchema` and never write this
 * text themselves; the SDK sends it in the factory descriptor. Exposed for
 * tests and tools that want to see what goes out.
 */
[[nodiscard]] phicore::adapter::v1::JsonText configSchemaToJson(
    const phicore::adapter::v1::AdapterConfigSchema &schema);

/// The `formValues` object of an action result: `{key: scalar | list | {choice: scalar}}`.
[[nodiscard]] phicore::adapter::v1::JsonText formValuesToJson(
    const phicore::adapter::v1::AdapterFormValues &values);

/// The `fieldChoices` object of an action result: `{key: [{value, label}]}`.
[[nodiscard]] phicore::adapter::v1::JsonText fieldChoicesToJson(
    const phicore::adapter::v1::AdapterFieldChoicesList &choices);

} // namespace phicore::adapter::sdk
