#include "phi/adapter/net/tdp.h"

#include "phi/runtime/str.h"

#include <cctype>
#include <memory>
#include <random>

#include <openssl/evp.h>
#include <openssl/pem.h>

namespace phicore::adapter::net {

struct TdpAnswer::Private {
    v1::Utf8String deviceType;
    v1::Utf8String model;
    v1::Utf8String role;
    v1::Utf8String mac;
    v1::Utf8String groupName;
    v1::Utf8String ip;
};

TdpAnswer::TdpAnswer() : d(std::make_unique<Private>()) {}
TdpAnswer::TdpAnswer(const TdpAnswer &other) : d(other.d ? std::make_unique<Private>(*other.d) : nullptr) {}
TdpAnswer::TdpAnswer(TdpAnswer &&other) noexcept = default;
TdpAnswer &TdpAnswer::operator=(const TdpAnswer &other)
{
    if (this != &other)
        d = other.d ? std::make_unique<Private>(*other.d) : nullptr;
    return *this;
}
TdpAnswer &TdpAnswer::operator=(TdpAnswer &&other) noexcept = default;
TdpAnswer::~TdpAnswer() = default;

// A moved-from answer has no Private; reading or writing it makes a fresh one.
const TdpAnswer::Private &TdpAnswer::p() const noexcept
{
    static const Private kEmpty;
    return d ? *d : kEmpty;
}

TdpAnswer::Private &TdpAnswer::m()
{
    if (!d)
        d = std::make_unique<Private>();
    return *d;
}

#define PHI_TDP_FIELD(name, setter)                                                                        \
    const v1::Utf8String &TdpAnswer::name() const noexcept { return p().name; }                         \
    TdpAnswer &TdpAnswer::setter(v1::Utf8String value)                                                  \
    {                                                                                                   \
        m().name = std::move(value);                                                                    \
        return *this;                                                                                   \
    }

PHI_TDP_FIELD(deviceType, setDeviceType)
PHI_TDP_FIELD(model, setModel)
PHI_TDP_FIELD(role, setRole)
PHI_TDP_FIELD(mac, setMac)
PHI_TDP_FIELD(groupName, setGroupName)
PHI_TDP_FIELD(ip, setIp)
#undef PHI_TDP_FIELD

namespace {

std::uint32_t crc32(const std::string &data)
{
    std::uint32_t crc = 0xFFFFFFFFu;
    for (const char c : data) {
        crc ^= static_cast<unsigned char>(c);
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

void putBe16(std::string &out, std::size_t at, std::uint16_t v)
{
    out[at] = static_cast<char>(v >> 8);
    out[at + 1] = static_cast<char>(v & 0xff);
}

void putBe32(std::string &out, std::size_t at, std::uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        out[at + i] = static_cast<char>((v >> (24 - 8 * i)) & 0xff);
}

void appendQuoted(std::string *out, std::string_view text)
{
    static const char kHex[] = "0123456789abcdef";
    out->push_back('"');
    for (const char ch : text) {
        const unsigned char c = static_cast<unsigned char>(ch);
        switch (c) {
        case '"': *out += "\\\""; break;
        case '\\': *out += "\\\\"; break;
        case '\n': *out += "\\n"; break;
        case '\r': *out += "\\r"; break;
        case '\t': *out += "\\t"; break;
        default:
            if (c < 0x20) {
                *out += "\\u00";
                out->push_back(kHex[c >> 4]);
                out->push_back(kHex[c & 15]);
            } else {
                out->push_back(ch);
            }
        }
    }
    out->push_back('"');
}

/// A throwaway key, made once: the devices want one in the probe, nobody
/// decrypts anything with it.
const std::string &throwawayPublicKeyPem()
{
    static const std::string pem = []() {
        std::string out;
        std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(EVP_RSA_gen(1024), EVP_PKEY_free);
        if (!key)
            return out;
        std::unique_ptr<BIO, decltype(&BIO_free)> bio(BIO_new(BIO_s_mem()), BIO_free);
        if (bio && PEM_write_bio_PUBKEY(bio.get(), key.get()) == 1) {
            char *data = nullptr;
            const long size = BIO_get_mem_data(bio.get(), &data);
            out.assign(data, static_cast<std::size_t>(size));
        }
        return out;
    }();
    return pem;
}

std::string base64Decoded(const std::string &text)
{
    if (text.empty() || text.size() % 4 != 0)
        return {};
    std::string out(text.size() / 4 * 3, '\0');
    const int n = EVP_DecodeBlock(reinterpret_cast<unsigned char *>(out.data()),
                                  reinterpret_cast<const unsigned char *>(text.data()),
                                  static_cast<int>(text.size()));
    if (n < 0)
        return {};
    std::size_t size = static_cast<std::size_t>(n);
    if (text.back() == '=')
        --size;
    if (text.size() >= 2 && text[text.size() - 2] == '=')
        --size;
    out.resize(size);
    return out;
}

// A small JSON reader for exactly one shape: an object of objects of scalars.
// The SDK carries no JSON library.
struct Reader {
    std::string_view s;
    std::size_t i = 0;

    void ws()
    {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])))
            ++i;
    }

