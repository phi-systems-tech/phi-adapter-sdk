#include "phi/adapter/ir/pronto.h"

#include <cmath>
#include <cstdio>

namespace phicore::adapter::ir {

namespace {

/// The Pronto clock: one unit of the frequency word is 0.241246 µs.
constexpr double kProntoClockUs = 0.241246;
constexpr std::uint32_t kTrailingGapUs = 100000;

bool hexWords(std::string_view text, std::vector<std::uint32_t> *words, std::string *error)
{
    std::vector<std::uint32_t> out;
    std::uint32_t current = 0;
    int digits = 0;
    const auto flush = [&]() -> bool {
        if (digits == 0)
            return true;
        if (digits != 4) {
            if (error)
                *error = "a Pronto word has four hex digits";
            return false;
        }
        out.push_back(current);
        current = 0;
        digits = 0;
        return true;
    };
    for (const char c : text) {
        int nibble = -1;
        if (c >= '0' && c <= '9')
            nibble = c - '0';
        else if (c >= 'a' && c <= 'f')
            nibble = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            nibble = c - 'A' + 10;
        else if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ',') {
            if (!flush())
                return false;
            continue;
        } else {
            if (error)
                *error = "not a Pronto string";
            return false;
        }
        current = (current << 4) | static_cast<std::uint32_t>(nibble);
        if (++digits == 4) {
            if (!flush())
                return false;
        }
    }
    if (!flush())
        return false;
    *words = std::move(out);
    return true;
}

} // namespace

bool fromPronto(std::string_view hex, IrCode *out, std::string *error)
{
    std::vector<std::uint32_t> words;
    if (!hexWords(hex, &words, error))
        return false;
    if (words.size() < 4 || words[0] != 0x0000) {
        if (error)
            *error = "only Pronto format 0000 is supported";
        return false;
    }
    const std::uint32_t frequencyWord = words[1];
    const std::size_t oncePairs = words[2];
    const std::size_t repeatPairs = words[3];
    if (frequencyWord == 0) {
        if (error)
            *error = "Pronto frequency word is zero";
        return false;
    }
    if (words.size() < 4 + 2 * (oncePairs + repeatPairs)) {
        if (error)
            *error = "Pronto string is shorter than its pair counts say";
        return false;
    }
    const double carrierHz = 1000000.0 / (static_cast<double>(frequencyWord) * kProntoClockUs);
    const double unitUs = 1000000.0 / carrierHz;
    const std::size_t begin = oncePairs > 0 ? 4 : 4 + 2 * oncePairs;
    const std::size_t pairs = oncePairs > 0 ? oncePairs : repeatPairs;
    if (pairs == 0) {
        if (error)
            *error = "Pronto string carries no pairs";
        return false;
    }
    IrCode code;
    code.carrierHz = static_cast<std::uint32_t>(std::lround(carrierHz));
    code.timingsUs.reserve(pairs * 2);
    for (std::size_t i = 0; i < pairs * 2; ++i)
        code.timingsUs.push_back(static_cast<std::uint32_t>(std::lround(words[begin + i] * unitUs)));
    *out = normalised(std::move(code));
    return !out->empty();
}

std::string toPronto(const IrCode &code)
{
    const std::uint32_t carrierHz = code.carrierHz == 0 ? 38000 : code.carrierHz;
    const std::uint32_t frequencyWord =
        static_cast<std::uint32_t>(std::lround(1000000.0 / (carrierHz * kProntoClockUs)));
    const double unitUs = 1000000.0 / carrierHz;
    std::vector<std::uint32_t> timings = code.timingsUs;
    if (timings.size() % 2 == 1)
        timings.push_back(kTrailingGapUs);
    std::string out;
    char word[8];
    const auto append = [&](std::uint32_t value) {
        if (value > 0xFFFF)
            value = 0xFFFF;
        std::snprintf(word, sizeof(word), "%04X", value);
        if (!out.empty())
            out.push_back(' ');
        out += word;
    };
    append(0x0000);
    append(frequencyWord);
    append(static_cast<std::uint32_t>(timings.size() / 2));
    append(0);
    for (const std::uint32_t t : timings) {
        const auto cycles = static_cast<std::uint32_t>(std::lround(t / unitUs));
        append(cycles == 0 ? 1 : cycles);
    }
    return out;
}

} // namespace phicore::adapter::ir
