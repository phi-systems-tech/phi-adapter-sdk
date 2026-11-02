#include "phi/adapter/ir/tuya.h"

#include "phi/adapter/ir/broadlink.h" // base64

namespace phicore::adapter::ir {

namespace {

/// The block grammar: a header byte whose top three bits are the kind.
///   000LLLLL              literal, L+1 bytes follow
///   LLLDDDDD DDDDDDDD     match of L+2 bytes, distance D+1 back
///   111DDDDD XX.. DDDDDDDD  match of 9+sum(XX) bytes, each 0xFF adds 255
bool decompress(std::string_view in, std::string *out)
{
    std::string result;
    std::size_t pos = 0;
    while (pos < in.size()) {
        const auto header = static_cast<unsigned char>(in[pos++]);
        const unsigned int kind = header >> 5;
        if (kind == 0) {
            const std::size_t length = (header & 0x1F) + 1;
            if (pos + length > in.size())
                return false;
            result.append(in.substr(pos, length));
            pos += length;
            continue;
        }
        std::size_t length = kind + 2;
        if (kind == 7) {
            while (pos < in.size() && static_cast<unsigned char>(in[pos]) == 0xFF) {
                length += 255;
                ++pos;
            }
            if (pos >= in.size())
                return false;
            length += static_cast<unsigned char>(in[pos++]);
        }
        if (pos >= in.size())
            return false;
        const std::size_t distance = ((static_cast<std::size_t>(header & 0x1F) << 8) | static_cast<unsigned char>(in[pos++])) + 1;
        if (distance > result.size())
            return false;
        // Overlap is allowed: a distance shorter than the length repeats.
        for (std::size_t i = 0; i < length; ++i)
            result.push_back(result[result.size() - distance]);
    }
    *out = std::move(result);
    return true;
}

bool timingsFromBytes(std::string_view bytes, IrCode *out)
{
    if (bytes.empty() || bytes.size() % 2 != 0)
        return false;
    IrCode code;
    code.carrierHz = 38000;
    code.timingsUs.reserve(bytes.size() / 2);
    for (std::size_t i = 0; i + 1 < bytes.size(); i += 2) {
        const std::uint32_t value = static_cast<unsigned char>(bytes[i])
            | (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[i + 1])) << 8);
        code.timingsUs.push_back(value);
    }
    *out = normalised(std::move(code));
    return !out->empty();
}

} // namespace

std::string toTuya(const IrCode &code)
{
    std::string raw;
    raw.reserve(code.timingsUs.size() * 2);
    for (std::uint32_t us : code.timingsUs) {
        if (us > 0xFFFF)
            us = 0xFFFF;
        raw.push_back(static_cast<char>(us & 0xFF));
        raw.push_back(static_cast<char>((us >> 8) & 0xFF));
    }
    std::string blocks;
    blocks.reserve(raw.size() + raw.size() / 32 + 1);
    for (std::size_t pos = 0; pos < raw.size(); pos += 32) {
        const std::size_t length = std::min<std::size_t>(32, raw.size() - pos);
        blocks.push_back(static_cast<char>(length - 1));
        blocks.append(raw, pos, length);
    }
    return base64Encode(blocks);
}

bool fromTuya(std::string_view text, IrCode *out, std::string *error)
{
    std::string bytes;
    if (!base64Decode(text, &bytes) || bytes.empty()) {
        if (error)
            *error = "not a base64 code";
        return false;
    }
    std::string plain;
    if (decompress(bytes, &plain) && timingsFromBytes(plain, out))
        return true;
    // Some Wi-Fi blasters send the 16-bit form without compression.
    if (timingsFromBytes(bytes, out))
        return true;
    if (error)
        *error = "not a Tuya IR code";
    return false;
}

} // namespace phicore::adapter::ir
