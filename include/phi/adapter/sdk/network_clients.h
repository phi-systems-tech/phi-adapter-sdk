#pragma once

// A network's clients, tracked the same way by every router and mesh adapter
// (ROADMAP "SDK 0.29: network clients").
//
// FRITZ!Box, TP-Link Deco, Zyxel, UniFi all see the phones and laptops in the
// house, and every adapter that did something with that did it its own way: a
// picker in its form, its own channel names, its own idea of when a phone that
// dozed off is gone. This holds the part that has to be the same everywhere:
//
//  - which clients the household picked (`trackedClients` in the adapter meta),
//  - what the instance has seen, kept in its state directory, offered as
//    candidates when core asks (pairing style Listing),
//  - presence with an away delay: arrival at once, gone only after the delay,
//    because a phone's WLAN sleeps,
//  - the device (class NetworkClient) and its fixed channel set, reported only
//    when something changed.
//
// What the adapter keeps: talking to its router, and handing over one snapshot
// per successful poll. A failed poll hands over nothing - not knowing is not
// the same as everyone having left. Like Reachability it has no clock and no
// timer; the caller passes the time.

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "phi/adapter/v1/candidate.h"
#include "phi/adapter/v1/channel.h"
#include "phi/adapter/v1/device.h"
#include "phi/adapter/v1/schema.h"

namespace phicore::adapter::sdk {

/// The fixed channel ids of a network client, the same from every adapter.
inline constexpr const char kClientChannelPresence[] = "presence";
inline constexpr const char kClientChannelRssi[] = "rssi";
inline constexpr const char kClientChannelAccessPoint[] = "access_point";
inline constexpr const char kClientChannelBand[] = "band";
inline constexpr const char kClientChannelNetworkAccess[] = "network_access";

/// The adapter meta keys the tracker's settings live under; configFields()
/// puts them in the instance form.
inline constexpr const char kClientMetaTracked[] = "trackedClients";
inline constexpr const char kClientMetaAwayDelayS[] = "awayDelayS";
inline constexpr const char kClientMetaShowGuests[] = "showGuestClients";
inline constexpr const char kClientMetaIgnorePrivateMacs[] = "ignorePrivateMacs";

/// One client as the router lists it in this poll.
struct NetworkClientSighting {
    /// Any spelling; normalised to `aa:bb:cc:dd:ee:ff`. Required.
    std::string mac;
    std::string name;
    std::string ipv4;
    std::string ipv6;
    std::string host;
    /// The access point or mesh node it is on, by the name the household
    /// knows it ("Living Room"); empty when the router does not say.
    std::string accessPoint;
    phicore::adapter::v1::WifiBand band = phicore::adapter::v1::WifiBand::Unknown;
    /// dBm, when the router reports a signal for it.
    std::optional<int> rssiDbm;
    /// Set when the router can tell whether the client may use the network.
    std::optional<bool> blocked;
    phicore::adapter::v1::NetworkClientType type = phicore::adapter::v1::NetworkClientType::Unknown;
    /// On the guest network.
    bool guest = false;
    /// In the network now. A blocked client the router still lists is handed
    /// over with false, so its NetworkAccess switch stays right.
    bool online = true;
};

class ClientTracker
{
public:
    struct Settings {
        /// How long a tracked client may be unseen before Presence goes false.
        std::int64_t awayDelayMs = 5 * 60 * 1000;
        /// Offer clients of the guest network as candidates.
        bool showGuests = false;
        /// Offer clients with a locally administered (private, randomised)
        /// MAC as candidates.
        bool ignorePrivateMacs = false;
        /// The router can block a client: the NetworkAccess channel is
        /// writable and offered.
        bool canBlock = false;
        /// Candidates are clients seen within this long.
        std::int64_t candidateMaxAgeMs = 7LL * 24 * 60 * 60 * 1000;
        /// A client unseen this long is forgotten (unless tracked).
        std::int64_t forgetAfterMs = 30LL * 24 * 60 * 60 * 1000;
    };

    /// Where the tracker's output goes: the instance's own send* calls, and
    /// writeStateFile for the state. Every member is optional.
    struct Sink {
        std::function<void(const phicore::adapter::v1::Device &, const phicore::adapter::v1::ChannelList &)> device;
        std::function<void(const std::string &deviceExternalId, const std::string &channelExternalId,
                           const phicore::adapter::v1::ScalarValue &value)> value;
        std::function<void(const phicore::adapter::v1::DeviceCandidate &)> candidate;
        std::function<void(const std::string &candidateId)> candidateRemoved;
        /// The text restore() takes back; write it to the state directory.
        std::function<void(const std::string &state)> save;
    };

    ClientTracker();
    ~ClientTracker();
    ClientTracker(const ClientTracker &) = delete;
    ClientTracker &operator=(const ClientTracker &) = delete;

    void setSink(Sink sink);
    void setSettings(const Settings &settings);
    [[nodiscard]] const Settings &settings() const;

    /// The household's picks, from the adapter meta (`trackedClients`).
    void setTracked(const std::vector<std::string> &macs);
    [[nodiscard]] const std::vector<std::string> &tracked() const;
    [[nodiscard]] bool isTracked(std::string_view mac) const;

    /// What save() wrote last time; unknown or broken text is ignored.
    void restore(std::string_view state);

    /// One successful poll: every client the router lists now. Updates what
    /// was seen, applies the away delay, announces and reports the tracked.
    void update(const std::vector<NetworkClientSighting> &seen, std::int64_t nowMs);

    /// PairingStart for an instance with the style Listing: every client seen
    /// recently and not tracked, as a candidate that expires after
    /// `expiresInMs`.
    void listCandidates(std::int64_t expiresInMs, std::int64_t nowMs);

    /// PairCandidate: tracks the client and announces it at once. False when
    /// the id is no client this instance has seen. The caller writes
    /// tracked() to the adapter meta.
    bool track(std::string_view mac, std::int64_t nowMs);
    /// DeviceRemoved: stops tracking. False when it was not tracked. The
    /// caller writes tracked() to the adapter meta.
    bool untrack(std::string_view mac);

    /// After a reconnect: say everything again on the next update.
    void forgetReported();

    /// Whether a MAC is locally administered (the second bit of its first
    /// octet), as phones' private WLAN addresses are.
    [[nodiscard]] static bool isPrivateMac(std::string_view mac);
    /// `aa:bb:cc:dd:ee:ff`, or empty when it is not twelve hex digits.
    [[nodiscard]] static std::string normalizedMac(std::string_view mac);

    /// The instance-form fields every tracking adapter offers: away delay,
    /// guest clients, private MACs. Their keys are the kClientMeta* above.
    [[nodiscard]] static std::vector<phicore::adapter::v1::AdapterConfigField> configFields();

private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace phicore::adapter::sdk
