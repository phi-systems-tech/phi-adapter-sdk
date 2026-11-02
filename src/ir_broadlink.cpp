#include "phi/adapter/ir/broadlink.h"

#include <cmath>

namespace phicore::adapter::ir {

namespace {

/// 269/8192 ms per tick.
constexpr double kTickUs = 269000.0 / 8192.0;
/// What the device itself puts after a learnt code: 0x0D 0x05 = 3333 ticks,
/// about 109 ms of silence.
constexpr std::uint32_t kTrailingGapTicks = 0x0D05;
constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

void appendTicks(std::string &out, std::uint32_t ticks)
{
    if (ticks == 0)
        ticks = 1;
    if (ticks < 256) {
        out.push_back(static_cast<char>(ticks));
        return;
    }
    if (ticks > 0xFFFF)
        ticks = 0xFFFF;
    out.push_back('\0');
    out.push_back(static_cast<char>((ticks >> 8) & 0xFF));
    out.push_back(static_cast<char>(ticks & 0xFF));
}

} // namespace

std::string toBroadlink(const IrCode &code, BroadlinkFrame frame, std::uint8_t repeat)
{
    std::string data;
    data.reserve(code.timingsUs.size() + 8);
    for (const std::uint32_t us : code.timingsUs)
        appendTicks(data, static_cast<std::uint32_t>(std::lround(us / kTickUs)));
    if (code.timingsUs.size() % 2 == 1)
        appendTicks(data, kTrailingGapTicks);
    std::string packet;
    packet.reserve(data.size() + 8);
    packet.push_back(static_cast<char>(frame));
    packet.push_back(static_cast<char>(repeat));
    packet.push_back(static_cast<char>(data.size() & 0xFF));
    packet.push_back(static_cast<char>((data.size() >> 8) & 0xFF));
    packet += data;
    while (packet.size() % 16 != 0)
        packet.push_back('\0');
    return packet;
}

bool fromBroadlink(std::string_view packet, IrCode *out, std::string *error, BroadlinkFrame *frame,
                   std::uint8_t *repeat)
{
    if (packet.size() < 6) {
        if (error)
            *error = "Broadlink packet too short";
        return false;
    }
    const auto first = static_cast<std::uint8_t>(packet[0]);
    if (first != static_cast<std::uint8_t>(BroadlinkFrame::Ir) && first != static_cast<std::uint8_t>(BroadlinkFrame::Rf433)
        && first != static_cast<std::uint8_t>(BroadlinkFrame::Rf315)) {
        if (error)
            *error = "not a Broadlink IR/RF packet";
        return false;
    }
    const std::size_t length = static_cast<std::uint8_t>(packet[2]) | (static_cast<std::size_t>(static_cast<std::uint8_t>(packet[3])) << 8);
    if (length == 0 || 4 + length > packet.size()) {
        if (error)
            *error = "Broadlink packet length does not fit";
        return false;
    }
    IrCode code;
    code.carrierHz = 38000;
    std::size_t pos = 4;
    const std::size_t end = 4 + length;
    while (pos < end) {
        std::uint32_t ticks = static_cast<std::uint8_t>(packet[pos++]);
        if (ticks == 0) {
            if (pos + 1 >= end + 1 || pos + 2 > end) {
                if (error)
                    *error = "Broadlink packet ends inside an escape";
                return false;
            }
            ticks = (static_cast<std::uint32_t>(static_cast<std::uint8_t>(packet[pos])) << 8)
                | static_cast<std::uint8_t>(packet[pos + 1]);
            pos += 2;
        }
        code.timingsUs.push_back(static_cast<std::uint32_t>(std::lround(ticks * kTickUs)));
    }
    // The device's own trailing gap is not part of the code.
    if (code.timingsUs.size() % 2 == 0 && !code.timingsUs.empty()
        && code.timingsUs.back() >= static_cast<std::uint32_t>(std::lround(kTrailingGapTicks * kTickUs)) - 50)
        code.timingsUs.pop_back();
    if (frame)
        *frame = static_cast<BroadlinkFrame>(first);
    if (repeat)
        *repeat = static_cast<std::uint8_t>(packet[1]);
    *out = normalised(std::move(code));
    return !out->empty();
}

std::string base64Encode(std::string_view bytes)
{
    std::string out;
    out.reserve(((bytes.size() + 2) / 3) * 4);
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const unsigned char b0 = static_cast<unsigned char>(bytes[i]);
        const bool has1 = i + 1 < bytes.size();
        const bool has2 = i + 2 < bytes.size();
        const unsigned char b1 = has1 ? static_cast<unsigned char>(bytes[i + 1]) : 0;
        const unsigned char b2 = has2 ? static_cast<unsigned char>(bytes[i + 2]) : 0;
        out.push_back(kAlphabet[b0 >> 2]);
        out.push_back(kAlphabet[((b0 & 0x03) << 4) | (b1 >> 4)]);
        out.push_back(has1 ? kAlphabet[((b1 & 0x0F) << 2) | (b2 >> 6)] : '=');
        out.push_back(has2 ? kAlphabet[b2 & 0x3F] : '=');
    }
    return out;
}

bool base64Decode(std::string_view text, std::string *out)
{
    std::string result;
    unsigned int accumulator = 0;
    int bits = 0;
    for (const char c : text) {
        int value = -1;
        if (c >= 'A' && c <= 'Z')
            value = c - 'A';
        else if (c >= 'a' && c <= 'z')
            value = c - 'a' + 26;
        else if (c >= '0' && c <= '9')
            value = c - '0' + 52;
        else if (c == '+' || c == '-')
            value = 62;
        else if (c == '/' || c == '_')
            value = 63;
        else if (c == '=' || c == ' ' || c == '\n' || c == '\r' || c == '\t')
            continue;
        else
            return false;
        accumulator = (accumulator << 6) | static_cast<unsigned int>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            result.push_back(static_cast<char>((accumulator >> bits) & 0xFF));
        }
    }
    *out = std::move(result);
    return true;
}

} // namespace phicore::adapter::ir
