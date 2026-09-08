#include "phi/adapter/net/http_auth.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <random>
#include <vector>

#include <openssl/evp.h>

namespace phicore::adapter::net {

namespace {

bool isWs(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

std::string_view trim(std::string_view s)
{
    while (!s.empty() && isWs(s.front()))
        s.remove_prefix(1);
    while (!s.empty() && isWs(s.back()))
        s.remove_suffix(1);
    return s;
}

std::string toLowerAscii(std::string_view s)
{
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

bool equalsIgnoreCase(std::string_view lhs, std::string_view rhs)
{
    return lhs.size() == rhs.size() && toLowerAscii(lhs) == toLowerAscii(rhs);
}

/**
 * @brief Splits auth parameters on commas that are not inside a quoted value.
 *
 * `qop="auth,auth-int"` is one parameter, not two, and a split that does not
 * know that picks up `auth-int"` as a parameter name.
 */
std::vector<std::string_view> splitParameters(std::string_view text)
{
    std::vector<std::string_view> parts;
    bool quoted = false;
    std::size_t start = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '"' && (i == 0 || text[i - 1] != '\\'))
            quoted = !quoted;
        else if (c == ',' && !quoted) {
            parts.push_back(text.substr(start, i - start));
            start = i + 1;
        }
    }
    parts.push_back(text.substr(start));
    return parts;
}

/// Strips surrounding quotes and unescapes what RFC 7230 allows inside them.
std::string unquote(std::string_view value)
{
    value = trim(value);
    if (value.size() < 2 || value.front() != '"' || value.back() != '"')
        return std::string(value);
    value.remove_prefix(1);
    value.remove_suffix(1);

    std::string out;
    out.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '\\' && i + 1 < value.size())
            ++i;
        out.push_back(value[i]);
    }
    return out;
}

