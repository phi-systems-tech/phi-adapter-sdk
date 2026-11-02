#pragma once
// TP-Link's TDP: the UDP search a Deco (and other TP-Link home gear) answers
// on port 20002. A probe is a 16-byte header, a CRC-32 and a small JSON body
// that carries an RSA public key; an answer is the same header and a JSON
// document with the device's type, model, role, MAC, group name and address.
//
// Pure functions, so the bytes can be pinned in a test without a socket. Send
// the probe with `UdpProbe` (udp_probe.h); a factory that answers
// `factory.discover` (phi/adapter/v1/discovery.h) turns each answer into a
// candidate with `tdpCandidateFor`.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "phi/adapter/v1/discovery.h"
#include "phi/adapter/v1/types.h"

namespace phicore::adapter::net {

/// The port every TDP device listens on.
inline constexpr std::uint16_t kTdpPort = 20002;

/// What a device says about itself. Behind a d-pointer, see
/// phi/adapter/v1/channel.h for the rule.
class TdpAnswer
{
public:
    TdpAnswer();
    TdpAnswer(const TdpAnswer &other);
    TdpAnswer(TdpAnswer &&other) noexcept;
    TdpAnswer &operator=(const TdpAnswer &other);
    TdpAnswer &operator=(TdpAnswer &&other) noexcept;
    ~TdpAnswer();

    /// `device_type`, such as "HOMEWIFISYSTEM".
    const v1::Utf8String &deviceType() const noexcept;
    TdpAnswer &setDeviceType(v1::Utf8String deviceType);
    /// `device_model`, such as "X55".
    const v1::Utf8String &model() const noexcept;
    TdpAnswer &setModel(v1::Utf8String model);
    /// "master" or another mesh role; empty when the device names none.
    const v1::Utf8String &role() const noexcept;
    TdpAnswer &setRole(v1::Utf8String role);
    /// Lower case with colons (`3c:64:cf:86:50:f8`); empty when the answer has none.
    const v1::Utf8String &mac() const noexcept;
    TdpAnswer &setMac(v1::Utf8String mac);
    /// The name the owner gave the mesh, already base64-decoded.
    const v1::Utf8String &groupName() const noexcept;
    TdpAnswer &setGroupName(v1::Utf8String groupName);
    /// The address the device reports for itself; empty when it names none.
    const v1::Utf8String &ip() const noexcept;
    TdpAnswer &setIp(v1::Utf8String ip);

private:
    struct Private;
    const Private &p() const noexcept;
    Private &m();
    std::unique_ptr<Private> d;
};

/// A probe with a throwaway RSA key (made once per process) and a random
/// serial: what a factory sends to port 20002.
std::string tdpProbePacket();

/// The same with the key and the serial given, so the bytes can be pinned.
/// Header: version 2, opcode 1, body length, flags 0x11, serial, CRC-32 of
/// the whole datagram (computed with the CRC field holding 0x5A6B7C8D).
std::string tdpProbePacket(std::string_view publicKeyPem, std::uint32_t serial);

/// Reads an answer datagram. False, with `error`, when it is too short, not
/// version 2, or its body is not a JSON object with a "result" object.
/// `out` and `error` may be null.
bool parseTdpAnswer(std::string_view packet, TdpAnswer *out, v1::Utf8String *error);

/// The candidate for an answer: externalId = MAC (the address when there is
/// none), label = "model (group)", host and ip = the reported address (or
/// `fromIp`, the datagram's sender), meta {deviceType, role, groupName}.
v1::DiscoveryCandidate tdpCandidateFor(const TdpAnswer &answer, const std::string &fromIp);

} // namespace phicore::adapter::net
