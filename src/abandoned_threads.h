#pragma once

// Threads that missed their stop deadline, kept rather than thrown away.
//
// This used to sit in sidecar.cpp next to its only user. It is a header now
// because there is a second execution backend (loop_execution_backend.cpp),
// and both hand over a worker the same way: a thread that did not come back
// in time is still executing adapter code, and there is no safe way to unwind
// a foreign call stack.

#include <chrono>
#include <cstddef>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace phicore::adapter::sdk::detail {

/**
 * @brief The parking place for workers that outlived their stop budget.
 *
 * Detaching such a thread threw the handle away along with any chance of
 * accounting for it. Keeping it costs nothing and buys two things: the process
 * can tell how many are still running, and it can still reap one that finishes
 * a moment late.
 *
 * The registry is deliberately never destroyed. A destructor running at exit
 * would have to join or abandon threads that are, by definition, stuck.
 */
class AbandonedThreadRegistry
{
public:
    static AbandonedThreadRegistry &instance()
    {
        static AbandonedThreadRegistry *registry = new AbandonedThreadRegistry();
        return *registry;
    }

    // `hasExited` must keep whatever state it inspects alive on its own; the
    // worker owns nothing the caller may destroy while this entry lives.
    void adopt(std::thread thread, std::function<bool()> hasExited)
    {
        if (!thread.joinable())
            return;
        std::lock_guard<std::mutex> lock(m_mutex);
        m_entries.push_back(Entry{std::move(thread), std::move(hasExited)});
    }

    std::size_t reap(std::chrono::milliseconds grace)
    {
        const auto deadline = std::chrono::steady_clock::now() + grace;
        for (;;) {
            const std::size_t left = reapFinished();
            if (left == 0 || std::chrono::steady_clock::now() >= deadline)
                return left;
            std::this_thread::sleep_for(kReapPollInterval);
        }
    }

private:
    struct Entry {
        std::thread thread;
        std::function<bool()> hasExited;
    };

    static constexpr auto kReapPollInterval = std::chrono::milliseconds(10);

    std::size_t reapFinished()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto it = m_entries.begin(); it != m_entries.end();) {
            // Only join once the worker has left its loop: the join then returns
            // immediately instead of blocking on the very thread we gave up on.
            if (it->hasExited && it->hasExited()) {
                it->thread.join();
                it = m_entries.erase(it);
            } else {
                ++it;
            }
        }
        return m_entries.size();
    }

    std::mutex m_mutex;
    std::vector<Entry> m_entries;
};

} // namespace phicore::adapter::sdk::detail
