// Factory discovery: the candidate JSON both ways and the timeout helper.

#include <phi/adapter/testing/check.h>

#include "phi/adapter/v1/discovery.h"

#include <string>

namespace v1 = phicore::adapter::v1;

namespace {

v1::DiscoveryCandidateList twoCandidates()
{
    v1::DiscoveryCandidate a;
    a.setExternalId("34:ea:34:00:11:22").setLabel("Living \"RM4\"").setHost("rm4.lan").setIp("192.168.1.40")
        .setPort(80).setMac("34:ea:34:00:11:22").setModel("RM4 mini")
        .setMetaJson("{\"type\":\"rm4\",\"n\":[1,2]}");
    v1::DiscoveryCandidate b;
    b.setExternalId("x2").setIp("10.0.0.2");
    return {a, b};
}

void testRoundTrip()
{
    const auto json = v1::discoveryCandidatesToJson(twoCandidates());
    v1::DiscoveryCandidateList back;
    std::string error;
    PHI_CHECK(v1::discoveryCandidatesFromJson(json, &back, &error));
    PHI_CHECK(back.size() == 2);
    if (back.size() != 2)
        return;
    PHI_CHECK(back[0].externalId() == "34:ea:34:00:11:22");
    PHI_CHECK(back[0].label() == "Living \"RM4\"");
    PHI_CHECK(back[0].host() == "rm4.lan");
    PHI_CHECK(back[0].ip() == "192.168.1.40");
    PHI_CHECK(back[0].port() == 80);
    PHI_CHECK(back[0].mac() == "34:ea:34:00:11:22");
    PHI_CHECK(back[0].model() == "RM4 mini");
    PHI_CHECK(back[0].metaJson() == "{\"type\":\"rm4\",\"n\":[1,2]}");
    PHI_CHECK(back[1].externalId() == "x2");
    PHI_CHECK(back[1].ip() == "10.0.0.2");
    PHI_CHECK(back[1].port() == 0);
    PHI_CHECK(back[1].metaJson().empty());
}

void testOmission()
{
    v1::DiscoveryCandidate c;
    c.setIp("1.2.3.4");
    PHI_CHECK(v1::discoveryCandidatesToJson({c}) == "{\"candidates\":[{\"ip\":\"1.2.3.4\"}]}");
    c.setMetaJson("[1]");
    PHI_CHECK(v1::discoveryCandidatesToJson({c}) == "{\"candidates\":[{\"ip\":\"1.2.3.4\"}]}");
    c.setMetaJson("{\"a\":1}");
    PHI_CHECK(v1::discoveryCandidatesToJson({c}) == "{\"candidates\":[{\"ip\":\"1.2.3.4\",\"meta\":{\"a\":1}}]}");
    PHI_CHECK(v1::discoveryCandidatesToJson({}) == "{\"candidates\":[]}");
}

void testLenientParse()
{
    v1::DiscoveryCandidateList out;
    std::string error;
    const char *json = R"({"other":1,"candidates":[5,"x",null,{"ip":"1.1.1.1","port":8080,"future":{"a":[1]},"label":"Aä"},[],{}]})";
    PHI_CHECK(v1::discoveryCandidatesFromJson(json, &out, &error));
    PHI_CHECK(out.size() == 2);
    if (out.size() == 2) {
        PHI_CHECK(out[0].ip() == "1.1.1.1");
        PHI_CHECK(out[0].port() == 8080);
        PHI_CHECK(out[0].label() == "A\xc3\xa4");
        PHI_CHECK(out[1].ip().empty());
    }
    PHI_CHECK(!v1::discoveryCandidatesFromJson("nope", &out, &error));
    PHI_CHECK(!error.empty());
    PHI_CHECK(!v1::discoveryCandidatesFromJson("{\"x\":1}", &out, &error));
    PHI_CHECK(v1::discoveryCandidatesFromJson("{\"candidates\":[]}", &out, nullptr));
    PHI_CHECK(out.empty());
}

void testResponse()
{
    auto response = v1::ActionResponse::success(1);
    v1::setDiscoveryCandidates(response, twoCandidates());
    PHI_CHECK(response.resultType() == v1::ActionResultType::Data);
    PHI_CHECK(response.dataJson() == v1::discoveryCandidatesToJson(twoCandidates()));
}

void testTimeout()
{
    PHI_CHECK(v1::discoveryTimeoutMs("{\"timeoutMs\":1500}") == 1500);
    PHI_CHECK(v1::discoveryTimeoutMs(" { \"timeoutMs\" : 250 , \"x\":1 } ") == 250);
    PHI_CHECK(v1::discoveryTimeoutMs("") == 3000);
    PHI_CHECK(v1::discoveryTimeoutMs("{}") == 3000);
    PHI_CHECK(v1::discoveryTimeoutMs("{\"timeoutMs\":0}") == 3000);
    PHI_CHECK(v1::discoveryTimeoutMs("{\"timeoutMs\":-5}", 700) == 700);
    PHI_CHECK(v1::discoveryTimeoutMs("{\"timeoutMs\":\"x\"}", 700) == 700);
    PHI_CHECK(v1::discoveryTimeoutMs("garbage", 700) == 700);
}

void testEnums()
{
    PHI_CHECK(static_cast<int>(v1::AdapterCapability::FactoryDiscovery) == 0x2000);
    PHI_CHECK(static_cast<int>(v1::DiscoveryKind::Adapter) == 4);
    PHI_CHECK(std::string(v1::kFactoryDiscoverActionId) == "factory.discover");
}

} // namespace

int main()
{
    testRoundTrip();
    testOmission();
    testLenientParse();
    testResponse();
    testTimeout();
    testEnums();
    return phi::testing::report("sdk_discovery_tests");
}
