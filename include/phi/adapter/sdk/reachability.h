#pragma once

// What every adapter does when a device stops answering, in one place.
//
// The shape was the same in all of them and the numbers were not: count the
// failures in a row, call the thing gone after the third, say so once, keep
// asking - and there the copies drifted. Two adapters re-logged the same line
// every ten seconds for as long as a receiver was in standby and said nothing
// at all when it came back. One kept asking every five seconds forever. The
// timing of that is the whole difference between a device that is off and a
// log nobody reads.
//
// This holds the counting, the growing wait and the rule about when a thing is
// worth saying. It has no clock and no timer, in the manner of ButtonPresses:
// the caller passes the time it already has and arms the timer it already
// owns. What counts as an answer stays with the adapter, because only it knows
// - a decoded frame, four eISCP replies, an HTTP status, a message on a topic.
//
// One of these per thing that can go away on its own: a bridge, a receiver, a
// device behind a gateway. Not one per adapter.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace phicore::adapter::sdk {

class Reachability
{
public:
    struct Policy {
        /// How long between attempts while the thing answers.
        std::int64_t intervalMs = 5000;
        /// Failures in a row before it counts as gone. Below that a miss is a
        /// blip: it is not reported and not logged, only waited out.
        int strikes = 3;
        /// The wait after the first, second, third ... failure; the last entry
        /// repeats for as long as the silence lasts. Empty means the interval
        /// above, unchanged - a caller that wants no backoff says so here.
        std::vector<std::int64_t> retryDelaysMs{10000};
        /// Say the same thing again after this long. Zero says it once, which
        /// is what a log that is read wants; a few minutes suits a line that
        /// has to prove the adapter is still trying.
        std::int64_t sayAgainAfterMs = 0;
    };

    enum class State { Up, Down };

    /// What the caller does about the attempt it just made: wait this long
    /// before the next one, and say this much about it.
    struct Verdict {
        std::int64_t waitMs = 0;
        /// Up became Down or Down became Up, with this attempt. The moment to
        /// report connectivity - and the only moment, for a device whose state
        /// core already knows.
        bool changed = false;
        /// Worth a line: the state changed, the reason is a new one, or the
        /// policy says it is time to repeat it.
        bool say = false;
        State state = State::Up;
    };

    /// The policy above, as it stands: three strikes, ten seconds.
    Reachability() = default;
    explicit Reachability(Policy policy);

    /// The thing answered.
    Verdict answered(std::int64_t nowMs);
    /// It did not, for the reason the caller would log.
    Verdict missed(std::string_view reason, std::int64_t nowMs);

    /// Back to the beginning, silently: a new configuration, a new session,
    /// anything that makes what went before say nothing about what follows.
    void forget();

    /// The wait while it answers, changed: a new setting, or a second
    /// transport that took over and left this poll as a safety net.
    void setIntervalMs(std::int64_t intervalMs);

    [[nodiscard]] State state() const { return m_state; }
    [[nodiscard]] bool down() const { return m_state == State::Down; }
    /// Failures in a row; zero after an answer.
    [[nodiscard]] int misses() const { return m_misses; }
    /// The reason last given to `missed()`.
    [[nodiscard]] const std::string &reason() const { return m_reason; }

private:
    [[nodiscard]] std::int64_t waitAfterMisses() const;

    Policy m_policy;
    State m_state = State::Up;
    int m_misses = 0;
    std::string m_reason;
    std::int64_t m_saidMs = 0;
};

} // namespace phicore::adapter::sdk
