// Infrared codes as light and in every dialect: normalising, Pronto both
// ways, the Broadlink packet, the Tuya string against the community's
// vectors, the protocol encoders against their timing tables, and a Flipper
// file from the official format document.

#include <phi/adapter/testing/check.h>

#include "phi/adapter/ir/broadlink.h"
#include "phi/adapter/ir/flipper.h"
#include "phi/adapter/ir/ir_code.h"
#include "phi/adapter/ir/pronto.h"
#include "phi/adapter/ir/protocols.h"
#include "phi/adapter/ir/tuya.h"

#include <string>
#include <vector>

using namespace phicore::adapter::ir;

namespace {

bool near(std::uint32_t a, std::uint32_t b, std::uint32_t tolerance)
{
    return (a > b ? a - b : b - a) <= tolerance;
}

std::string hex(std::string_view bytes)
{
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    for (const char c : bytes) {
        out.push_back(digits[static_cast<unsigned char>(c) >> 4]);
        out.push_back(digits[static_cast<unsigned char>(c) & 0xF]);
    }
    return out;
}

void testNormalise()
{
    IrCode code;
    code.timingsUs = {0, 500, 100, 0, 200, 300, 300, 0, 0, 400};
    const IrCode out = normalised(code);
    // leading space dropped; 100 mark + 0 space + 200 mark = 300 mark; 300 space;
    // 300 mark; 0 space 0 mark merge; 400 space
    PHI_CHECK((out.timingsUs == std::vector<std::uint32_t>{300, 300, 300, 400}));

    std::vector<std::uint32_t> parsed;
    std::string error;
    PHI_CHECK(timingsFromText("9000 4500,560\t560", &parsed, &error));
    PHI_CHECK((parsed == std::vector<std::uint32_t>{9000, 4500, 560, 560}));
    PHI_CHECK(!timingsFromText("9000 abc", &parsed, &error));

    IrCode a;
    a.timingsUs = {9000, 4500, 560, 40000};
    IrCode b = a;
    b.timingsUs[0] = 9100;
    b.timingsUs[3] = 45000;
    PHI_CHECK(similar(a, b));
    b.timingsUs[1] = 5000;
    PHI_CHECK(!similar(a, b));
}

void testPronto()
{
    // NEC header and three bits, from the Remote Central description.
    IrCode code;
    std::string error;
    PHI_CHECK_MSG(fromPronto("0000 006D 0003 0000 0156 00AB 0015 0015 0015 0040", &code, &error), "%s", error.c_str());
    PHI_CHECK(near(code.carrierHz, 38029, 5));
    PHI_CHECK(code.timingsUs.size() == 6);
    const std::vector<std::uint32_t> expected = {8993, 4497, 552, 552, 552, 1683};
    for (std::size_t i = 0; i < expected.size() && i < code.timingsUs.size(); ++i)
        PHI_CHECK_MSG(near(code.timingsUs[i], expected[i], 2), "timing %zu = %u", i, code.timingsUs[i]);

    // Round trip keeps the signal; words come out upper case, once sequence.
    const std::string text = toPronto(code);
    PHI_CHECK(text.rfind("0000 006D 0003 0000 ", 0) == 0);
    IrCode again;
    PHI_CHECK(fromPronto(text, &again, &error));
    PHI_CHECK(similar(code, again, 30));

    // No spaces, lower case, repeat sequence only.
    PHI_CHECK(fromPronto("0000006d00000002015600ab00150015", &code, &error));
    PHI_CHECK(code.timingsUs.size() == 4);

    PHI_CHECK(!fromPronto("5000 0073 0000 0001 0001 0001", &code, &error));
    PHI_CHECK(!fromPronto("0000 006D 0009 0000 0156", &code, &error));
    PHI_CHECK(!fromPronto("hello", &code, &error));

    // An odd count (ending in a mark) gets a gap so the pairs close.
    IrCode odd;
    odd.timingsUs = {9000, 4500, 560};
    const std::string oddText = toPronto(odd);
    PHI_CHECK(oddText.rfind("0000 006D 0002 0000 ", 0) == 0);
}

void testBroadlink()
{
    IrCode code;
    code.timingsUs = {9000, 4500, 560, 560, 560, 1690};
    const std::string packet = toBroadlink(code);
    PHI_CHECK(packet.size() == 16);
    PHI_CHECK_MSG(hex(packet.substr(0, 12)) == "260008000001128911111133", "%s", hex(packet).c_str());
    PHI_CHECK(base64Encode(packet.substr(0, 12)) == "JgAIAAABEokREREz");

    IrCode back;
    std::string error;
    BroadlinkFrame frame = BroadlinkFrame::Rf315;
    std::uint8_t repeat = 9;
    PHI_CHECK_MSG(fromBroadlink(packet, &back, &error, &frame, &repeat), "%s", error.c_str());
    PHI_CHECK(frame == BroadlinkFrame::Ir && repeat == 0);
    PHI_CHECK(back.carrierHz == 38000);
    PHI_CHECK(similar(code, back, 33));

    // A code ending in a mark gets the device's own trailer, which the
    // decoder takes off again.
    IrCode withStop = code;
    withStop.timingsUs.push_back(560);
    const std::string stopped = toBroadlink(withStop, BroadlinkFrame::Ir, 2);
    PHI_CHECK(static_cast<unsigned char>(stopped[1]) == 2);
    PHI_CHECK(stopped.find("\x0d\x05") != std::string::npos);
    PHI_CHECK(fromBroadlink(stopped, &back, &error, &frame, &repeat));
    PHI_CHECK(repeat == 2);
    PHI_CHECK(back.timingsUs.size() == 7);

    // Base64 as shared on the forums.
    std::string bytes;
    PHI_CHECK(base64Decode("JgAKAAABEokREREzDQU=", &bytes));
    PHI_CHECK(hex(bytes) == "26000a00000112891111113 30d05" || hex(bytes) == "26000a000001128911111133" "0d05");
    PHI_CHECK(fromBroadlink(bytes, &back, &error));

    PHI_CHECK(!fromBroadlink("\x26\x00", &back, &error));
    PHI_CHECK(!fromBroadlink("\x99\x00\x02\x00\x11\x11", &back, &error));
    PHI_CHECK(!fromBroadlink(std::string("\x26\x00\x09\x00\x11", 5), &back, &error));

    // RF frames keep their byte.
    const std::string rf = toBroadlink(code, BroadlinkFrame::Rf433);
    PHI_CHECK(static_cast<unsigned char>(rf[0]) == 0xB2);
    PHI_CHECK(fromBroadlink(rf, &back, &error, &frame) && frame == BroadlinkFrame::Rf433);
}

void testTuya()
{
    // The gist's README example: five timings, literal block, base64.
    IrCode five;
    five.timingsUs = {7, 6, 11, 3, 2};
    PHI_CHECK(toTuya(five) == "CQcABgALAAMAAgA=");
    IrCode back;
    std::string error;
    PHI_CHECK_MSG(fromTuya("CQcABgALAAMAAgA=", &back, &error), "%s", error.c_str());
    PHI_CHECK((back.timingsUs == five.timingsUs));

    IrCode nec;
    nec.timingsUs = {9000, 4500, 560, 560, 560, 1690, 560};
    PHI_CHECK(toTuya(nec) == "DSgjlBEwAjACMAKaBjAC");
    PHI_CHECK(fromTuya("DSgjlBEwAjACMAKaBjAC", &back, &error) && back.timingsUs == nec.timingsUs);

    // A real compressed code from the gist: matches, long matches, overlap.
    const char *sample =
        "A/IEiwFAAwbJAfIE8gSLIAUBiwFAC+ADAwuLAfIE8gSLAckBRx9AB0ADBskB8gTyBIsgBQGLAUALA4sB8gRAB8ADBfIEiwHJAeARLwHJ"
        "AeAFAwHyBOC5LwGLAeA97wOLAfIE4RcfBYsB8gTyBEAFAYsB4AcrCYsB8gTyBIsByQHgPY8DyQHyBOAHAwHyBEAX4BVfBIsB8gTJoAMF"
        "8gSLAckB4BUvAckB4AEDBfIEiwHJAQ==";
    PHI_CHECK_MSG(fromTuya(sample, &back, &error), "%s", error.c_str());
    PHI_CHECK_MSG(back.timingsUs.size() == 335, "got %zu timings", back.timingsUs.size());
    const std::vector<std::uint32_t> head = {1266, 395, 1266, 395, 457, 1266, 1266, 395, 1266, 395, 457, 1266,
                                             457,  1266, 457, 1266, 457, 1266, 395, 1266, 1266, 395, 457, 8007};
    for (std::size_t i = 0; i < head.size() && i < back.timingsUs.size(); ++i)
        PHI_CHECK_MSG(back.timingsUs[i] == head[i], "timing %zu = %u", i, back.timingsUs[i]);
    // ...and it survives our literal-only writer.
    IrCode again;
    PHI_CHECK(fromTuya(toTuya(back), &again, &error) && again.timingsUs == back.timingsUs);

    // Long codes: more than one literal block, 16-bit clamp.
    IrCode longCode;
    for (int i = 0; i < 300; ++i)
        longCode.timingsUs.push_back(static_cast<std::uint32_t>(500 + i));
    longCode.timingsUs.push_back(90000);
    PHI_CHECK(fromTuya(toTuya(longCode), &again, &error));
    PHI_CHECK(again.timingsUs.size() == 301 && again.timingsUs[299] == 799 && again.timingsUs[300] == 65535);

    // The plain 16-bit form some Wi-Fi blasters emit.
    PHI_CHECK(fromTuya(base64Encode(std::string("\x28\x23\x94\x11", 4)), &back, &error));
    PHI_CHECK((back.timingsUs == std::vector<std::uint32_t>{9000, 4500}));
    PHI_CHECK(!fromTuya("!!!", &back, &error));
}

void testProtocols()
{
    IrCode code;
    std::string error;

    // NEC address 0x04, command 0x08: header, 32 bits LSB first, stop, 108 ms frame.
    PHI_CHECK_MSG(encode(Protocol::Nec, 0x04, 0x08, &code, &error), "%s", error.c_str());
    PHI_CHECK(code.carrierHz == 38000);
    PHI_CHECK(code.timingsUs.size() == 68);
    PHI_CHECK(code.timingsUs[0] == 9000 && code.timingsUs[1] == 4500);
    // address bits 0..7 of 0x04: 0 0 1 0 0 0 0 0 -> spaces at odd indexes 3,5,7...
    PHI_CHECK(code.timingsUs[3] == 560 && code.timingsUs[5] == 560 && code.timingsUs[7] == 1690 && code.timingsUs[9] == 560);
    // ~address 0xFB: 1 1 0 1 1 1 1 1
    PHI_CHECK(code.timingsUs[19] == 1690 && code.timingsUs[21] == 1690 && code.timingsUs[23] == 560);
    PHI_CHECK(code.timingsUs[66] == 560);
    PHI_CHECK(code.durationUs() == 108000);
    PHI_CHECK(!encode(Protocol::Nec, 0x100, 0x08, &code, &error));

    PHI_CHECK(encode(Protocol::NecExt, 0x87EE, 0x5D, &code, &error));
    PHI_CHECK(code.timingsUs.size() == 68 && code.timingsUs[53] == 1690); // ~0x5D = 0xA2, bit 1 set
    PHI_CHECK(encode(Protocol::NecExt, 0x87EE, 0xA05D, &code, &error));
    PHI_CHECK(code.timingsUs.size() == 68 && code.timingsUs[53] == 560);
    PHI_CHECK(!encode(Protocol::Nec, 0x04, 0xA05D, &code, &error));
    PHI_CHECK(encode(Protocol::Nec42, 0x1FFF, 0x01, &code, &error));
    PHI_CHECK(code.timingsUs.size() == 2 + 84 + 1 + 1);
    PHI_CHECK(encode(Protocol::Nec42Ext, 0x3FFFFFF, 0x01, &code, &error));
    PHI_CHECK(code.timingsUs.size() == 2 + 84 + 1 + 1);

    PHI_CHECK(encode(Protocol::Samsung32, 0x07, 0x02, &code, &error));
    PHI_CHECK(code.timingsUs[0] == 4500 && code.timingsUs[1] == 4500);
    PHI_CHECK(code.timingsUs.size() == 68);
    // address 0x07 twice: bits 1 1 1 0 0 0 0 0 | 1 1 1 0 ...
    PHI_CHECK(code.timingsUs[3] == 1650 && code.timingsUs[9] == 550 && code.timingsUs[19] == 1650);

    // Sony: three frames, 45 ms apart, 12 bits.
    PHI_CHECK(encode(Protocol::Sirc, 0x01, 0x15, &code, &error));
    PHI_CHECK(code.carrierHz == 40000);
    PHI_CHECK(code.timingsUs.size() == 3 * (2 + 24) - 1);
    PHI_CHECK(code.timingsUs[0] == 2400 && code.timingsUs[1] == 600);
    // command 0x15 = 1 0 1 0 1 0 0 LSB first: marks 1200,600,1200,600,1200,600,600
    PHI_CHECK(code.timingsUs[2] == 1200 && code.timingsUs[4] == 600 && code.timingsUs[6] == 1200 && code.timingsUs[14] == 600);
    // address 0x01 then: mark 1200 then four 600
    PHI_CHECK(code.timingsUs[16] == 1200 && code.timingsUs[18] == 600);
    std::uint64_t firstFrame = 0;
    for (std::size_t i = 0; i < 26; ++i)
        firstFrame += code.timingsUs[i];
    PHI_CHECK(firstFrame == 45000);
    PHI_CHECK(encode(Protocol::Sirc15, 0xFF, 0x7F, &code, &error));
    PHI_CHECK(encode(Protocol::Sirc20, 0x1FFF, 0x7F, &code, &error));
    PHI_CHECK(!encode(Protocol::Sirc, 0x20, 0x01, &code, &error));

    // RC5 address 0, command 0: S1 S2 T=1 1 0, then zeros. Manchester 889.
    PHI_CHECK(encode(Protocol::Rc5, 0, 0, &code, &error));
    PHI_CHECK(code.carrierHz == 36000);
    // S1=1: (space) mark; S2=1: space mark; T=0: mark space; then 0s: mark space ...
    // -> mark 889, space 889, mark 1778, space 889, mark 889, space 889, ... (the
    // zero bits merge mark-space-mark-space into 889 each)
    PHI_CHECK(code.timingsUs[0] == 889 && code.timingsUs[1] == 889 && code.timingsUs[2] == 1778);
    PHI_CHECK(code.durationUs() == 114000);
    PHI_CHECK(encode(Protocol::Rc5X, 0x1F, 0x7F, &code, &error));
    PHI_CHECK(!encode(Protocol::Rc5, 0, 0x40, &code, &error));

    // RC6: leader 2666/889, start bit 1 (444 mark, 444 space), mode 000 ...
    PHI_CHECK(encode(Protocol::Rc6, 0x00, 0x0C, &code, &error));
    PHI_CHECK(code.timingsUs[0] == 2666 && code.timingsUs[1] == 889);
    PHI_CHECK(code.timingsUs[2] == 444);
    PHI_CHECK(code.durationUs() == 107000);

    PHI_CHECK(!encode(Protocol::Unknown, 0, 0, &code, &error));
    PHI_CHECK(protocolFromName("necext") == Protocol::NecExt);
    PHI_CHECK(protocolFromName("Kaseikyo") == Protocol::Unknown);
    PHI_CHECK(protocolName(Protocol::Sirc20) == "SIRC20");
}

void testFlipper()
{
    const char *file =
        "Filetype: IR signals file\n"
        "Version: 1\n"
        "#\n"
        "name: Button_1\n"
        "type: parsed\n"
        "protocol: NECext\n"
        "address: EE 87 00 00\n"
        "command: 5D A0 00 00\n"
        "#\n"
        "name: Button_2\n"
        "type: raw\n"
        "frequency: 38000\n"
        "duty_cycle: 0.330000\n"
        "data: 504 3432 502 483 500 484 510 502 502 482 501 485 509 1452 504 1458 509 1452 504 481 501 474 509 3420 503\n"
        "#\n"
        "name: Button_3\n"
        "type: parsed\n"
        "protocol: SIRC\n"
        "address: 01 00 00 00\n"
        "command: 15 00 00 00\n"
        "#\n"
        "name: Odd\n"
        "type: parsed\n"
        "protocol: Kaseikyo\n"
        "address: 00 20 02 00\n"
        "command: 3D 00 00 00\n";
    const std::vector<FlipperRecord> records = parseFlipperFile(file);
    PHI_CHECK(records.size() == 4);
    if (records.size() != 4)
        return;
    PHI_CHECK(records[0].name == "Button_1" && !records[0].raw && records[0].protocol == Protocol::NecExt);
    PHI_CHECK(records[0].address == 0x87EE && records[0].command == 0xA05D);
    PHI_CHECK(records[1].raw && records[1].code.carrierHz == 38000 && records[1].code.timingsUs.size() == 25);
    PHI_CHECK(records[2].protocol == Protocol::Sirc && records[2].address == 1 && records[2].command == 0x15);
    PHI_CHECK(records[3].protocol == Protocol::Unknown && records[3].protocolName == "Kaseikyo");

    IrCode code;
    std::string error;
    PHI_CHECK(codeOf(records[1], &code, &error) && code.timingsUs.size() == 25);
    PHI_CHECK(codeOf(records[2], &code, &error) && code.carrierHz == 40000);
    // NECext with a 16-bit command: the low byte is the command, the high
    // byte its inverse as the file carries it; the encoder takes the low byte.
    PHI_CHECK(codeOf(records[0], &code, &error) && code.timingsUs.size() == 68);
    // 0xA0 goes out as it is (bit 1 clear), where ~0x5D = 0xA2 would set it.
    PHI_CHECK(code.timingsUs.size() == 68 && code.timingsUs[51] == 560 && code.timingsUs[53] == 560);
    PHI_CHECK(!codeOf(records[3], &code, &error));
    PHI_CHECK(error.find("Kaseikyo") != std::string::npos);

    PHI_CHECK(parseFlipperFile("just text").empty());
}

} // namespace

int main()
{
    testNormalise();
    testPronto();
    testBroadlink();
    testTuya();
    testProtocols();
    testFlipper();
    return phi::testing::report("sdk_ir_tests");
}
