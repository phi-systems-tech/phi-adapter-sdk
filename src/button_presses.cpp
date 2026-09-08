#include "phi/adapter/sdk/button_presses.h"

namespace phicore::adapter::sdk {

using Code = v1::ButtonEventCode;

std::vector<ButtonPresses::Report> ButtonPresses::flush(Memory &memory)
{
    std::vector<Report> out;
    if (memory.releases <= 0)
        return out;
    Code code = Code::ShortPressRelease;
    if (memory.releases == 2)
        code = Code::DoublePress;
    else if (memory.releases == 3)
        code = Code::TriplePress;
    else if (memory.releases == 4)
        code = Code::QuadruplePress;
    else if (memory.releases >= 5)
        code = Code::QuintuplePress;
    out.push_back({code, memory.lastReleaseTs});
    memory.releases = 0;
    memory.lastReleaseTs = 0;
    return out;
}

ButtonPresses::Outcome ButtonPresses::onEvent(const std::string &key, Code code,
                                              std::int64_t tsMs, std::int64_t nowMs)
{
    Outcome out;
    if (code == Code::None)
        return out;
    Memory &memory = m_keys[key];

    switch (code) {
    case Code::ShortPressRelease:
        // Held: the next half second decides what it was. Half a second
        // from now, not from when the device says it happened.
        ++memory.releases;
        memory.lastReleaseTs = tsMs;
        out.windowUntilMs = nowMs + kMultiPressWindowMs;
        break;
    case Code::InitialPress:
        // The second press of a double click is a press like any other;
        // whatever is held stays held.
        out.report.push_back({code, tsMs});
        break;
    case Code::LongPress: {
        // A hold is a different gesture: a click held before it is a click.
        out.report = flush(memory);
        out.cancelWindow = true;
        const bool repeating = (memory.lastCode == static_cast<int>(Code::LongPress)
                                || memory.lastCode == static_cast<int>(Code::Repeat))
            && memory.lastTs > 0 && (tsMs - memory.lastTs) <= kLongPressRepeatWindowMs;
        code = repeating ? Code::Repeat : Code::LongPress;
        out.report.push_back({code, tsMs});
        break;
    }
    case Code::Repeat: {
        // A repeat with no hold before it is a hold that was not announced
        // - Hue's first "repeat" after a press is exactly that.
        out.report = flush(memory);
        out.cancelWindow = true;
        const bool holding = memory.lastCode == static_cast<int>(Code::LongPress)
            || memory.lastCode == static_cast<int>(Code::Repeat);
        if (!holding)
            out.report.push_back({Code::LongPress, tsMs});
        out.report.push_back({Code::Repeat, tsMs});
        break;
    }
    default:
        // A device that says "double" itself has done the counting; so has
        // one that says "long release".
        out.report = flush(memory);
        out.cancelWindow = true;
        out.report.push_back({code, tsMs});
        break;
    }

    if (code == Code::LongPressRelease) {
        memory.lastCode = 0;
        memory.lastTs = 0;
    } else if (code != Code::ShortPressRelease) {
        memory.lastCode = static_cast<int>(code);
        memory.lastTs = tsMs;
    }
    return out;
}

std::vector<ButtonPresses::Report> ButtonPresses::onWindowClosed(const std::string &key)
{
    const auto it = m_keys.find(key);
    if (it == m_keys.end())
        return {};
    std::vector<Report> out = flush(it->second);
    if (!out.empty()) {
        it->second.lastCode = static_cast<int>(out.back().code);
        it->second.lastTs = out.back().tsMs;
    }
    return out;
}

} // namespace phicore::adapter::sdk
