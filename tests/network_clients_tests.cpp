// A network's clients: what is seen, what is offered, what is tracked, and
// when a phone that dozed off counts as gone.

#include <phi/adapter/testing/check.h>

#include "phi/adapter/sdk/network_clients.h"

#include <map>
#include <string>
#include <vector>

namespace sdk = phicore::adapter::sdk;
namespace v1 = phicore::adapter::v1;
using sdk::ClientTracker;
using sdk::NetworkClientSighting;

namespace {

struct Recorder {
    std::vector<v1::Device> devices;
    std::vector<v1::ChannelList> channelLists;
    std::map<std::string, v1::ScalarValue> values;
    int valueCount = 0;
    std::vector<v1::DeviceCandidate> candidates;
    std::vector<std::string> removed;
    std::string state;
    int saves = 0;

    ClientTracker::Sink sink()
    {
        ClientTracker::Sink s;
        s.device = [this](const v1::Device &d, const v1::ChannelList &c) {
            devices.push_back(d);
            channelLists.push_back(c);
        };
        s.value = [this](const std::string &dev, const std::string &ch, const v1::ScalarValue &v) {
            values[dev + "/" + ch] = v;
            ++valueCount;
        };
        s.candidate = [this](const v1::DeviceCandidate &c) { candidates.push_back(c); };
        s.candidateRemoved = [this](const std::string &id) { removed.push_back(id); };
        s.save = [this](const std::string &text) {
            state = text;
            ++saves;
        };
        return s;
    }
};

NetworkClientSighting phone(bool online = true)
{
    NetworkClientSighting s;
    s.mac = "D8-C8-0C-02-22-44";
    s.name = "Marius iPhone";
    s.ipv4 = "192.168.18.70";
    s.accessPoint = "Living Room";
    s.band = v1::WifiBand::Band5;
    s.rssiDbm = -56;
    s.type = v1::NetworkClientType::Phone;
    s.online = online;
    return s;
}

NetworkClientSighting plug()
{
    NetworkClientSighting s;
    s.mac = "b8:16:5f:30:bb:c8";
    s.name = "plug";
    s.band = v1::WifiBand::Band24;
    s.type = v1::NetworkClientType::Iot;
    return s;
}

bool boolValue(const Recorder &r, const std::string &key, bool *out)
{
    const auto it = r.values.find(key);
    if (it == r.values.end())
        return false;
    const auto *b = std::get_if<bool>(&it->second);
    if (!b)
        return false;
    *out = *b;
    return true;
}

void testMacs()
{
    PHI_CHECK(ClientTracker::normalizedMac("D8-C8-0C-02-22-44") == "d8:c8:0c:02:22:44");
    PHI_CHECK(ClientTracker::normalizedMac("d8c80c022244") == "d8:c8:0c:02:22:44");
    PHI_CHECK(ClientTracker::normalizedMac("not a mac").empty());
    PHI_CHECK(ClientTracker::isPrivateMac("02:00:00:00:00:01"));
    PHI_CHECK(ClientTracker::isPrivateMac("da:a1:19:00:00:01"));
    PHI_CHECK(!ClientTracker::isPrivateMac("d8:c8:0c:02:22:44"));
}

void testUntrackedClientsAreOfferedNotAnnounced()
{
    Recorder r;
    ClientTracker t;
    t.setSink(r.sink());
    t.update({phone(), plug()}, 1000);
    PHI_CHECK(r.devices.empty());
    PHI_CHECK_MSG(r.saves == 1, "saves: %d", r.saves);

    t.listCandidates(120000, 2000);
    PHI_CHECK_MSG(r.candidates.size() == 2, "candidates: %zu", r.candidates.size());
    const v1::DeviceCandidate &c = r.candidates[0].mac() == "d8:c8:0c:02:22:44" ? r.candidates[0] : r.candidates[1];
    PHI_CHECK(c.id() == "d8:c8:0c:02:22:44");
    PHI_CHECK(c.name() == "Marius iPhone");
    PHI_CHECK(c.deviceClass() == v1::DeviceClass::NetworkClient);
    PHI_CHECK(c.radio() == v1::Radio::Wifi);
    PHI_CHECK(c.needs() == v1::PairingNeed::None);
    PHI_CHECK(c.ipv4() == "192.168.18.70");
    PHI_CHECK(c.expiresInMs() == 120000);
    PHI_CHECK(c.metaJson().find("\"type\":1") != std::string::npos);
}

void testTrackAnnouncesAndReports()
{
    Recorder r;
    ClientTracker t;
    t.setSink(r.sink());
    t.update({phone()}, 1000);
    PHI_CHECK(t.track("d8:c8:0c:02:22:44", 1500));
    PHI_CHECK(!t.track("aa:aa:aa:aa:aa:aa", 1500));
    PHI_CHECK(r.removed.size() == 1 && r.removed[0] == "d8:c8:0c:02:22:44");
    PHI_CHECK(t.tracked().size() == 1);
    PHI_CHECK(r.devices.size() == 1);
    const v1::Device &d = r.devices[0];
    PHI_CHECK(d.externalId() == "d8:c8:0c:02:22:44");
    PHI_CHECK(d.deviceClass() == v1::DeviceClass::NetworkClient);
    PHI_CHECK(d.mac() == "d8:c8:0c:02:22:44");
    PHI_CHECK(d.ipv4() == "192.168.18.70");
    PHI_CHECK_MSG(r.channelLists[0].size() == 4, "channels: %zu", r.channelLists[0].size());
    bool here = false;
    PHI_CHECK(boolValue(r, "d8:c8:0c:02:22:44/presence", &here) && here);
    PHI_CHECK(r.values.count("d8:c8:0c:02:22:44/rssi") == 1);

    // A tracked client is no candidate.
    r.candidates.clear();
    t.listCandidates(1000, 2000);
    PHI_CHECK(r.candidates.empty());

    // Nothing changed: nothing said again.
    const int before = r.valueCount;
    const std::size_t devicesBefore = r.devices.size();
    t.update({phone()}, 2000);
    PHI_CHECK(r.devices.size() == devicesBefore);
    PHI_CHECK_MSG(r.valueCount == before, "values %d -> %d", before, r.valueCount);
}

void testAwayDelay()
{
    Recorder r;
    ClientTracker t;
    t.setSink(r.sink());
    ClientTracker::Settings s;
    s.awayDelayMs = 300000;
    t.setSettings(s);
    t.setTracked({"d8:c8:0c:02:22:44"});
    t.update({phone()}, 1000);
    bool here = false;
    PHI_CHECK(boolValue(r, "d8:c8:0c:02:22:44/presence", &here) && here);

    // The phone dozes off: still here within the delay.
    t.update({}, 121000);
    PHI_CHECK(boolValue(r, "d8:c8:0c:02:22:44/presence", &here) && here);
    t.update({}, 300000);
    PHI_CHECK(boolValue(r, "d8:c8:0c:02:22:44/presence", &here) && here);
    // Gone after it, and the access point is cleared.
    t.update({}, 302000);
    PHI_CHECK(boolValue(r, "d8:c8:0c:02:22:44/presence", &here) && !here);
    const auto ap = r.values.find("d8:c8:0c:02:22:44/access_point");
    PHI_CHECK(ap != r.values.end() && std::get<v1::Utf8String>(ap->second).empty());
    // Back at once.
    t.update({phone()}, 400000);
    PHI_CHECK(boolValue(r, "d8:c8:0c:02:22:44/presence", &here) && here);
}

void testRestoreKeepsWhatWasSeen()
{
    Recorder r;
    {
        ClientTracker t;
        t.setSink(r.sink());
        t.setTracked({"d8:c8:0c:02:22:44"});
        t.update({phone(), plug()}, 1000);
    }
    PHI_CHECK(r.state.rfind("phi-network-clients 1\n", 0) == 0);

    Recorder r2;
    ClientTracker t2;
    t2.setSink(r2.sink());
    t2.restore(r.state);
    t2.setTracked({"d8:c8:0c:02:22:44"});
    // A restart a minute later with the phone dozing: still here, still with
    // the channels it had - a report is never smaller after a start.
    t2.update({}, 61000);
    bool here = false;
    PHI_CHECK(boolValue(r2, "d8:c8:0c:02:22:44/presence", &here) && here);
    PHI_CHECK(r2.devices.size() == 1 && r2.devices[0].name() == "Marius iPhone");
    PHI_CHECK_MSG(r2.channelLists[0].size() == 4, "channels: %zu", r2.channelLists[0].size());
    t2.listCandidates(1000, 62000);
    PHI_CHECK(r2.candidates.size() == 1 && r2.candidates[0].id() == "b8:16:5f:30:bb:c8");

    ClientTracker broken;
    broken.restore("something else\nd8:c8:0c:02:22:44\t1\t1\t0\t2\t\tx\t\t\n");
    Recorder r3;
    broken.setSink(r3.sink());
    broken.listCandidates(1000, 2);
    PHI_CHECK(r3.candidates.empty());
}

void testFilters()
{
    Recorder r;
    ClientTracker t;
    t.setSink(r.sink());
    NetworkClientSighting guest = phone();
    guest.mac = "11:22:33:44:55:66";
    guest.guest = true;
    NetworkClientSighting randomised = phone();
    randomised.mac = "da:a1:19:12:34:56";
    t.update({guest, randomised, plug()}, 1000);

    t.listCandidates(1000, 1000);
    PHI_CHECK_MSG(r.candidates.size() == 2, "default: %zu", r.candidates.size());

    ClientTracker::Settings s;
    s.showGuests = true;
    s.ignorePrivateMacs = true;
    t.setSettings(s);
    r.candidates.clear();
    t.listCandidates(1000, 1000);
    PHI_CHECK_MSG(r.candidates.size() == 2, "swapped: %zu", r.candidates.size());
    bool sawGuest = false;
    for (const auto &c : r.candidates)
        sawGuest = sawGuest || c.id() == "11:22:33:44:55:66";
    PHI_CHECK(sawGuest);

    // Seen long ago: no longer offered.
    const std::int64_t later = 1000 + 8LL * 24 * 3600 * 1000;
    t.update({}, later);
    r.candidates.clear();
    t.listCandidates(1000, later);
    PHI_CHECK(r.candidates.empty());
}

void testBlockingAndUntrack()
{
    Recorder r;
    ClientTracker t;
    t.setSink(r.sink());
    ClientTracker::Settings s;
    s.canBlock = true;
    t.setSettings(s);
    t.setTracked({"d8:c8:0c:02:22:44"});
    NetworkClientSighting blocked = phone(false);
    blocked.blocked = true;
    t.update({blocked}, 1000);
    // Never seen online: presence and network access only.
    PHI_CHECK_MSG(r.channelLists.back().size() == 2, "channels: %zu", r.channelLists.back().size());
    bool access = true;
    PHI_CHECK(boolValue(r, "d8:c8:0c:02:22:44/network_access", &access) && !access);
    bool here = true;
    PHI_CHECK(boolValue(r, "d8:c8:0c:02:22:44/presence", &here) && !here);

    PHI_CHECK(t.untrack("D8:C8:0C:02:22:44"));
    PHI_CHECK(!t.untrack("d8:c8:0c:02:22:44"));
    PHI_CHECK(t.tracked().empty());
}

void testConfigFields()
{
    const auto fields = ClientTracker::configFields();
    PHI_CHECK(fields.size() == 3);
    PHI_CHECK(fields[0].key() == sdk::kClientMetaAwayDelayS);
}

} // namespace

int main()
{
    testMacs();
    testUntrackedClientsAreOfferedNotAnnounced();
    testTrackAnnouncesAndReports();
    testAwayDelay();
    testRestoreKeepsWhatWasSeen();
    testFilters();
    testBlockingAndUntrack();
    testConfigFields();
    return phi::testing::report("sdk_network_clients_tests");
}
