#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "phi/adapter/v1/response.h"
#include "phi/adapter/v1/types.h"

namespace phicore::adapter::v1 {

// Factory discovery: core asks a factory that declares
// AdapterCapability::FactoryDiscovery to search once and say what it found.
// The request is the reserved factory action kFactoryDiscoverActionId with the
// params `{"timeoutMs": N}`; the answer is an ActionResponse of type Data that
// holds the candidates as JSON (setDiscoveryCandidates). The README section
// "Factory discovery" has the full contract.

/// The reserved factory action id. Core hides it from the UI's action buttons.
inline constexpr const char kFactoryDiscoverActionId[] = "factory.discover";

/// What a search found: a device the adapter has not been told about yet.
/// Behind a d-pointer, see channel.h for the rule.
class DiscoveryCandidate
{
public:
    DiscoveryCandidate();
    DiscoveryCandidate(const DiscoveryCandidate &other);
    DiscoveryCandidate(DiscoveryCandidate &&other) noexcept;
    DiscoveryCandidate &operator=(const DiscoveryCandidate &other);
    DiscoveryCandidate &operator=(DiscoveryCandidate &&other) noexcept;
    ~DiscoveryCandidate();

    /// Stable id of the device, such as its MAC.
    const Utf8String &externalId() const noexcept;
    DiscoveryCandidate &setExternalId(Utf8String externalId);
    /// What the list shows.
    const Utf8String &label() const noexcept;
    DiscoveryCandidate &setLabel(Utf8String label);
    /// Where the form's host field points: a name, or the address when there is none.
    const Utf8String &host() const noexcept;
    DiscoveryCandidate &setHost(Utf8String host);
    const Utf8String &ip() const noexcept;
    DiscoveryCandidate &setIp(Utf8String ip);
    /// 0 when the device has no port of its own.
    std::uint16_t port() const noexcept;
    DiscoveryCandidate &setPort(std::uint16_t port);
    const Utf8String &mac() const noexcept;
    DiscoveryCandidate &setMac(Utf8String mac);
    const Utf8String &model() const noexcept;
    DiscoveryCandidate &setModel(Utf8String model);
    /// A free JSON object. Core keeps mac, model, type and name of it.
    const JsonText &metaJson() const noexcept;
    DiscoveryCandidate &setMetaJson(JsonText metaJson);

private:
    struct Private;
    const Private &p() const noexcept;
    Private &m();
    std::unique_ptr<Private> d;
};

using DiscoveryCandidateList = std::vector<DiscoveryCandidate>;

/// `{"candidates":[{...}]}`. Empty strings and port 0 are left out; "meta" is
/// written only when metaJson is a JSON object.
Utf8String discoveryCandidatesToJson(const DiscoveryCandidateList &candidates);

/// Reads that shape back. Lenient: unknown keys are ignored and entries that
/// are not objects are skipped. False, with `error`, when the text is not
/// JSON or has no "candidates" array. `out` and `error` may be null.
bool discoveryCandidatesFromJson(const Utf8String &json, DiscoveryCandidateList *out, Utf8String *error);

/// Answers a factory.discover request: sets the Data result to the candidates' JSON.
ActionResponse &setDiscoveryCandidates(ActionResponse &response, const DiscoveryCandidateList &candidates);

/// The time budget in `{"timeoutMs": N}`; `fallback` when it is missing,
/// not a number, or not positive.
int discoveryTimeoutMs(const Utf8String &paramsJson, int fallback = 3000);

} // namespace phicore::adapter::v1
