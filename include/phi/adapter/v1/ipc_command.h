#pragma once

#include <cstdint>

namespace phicore::adapter::v1 {

enum class IpcCommand : std::uint16_t {
    // Sync (core -> adapter, no response)
    SyncAdapterBootstrap = 0x0101,
    // Scope is resolved by payload `externalId`:
    //   externalId == ""  -> factory scope
    //   externalId != ""  -> instance scope
    SyncAdapterConfigChanged = 0x0102,
    SyncAdapterInstanceRemoved = 0x0103,
    /// The household deleted one of the instance's devices; the adapter
    /// forgets it (a router drops the client from its tracked list) so the
    /// next report does not bring it back.
    SyncDeviceRemoved = 0x0104,

    // Cmd (core -> adapter, always followed by Result*)
    CmdChannelInvoke = 0x0201,
    CmdAdapterActionInvoke = 0x0202,
    CmdDeviceNameUpdate = 0x0203,
    CmdDeviceEffectInvoke = 0x0204,
    CmdSceneInvoke = 0x0205,
    CmdAdaptersStreamStart = 0x0206,
    CmdAdaptersStreamStop = 0x0207,
    /// Rename a room, a group or a scene. One command for the three because
    /// the payload is the same for all of them - which of them it is rides in
    /// `kind`. Devices keep CmdDeviceNameUpdate above.
    CmdEntityNameUpdate = 0x0208,
    /// Invoke an imperative playback operation on a media-player device.
    CmdMediaPlayerInvoke = 0x0209,
    /// Ask for where a camera's picture comes from (`camera.live`); answered
    /// with an ActionResponse of type Data, see StreamSourceRequest.
    CmdStreamSourceRequest = 0x020A,
    /// Open the network for joining / start looking for candidates, for
    /// `durationS`; answered at once, the window reports by PairingState.
    CmdPairingStart = 0x020B,
    CmdPairingStop = 0x020C,
    /// Pair one reported candidate. Answered when the pairing has started;
    /// how it goes follows as PairingProgress under `pairingId`.
    CmdPairCandidate = 0x020D,
    /// Pair the device behind a setup code no candidate was seen for; like
    /// CmdPairCandidate otherwise.
    CmdSetupCode = 0x020E,

    // Response/Event (adapter -> core)
    ResponseFactoryDescriptor = 0x1001,
    EventFactoryDescriptorUpdated = 0x1002,
    EventAdapterMetaUpdated = 0x1003,
    EventConnectionStateChanged = 0x1004,
    EventLog = 0x1005,
    /// Whether the instance's chosen topology works ("local chosen, the
    /// redirect is missing"); core keeps it in memory and shows it.
    EventTopologyState = 0x1006,

    EventDeviceUpdated = 0x1101,
    EventDeviceRemoved = 0x1102,

    EventChannelUpdated = 0x1201,
    EventChannelStateUpdated = 0x1202,

    EventRoomUpdated = 0x1301,
    EventRoomRemoved = 0x1302,

    EventGroupUpdated = 0x1401,
    EventGroupRemoved = 0x1402,

    EventSceneUpdated = 0x1501,
    EventSceneRemoved = 0x1502,
    EventStreamOpen = 0x1601,
    EventStreamData = 0x1602,
    EventStreamError = 0x1603,
    EventStreamEnd = 0x1604,

    EventDeviceCandidateUpdated = 0x1701,
    EventDeviceCandidateRemoved = 0x1702,
    EventPairingState = 0x1703,
    EventPairingProgress = 0x1704,

    // Result (adapter -> core, correlated response to Cmd*)
    ResultCmd = 0x2001,
    ResultAction = 0x2002,
};

[[nodiscard]] constexpr std::uint16_t toUint16(IpcCommand value) noexcept
{
    return static_cast<std::uint16_t>(value);
}

[[nodiscard]] constexpr bool isSyncCommand(IpcCommand value) noexcept
{
    const std::uint16_t raw = toUint16(value);
    return raw >= 0x0100 && raw < 0x0200;
}

[[nodiscard]] constexpr bool isCmdCommand(IpcCommand value) noexcept
{
    const std::uint16_t raw = toUint16(value);
    return raw >= 0x0200 && raw < 0x1000;
}

[[nodiscard]] constexpr bool isEventCommand(IpcCommand value) noexcept
{
    const std::uint16_t raw = toUint16(value);
    return raw >= 0x1000 && raw < 0x2000;
}

[[nodiscard]] constexpr bool isResultCommand(IpcCommand value) noexcept
{
    const std::uint16_t raw = toUint16(value);
    return raw >= 0x2000 && raw < 0x3000;
}

} // namespace phicore::adapter::v1
