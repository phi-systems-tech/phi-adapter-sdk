#pragma once

#include <memory>
#include <string>
#include <vector>

#include "phi/adapter/v1/types.h"

namespace phicore::adapter::v1 {

// An effect a device offers (identify, a light show, a sound). Behind a
// d-pointer like every entity, see channel.h for the rule.
class DeviceEffectDescriptor
{
public:
    DeviceEffectDescriptor();
    DeviceEffectDescriptor(const DeviceEffectDescriptor &other);
    DeviceEffectDescriptor(DeviceEffectDescriptor &&other) noexcept;
    DeviceEffectDescriptor &operator=(const DeviceEffectDescriptor &other);
    DeviceEffectDescriptor &operator=(DeviceEffectDescriptor &&other) noexcept;
    ~DeviceEffectDescriptor();

    DeviceEffect effect() const noexcept;
    DeviceEffectDescriptor &setEffect(DeviceEffect effect);
    const Utf8String &id() const noexcept;
    DeviceEffectDescriptor &setId(Utf8String id);
    const Utf8String &label() const noexcept;
    DeviceEffectDescriptor &setLabel(Utf8String label);
    const Utf8String &description() const noexcept;
    DeviceEffectDescriptor &setDescription(Utf8String description);
    bool requiresParams() const noexcept;
    DeviceEffectDescriptor &setRequiresParams(bool requiresParams);
    const JsonText &metaJson() const noexcept;
    DeviceEffectDescriptor &setMetaJson(JsonText metaJson);

private:
    struct Private;
    const Private &p() const noexcept;
    Private &m();
    std::unique_ptr<Private> d;
};

using DeviceEffectDescriptorList = std::vector<DeviceEffectDescriptor>;

// A device as the adapter reports it; its channels travel beside it in the
// report. Behind a d-pointer, see channel.h for the rule.
class Device
{
public:
    Device();
    Device(const Device &other);
    Device(Device &&other) noexcept;
    Device &operator=(const Device &other);
    Device &operator=(Device &&other) noexcept;
    ~Device();

    const Utf8String &name() const noexcept;
    Device &setName(Utf8String name);
    DeviceClass deviceClass() const noexcept;
    Device &setDeviceClass(DeviceClass deviceClass);
    DeviceFlags flags() const noexcept;
    Device &setFlags(DeviceFlags flags);
    /// Ors into what is set.
    Device &addFlags(DeviceFlags flags);
    const ExternalId &externalId() const noexcept;
    Device &setExternalId(ExternalId externalId);
    const Utf8String &manufacturer() const noexcept;
    Device &setManufacturer(Utf8String manufacturer);
    const Utf8String &firmware() const noexcept;
    Device &setFirmware(Utf8String firmware);
    const Utf8String &model() const noexcept;
    Device &setModel(Utf8String model);
    const JsonText &metaJson() const noexcept;
    Device &setMetaJson(JsonText metaJson);
    /// Where the device is on the network, one spelling each so core,
    /// phi-hostd (firewall, redirects) and the cloud light can rely on them.
    /// Set what the adapter knows, leave the rest empty; every report
    /// overwrites them. The setters normalise (a MAC in any separator style
    /// becomes `aa:bb:cc:dd:ee:ff`, an IPv6 address its canonical form); a
    /// value that does not parse makes sendDeviceUpdated fail with it named.
    const Utf8String &ipv4() const noexcept;
    Device &setIpv4(Utf8String ipv4);
    /// One address, the one phi should use: global or ULA before link-local.
    const Utf8String &ipv6() const noexcept;
    Device &setIpv6(Utf8String ipv6);
    /// A name the device answers to (`smartbox.local`), not an address.
    const Utf8String &host() const noexcept;
    Device &setHost(Utf8String host);
    /// The LAN (Wi-Fi or Ethernet) MAC.
    const Utf8String &mac() const noexcept;
    Device &setMac(Utf8String mac);
    /// The Bluetooth LE address, for matching proxy sightings to the device.
    /// A device that rotates resolvable private addresses has none stable;
    /// leave it empty, its IRK stays with the adapter.
    const Utf8String &bleAddress() const noexcept;
    Device &setBleAddress(Utf8String bleAddress);
    MediaPlayerFeatures mediaPlayerFeatures() const noexcept;
    Device &setMediaPlayerFeatures(MediaPlayerFeatures features);
    Device &addMediaPlayerFeatures(MediaPlayerFeatures features);
    CameraFeatures cameraFeatures() const noexcept;
    Device &setCameraFeatures(CameraFeatures features);
    Device &addCameraFeatures(CameraFeatures features);
    const DeviceEffectDescriptorList &effects() const noexcept;
    Device &setEffects(DeviceEffectDescriptorList effects);
    Device &addEffect(DeviceEffectDescriptor effect);

private:
    struct Private;
    const Private &p() const noexcept;
    Private &m();
    std::unique_ptr<Private> d;
};

using DeviceList = std::vector<Device>;

} // namespace phicore::adapter::v1