std::string quote(std::string_view value)
{
    std::string out;
    out.reserve(value.size() + 2);
    out.push_back('"');
    for (const char c : value) {
        if (c == '"' || c == '\\')
            out.push_back('\\');
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

/// `auth` when the server offers it, otherwise nothing. `auth-int` hashes the
/// body and no device an adapter speaks to has ever asked for it.
std::string pickQop(std::string_view offered)
{
    for (const std::string_view candidate : splitParameters(offered)) {
        if (equalsIgnoreCase(trim(candidate), "auth"))
            return "auth";
    }
    return {};
}

std::string formatNonceCount(std::uint32_t count)
{
    std::array<char, 16> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%08x", count);
    return std::string(buffer.data());
}

} // namespace

std::string md5Hex(std::string_view data)
{
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int length = 0;
    // EVP rather than the MD5_* functions: those are deprecated in OpenSSL 3
    // and compile to warnings. MD5 itself is not a choice - it is what the
    // scheme is defined in terms of, and it is the only thing a FRITZ!Box
    // offers.
    if (EVP_Digest(data.data(), data.size(), digest.data(), &length, EVP_md5(), nullptr) != 1)
        return {};

    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(static_cast<std::size_t>(length) * 2);
    for (unsigned int i = 0; i < length; ++i) {
        out.push_back(kHex[(digest[i] >> 4) & 0x0F]);
        out.push_back(kHex[digest[i] & 0x0F]);
    }
    return out;
}

Challenge parseChallenge(std::string_view header)
{
    Challenge best;

    // A server may offer several schemes, in one header or in several. They are
    // walked in order and the strongest usable one wins, so Basic never gets
    // picked while Digest is on the table.
    std::size_t pos = 0;
    while (pos < header.size()) {
        const std::string_view rest = trim(header.substr(pos));
        if (rest.empty())
            break;

        const std::size_t space = rest.find(' ');
        const std::string_view scheme = space == std::string_view::npos ? rest : rest.substr(0, space);

        if (equalsIgnoreCase(scheme, "basic")) {
            if (best.scheme == Challenge::Scheme::None) {
                best.scheme = Challenge::Scheme::Basic;
                if (space != std::string_view::npos) {
                    for (const std::string_view part : splitParameters(rest.substr(space + 1))) {
                        const std::size_t eq = part.find('=');
                        if (eq == std::string_view::npos)
                            continue;
                        if (equalsIgnoreCase(trim(part.substr(0, eq)), "realm"))
                            best.realm = unquote(part.substr(eq + 1));
                    }
                }
            }
            break;
        }

        if (!equalsIgnoreCase(scheme, "digest"))
            break;
        if (space == std::string_view::npos)
            break;

        Challenge digest;
        digest.scheme = Challenge::Scheme::Digest;
        for (const std::string_view part : splitParameters(rest.substr(space + 1))) {
            const std::size_t eq = part.find('=');
            if (eq == std::string_view::npos)
                continue;
            const std::string_view key = trim(part.substr(0, eq));
            const std::string value = unquote(part.substr(eq + 1));
            if (equalsIgnoreCase(key, "realm"))
                digest.realm = value;
            else if (equalsIgnoreCase(key, "nonce"))
                digest.nonce = value;
            else if (equalsIgnoreCase(key, "opaque"))
                digest.opaque = value;
            else if (equalsIgnoreCase(key, "algorithm"))
                digest.algorithm = value;
            else if (equalsIgnoreCase(key, "qop"))
                digest.qop = pickQop(value);
            else if (equalsIgnoreCase(key, "stale"))
                digest.stale = equalsIgnoreCase(value, "true");
        }
        return digest;
    }

    return best;
}

std::string buildAuthorization(const Challenge &challenge,
                               const Credentials &credentials,
                               std::string_view method,
                               std::string_view uri,
                               std::uint32_t nonceCount,
                               std::string_view cnonce)
{
    if (challenge.scheme == Challenge::Scheme::Basic) {
        static constexpr char kAlphabet[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        const std::string raw = credentials.user + ":" + credentials.password;
        std::string encoded;
        encoded.reserve(((raw.size() + 2) / 3) * 4);
        for (std::size_t i = 0; i < raw.size(); i += 3) {
            const auto b0 = static_cast<unsigned char>(raw[i]);
            const bool has1 = i + 1 < raw.size();
            const bool has2 = i + 2 < raw.size();
            const auto b1 = has1 ? static_cast<unsigned char>(raw[i + 1]) : 0;
            const auto b2 = has2 ? static_cast<unsigned char>(raw[i + 2]) : 0;
            encoded.push_back(kAlphabet[b0 >> 2]);
            encoded.push_back(kAlphabet[((b0 & 0x03) << 4) | (b1 >> 4)]);
            encoded.push_back(has1 ? kAlphabet[((b1 & 0x0F) << 2) | (b2 >> 6)] : '=');
            encoded.push_back(has2 ? kAlphabet[b2 & 0x3F] : '=');
        }
        return "Basic " + encoded;
    }

    if (challenge.scheme != Challenge::Scheme::Digest || challenge.nonce.empty())
        return {};

    const std::string algorithm = challenge.algorithm.empty() ? "MD5" : challenge.algorithm;
    const bool sessionAlgorithm = equalsIgnoreCase(algorithm, "MD5-sess");
    if (!sessionAlgorithm && !equalsIgnoreCase(algorithm, "MD5")) {
        // SHA-256 and the rest exist in RFC 7616 and nothing an adapter talks
        // to offers them. Refusing is better than answering with the wrong
        // hash and reporting bad credentials.
        return {};
    }

    std::string ha1 = md5Hex(credentials.user + ":" + challenge.realm + ":" + credentials.password);
    if (sessionAlgorithm)
        ha1 = md5Hex(ha1 + ":" + challenge.nonce + ":" + std::string(cnonce));

    const std::string ha2 = md5Hex(std::string(method) + ":" + std::string(uri));

    const std::string nc = formatNonceCount(nonceCount);
    std::string response;
    if (challenge.qop.empty()) {
        // RFC 2069: no qop, no count, no client nonce.
        response = md5Hex(ha1 + ":" + challenge.nonce + ":" + ha2);
    } else {
        response = md5Hex(ha1 + ":" + challenge.nonce + ":" + nc + ":" + std::string(cnonce) + ":"
                          + challenge.qop + ":" + ha2);
    }

    std::string out = "Digest username=" + quote(credentials.user)
        + ", realm=" + quote(challenge.realm)
        + ", nonce=" + quote(challenge.nonce)
        + ", uri=" + quote(uri)
        + ", response=" + quote(response);
    if (!challenge.algorithm.empty())
        out += ", algorithm=" + challenge.algorithm;
    if (!challenge.qop.empty()) {
        out += ", qop=" + challenge.qop;
        out += ", nc=" + nc;
        out += ", cnonce=" + quote(cnonce);
    }
    if (!challenge.opaque.empty())
        out += ", opaque=" + quote(challenge.opaque);
    return out;
}

std::string makeClientNonce()
{
    static constexpr char kHex[] = "0123456789abcdef";
    std::random_device source;
    std::uniform_int_distribution<int> nibble(0, 15);
    std::string out;
    out.reserve(16);
    for (int i = 0; i < 16; ++i)
        out.push_back(kHex[nibble(source)]);
    return out;
}

} // namespace phicore::adapter::net
