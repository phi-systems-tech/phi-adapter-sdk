#pragma once
// The words every blaster adapter uses for the same two things, so the UI,
// the app and a person's habits carry over from Broadlink to a Tuya or
// Zigbee blaster: making a device that has no body of its own, and teaching
// it a key from the remote in the drawer.
//
// Both are adapter actions on the instance (AdapterActionPlacement::Card)
// with a form; an adapter declares them with these ids and reads the params
// with these keys. The form's choices (which device, which keys it has) are
// the adapter's, filled through the result's form patch.

namespace phicore::adapter::ir::actions {

/// "New device": a device behind this blaster. Params: kName, kDeviceClass
/// (a DeviceClass name), optionally kBrand and kModel for a catalogue entry
/// that pre-fills the keys.
inline constexpr const char kNewDevice[] = "ir.device.new";
/// "Learn key": the blaster listens, the person presses the key on the
/// remote. Params: kDevice (the device's external id), kKey (the key's
/// name; an existing key is replaced). The result carries the learnt code
/// as kCode, in timingsText() form, and the key is a RemoteKey channel of
/// the device from then on.
inline constexpr const char kLearnKey[] = "ir.key.learn";
/// "Send code": a code by its timings, for a test or a catalogue import.
/// Params: kDevice or none, kCode, optionally kCarrierHz.
inline constexpr const char kSendCode[] = "ir.code.send";

inline constexpr const char kName[] = "name";
inline constexpr const char kDeviceClass[] = "deviceClass";
inline constexpr const char kBrand[] = "brand";
inline constexpr const char kModel[] = "model";
inline constexpr const char kDevice[] = "device";
inline constexpr const char kKey[] = "key";
inline constexpr const char kCode[] = "code";
inline constexpr const char kCarrierHz[] = "carrierHz";

/// What a key channel's external id looks like: `key:` and the key's name,
/// so a device's keys and its state channels never collide.
inline constexpr const char kKeyChannelPrefix[] = "key:";

} // namespace phicore::adapter::ir::actions
