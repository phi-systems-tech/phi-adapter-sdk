#include "phi/adapter/sdk/network_clients.h"

#include <phi/runtime/str.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <map>
#include <set>
#include <utility>

namespace phicore::adapter::sdk {

namespace v1 = phicore::adapter::v1;

namespace {

// The state file: a header line, then one client per line, tab-separated.
// Plain text because the SDK carries no JSON library; names lose their tabs
// and line breaks on the way in.
constexpr std::string_view kStateHeader = "phi-network-clients 1";

struct Known {
    std::string name;
    std::string ipv4;
    std::string ipv6;
    std::string host;
    std::string accessPoint;
    v1::WifiBand band = v1::WifiBand::Unknown;
    std::optional<int> rssiDbm;
    std::optional<bool> blocked;
    v1::NetworkClientType type = v1::NetworkClientType::Unknown;
    bool guest = false;
    bool online = false;
    std::int64_t lastSeenMs = 0;
    /// Ever had a signal or an access point: the channel stays while it is
    /// gone (a report is never smaller than the one before).
    bool hadSignal = false;
    bool hadAccessPoint = false;
};

std::string flat(std::string_view text)
{
    std::string out(text);
    for (char &c : out)
        if (c == '\t' || c == '\n' || c == '\r')
            c = ' ';
    return out;
}

std::vector<std::string_view> splitTabs(std::string_view line)
{
    std::vector<std::string_view> out;
    std::size_t start = 0;
    while (true) {
        const std::size_t tab = line.find('\t', start);
        out.push_back(line.substr(start, tab == std::string_view::npos ? std::string_view::npos : tab - start));
        if (tab == std::string_view::npos)
            break;
        start = tab + 1;
    }
    return out;
}

std::int64_t toInt64(std::string_view text, std::int64_t fallback)
{
    std::int64_t value = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    return ec == std::errc() && end == text.data() + text.size() ? value : fallback;
}

bool wireless(v1::WifiBand band)
{
    return band == v1::WifiBand::Band24 || band == v1::WifiBand::Band5 || band == v1::WifiBand::Band6;
}

/// The candidate's class guess: what a household would track as a client.
v1::DeviceClass classGuess(v1::NetworkClientType type)
{
    switch (type) {
    case v1::NetworkClientType::Phone:
    case v1::NetworkClientType::Tablet:
    case v1::NetworkClientType::Computer:
    case v1::NetworkClientType::Console:
        return v1::DeviceClass::NetworkClient;
    case v1::NetworkClientType::Tv:
    case v1::NetworkClientType::MediaPlayer:
        return v1::DeviceClass::MediaPlayer;
    default:
        return v1::DeviceClass::Unknown;
    }
}

std::string jsonEscaped(std::string_view text)
{
    std::string out;
    out.reserve(text.size() + 2);
    out.push_back('"');
    for (const char c : text) {
        const auto u = static_cast<unsigned char>(c);
        if (c == '"' || c == '\\') {
            out.push_back('\\');
            out.push_back(c);
        } else if (u < 0x20) {
            static constexpr char kHex[] = "0123456789abcdef";
            out += "\\u00";
            out.push_back(kHex[u >> 4]);
            out.push_back(kHex[u & 0x0f]);
        } else {
            out.push_back(c);
        }
    }
    out.push_back('"');
    return out;
}

} // namespace

struct ClientTracker::Private {
    Sink sink;
    Settings settings;
    std::vector<std::string> tracked;
    std::map<std::string, Known> known;
    /// Tracked clients whose Presence is true now.
    std::set<std::string> present;
    std::map<std::string, std::string> reportedDescriptors;
    std::map<std::string, v1::ScalarValue> reportedValues;

    bool tracks(const std::string &mac) const
    {
        return std::find(tracked.begin(), tracked.end(), mac) != tracked.end();
    }

