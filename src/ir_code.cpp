#include "phi/adapter/ir/ir_code.h"

#include <cstdlib>

namespace phicore::adapter::ir {

std::uint64_t IrCode::durationUs() const noexcept
{
    std::uint64_t total = 0;
    for (const std::uint32_t t : timingsUs)
        total += t;
    return total;
}

IrCode normalised(IrCode code)
{
    std::vector<std::uint32_t> out;
    out.reserve(code.timingsUs.size());
    // The state of the entry being built: even index = mark, odd = space.
    bool mark = true;
    for (std::size_t i = 0; i < code.timingsUs.size(); ++i) {
        const bool thisIsMark = (i % 2) == 0;
        const std::uint32_t t = code.timingsUs[i];
        if (t == 0) {
            // A zero-length entry merges its neighbours: mark, 0 space, mark
            // is one mark.
            if (!out.empty() && i + 1 < code.timingsUs.size()) {
                out.back() += code.timingsUs[i + 1];
                ++i;
            }
            continue;
        }
        if (out.empty()) {
            if (!thisIsMark)
                continue; // a leading space is silence before the signal
            out.push_back(t);
            mark = true;
            continue;
        }
        if (thisIsMark == mark) {
            out.back() += t;
        } else {
            out.push_back(t);
            mark = thisIsMark;
        }
    }
    code.timingsUs = std::move(out);
    return code;
}

bool similar(const IrCode &a, const IrCode &b, std::uint32_t toleranceUs)
{
    if (a.timingsUs.size() != b.timingsUs.size())
        return false;
    const std::uint32_t carrierTolerance = a.carrierHz / 20;
    if (b.carrierHz + carrierTolerance < a.carrierHz || a.carrierHz + carrierTolerance < b.carrierHz)
        return false;
    for (std::size_t i = 0; i < a.timingsUs.size(); ++i) {
        const std::uint32_t x = a.timingsUs[i];
        const std::uint32_t y = b.timingsUs[i];
        const std::uint32_t diff = x > y ? x - y : y - x;
        // A long gap is never exact; what matters is that both are long.
        const std::uint32_t allowed = x > 10000 ? x / 4 : toleranceUs;
        if (diff > allowed)
            return false;
    }
    return true;
}

std::string timingsText(const IrCode &code)
{
    std::string out;
    for (const std::uint32_t t : code.timingsUs) {
        if (!out.empty())
            out.push_back(' ');
        out += std::to_string(t);
    }
    return out;
}

bool timingsFromText(std::string_view text, std::vector<std::uint32_t> *out, std::string *error)
{
    std::vector<std::uint32_t> values;
    std::size_t pos = 0;
    while (pos < text.size()) {
        while (pos < text.size() && (text[pos] == ' ' || text[pos] == ',' || text[pos] == '\t'))
            ++pos;
        if (pos >= text.size())
            break;
        std::uint64_t value = 0;
        std::size_t digits = 0;
        while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
            value = value * 10 + static_cast<std::uint64_t>(text[pos] - '0');
            ++pos;
            ++digits;
            if (value > 0xFFFFFFFFu)
                break;
        }
        if (digits == 0 || value > 0xFFFFFFFFu) {
            if (error)
                *error = "timings must be whole microseconds";
            return false;
        }
        values.push_back(static_cast<std::uint32_t>(value));
    }
    *out = std::move(values);
    return true;
}

} // namespace phicore::adapter::ir
