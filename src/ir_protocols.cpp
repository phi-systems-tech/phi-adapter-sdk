#include "phi/adapter/ir/protocols.h"

#include <cctype>

namespace phicore::adapter::ir {

namespace {

bool sameName(std::string_view a, std::string_view b)
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

struct Builder {
    IrCode code;
    void mark(std::uint32_t us) { code.timingsUs.push_back(us); }
    void space(std::uint32_t us) { code.timingsUs.push_back(us); }
    /// Pads with a space so the frame takes `periodUs` in all (a repeat gap).
    void padTo(std::uint64_t periodUs)
    {
        const std::uint64_t used = code.durationUs();
        if (used < periodUs)
            space(static_cast<std::uint32_t>(periodUs - used));
    }
};

// --- pulse-distance families (NEC, Samsung) ------------------------------------

void pulseDistanceBits(Builder &b, std::uint64_t value, int bits, std::uint32_t markUs, std::uint32_t zeroUs,
                       std::uint32_t oneUs)
{
    // LSB first, as these protocols send them.
    for (int i = 0; i < bits; ++i) {
        b.mark(markUs);
        b.space((value >> i) & 1 ? oneUs : zeroUs);
    }
}

bool encodeNecFamily(Protocol protocol, std::uint32_t address, std::uint32_t command, IrCode *out, std::string *error)
{
    int addressBits = 0;
    bool addressInverse = false;
    switch (protocol) {
    case Protocol::Nec: addressBits = 8; addressInverse = true; break;
    case Protocol::NecExt: addressBits = 16; break;
    case Protocol::Nec42: addressBits = 13; addressInverse = true; break;
    case Protocol::Nec42Ext: addressBits = 26; break;
    default: return false;
    }
    // The extended forms carry the command's second byte literally: a
    // Flipper NECext record keeps whatever the remote sent there, inverse or
    // not. An 8-bit command still gets its inverse, as the remote would send.
    const bool literalCommand = !addressInverse && command > 0xFF;
    if (address >= (1u << addressBits) || command > (literalCommand ? 0xFFFFu : 0xFFu)) {
        if (error)
            *error = "address or command does not fit the protocol";
        return false;
    }
    Builder b;
    b.code.carrierHz = 38000;
    b.mark(9000);
    b.space(4500);
    std::uint64_t payload = address;
    int bits = addressBits;
    if (addressInverse) {
        payload |= static_cast<std::uint64_t>(~address & ((1u << addressBits) - 1)) << bits;
        bits += addressBits;
    }
    payload |= static_cast<std::uint64_t>(command & 0xFF) << bits;
    bits += 8;
    payload |= static_cast<std::uint64_t>(literalCommand ? (command >> 8) & 0xFF : ~command & 0xFF) << bits;
    bits += 8;
    pulseDistanceBits(b, payload, bits, 560, 560, 1690);
    b.mark(560);
    // A press is a frame and then the repeat gap: 108 ms from frame start.
    b.padTo(108000);
    *out = std::move(b.code);
    return true;
}

bool encodeSamsung32(std::uint32_t address, std::uint32_t command, IrCode *out, std::string *error)
{
    if (address > 0xFF || command > 0xFF) {
        if (error)
            *error = "Samsung32 takes an 8-bit address and command";
        return false;
    }
    Builder b;
    b.code.carrierHz = 38000;
    b.mark(4500);
    b.space(4500);
    const std::uint64_t payload = address | (static_cast<std::uint64_t>(address) << 8)
        | (static_cast<std::uint64_t>(command) << 16) | (static_cast<std::uint64_t>(~command & 0xFF) << 24);
    pulseDistanceBits(b, payload, 32, 550, 550, 1650);
    b.mark(550);
    b.padTo(108000);
    *out = std::move(b.code);
    return true;
}

// --- Sony SIRC: pulse width, three frames per press ------------------------------

bool encodeSirc(Protocol protocol, std::uint32_t address, std::uint32_t command, IrCode *out, std::string *error)
{
    int addressBits = 0;
    switch (protocol) {
    case Protocol::Sirc: addressBits = 5; break;
    case Protocol::Sirc15: addressBits = 8; break;
    case Protocol::Sirc20: addressBits = 13; break;
    default: return false;
    }
    if (command > 0x7F || address >= (1u << addressBits)) {
        if (error)
            *error = "address or command does not fit SIRC";
        return false;
    }
    Builder b;
    b.code.carrierHz = 40000;
    const std::uint64_t payload = command | (static_cast<std::uint64_t>(address) << 7);
    const int bits = 7 + addressBits;
    // Every press is sent three times, 45 ms apart.
    for (int frame = 0; frame < 3; ++frame) {
        const std::uint64_t frameStart = b.code.durationUs();
        b.mark(2400);
        b.space(600);
        for (int i = 0; i < bits; ++i) {
            b.mark((payload >> i) & 1 ? 1200 : 600);
            if (i + 1 < bits || frame + 1 < 3)
                b.space(600);
        }
        if (frame + 1 < 3) {
            const std::uint64_t used = b.code.durationUs() - frameStart;
            // The last space of the frame grows into the gap.
            if (used < 45000)
                b.code.timingsUs.back() += static_cast<std::uint32_t>(45000 - used);
        }
    }
    *out = std::move(b.code);
    return true;
}

// --- Philips RC5 / RC6: Manchester ------------------------------------------------

/// Appends Manchester-coded bits; `oneIsMarkFirst` says which half carries
/// the mark for a 1 (RC6) or for a 0 (RC5). Equal neighbouring halves are
/// merged by normalised() afterwards.
void manchester(std::vector<std::uint32_t> &halves, std::vector<bool> &states, std::uint64_t value, int bits,
                std::uint32_t halfUs, bool oneIsMarkFirst)
{
    for (int i = bits - 1; i >= 0; --i) {
        const bool one = (value >> i) & 1;
        const bool firstMark = one == oneIsMarkFirst;
        halves.push_back(halfUs);
        states.push_back(firstMark);
        halves.push_back(halfUs);
        states.push_back(!firstMark);
    }
}

IrCode fromHalves(const std::vector<std::uint32_t> &halves, const std::vector<bool> &states, std::uint32_t carrierHz)
{
    // Fold the half-bit sequence into mark/space runs, leading space dropped.
    IrCode code;
    code.carrierHz = carrierHz;
    bool current = true;
    std::uint32_t run = 0;
    bool started = false;
    for (std::size_t i = 0; i < halves.size(); ++i) {
        if (!started) {
            if (!states[i])
                continue;
            started = true;
            current = true;
            run = halves[i];
            continue;
        }
        if (states[i] == current) {
            run += halves[i];
        } else {
            code.timingsUs.push_back(run);
            current = states[i];
            run = halves[i];
        }
    }
    if (started)
        code.timingsUs.push_back(run);
    return code;
}

bool encodeRc5(Protocol protocol, std::uint32_t address, std::uint32_t command, IrCode *out, std::string *error)
{
    const std::uint32_t maxCommand = protocol == Protocol::Rc5X ? 0x7F : 0x3F;
    if (address > 0x1F || command > maxCommand) {
        if (error)
            *error = "address or command does not fit RC5";
        return false;
    }
    // S1 = 1, S2 = 1 (RC5) or the inverted seventh command bit (RC5X),
    // toggle 0, 5 address bits, 6 command bits; a 1 is space then mark.
    const std::uint32_t s2 = protocol == Protocol::Rc5X ? (((command >> 6) & 1) ^ 1) : 1;
    const std::uint64_t frame = (1u << 13) | (s2 << 12) | (0u << 11) | ((address & 0x1F) << 6) | (command & 0x3F);
    std::vector<std::uint32_t> halves;
    std::vector<bool> states;
    manchester(halves, states, frame, 14, 889, false);
    IrCode code = fromHalves(halves, states, 36000);
    // Presses repeat every 114 ms.
    const std::uint64_t used = code.durationUs();
    if (code.timingsUs.size() % 2 == 1 && used < 114000)
        code.timingsUs.push_back(static_cast<std::uint32_t>(114000 - used));
    else if (used < 114000)
        code.timingsUs.back() += static_cast<std::uint32_t>(114000 - used);
    *out = std::move(code);
    return true;
}

bool encodeRc6(std::uint32_t address, std::uint32_t command, IrCode *out, std::string *error)
{
    if (address > 0xFF || command > 0xFF) {
        if (error)
            *error = "RC6 mode 0 takes an 8-bit address and command";
        return false;
    }
    std::vector<std::uint32_t> halves;
    std::vector<bool> states;
    // Leader: 2666 µs mark, 889 µs space.
    halves.push_back(2666);
    states.push_back(true);
    halves.push_back(889);
    states.push_back(false);
    // Start bit 1, mode 000: a 1 is mark then space.
    manchester(halves, states, 0b1000, 4, 444, true);
    // Trailer (toggle) bit, double length, 0.
    manchester(halves, states, 0, 1, 889, true);
    manchester(halves, states, address, 8, 444, true);
    manchester(halves, states, command, 8, 444, true);
    IrCode code = fromHalves(halves, states, 36000);
    // Signal-free time of at least 2666 µs; presses repeat at ~107 ms.
    const std::uint64_t used = code.durationUs();
    if (code.timingsUs.size() % 2 == 1)
        code.timingsUs.push_back(static_cast<std::uint32_t>(used < 107000 ? 107000 - used : 2666));
    else if (used < 107000)
        code.timingsUs.back() += static_cast<std::uint32_t>(107000 - used);
    *out = std::move(code);
    return true;
}

} // namespace

Protocol protocolFromName(std::string_view name)
{
    static constexpr std::pair<const char *, Protocol> kNames[] = {
        {"NEC", Protocol::Nec},         {"NECext", Protocol::NecExt},   {"NEC42", Protocol::Nec42},
        {"NEC42ext", Protocol::Nec42Ext}, {"Samsung32", Protocol::Samsung32}, {"SIRC", Protocol::Sirc},
        {"SIRC15", Protocol::Sirc15},   {"SIRC20", Protocol::Sirc20},   {"RC5", Protocol::Rc5},
        {"RC5X", Protocol::Rc5X},       {"RC6", Protocol::Rc6},
    };
    for (const auto &[text, protocol] : kNames)
        if (sameName(name, text))
            return protocol;
    return Protocol::Unknown;
}

std::string_view protocolName(Protocol protocol)
{
    switch (protocol) {
    case Protocol::Nec: return "NEC";
    case Protocol::NecExt: return "NECext";
    case Protocol::Nec42: return "NEC42";
    case Protocol::Nec42Ext: return "NEC42ext";
    case Protocol::Samsung32: return "Samsung32";
    case Protocol::Sirc: return "SIRC";
    case Protocol::Sirc15: return "SIRC15";
    case Protocol::Sirc20: return "SIRC20";
    case Protocol::Rc5: return "RC5";
    case Protocol::Rc5X: return "RC5X";
    case Protocol::Rc6: return "RC6";
    case Protocol::Unknown: break;
    }
    return "Unknown";
}

bool encode(Protocol protocol, std::uint32_t address, std::uint32_t command, IrCode *out, std::string *error)
{
    IrCode code;
    bool ok = false;
    switch (protocol) {
    case Protocol::Nec:
    case Protocol::NecExt:
    case Protocol::Nec42:
    case Protocol::Nec42Ext:
        ok = encodeNecFamily(protocol, address, command, &code, error);
        break;
    case Protocol::Samsung32:
        ok = encodeSamsung32(address, command, &code, error);
        break;
    case Protocol::Sirc:
    case Protocol::Sirc15:
    case Protocol::Sirc20:
        ok = encodeSirc(protocol, address, command, &code, error);
        break;
    case Protocol::Rc5:
    case Protocol::Rc5X:
        ok = encodeRc5(protocol, address, command, &code, error);
        break;
    case Protocol::Rc6:
        ok = encodeRc6(address, command, &code, error);
        break;
    case Protocol::Unknown:
        if (error)
            *error = "protocol not encoded here";
        return false;
    }
    if (!ok)
        return false;
    *out = normalised(std::move(code));
    return true;
}

} // namespace phicore::adapter::ir