    void save() const
    {
        if (!sink.save)
            return;
        std::string out(kStateHeader);
        out.push_back('\n');
        for (const auto &[mac, k] : known) {
            out += mac;
            out.push_back('\t');
            out += std::to_string(k.lastSeenMs);
            out.push_back('\t');
            out += std::to_string(static_cast<int>(k.type));
            out.push_back('\t');
            out += k.guest ? "1" : "0";
            out.push_back('\t');
            out += std::to_string(static_cast<int>(k.band));
            out.push_back('\t');
            out += std::string(k.hadSignal ? "s" : "") + (k.hadAccessPoint ? "a" : "");
            out.push_back('\t');
            out += flat(k.name);
            out.push_back('\t');
            out += flat(k.ipv4);
            out.push_back('\t');
            out += flat(k.accessPoint);
            out.push_back('\n');
        }
        sink.save(out);
    }

    void announce(const std::string &mac, const Known &k)
    {
        v1::Device device;
        device.setExternalId(mac);
        device.setName(k.name.empty() ? mac : k.name);
        device.setDeviceClass(v1::DeviceClass::NetworkClient);
        if (wireless(k.band) || k.hadSignal)
            device.setFlags(device.flags() | v1::DeviceFlag::Wireless);
        device.setMac(mac);
        if (!k.ipv4.empty())
            device.setIpv4(k.ipv4);
        if (!k.ipv6.empty())
            device.setIpv6(k.ipv6);
        if (!k.host.empty())
            device.setHost(k.host);

        v1::ChannelList channels;
        v1::Channel presence;
        presence.setExternalId(kClientChannelPresence);
        presence.setName("Presence");
        presence.setKind(v1::ChannelKind::Presence);
        presence.setDataType(v1::ChannelDataType::Bool);
        presence.setFlags(v1::kChannelFlagDefaultRead);
        channels.push_back(std::move(presence));

        if (k.hadAccessPoint) {
            v1::Channel ap;
            ap.setExternalId(kClientChannelAccessPoint);
            ap.setName("Access point");
            ap.setKind(v1::ChannelKind::AccessPoint);
            ap.setDataType(v1::ChannelDataType::String);
            ap.setFlags(v1::kChannelFlagDefaultRead);
            channels.push_back(std::move(ap));
        }
        if (k.band != v1::WifiBand::Unknown) {
            v1::Channel band;
            band.setExternalId(kClientChannelBand);
            band.setName("Band");
            band.setKind(v1::ChannelKind::WifiBand);
            band.setDataType(v1::ChannelDataType::Enum);
            band.setFlags(v1::kChannelFlagDefaultRead);
            channels.push_back(std::move(band));
        }
        if (k.hadSignal) {
            v1::Channel rssi;
            rssi.setExternalId(kClientChannelRssi);
            rssi.setName("RSSI");
            rssi.setKind(v1::ChannelKind::SignalStrength);
            rssi.setDataType(v1::ChannelDataType::Float);
            // One row a poll per client says little a household reads back;
            // the room estimate uses the live value.
            rssi.setFlags(v1::kChannelFlagDefaultRead | v1::ChannelFlag::NoHistory);
            rssi.setUnit("dBm");
            rssi.setRange(-100.0, 0.0);
            channels.push_back(std::move(rssi));
        }
        if (settings.canBlock) {
            v1::Channel access;
            access.setExternalId(kClientChannelNetworkAccess);
            access.setName("Network access");
            access.setKind(v1::ChannelKind::NetworkAccess);
            access.setDataType(v1::ChannelDataType::Bool);
            access.setFlags(v1::kChannelFlagDefaultWrite);
            channels.push_back(std::move(access));
        }

        const std::string fingerprint = device.name() + '\x1f' + k.ipv4 + '\x1f' + k.ipv6 + '\x1f' + k.host
            + '\x1f' + std::to_string(channels.size()) + (k.hadAccessPoint ? "a" : "")
            + (k.band != v1::WifiBand::Unknown ? "b" : "") + (k.hadSignal ? "s" : "")
            + (settings.canBlock ? "x" : "") + (device.flags() == v1::DeviceFlag::Wireless ? "w" : "");
        auto it = reportedDescriptors.find(mac);
        if (it != reportedDescriptors.end() && it->second == fingerprint)
            return;
        reportedDescriptors[mac] = fingerprint;
        // A channel that is new to core drops a value sent before it knew it;
        // say every value again after a new descriptor.
        forgetValues(mac);
        if (sink.device)
            sink.device(device, channels);
    }

