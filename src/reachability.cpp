#include "phi/adapter/sdk/reachability.h"

#include <algorithm>
#include <utility>

namespace phicore::adapter::sdk {

namespace {

/// Far beyond any delay list, and far from overflowing: the counter only has
/// to keep saying "still gone".
constexpr int kMissCeiling = 1000000;

} // namespace

Reachability::Reachability(Policy policy)
    : m_policy(std::move(policy))
{
    m_policy.strikes = std::max(1, m_policy.strikes);
    m_policy.intervalMs = std::max<std::int64_t>(0, m_policy.intervalMs);
    m_policy.sayAgainAfterMs = std::max<std::int64_t>(0, m_policy.sayAgainAfterMs);
    for (std::int64_t &delay : m_policy.retryDelaysMs)
        delay = std::max<std::int64_t>(0, delay);
}

Reachability::Verdict Reachability::answered(std::int64_t nowMs)
{
    Verdict verdict;
    verdict.state = State::Up;
    verdict.waitMs = m_policy.intervalMs;
    verdict.changed = m_state == State::Down;
    verdict.say = verdict.changed;
    m_state = State::Up;
    m_misses = 0;
    m_reason.clear();
    if (verdict.say)
        m_saidMs = nowMs;
    return verdict;
}

Reachability::Verdict Reachability::missed(std::string_view reason, std::int64_t nowMs)
{
    const bool differentReason = m_reason != reason;
    m_reason.assign(reason);
    if (m_misses < kMissCeiling)
        ++m_misses;

    Verdict verdict;
    verdict.waitMs = waitAfterMisses();
    if (m_state == State::Up && m_misses >= m_policy.strikes) {
        m_state = State::Down;
        verdict.changed = true;
    }
    verdict.state = m_state;
    // Under the strike count a miss is a blip: waited out, not announced. Once
    // it is gone, the line comes when the state turned, when the reason turned
    // into another one, or when the policy asks for it to be said again.
    const bool timeToRepeat = m_policy.sayAgainAfterMs > 0 && nowMs - m_saidMs >= m_policy.sayAgainAfterMs;
    verdict.say = verdict.changed || (m_state == State::Down && (differentReason || timeToRepeat));
    if (verdict.say)
        m_saidMs = nowMs;
    return verdict;
}

void Reachability::forget()
{
    m_state = State::Up;
    m_misses = 0;
    m_reason.clear();
    m_saidMs = 0;
}

void Reachability::setIntervalMs(std::int64_t intervalMs)
{
    m_policy.intervalMs = std::max<std::int64_t>(0, intervalMs);
}

std::int64_t Reachability::waitAfterMisses() const
{
    if (m_policy.retryDelaysMs.empty())
        return m_policy.intervalMs;
    const auto step = static_cast<std::size_t>(std::max(1, m_misses)) - 1;
    return m_policy.retryDelaysMs[std::min(step, m_policy.retryDelaysMs.size() - 1)];
}

} // namespace phicore::adapter::sdk
