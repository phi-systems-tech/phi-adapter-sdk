#pragma once

// The fields an adapter offers when it can talk TLS, and what they mean.
//
// Vocabulary rather than code, which is why it is here and not in a backend:
// the danger is not that two adapters write the same three fields twice - they
// are three fields - but that they disagree about them. One calls the switch
// `tls`, the next `useSsl`, the third `secure`; one verifies the certificate by
// default and the next does not. An operator would then have to learn each
// adapter's opinion about the same question, and the one that quietly accepts
// any certificate would look exactly like the one that does not.
//
// Header-only and in the contract, so that an adapter written without Qt, or
// without the Linux runtime, still has the same answer. What a field is called
// is not a property of an event loop.
//
// What is deliberately absent is a switch that trusts any certificate.
// Encryption without verification stops somebody reading the wire and does
// nothing about somebody standing in the middle of it - and unlike a browser,
// which at least shows a warning page, an adapter would connect silently while
// the interface said "TLS". An endpoint with a self-signed certificate has a
// correct answer already: name the certificate in `tlsCaFile`. That is one more
// step for the operator and the difference between encrypted and secure.

#include <string>
#include <variant>

#include "phi/adapter/v1/schema.h"
#include "phi/adapter/v1/types.h"
#include "phi/adapter/v1/value.h"

namespace phicore::adapter::v1 {

inline constexpr const char *kTlsFieldKey = "tls";
inline constexpr const char *kTlsCaFileFieldKey = "tlsCaFile";
inline constexpr const char *kTlsVerifyHostnameFieldKey = "tlsVerifyHostname";

/// What an instance says about TLS, once the three values have been read.
struct TlsSettings {
    /// Whether to speak TLS at all. Off unless the operator said otherwise: an
    /// adapter pointed at a plain endpoint has to keep working, and an upgrade
    /// that silently started requiring TLS would take every one of them off the
    /// air.
    bool enabled = false;
    /// A certificate or bundle to trust in addition to the system store, for an
    /// endpoint whose certificate no public authority signed - which is the
    /// usual case on a local network. Empty means the system store alone.
    Utf8String caFile;
    /// Whether the certificate has to name the address that was dialled. On
    /// unless the operator turned it off, which they need when they connect by
    /// IP to a certificate that names a host. The chain is still checked.
    bool verifyHostname = true;
};

/// Whether a stored value means yes.
///
/// A setting has been through a form, a database and JSON, and comes back as
/// whatever those left it as. Deciding this once is most of the point: the
/// string "false" is not empty and reads as true to anybody who writes the
/// obvious thing.
[[nodiscard]] inline bool tlsTruthOf(const ScalarValue &value, bool fallback)
{
    if (const bool *flag = std::get_if<bool>(&value))
        return *flag;
    if (const std::int64_t *number = std::get_if<std::int64_t>(&value))
        return *number != 0;
    if (const double *number = std::get_if<double>(&value))
        return *number != 0.0;
    if (const Utf8String *text = std::get_if<Utf8String>(&value)) {
        Utf8String lowered;
        lowered.reserve(text->size());
        for (const char c : *text) {
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
                continue;
            lowered.push_back(static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c));
        }
        if (lowered == "true" || lowered == "1" || lowered == "yes" || lowered == "on")
            return true;
        if (lowered == "false" || lowered == "0" || lowered == "no" || lowered == "off")
            return false;
        return fallback;
    }
    return fallback;
}

/// The three values as they came back, turned into one answer.
///
/// The defaults are not symmetric and that is deliberate: a value nobody can
/// make sense of leaves TLS off and leaves the check on. Both are the safe
/// reading of "we do not know".
[[nodiscard]] inline TlsSettings tlsSettingsFrom(const ScalarValue &tls,
                                                 const ScalarValue &caFile,
                                                 const ScalarValue &verifyHostname)
{
    TlsSettings settings;
    settings.enabled = tlsTruthOf(tls, false);
    if (const Utf8String *text = std::get_if<Utf8String>(&caFile)) {
        const auto first = text->find_first_not_of(" \t\n\r");
        const auto last = text->find_last_not_of(" \t\n\r");
        if (first != Utf8String::npos)
            settings.caFile = text->substr(first, last - first + 1);
    }
    settings.verifyHostname = tlsTruthOf(verifyHostname, true);
    return settings;
}

/// The three fields, to append to a schema section.
///
/// The certificate and the hostname check are shown only when the switch is on,
/// through the schema's own visibility rules, so a form for a plain connection
/// does not carry two questions that cannot apply to it.
///
/// `parentActionId` belongs to adapters that build these into an action's form
/// rather than into the instance section; empty otherwise.
[[nodiscard]] inline AdapterConfigFieldList tlsConfigFields(
    const Utf8String &parentActionId = {})
{
    AdapterConfigFieldVisibility onlyWhenOn;
    onlyWhenOn.fieldKey = kTlsFieldKey;
    onlyWhenOn.value = true;
    onlyWhenOn.op = AdapterConfigVisibilityOp::Equals;

    AdapterConfigField tls;
    tls.key = kTlsFieldKey;
    tls.type = AdapterConfigFieldType::Boolean;
    tls.label = "TLS";
    tls.description = "Encrypt the connection. Off unless the endpoint offers it - a plain"
                      " endpoint has to keep working.";
    tls.defaultValue = false;
    tls.parentActionId = parentActionId;

    AdapterConfigField caFile;
    caFile.key = kTlsCaFileFieldKey;
    caFile.type = AdapterConfigFieldType::String;
    caFile.label = "CA certificate";
    caFile.description = "Path to a certificate or bundle to trust in addition to the system"
                         " store. Needed when the endpoint's certificate was not signed by a"
                         " public authority, which is the usual case on a local network.";
    caFile.defaultValue = Utf8String();
    caFile.visibility = onlyWhenOn;
    caFile.parentActionId = parentActionId;

    AdapterConfigField verifyHostname;
    verifyHostname.key = kTlsVerifyHostnameFieldKey;
    verifyHostname.type = AdapterConfigFieldType::Boolean;
    verifyHostname.label = "Check the certificate's hostname";
    verifyHostname.description = "Require the certificate to name the address that was dialled."
                                 " Turn this off only when connecting by IP to a certificate"
                                 " that names a host - the certificate is still checked against"
                                 " the trusted authorities.";
    verifyHostname.defaultValue = true;
    verifyHostname.visibility = onlyWhenOn;
    verifyHostname.parentActionId = parentActionId;

    return {tls, caFile, verifyHostname};
}

} // namespace phicore::adapter::v1