    void report(const std::string &mac, const char *channel, const v1::ScalarValue &value)
    {
        const std::string key = mac + '\x1f' + channel;
        auto it = reportedValues.find(key);
        if (it != reportedValues.end() && it->second == value)
            return;
        reportedValues[key] = value;
        if (sink.value)
            sink.value(mac, channel, value);
    }

    void forgetValues(const std::string &mac)
    {
        const std::string prefix = mac + '\x1f';
        for (auto it = reportedValues.begin(); it != reportedValues.end();)
            it = it->first.rfind(prefix, 0) == 0 ? reportedValues.erase(it) : std::next(it);
    }

    /// The tracked client's device and values as they stand at `nowMs`.
    /// True when Presence changed.
    bool publish(const std::string &mac, std::int64_t nowMs)
    {
        auto it = known.find(mac);
        Known empty;
        Known &k = it != known.end() ? it->second : empty;
        announce(mac, k);
        // Seen within the delay counts as here, across a restart too: the
        // state file keeps when, so a dozing phone does not flap at a start.
        const bool here = k.online || (k.lastSeenMs > 0 && nowMs - k.lastSeenMs < settings.awayDelayMs);
        const bool changed = here != (present.count(mac) > 0);
        if (here)
            present.insert(mac);
        else
            present.erase(mac);
        report(mac, kClientChannelPresence, here);
        if (k.hadAccessPoint)
            report(mac, kClientChannelAccessPoint, v1::Utf8String(here ? k.accessPoint : std::string()));
        if (k.band != v1::WifiBand::Unknown)
            report(mac, kClientChannelBand, static_cast<std::int64_t>(k.band));
        if (k.hadSignal && k.online && k.rssiDbm)
            report(mac, kClientChannelRssi, static_cast<double>(*k.rssiDbm));
        if (settings.canBlock && k.blocked)
            report(mac, kClientChannelNetworkAccess, !*k.blocked);
        return changed;
    }

    bool offerable(const std::string &mac, const Known &k, std::int64_t nowMs) const
    {
        if (tracks(mac))
            return false;
        if (k.guest && !settings.showGuests)
            return false;
        if (settings.ignorePrivateMacs && ClientTracker::isPrivateMac(mac))
            return false;
        return k.online || nowMs - k.lastSeenMs < settings.candidateMaxAgeMs;
    }

