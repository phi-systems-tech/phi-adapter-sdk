// TDP as bytes: the probe's header and CRC, and an answer as the Deco X55 gives it.

#include <phi/adapter/testing/check.h>

#include "phi/adapter/net/tdp.h"

using namespace phicore::adapter::net;

int main()
{
    PHI_CHECK(kTdpPort == 20002);

    const std::string probe = tdpProbePacket("KEY", 0x01020304u);
    PHI_CHECK(probe.size() > 16 && probe[0] == 2 && probe[3] == 1 && static_cast<unsigned char>(probe[6]) == 0x11);
    PHI_CHECK(probe.substr(16) == R"({"params":{"rsa_key":"KEY"}})");
    PHI_CHECK((static_cast<unsigned char>(probe[4]) << 8 | static_cast<unsigned char>(probe[5])) == 28);
    PHI_CHECK(static_cast<unsigned char>(probe[11]) == 0x04 && static_cast<unsigned char>(probe[8]) == 0x01);
    std::string seeded = probe;
    seeded[12] = '\x5A'; seeded[13] = '\x6B'; seeded[14] = '\x7C'; seeded[15] = '\x8D';
    PHI_CHECK(seeded != probe);
    // A PEM's newlines are escaped, so the body stays one JSON string.
    PHI_CHECK(tdpProbePacket("A\nB", 1).substr(16) == R"({"params":{"rsa_key":"A\nB"}})");

    // The default probe carries a real PEM key.
    const std::string real = tdpProbePacket();
    PHI_CHECK(real.find("BEGIN PUBLIC KEY") != std::string::npos);

    const std::string answer = std::string("\x02\x00\x00\x01\x02\x52\x00\x00\xf5\x7e\x0e\x32\xeb\x13\x36\x86", 16)
        + R"({"result":{"role":"master","mac":"3C:64:CF:86:50:F8","device_model":"X55","ip":"192.168.18.3","device_type":"HOMEWIFISYSTEM","group_name":"VlNQ"},"error_code":0})";
    TdpAnswer parsed;
    phicore::adapter::v1::Utf8String error;
    PHI_CHECK(parseTdpAnswer(answer, &parsed, &error));
    PHI_CHECK(parsed.role() == "master");
    PHI_CHECK(parsed.mac() == "3c:64:cf:86:50:f8");
    PHI_CHECK(parsed.model() == "X55");
    PHI_CHECK(parsed.ip() == "192.168.18.3");
    PHI_CHECK(parsed.deviceType() == "HOMEWIFISYSTEM");
    PHI_CHECK(parsed.groupName() == "VSP");

    const auto candidate = tdpCandidateFor(parsed, "192.168.18.9");
    PHI_CHECK(candidate.externalId() == "3c:64:cf:86:50:f8");
    PHI_CHECK(candidate.label() == "X55 (VSP)");
    PHI_CHECK(candidate.host() == "192.168.18.3" && candidate.ip() == "192.168.18.3");
    PHI_CHECK(candidate.mac() == "3c:64:cf:86:50:f8" && candidate.model() == "X55");
    PHI_CHECK(candidate.metaJson() == R"({"deviceType":"HOMEWIFISYSTEM","role":"master","groupName":"VSP"})");

    // No address, no MAC in the answer: the sender stands in.
    TdpAnswer bare;
    bare.setDeviceType("HOMEWIFISYSTEM");
    const auto fallback = tdpCandidateFor(bare, "10.0.0.7");
    PHI_CHECK(fallback.externalId() == "10.0.0.7" && fallback.ip() == "10.0.0.7" && fallback.label() == "HOMEWIFISYSTEM");

    PHI_CHECK(!parseTdpAnswer("short", nullptr, &error) && !error.empty());
    PHI_CHECK(!parseTdpAnswer(std::string(16, '\x02') + "not json", nullptr, nullptr));
    PHI_CHECK(!parseTdpAnswer(std::string(16, '\x02') + R"({"error_code":0})", nullptr, &error));
    PHI_CHECK(!parseTdpAnswer(std::string(16, '\x01') + R"({"result":{}})", nullptr, nullptr));

    return phi::testing::report("sdk_tdp_tests");
}