    bool str(std::string *out)
    {
        if (i >= s.size() || s[i] != '"')
            return false;
        ++i;
        std::string r;
        while (i < s.size() && s[i] != '"') {
            const char c = s[i++];
            if (c != '\\') {
                r.push_back(c);
                continue;
            }
            if (i >= s.size())
                return false;
            const char e = s[i++];
            switch (e) {
            case 'b': r.push_back('\b'); break;
            case 'f': r.push_back('\f'); break;
            case 'n': r.push_back('\n'); break;
            case 'r': r.push_back('\r'); break;
            case 't': r.push_back('\t'); break;
            case 'u': {
                if (i + 4 > s.size())
                    return false;
                unsigned cp = 0;
                for (int k = 0; k < 4; ++k) {
                    const char h = s[i++];
                    cp <<= 4;
                    if (h >= '0' && h <= '9') cp |= static_cast<unsigned>(h - '0');
                    else if (h >= 'a' && h <= 'f') cp |= static_cast<unsigned>(h - 'a' + 10);
                    else if (h >= 'A' && h <= 'F') cp |= static_cast<unsigned>(h - 'A' + 10);
                    else return false;
                }
                if (cp >= 0xD800 && cp <= 0xDFFF)
                    cp = 0xFFFD;
                if (cp < 0x80) {
                    r.push_back(static_cast<char>(cp));
                } else if (cp < 0x800) {
                    r.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                    r.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                } else {
                    r.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                    r.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                    r.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                }
                break;
            }
            default: r.push_back(e); break; // " \ /
            }
        }
        if (i >= s.size())
            return false;
        ++i;
        *out = std::move(r);
        return true;
    }

    /// Skips one value of any kind; `text` gets it decoded when it is a string.
    bool value(std::string *text, int depth = 0)
    {
        ws();
        if (i >= s.size() || depth > 16)
            return false;
        const char c = s[i];
        if (c == '"')
            return str(text);
        if (c == '{' || c == '[') {
            const char close = c == '{' ? '}' : ']';
            ++i;
            ws();
            if (i < s.size() && s[i] == close) {
                ++i;
                return true;
            }
            for (;;) {
                std::string ignored;
                if (c == '{') {
                    ws();
                    if (!str(&ignored))
                        return false;
                    ws();
                    if (i >= s.size() || s[i] != ':')
                        return false;
                    ++i;
                }
                if (!value(&ignored, depth + 1))
                    return false;
                ws();
                if (i < s.size() && s[i] == ',') {
                    ++i;
                    continue;
                }
                if (i < s.size() && s[i] == close) {
                    ++i;
                    return true;
                }
                return false;
            }
        }
        const std::size_t start = i;
        while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ']' && !std::isspace(static_cast<unsigned char>(s[i])))
            ++i;
        if (i == start)
            return false;
        text->assign(s.substr(start, i - start));
        return true;
    }

    /// Walks an object, calling `onMember(key, reader)` positioned at the value;
    /// the callback must consume it.
    template<typename F>
    bool object(F onMember)
    {
        ws();
        if (i >= s.size() || s[i] != '{')
            return false;
        ++i;
        ws();
        if (i < s.size() && s[i] == '}') {
            ++i;
            return true;
        }
        for (;;) {
            std::string key;
            ws();
            if (!str(&key))
                return false;
            ws();
            if (i >= s.size() || s[i] != ':')
                return false;
            ++i;
            if (!onMember(key))
                return false;
            ws();
            if (i < s.size() && s[i] == ',') {
                ++i;
                continue;
            }
            if (i < s.size() && s[i] == '}') {
                ++i;
                return true;
            }
            return false;
        }
    }
};

} // namespace

