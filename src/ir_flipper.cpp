#include "phi/adapter/ir/flipper.h"

#include <cmath>
#include <cstdlib>

namespace phicore::adapter::ir {

namespace {

std::string_view trimmed(std::string_view text)
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r'))
        text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
        text.remove_suffix(1);
    return text;
}

/// "04 00 00 00" little-endian bytes to a number; false when not hex bytes.
bool bytesValue(std::string_view text, std::uint32_t *out)
{
    std::uint64_t value = 0;
    int shift = 0;
    std::size_t pos = 0;
    while (pos < text.size()) {
        while (pos < text.size() && text[pos] == ' ')
            ++pos;
        if (pos >= text.size())
            break;
        if (pos + 2 > text.size())
            return false;
        unsigned int byte = 0;
        for (int i = 0; i < 2; ++i) {
            const char c = text[pos + static_cast<std::size_t>(i)];
            unsigned int nibble = 0;
            if (c >= '0' && c <= '9') nibble = static_cast<unsigned int>(c - '0');
            else if (c >= 'a' && c <= 'f') nibble = static_cast<unsigned int>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') nibble = static_cast<unsigned int>(c - 'A' + 10);
            else return false;
            byte = (byte << 4) | nibble;
        }
        pos += 2;
        if (shift < 64)
            value |= static_cast<std::uint64_t>(byte) << shift;
        shift += 8;
    }
    if (value > 0xFFFFFFFFu)
        return false;
    *out = static_cast<std::uint32_t>(value);
    return true;
}

bool complete(const FlipperRecord &record)
{
    if (record.name.empty())
        return false;
    if (record.raw)
        return !record.code.timingsUs.empty();
    return !record.protocolName.empty();
}

} // namespace

std::vector<FlipperRecord> parseFlipperFile(std::string_view text)
{
    std::vector<FlipperRecord> out;
    if (text.find("IR signals file") == std::string_view::npos)
        return out;
    FlipperRecord current;
    bool open = false;
    bool hasType = false;
    const auto flush = [&]() {
        if (open && hasType && complete(current))
            out.push_back(current);
        current = FlipperRecord{};
        open = false;
        hasType = false;
    };
    std::size_t pos = 0;
    while (pos <= text.size()) {
        const std::size_t eol = text.find('\n', pos);
        const std::string_view line = trimmed(text.substr(pos, eol == std::string_view::npos ? std::string_view::npos : eol - pos));
        pos = eol == std::string_view::npos ? text.size() + 1 : eol + 1;
        if (line.empty() || line.front() == '#') {
            // A '#' line separates records in the files Flipper writes.
            if (line.empty() || line == "#")
                flush();
            continue;
        }
        const std::size_t colon = line.find(':');
        if (colon == std::string_view::npos)
            continue;
        const std::string_view key = trimmed(line.substr(0, colon));
        const std::string_view value = trimmed(line.substr(colon + 1));
        if (key == "name") {
            flush();
            open = true;
            current.name = std::string(value);
        } else if (!open) {
            continue;
        } else if (key == "type") {
            hasType = true;
            current.raw = value == "raw";
        } else if (key == "protocol") {
            current.protocolName = std::string(value);
            current.protocol = protocolFromName(value);
        } else if (key == "address") {
            std::uint32_t number = 0;
            if (bytesValue(value, &number))
                current.address = number;
        } else if (key == "command") {
            std::uint32_t number = 0;
            if (bytesValue(value, &number))
                current.command = number;
        } else if (key == "frequency") {
            current.code.carrierHz = static_cast<std::uint32_t>(std::strtoul(std::string(value).c_str(), nullptr, 10));
        } else if (key == "data") {
            std::vector<std::uint32_t> timings;
            if (timingsFromText(value, &timings, nullptr))
                current.code.timingsUs = std::move(timings);
        }
    }
    flush();
    return out;
}

bool codeOf(const FlipperRecord &record, IrCode *out, std::string *error)
{
    if (record.raw) {
        if (record.code.timingsUs.empty()) {
            if (error)
                *error = "raw record without data";
            return false;
        }
        IrCode code = record.code;
        if (code.carrierHz == 0)
            code.carrierHz = 38000;
        *out = normalised(std::move(code));
        return true;
    }
    if (record.protocol == Protocol::Unknown) {
        if (error)
            *error = "protocol " + record.protocolName + " is not encoded here";
        return false;
    }
    return encode(record.protocol, record.address, record.command, out, error);
}

} // namespace phicore::adapter::ir