    v1::DeviceCandidate candidateFor(const std::string &mac, const Known &k, std::int64_t expiresInMs,
                                     std::int64_t nowMs) const
    {
        v1::DeviceCandidate c;
        c.setId(mac);
        c.setName(k.name.empty() ? mac : k.name);
        c.setDeviceClass(classGuess(k.type));
        c.setRadio(k.band == v1::WifiBand::Wired ? v1::Radio::Lan
                   : wireless(k.band)            ? v1::Radio::Wifi
                                                 : v1::Radio::Unknown);
        if (k.rssiDbm)
            c.setRssi(*k.rssiDbm);
        c.setNeeds(v1::PairingNeed::None);
        c.setExpiresInMs(expiresInMs);
        c.setMac(mac);
        if (!k.ipv4.empty())
            c.setIpv4(k.ipv4);
        // What the client view sorts and labels by: phones first, where it
        // was seen, whether it is in the network now.
        std::string meta = "{\"networkClient\":{\"type\":"
            + std::to_string(static_cast<int>(k.type)) + ",\"guest\":" + (k.guest ? "true" : "false")
            + ",\"online\":" + (k.online ? "true" : "false") + ",\"accessPoint\":" + jsonEscaped(k.accessPoint)
            + ",\"lastSeenAgoMs\":" + std::to_string(k.online ? 0 : std::max<std::int64_t>(0, nowMs - k.lastSeenMs))
            + "}}";
        c.setMetaJson(std::move(meta));
        return c;
    }
};

ClientTracker::ClientTracker() : d(std::make_unique<Private>()) {}
ClientTracker::~ClientTracker() = default;

void ClientTracker::setSink(Sink sink) { d->sink = std::move(sink); }

void ClientTracker::setSettings(const Settings &settings)
{
    const bool blockChanged = settings.canBlock != d->settings.canBlock;
    d->settings = settings;
    if (d->settings.awayDelayMs < 0)
        d->settings.awayDelayMs = 0;
    if (blockChanged)
        d->reportedDescriptors.clear();
}

const ClientTracker::Settings &ClientTracker::settings() const { return d->settings; }

void ClientTracker::setTracked(const std::vector<std::string> &macs)
{
    std::vector<std::string> next;
    for (const std::string &raw : macs) {
        const std::string mac = normalizedMac(raw);
        if (!mac.empty() && std::find(next.begin(), next.end(), mac) == next.end())
            next.push_back(mac);
    }
    for (const std::string &mac : d->tracked)
        if (std::find(next.begin(), next.end(), mac) == next.end()) {
            d->present.erase(mac);
            d->reportedDescriptors.erase(mac);
            d->forgetValues(mac);
        }
    d->tracked = std::move(next);
}

const std::vector<std::string> &ClientTracker::tracked() const { return d->tracked; }

bool ClientTracker::isTracked(std::string_view mac) const { return d->tracks(normalizedMac(mac)); }

void ClientTracker::restore(std::string_view state)
{
    std::size_t start = 0;
    bool header = true;
    while (start < state.size()) {
        std::size_t end = state.find('\n', start);
        if (end == std::string_view::npos)
            end = state.size();
        const std::string_view line = state.substr(start, end - start);
        start = end + 1;
        if (header) {
            if (line != kStateHeader)
                return;
            header = false;
            continue;
        }
        const std::vector<std::string_view> f = splitTabs(line);
        if (f.size() < 9)
            continue;
        const std::string mac = normalizedMac(f[0]);
        if (mac.empty())
            continue;
        Known &k = d->known[mac];
        k.lastSeenMs = toInt64(f[1], 0);
        k.type = static_cast<v1::NetworkClientType>(toInt64(f[2], 0));
        k.guest = f[3] == "1";
        k.band = static_cast<v1::WifiBand>(toInt64(f[4], 0));
        k.hadSignal = f[5].find('s') != std::string_view::npos;
        k.hadAccessPoint = f[5].find('a') != std::string_view::npos;
        k.name = std::string(f[6]);
        k.ipv4 = std::string(f[7]);
        k.accessPoint = std::string(f[8]);
        k.online = false;
    }
}

void ClientTracker::update(const std::vector<NetworkClientSighting> &seen, std::int64_t nowMs)
{
    bool dirty = false;
    std::set<std::string> online;
    for (const NetworkClientSighting &s : seen) {
        const std::string mac = normalizedMac(s.mac);
        if (mac.empty())
            continue;
        auto [it, inserted] = d->known.try_emplace(mac);
        Known &k = it->second;
        dirty = dirty || inserted;
        if (!s.name.empty() && s.name != k.name) {
            k.name = s.name;
            dirty = true;
        }
        if (!s.ipv4.empty() && s.ipv4 != k.ipv4) {
            k.ipv4 = s.ipv4;
            dirty = true;
        }
        if (!s.ipv6.empty())
            k.ipv6 = s.ipv6;
        if (!s.host.empty())
            k.host = s.host;
        if (s.type != v1::NetworkClientType::Unknown && s.type != k.type) {
            k.type = s.type;
            dirty = true;
        }
        if (s.guest != k.guest) {
            k.guest = s.guest;
            dirty = true;
        }
        if (s.blocked)
            k.blocked = s.blocked;
        if (!s.online)
            continue;
        online.insert(mac);
        k.lastSeenMs = nowMs;
        if (s.band != v1::WifiBand::Unknown && s.band != k.band) {
            k.band = s.band;
            dirty = true;
        }
        k.rssiDbm = s.rssiDbm;
        if (s.rssiDbm && !k.hadSignal) {
            k.hadSignal = true;
            dirty = true;
        }
        if (!s.accessPoint.empty()) {
            if (!k.hadAccessPoint || s.accessPoint != k.accessPoint)
                dirty = true;
            k.accessPoint = s.accessPoint;
            k.hadAccessPoint = true;
        }
    }
    for (auto it = d->known.begin(); it != d->known.end();) {
        Known &k = it->second;
        k.online = online.count(it->first) > 0;
        if (!k.online && !d->tracks(it->first) && nowMs - k.lastSeenMs > d->settings.forgetAfterMs) {
            it = d->known.erase(it);
            dirty = true;
            continue;
        }
        ++it;
    }
    for (const std::string &mac : d->tracked)
        dirty = d->publish(mac, nowMs) || dirty;
    if (dirty)
        d->save();
}

void ClientTracker::listCandidates(std::int64_t expiresInMs, std::int64_t nowMs)
{
    if (!d->sink.candidate)
        return;
    for (const auto &[mac, k] : d->known)
        if (d->offerable(mac, k, nowMs))
            d->sink.candidate(d->candidateFor(mac, k, expiresInMs, nowMs));
}

bool ClientTracker::track(std::string_view raw, std::int64_t nowMs)
{
    const std::string mac = normalizedMac(raw);
    if (mac.empty() || d->known.find(mac) == d->known.end())
        return false;
    if (!d->tracks(mac))
        d->tracked.push_back(mac);
    if (d->sink.candidateRemoved)
        d->sink.candidateRemoved(mac);
    d->publish(mac, nowMs);
    d->save();
    return true;
}

bool ClientTracker::untrack(std::string_view raw)
{
    const std::string mac = normalizedMac(raw);
    auto it = std::find(d->tracked.begin(), d->tracked.end(), mac);
    if (it == d->tracked.end())
        return false;
    d->tracked.erase(it);
    d->present.erase(mac);
    d->reportedDescriptors.erase(mac);
    d->forgetValues(mac);
    return true;
}

void ClientTracker::forgetReported()
{
    d->reportedDescriptors.clear();
    d->reportedValues.clear();
}

bool ClientTracker::isPrivateMac(std::string_view mac)
{
    const std::string normal = normalizedMac(mac);
    if (normal.empty())
        return false;
    const auto hex = [](char c) { return std::isdigit(static_cast<unsigned char>(c)) ? c - '0' : c - 'a' + 10; };
    return (hex(normal[1]) & 0x2) != 0;
}

std::string ClientTracker::normalizedMac(std::string_view mac)
{
    // Some sources print "00 11 22 aa bb cc"; phi::str takes no inner spaces.
    std::string compact;
    for (const char c : mac)
        if (c != ' ')
            compact.push_back(c);
    const std::string key = phi::str::macKey(compact);
    return key.empty() ? std::string() : phi::str::normalizedMac(key);
}

std::vector<v1::AdapterConfigField> ClientTracker::configFields()
{
    using Type = v1::AdapterConfigFieldType;
    v1::AdapterConfigField away(kClientMetaAwayDelayS, Type::Integer, "Away after (s)",
                                "How long a tracked client may be unseen before it counts as gone; "
                                "phones switch their WLAN off while they sleep.");
    away.setDefaultValue(std::int64_t{300}).setMinValue(0).setMaxValue(3600).setStep(30);
    away.layout().setControlWidth(v1::AdapterConfigSize::Narrow);
    v1::AdapterConfigField guests(kClientMetaShowGuests, Type::Boolean, "Offer guest clients",
                                  "List clients of the guest network under Add device.");
    guests.setDefaultValue(false);
    v1::AdapterConfigField privateMacs(kClientMetaIgnorePrivateMacs, Type::Boolean, "Hide private addresses",
                                       "Do not list clients with a private (randomised) MAC under Add device.");
    privateMacs.setDefaultValue(false);
    return {away, guests, privateMacs};
}

} // namespace phicore::adapter::sdk
