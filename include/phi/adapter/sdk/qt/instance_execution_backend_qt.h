#pragma once

#include <memory>

#include "phi/adapter/sdk/sidecar.h"

namespace phicore::adapter::sdk::qt {

/**
 * @brief Create a Qt event-loop based instance execution backend.
 *
 * Returned backend runs adapter instance callbacks on a dedicated `QThread`
 * and enqueues work with queued Qt invocations.
 */
std::unique_ptr<phicore::adapter::sdk::InstanceExecutionBackend> createInstanceExecutionBackend();

} // namespace phicore::adapter::sdk::qt