std::string tdpProbePacket(std::string_view publicKeyPem, std::uint32_t serial)
{
    std::string body = "{\"params\":{\"rsa_key\":";
    appendQuoted(&body, publicKeyPem);
    body += "}}";
    std::string out(16, '\0');
    out[0] = 2;                                                  // version
    out[1] = 0;                                                  // message type
    putBe16(out, 2, 1);                                          // opcode: discovery query
    putBe16(out, 4, static_cast<std::uint16_t>(body.size()));
    out[6] = 0x11;                                               // flags
    out[7] = 0;
    putBe32(out, 8, serial);
    putBe32(out, 12, 0x5A6B7C8Du);                               // the CRC field's seed while computing
    out += body;
    putBe32(out, 12, crc32(out));
    return out;
}

std::string tdpProbePacket()
{
    std::random_device random;
    return tdpProbePacket(throwawayPublicKeyPem(), static_cast<std::uint32_t>(random()));
}

bool parseTdpAnswer(std::string_view packet, TdpAnswer *out, v1::Utf8String *error)
{
    auto fail = [&](const char *why) {
        if (error)
            *error = why;
        return false;
    };
    if (packet.size() <= 16 || packet[0] != 2)
        return fail("Not a TDP answer");
    Reader root{packet.substr(16)};
    TdpAnswer answer;
    bool haveResult = false;
    const bool ok = root.object([&](const std::string &key) {
        if (key != "result") {
            std::string ignored;
            return root.value(&ignored);
        }
        root.ws();
        if (!(root.i < root.s.size() && root.s[root.i] == '{'))
            return false;
        haveResult = true;
        return root.object([&](const std::string &field) {
            std::string text;
            if (!root.value(&text))
                return false;
            if (field == "device_type") answer.setDeviceType(text);
            else if (field == "device_model") answer.setModel(text);
            else if (field == "role") answer.setRole(text);
            else if (field == "mac") {
                const std::string key12 = phi::str::macKey(text);
                answer.setMac(key12.empty() ? std::string() : phi::str::normalizedMac(key12));
            } else if (field == "group_name") answer.setGroupName(base64Decoded(text));
            else if (field == "ip") answer.setIp(text);
            return true;
        });
    });
    if (!ok)
        return fail("Body is not JSON");
    if (!haveResult)
        return fail("No result object");
    if (out)
        *out = std::move(answer);
    return true;
}

v1::DiscoveryCandidate tdpCandidateFor(const TdpAnswer &answer, const std::string &fromIp)
{
    const std::string ip = answer.ip().empty() ? fromIp : answer.ip();
    std::string label = phi::str::trimmed(
        answer.model() + (answer.groupName().empty() ? std::string() : " (" + answer.groupName() + ")"));
    if (label.empty())
        label = answer.deviceType();

    std::string meta = "{\"deviceType\":";
    appendQuoted(&meta, answer.deviceType());
    if (!answer.role().empty()) {
        meta += ",\"role\":";
        appendQuoted(&meta, answer.role());
    }
    if (!answer.groupName().empty()) {
        meta += ",\"groupName\":";
        appendQuoted(&meta, answer.groupName());
    }
    meta += "}";

    v1::DiscoveryCandidate c;
    c.setExternalId(answer.mac().empty() ? ip : answer.mac())
        .setLabel(std::move(label))
        .setHost(ip)
        .setIp(ip)
        .setMac(answer.mac())
        .setModel(answer.model())
        .setMetaJson(std::move(meta));
    return c;
}

} // namespace phicore::adapter::net
