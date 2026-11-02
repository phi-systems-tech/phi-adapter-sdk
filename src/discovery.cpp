#include "phi/adapter/v1/discovery.h"

#include <cctype>
#include <cstdlib>
#include <string_view>
#include <utility>

namespace phicore::adapter::v1 {

struct DiscoveryCandidate::Private {
    Utf8String externalId;
    Utf8String label;
    Utf8String host;
    Utf8String ip;
    std::uint16_t port = 0;
    Utf8String mac;
    Utf8String model;
    JsonText metaJson;
};

DiscoveryCandidate::DiscoveryCandidate() : d(std::make_unique<Private>()) {}
DiscoveryCandidate::DiscoveryCandidate(const DiscoveryCandidate &other) : d(std::make_unique<Private>(*other.d)) {}
DiscoveryCandidate::DiscoveryCandidate(DiscoveryCandidate &&other) noexcept = default;
DiscoveryCandidate &DiscoveryCandidate::operator=(const DiscoveryCandidate &other)
{
    if (this != &other)
        d = std::make_unique<Private>(*other.d);
    return *this;
}
DiscoveryCandidate &DiscoveryCandidate::operator=(DiscoveryCandidate &&other) noexcept = default;
DiscoveryCandidate::~DiscoveryCandidate() = default;

// A moved-from candidate has no Private; reading or writing it makes a fresh one.
const DiscoveryCandidate::Private &DiscoveryCandidate::p() const noexcept
{
    static const Private kEmpty;
    return d ? *d : kEmpty;
}

DiscoveryCandidate::Private &DiscoveryCandidate::m()
{
    if (!d)
        d = std::make_unique<Private>();
    return *d;
}

const Utf8String &DiscoveryCandidate::externalId() const noexcept { return p().externalId; }
DiscoveryCandidate &DiscoveryCandidate::setExternalId(Utf8String v) { m().externalId = std::move(v); return *this; }
const Utf8String &DiscoveryCandidate::label() const noexcept { return p().label; }
DiscoveryCandidate &DiscoveryCandidate::setLabel(Utf8String v) { m().label = std::move(v); return *this; }
const Utf8String &DiscoveryCandidate::host() const noexcept { return p().host; }
DiscoveryCandidate &DiscoveryCandidate::setHost(Utf8String v) { m().host = std::move(v); return *this; }
const Utf8String &DiscoveryCandidate::ip() const noexcept { return p().ip; }
DiscoveryCandidate &DiscoveryCandidate::setIp(Utf8String v) { m().ip = std::move(v); return *this; }
std::uint16_t DiscoveryCandidate::port() const noexcept { return p().port; }
DiscoveryCandidate &DiscoveryCandidate::setPort(std::uint16_t v) { m().port = v; return *this; }
const Utf8String &DiscoveryCandidate::mac() const noexcept { return p().mac; }
DiscoveryCandidate &DiscoveryCandidate::setMac(Utf8String v) { m().mac = std::move(v); return *this; }
const Utf8String &DiscoveryCandidate::model() const noexcept { return p().model; }
DiscoveryCandidate &DiscoveryCandidate::setModel(Utf8String v) { m().model = std::move(v); return *this; }
const JsonText &DiscoveryCandidate::metaJson() const noexcept { return p().metaJson; }
DiscoveryCandidate &DiscoveryCandidate::setMetaJson(JsonText v) { m().metaJson = std::move(v); return *this; }

namespace {

// A small reader for exactly what this header needs. The SDK carries no JSON
// library; values are kept as raw tokens and strings are decoded on demand.
struct Reader {
    std::string_view s;
    std::size_t i = 0;

