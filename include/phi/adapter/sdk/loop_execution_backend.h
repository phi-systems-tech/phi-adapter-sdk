#pragma once

#include <memory>
#include <string>

#include "phi/adapter/sdk/sidecar.h"

namespace phicore::adapter::sdk {

/**
 * @brief An execution backend that runs adapter callbacks on a phi::runtime loop.
 *
 * The Qt-free counterpart to `sdk::qt::createInstanceExecutionBackend()`: one
 * named thread, one event loop, adapter callbacks one at a time in submission
 * order. Everything the Qt backend offered, without libQt6Core.
 *
 * What it adds over `DefaultInstanceExecutionBackend` is time and descriptors.
 * The default backend is a task queue and nothing else: an adapter that wants
 * to poll every five seconds has no way to ask for that, which is why adapters
 * reached for `QTimer` and dragged the whole of QtCore in behind it. Inside any
 * callback on this backend, `phi::runtime::Loop::current()` is this thread's
 * loop, so the adapter can arm a timer or watch a socket itself:
 *
 * @code
 * bool start() override {                       // runs on the backend thread
 *     m_loop = phi::runtime::Loop::current();
 *     m_shots.emplace(*m_loop);                 // the QTimer::singleShot shape
 *     m_pollTimer = m_loop->timerEvery(std::chrono::seconds(5), [this] { poll(); });
 *     return true;
 * }
 * @endcode
 *
 * Two rules the loop enforces rather than documents:
 *  - A `Loop` belongs to the thread that built it. Timers and watches must be
 *    created, and dropped, on that thread - which means in a callback, not in
 *    the instance constructor or destructor: those run on the host thread.
 *    `assertOnLoop()` aborts loudly rather than misbehaving quietly.
 *  - The loop only advances between callbacks. A callback that blocks in a
 *    socket wait holds up every timer and every posted task behind it, exactly
 *    as it did under Qt. Non-blocking I/O through `Loop::watchFd` is the way
 *    out, and the reason this backend hands out descriptors at all.
 *
 * @param threadName Kernel thread name for `ps` and `gdb`; 15 bytes survive.
 */
std::unique_ptr<InstanceExecutionBackend> createLoopExecutionBackend(std::string threadName);

} // namespace phicore::adapter::sdk
