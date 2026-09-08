#pragma once

// What a stream of button events becomes, for every adapter alike.
//
// The contract's policy (README, "Button Event Normalization Policy"): a
// single click is exactly one ShortPressRelease, a multi-press is exactly one
// aggregated event, never both. In the moment a button is released it cannot
// be known whether a second release is on its way, so the release is held for
// kMultiPressWindowMs: if nothing follows it is reported as a single click
// when the window closes, if a second one follows they are reported as a
// double when the window closes, a third as a triple. Half a second, because
// that is the upper bound operating systems use for a double click; the 1.3
// seconds two adapters used to wait made the most common thing a button does
// the slowest thing they did.
//
// The machine has no clock of its own. It says when a window is due and the
// caller arms a timer for it and calls onWindowClosed() when it fires - which
// is what makes a double click playable in a test in no time at all, and what
// keeps this out of the adapters, where two copies had already drifted apart.
//
// Two times go in with every event: when the device says it happened, and
// when it arrived here. The window runs on arrival, because that is the clock
// the caller's timer runs on: a bridge that stamps an event and delivers it
// 300 ms later would otherwise shorten a 500 ms window to 200, and a fast
// quadruple click was measured arriving as two doubles. The report carries
// the device's time, which is when the finger was on the button.
//
// A long press that repeats within kLongPressRepeatWindowMs of the previous
// one is reported as a repeat. A device that counts for itself ("double" in
// its own vocabulary) is believed as it is.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "phi/adapter/v1/types.h"

namespace phicore::adapter::sdk {

class ButtonPresses
{
public:
    static constexpr std::int64_t kMultiPressWindowMs = 500;
    static constexpr std::int64_t kLongPressRepeatWindowMs = 800;

    struct Report {
        v1::ButtonEventCode code;
        std::int64_t tsMs;
    };

    struct Outcome {
        /// What to report now, in order.
        std::vector<Report> report;
        /// When the caller has to come back with onWindowClosed(), if at all.
        std::optional<std::int64_t> windowUntilMs;
        /// The pending window, if any, is void.
        bool cancelWindow = false;
    };

    /// `key` names one button: device and channel. `eventTsMs` is the
    /// device's time for the event, `nowMs` the caller's clock at arrival.
    Outcome onEvent(const std::string &key, v1::ButtonEventCode code, std::int64_t eventTsMs,
                    std::int64_t nowMs);

    /// The window for `key` has passed: what the held releases amount to.
    std::vector<Report> onWindowClosed(const std::string &key);

    void forget(const std::string &key) { m_keys.erase(key); }
    void clear() { m_keys.clear(); }

private:
    struct Memory {
        int lastCode = 0;
        std::int64_t lastTs = 0;
        int releases = 0;
        std::int64_t lastReleaseTs = 0;
    };
    static std::vector<Report> flush(Memory &memory);

    std::map<std::string, Memory> m_keys;
};

} // namespace phicore::adapter::sdk