    void ws()
    {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])))
            ++i;
    }

    bool str(Utf8String *out)
    {
        if (i >= s.size() || s[i] != '"')
            return false;
        ++i;
        Utf8String r;
        while (i < s.size() && s[i] != '"') {
            char c = s[i++];
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
                // A surrogate pair is joined; a lone half becomes U+FFFD.
                if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 <= s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                    unsigned lo = 0;
                    bool ok = true;
                    for (int k = 0; k < 4; ++k) {
                        const char h = s[i + 2 + k];
                        lo <<= 4;
                        if (h >= '0' && h <= '9') lo |= static_cast<unsigned>(h - '0');
                        else if (h >= 'a' && h <= 'f') lo |= static_cast<unsigned>(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') lo |= static_cast<unsigned>(h - 'A' + 10);
                        else ok = false;
                    }
                    if (ok && lo >= 0xDC00 && lo <= 0xDFFF) {
                        i += 6;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                }
                if (cp >= 0xD800 && cp <= 0xDFFF)
                    cp = 0xFFFD;
                if (cp < 0x80) {
                    r.push_back(static_cast<char>(cp));
                } else if (cp < 0x800) {
                    r.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                    r.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                } else if (cp < 0x10000) {
                    r.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                    r.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                    r.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                } else {
                    r.push_back(static_cast<char>(0xF0 | (cp >> 18)));
                    r.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
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

    // Skips one value and gives its raw text.
    bool value(std::string_view *raw)
    {
        ws();
        const std::size_t start = i;
        if (i >= s.size())
            return false;
        const char c = s[i];
        if (c == '"') {
            Utf8String tmp;
            if (!str(&tmp))
                return false;
        } else if (c == '{' || c == '[') {
            const char close = c == '{' ? '}' : ']';
            ++i;
            ws();
            if (i < s.size() && s[i] == close) {
                ++i;
            } else {
                for (;;) {
                    if (c == '{') {
                        Utf8String key;
                        ws();
                        if (!str(&key))
                            return false;
                        ws();
                        if (i >= s.size() || s[i] != ':')
                            return false;
                        ++i;
                    }
                    std::string_view inner;
                    if (!value(&inner))
                        return false;
                    ws();
                    if (i < s.size() && s[i] == ',') {
                        ++i;
                        continue;
                    }
                    if (i < s.size() && s[i] == close) {
                        ++i;
                        break;
                    }
                    return false;
                }
            }
        } else {
            while (i < s.size() && (std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '-' || s[i] == '+' || s[i] == '.'))
                ++i;
            if (i == start)
                return false;
        }
        *raw = s.substr(start, i - start);
        return true;
    }

    // Walks an object, calling f(key, rawValue) for each member.
    template <class F>
    bool object(F &&f)
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
            Utf8String key;
            ws();
            if (!str(&key))
                return false;
            ws();
            if (i >= s.size() || s[i] != ':')
                return false;
            ++i;
            std::string_view raw;
            if (!value(&raw))
                return false;
            f(key, raw);
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

bool decodeString(std::string_view raw, Utf8String *out)
{
    Reader r{raw};
    return r.str(out);
}

void appendQuoted(Utf8String *out, const Utf8String &text)
{
    static constexpr char kHex[] = "0123456789abcdef";
    out->push_back('"');
    for (const char c : text) {
        const auto u = static_cast<unsigned char>(c);
        if (c == '"' || c == '\\') {
            out->push_back('\\');
            out->push_back(c);
        } else if (u < 0x20) {
            *out += "\\u00";
            out->push_back(kHex[u >> 4]);
            out->push_back(kHex[u & 0x0f]);
        } else {
            out->push_back(c);
        }
    }
    out->push_back('"');
}

void appendField(Utf8String *out, bool *first, const char *key, const Utf8String &value)
{
    if (value.empty())
        return;
    if (!*first)
        out->push_back(',');
    *first = false;
    appendQuoted(out, key);
    out->push_back(':');
    appendQuoted(out, value);
}

// True when the text is exactly one JSON object.
bool isJsonObject(const Utf8String &text)
{
    Reader r{text};
    r.ws();
    if (r.i >= r.s.size() || r.s[r.i] != '{')
        return false;
    std::string_view raw;
    if (!r.value(&raw))
        return false;
    r.ws();
    return r.i == r.s.size();
}

} // namespace

Utf8String discoveryCandidatesToJson(const DiscoveryCandidateList &candidates)
{
    Utf8String out = "{\"candidates\":[";
    bool firstCandidate = true;
    for (const auto &c : candidates) {
        if (!firstCandidate)
            out.push_back(',');
        firstCandidate = false;
        out.push_back('{');
        bool first = true;
        appendField(&out, &first, "externalId", c.externalId());
        appendField(&out, &first, "label", c.label());
        appendField(&out, &first, "host", c.host());
        appendField(&out, &first, "ip", c.ip());
        if (c.port() != 0) {
            if (!first)
                out.push_back(',');
            first = false;
            out += "\"port\":" + std::to_string(c.port());
        }
        appendField(&out, &first, "mac", c.mac());
        appendField(&out, &first, "model", c.model());
        if (isJsonObject(c.metaJson())) {
            if (!first)
                out.push_back(',');
            first = false;
            out += "\"meta\":" + c.metaJson();
        }
        out.push_back('}');
    }
    out += "]}";
    return out;
}

bool discoveryCandidatesFromJson(const Utf8String &json, DiscoveryCandidateList *out, Utf8String *error)
{
    auto fail = [&](const char *why) {
        if (error)
            *error = why;
        return false;
    };
    DiscoveryCandidateList list;
    Reader root{json};
    bool haveArray = false;
    std::string_view arrayRaw;
    if (!root.object([&](const Utf8String &key, std::string_view raw) {
            if (key == "candidates" && !raw.empty() && raw.front() == '[') {
                haveArray = true;
                arrayRaw = raw;
            }
        }))
        return fail("Not a JSON object");
    if (!haveArray)
        return fail("Missing 'candidates' array");

    Reader arr{arrayRaw};
    ++arr.i; // '['
    arr.ws();
    if (arr.i < arr.s.size() && arr.s[arr.i] == ']') {
        if (out)
            *out = std::move(list);
        return true;
    }
    for (;;) {
        std::string_view entry;
        if (!arr.value(&entry))
            return fail("Invalid candidates array");
        if (!entry.empty() && entry.front() == '{') {
            DiscoveryCandidate c;
            Reader er{entry};
            er.object([&](const Utf8String &key, std::string_view raw) {
                Utf8String text;
                if (key == "port") {
                    const long n = std::strtol(Utf8String(raw).c_str(), nullptr, 10);
                    if (n > 0 && n <= 65535)
                        c.setPort(static_cast<std::uint16_t>(n));
                } else if (key == "meta") {
                    if (!raw.empty() && raw.front() == '{')
                        c.setMetaJson(Utf8String(raw));
                } else if (decodeString(raw, &text)) {
                    if (key == "externalId") c.setExternalId(std::move(text));
                    else if (key == "label") c.setLabel(std::move(text));
                    else if (key == "host") c.setHost(std::move(text));
                    else if (key == "ip") c.setIp(std::move(text));
                    else if (key == "mac") c.setMac(std::move(text));
                    else if (key == "model") c.setModel(std::move(text));
                }
            });
            list.push_back(std::move(c));
        }
        arr.ws();
        if (arr.i < arr.s.size() && arr.s[arr.i] == ',') {
            ++arr.i;
            continue;
        }
        break;
    }
    if (out)
        *out = std::move(list);
    return true;
}

ActionResponse &setDiscoveryCandidates(ActionResponse &response, const DiscoveryCandidateList &candidates)
{
    return response.setData(discoveryCandidatesToJson(candidates));
}

int discoveryTimeoutMs(const Utf8String &paramsJson, int fallback)
{
    int result = fallback;
    Reader r{paramsJson};
    r.object([&](const Utf8String &key, std::string_view raw) {
        if (key != "timeoutMs")
            return;
        const Utf8String token(raw);
        char *end = nullptr;
        const double n = std::strtod(token.c_str(), &end);
        if (end && *end == '\0' && !token.empty() && n >= 1 && n <= 2147483647.0)
            result = static_cast<int>(n);
    });
    return result;
}

} // namespace phicore::adapter::v1
