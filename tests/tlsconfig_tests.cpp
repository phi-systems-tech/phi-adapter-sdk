// The TLS fields every adapter offers, and what they mean.
//
// In the contract because a field's name is not a property of an event loop:
// an adapter written without Qt has to reach the same answer. And what is
// pinned here is not the code - it is three fields - but the two decisions
// that must never drift: off unless the operator asked, and verified when on.

#include <phi/adapter/testing/check.h>

#include "phi/adapter/v1/tlsconfig.h"

using namespace phicore::adapter::v1;

namespace {

const AdapterConfigField *fieldNamed(const AdapterConfigFieldList &fields, const char *key)
{
    for (const AdapterConfigField &field : fields) {
        if (field.key == key)
            return &field;
    }
    return nullptr;
}

void testTheVocabularyIsTheOneEverybodyUses()
{
    const AdapterConfigFieldList fields = tlsConfigFields();
    PHI_CHECK(fields.size() == 3);

    const AdapterConfigField *tls = fieldNamed(fields, kTlsFieldKey);
    PHI_CHECK(tls != nullptr);
    if (tls) {
        PHI_CHECK(tls->type == AdapterConfigFieldType::Boolean);
        // Off. An adapter pointed at a plain endpoint has to keep working, and
        // an upgrade that silently started requiring TLS would take every one
        // of them off the air.
        PHI_CHECK(std::get<bool>(tls->defaultValue) == false);
    }

    const AdapterConfigField *ca = fieldNamed(fields, kTlsCaFileFieldKey);
    PHI_CHECK(ca != nullptr);
    if (ca)
        PHI_CHECK(ca->type == AdapterConfigFieldType::String);

    const AdapterConfigField *verify = fieldNamed(fields, kTlsVerifyHostnameFieldKey);
    PHI_CHECK(verify != nullptr);
    if (verify) {
        PHI_CHECK(verify->type == AdapterConfigFieldType::Boolean);
        // On. This is the whole reason the fields are shared: an adapter that
        // shipped this off would encrypt and authenticate nothing, and would
        // look from the outside exactly like one that does both.
        PHI_CHECK(std::get<bool>(verify->defaultValue) == true);
    }
}

void testTheTwoDetailsAreOnlyAskedWhenTlsIsOn()
{
    const AdapterConfigFieldList fields = tlsConfigFields();
    for (const char *key : {kTlsCaFileFieldKey, kTlsVerifyHostnameFieldKey}) {
        const AdapterConfigField *field = fieldNamed(fields, key);
        PHI_CHECK(field != nullptr);
        if (!field)
            continue;
        PHI_CHECK(field->visibility.fieldKey == kTlsFieldKey);
        PHI_CHECK(std::get<bool>(field->visibility.value));
        PHI_CHECK(field->visibility.op == AdapterConfigVisibilityOp::Equals);
    }
    // The switch itself is always shown, or nothing could turn the others on.
    const AdapterConfigField *tls = fieldNamed(fields, kTlsFieldKey);
    PHI_CHECK(tls != nullptr);
    if (tls)
        PHI_CHECK(tls->visibility.fieldKey.empty());
}

void testAnActionsFormCarriesTheFieldsToo()
{
    for (const AdapterConfigField &field : tlsConfigFields("probe"))
        PHI_CHECK(field.parentActionId == "probe");
    // And an instance section's fields belong to no action.
    for (const AdapterConfigField &field : tlsConfigFields())
        PHI_CHECK(field.parentActionId.empty());
}

void testNothingSaidMeansOffAndVerified()
{
    // An instance made before the adapter offered these fields, which is every
    // instance that exists today.
    const TlsSettings settings = tlsSettingsFrom({}, {}, {});
    PHI_CHECK(!settings.enabled);
    PHI_CHECK(settings.caFile.empty());
    PHI_CHECK(settings.verifyHostname);
}

void testTheAnswerIsReadTheSameWhateverShapeItComesBackIn()
{
    // A setting has been through a form, a database and JSON, and comes back as
    // whatever those left it as. Deciding this once is most of the point: the
    // string "false" is not empty and reads as true to anybody who writes the
    // obvious thing.
    PHI_CHECK(tlsTruthOf(ScalarValue(true), false));
    PHI_CHECK(tlsTruthOf(ScalarValue(Utf8String("true")), false));
    PHI_CHECK(tlsTruthOf(ScalarValue(Utf8String("ON")), false));
    PHI_CHECK(tlsTruthOf(ScalarValue(Utf8String(" yes ")), false));
    PHI_CHECK(tlsTruthOf(ScalarValue(std::int64_t{1}), false));
    PHI_CHECK(!tlsTruthOf(ScalarValue(Utf8String("false")), true));
    PHI_CHECK(!tlsTruthOf(ScalarValue(Utf8String("0")), true));
    PHI_CHECK(!tlsTruthOf(ScalarValue(std::int64_t{0}), true));

    // And a value nobody can make sense of falls to the safe answer, which is
    // not the same one for both.
    PHI_CHECK(!tlsSettingsFrom(ScalarValue(Utf8String("perhaps")), {}, {}).enabled);
    PHI_CHECK(tlsSettingsFrom({}, {}, ScalarValue(Utf8String("perhaps"))).verifyHostname);
    PHI_CHECK(!tlsSettingsFrom({}, {}, ScalarValue(false)).verifyHostname);
    PHI_CHECK(!tlsSettingsFrom({}, {}, ScalarValue(Utf8String("no"))).verifyHostname);
}

void testTheCertificateIsReadWithoutItsSurroundingSpace()
{
    const TlsSettings settings =
        tlsSettingsFrom(ScalarValue(true), ScalarValue(Utf8String("  /etc/ssl/broker.crt  ")),
                        ScalarValue(true));
    PHI_CHECK(settings.caFile == "/etc/ssl/broker.crt");
    // A field left blank is not a path made of spaces.
    PHI_CHECK(tlsSettingsFrom({}, ScalarValue(Utf8String("   ")), {}).caFile.empty());
}

} // namespace

int main()
{
    testTheVocabularyIsTheOneEverybodyUses();
    testTheTwoDetailsAreOnlyAskedWhenTlsIsOn();
    testAnActionsFormCarriesTheFieldsToo();
    testNothingSaidMeansOffAndVerified();
    testTheAnswerIsReadTheSameWhateverShapeItComesBackIn();
    testTheCertificateIsReadWithoutItsSurroundingSpace();
    return phi::testing::report("sdk_tlsconfig_tests");
}
