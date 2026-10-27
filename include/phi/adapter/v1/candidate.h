#pragma once

#include <cstdint>
#include <memory>

#include "phi/adapter/v1/types.h"

namespace phicore::adapter::v1 {

// A device the adapter sees but has not paired: a Matter device announcing
// itself for commissioning, a BLE sensor, a Tuya plug in pairing mode. Core
// keeps candidates in memory only and lists them under "Add"; the household
// picks one and core sends PairCandidate. Behind a d-pointer, see channel.h
// for the rule.
class DeviceCandidate
{
public:
    DeviceCandidate();
    DeviceCandidate(const DeviceCandidate &other);
    DeviceCandidate(DeviceCandidate &&other) noexcept;
    DeviceCandidate &operator=(const DeviceCandidate &other);
    DeviceCandidate &operator=(DeviceCandidate &&other) noexcept;
    ~DeviceCandidate();

    /// Stable while the candidate is visible: a BLE address, a Matter
    /// discriminator, a vendor's device id. Unique within the instance.
    const Utf8String &id() const noexcept;
    DeviceCandidate &setId(Utf8String id);
    const Utf8String &name() const noexcept;
    DeviceCandidate &setName(Utf8String name);
    /// The adapter's best guess; Unknown is fine.
    DeviceClass deviceClass() const noexcept;
    DeviceCandidate &setDeviceClass(DeviceClass deviceClass);
    const Utf8String &manufacturer() const noexcept;
    DeviceCandidate &setManufacturer(Utf8String manufacturer);
    const Utf8String &model() const noexcept;
    DeviceCandidate &setModel(Utf8String model);
    Radio radio() const noexcept;
    DeviceCandidate &setRadio(Radio radio);
    /// dBm as heard; 0 when unknown.
    int rssi() const noexcept;
    DeviceCandidate &setRssi(int rssi);
    /// One of the adapter's own rooms the candidate is probably in, when the
    /// adapter knows (the room of the proxy that heard it loudest).
    const ExternalId &roomExternalId() const noexcept;
    DeviceCandidate &setRoomExternalId(ExternalId roomExternalId);
    PairingNeed needs() const noexcept;
    DeviceCandidate &setNeeds(PairingNeed needs);
    /// Which code, when needs() is SetupCode.
    SetupCodeScheme setupCodeScheme() const noexcept;
    DeviceCandidate &setSetupCodeScheme(SetupCodeScheme scheme);
    /// What the household does, in English ("Hold the button for 5 s until
    /// the light blinks"); shown beside the candidate.
    const Utf8String &instruction() const noexcept;
    DeviceCandidate &setInstruction(Utf8String instruction);
    /// After how long without a fresh report core drops the candidate; 0
    /// keeps it until CandidateRemoved.
    std::int64_t expiresInMs() const noexcept;
    DeviceCandidate &setExpiresInMs(std::int64_t expiresInMs);
    /// The LAN MAC, normalised like Device::setMac: core folds candidates of
    /// one MAC from several instances (a router and a mesh) into one row and
    /// hides one that already belongs to a device.
    const Utf8String &mac() const noexcept;
    DeviceCandidate &setMac(Utf8String mac);
    const Utf8String &ipv4() const noexcept;
    DeviceCandidate &setIpv4(Utf8String ipv4);
    const JsonText &metaJson() const noexcept;
    DeviceCandidate &setMetaJson(JsonText metaJson);
    /// What a stand-in needs for this device (ROADMAP "Cloud binding and
    /// network roles"): the device dials `port` over `protocol` and has to land
    /// on this instance's `localPort` (1024-65535). Set it on the candidate,
    /// together with the MAC: when the household adds the device, core has the
    /// host redirect it - never before, and never on the adapter's say-so
    /// alone. Port 0 (the default) asks for nothing.
    const Utf8String &redirectProtocol() const noexcept;
    int redirectPort() const noexcept;
    int redirectLocalPort() const noexcept;
    DeviceCandidate &setRedirect(int port, int localPort, Utf8String protocol = "tcp");

private:
    struct Private;
    const Private &p() const noexcept;
    Private &m();
    std::unique_ptr<Private> d;
};

} // namespace phicore::adapter::v1
