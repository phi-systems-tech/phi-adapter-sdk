#include "phi/adapter/sdk/sidecar.h"

#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace phicore::adapter::sdk {

namespace {

using phicore::adapter::v1::ActionResponse;
using phicore::adapter::v1::ActionResultType;
using phicore::adapter::v1::Adapter;
using phicore::adapter::v1::Channel;
using phicore::adapter::v1::ChannelList;
using phicore::adapter::v1::CmdId;
using phicore::adapter::v1::CmdResponse;
using phicore::adapter::v1::CmdStatus;
using phicore::adapter::v1::CorrelationId;
using phicore::adapter::v1::Device;
using phicore::adapter::v1::DeviceEffect;
using phicore::adapter::v1::Group;
using phicore::adapter::v1::MessageType;
using phicore::adapter::v1::Room;
using phicore::adapter::v1::ScalarList;
using phicore::adapter::v1::ScalarValue;
using phicore::adapter::v1::Scene;
using phicore::adapter::v1::SceneList;

using MemberMap = std::unordered_map<std::string, std::string_view>;

std::int64_t nowMs()
{
    const auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
}

bool isWs(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

void skipWs(std::string_view text, std::size_t &i)
{
    while (i < text.size() && isWs(text[i]))
        ++i;
}

std::string_view trim(std::string_view text)
{
    std::size_t begin = 0;
    while (begin < text.size() && isWs(text[begin]))
        ++begin;
    std::size_t end = text.size();
    while (end > begin && isWs(text[end - 1]))
        --end;
    return text.substr(begin, end - begin);
}

bool skipStringToken(std::string_view text, std::size_t &i, std::string *error)
{
    if (i >= text.size() || text[i] != '"') {
        if (error)
            *error = "Expected JSON string";
        return false;
    }
    ++i;
    while (i < text.size()) {
        const char ch = text[i++];
        if (ch == '"')
            return true;
        if (ch == '\\') {
            if (i >= text.size()) {
                if (error)
                    *error = "Invalid JSON string escape";
                return false;
            }
            ++i;
        }
    }
    if (error)
        *error = "Unterminated JSON string";
    return false;
}

bool skipNumberToken(std::string_view text, std::size_t &i)
{
    if (i < text.size() && (text[i] == '-' || text[i] == '+'))
        ++i;
    bool anyDigit = false;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
        anyDigit = true;
        ++i;
    }
    if (i < text.size() && text[i] == '.') {
        ++i;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
            anyDigit = true;
            ++i;
        }
    }
    if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
        ++i;
        if (i < text.size() && (text[i] == '+' || text[i] == '-'))
            ++i;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
            anyDigit = true;
            ++i;
        }
    }
    return anyDigit;
}

bool skipValueToken(std::string_view text, std::size_t &i, std::string *error);

bool skipArrayToken(std::string_view text, std::size_t &i, std::string *error)
{
    if (i >= text.size() || text[i] != '[') {
        if (error)
            *error = "Expected JSON array";
        return false;
    }
    ++i;
    skipWs(text, i);
    if (i < text.size() && text[i] == ']') {
        ++i;
        return true;
    }
    while (i < text.size()) {
        if (!skipValueToken(text, i, error))
            return false;
        skipWs(text, i);
        if (i < text.size() && text[i] == ',') {
            ++i;
            skipWs(text, i);
            continue;
        }
        if (i < text.size() && text[i] == ']') {
            ++i;
            return true;
        }
        if (error)
            *error = "Invalid JSON array";
        return false;
    }
    if (error)
        *error = "Unterminated JSON array";
    return false;
}

bool skipObjectToken(std::string_view text, std::size_t &i, std::string *error)
{
    if (i >= text.size() || text[i] != '{') {
        if (error)
            *error = "Expected JSON object";
        return false;
    }
    ++i;
    skipWs(text, i);
    if (i < text.size() && text[i] == '}') {
        ++i;
        return true;
    }
    while (i < text.size()) {
        if (!skipStringToken(text, i, error))
            return false;
        skipWs(text, i);
        if (i >= text.size() || text[i] != ':') {
            if (error)
                *error = "Expected ':' in JSON object";
            return false;
        }
        ++i;
        skipWs(text, i);
        if (!skipValueToken(text, i, error))
            return false;
        skipWs(text, i);
        if (i < text.size() && text[i] == ',') {
            ++i;
            skipWs(text, i);
            continue;
        }
        if (i < text.size() && text[i] == '}') {
            ++i;
            return true;
        }
        if (error)
            *error = "Invalid JSON object";
        return false;
    }
    if (error)
        *error = "Unterminated JSON object";
    return false;
}

bool skipValueToken(std::string_view text, std::size_t &i, std::string *error)
{
    skipWs(text, i);
    if (i >= text.size()) {
        if (error)
            *error = "Unexpected end of JSON";
        return false;
    }
    const char ch = text[i];
    if (ch == '"')
        return skipStringToken(text, i, error);
    if (ch == '{')
        return skipObjectToken(text, i, error);
    if (ch == '[')
        return skipArrayToken(text, i, error);
    if (ch == 't' && text.substr(i, 4) == "true") {
        i += 4;
        return true;
    }
    if (ch == 'f' && text.substr(i, 5) == "false") {
        i += 5;
        return true;
    }
    if (ch == 'n' && text.substr(i, 4) == "null") {
        i += 4;
        return true;
    }
    if (skipNumberToken(text, i))
        return true;
    if (error)
        *error = "Invalid JSON value";
    return false;
}

bool decodeJsonString(std::string_view token, std::string *out, std::string *error)
{
    token = trim(token);
    if (token.size() < 2 || token.front() != '"' || token.back() != '"') {
        if (error)
            *error = "Expected JSON string token";
        return false;
    }

    out->clear();
    out->reserve(token.size() - 2);
    for (std::size_t i = 1; i + 1 < token.size(); ++i) {
        const char ch = token[i];
        if (ch != '\\') {
            out->push_back(ch);
            continue;
        }
        if (i + 1 >= token.size() - 1) {
            if (error)
                *error = "Invalid JSON escape";
            return false;
        }
        const char esc = token[++i];
        switch (esc) {
        case '"': out->push_back('"'); break;
        case '\\': out->push_back('\\'); break;
        case '/': out->push_back('/'); break;
        case 'b': out->push_back('\b'); break;
        case 'f': out->push_back('\f'); break;
        case 'n': out->push_back('\n'); break;
        case 'r': out->push_back('\r'); break;
        case 't': out->push_back('\t'); break;
        case 'u': {
            // Keep unicode escapes as-is for now.
            if (i + 4 >= token.size()) {
                if (error)
                    *error = "Invalid unicode escape";
                return false;
            }
            out->push_back('\\');
            out->push_back('u');
            out->append(token.substr(i + 1, 4));
            i += 4;
            break;
        }
        default:
            if (error)
                *error = "Unsupported JSON escape";
            return false;
        }
    }
    return true;
}

bool parseObjectMembers(std::string_view objectJson, MemberMap *out, std::string *error)
{
    objectJson = trim(objectJson);
    std::size_t i = 0;
    if (i >= objectJson.size() || objectJson[i] != '{') {
        if (error)
            *error = "Expected JSON object";
        return false;
    }
    ++i;
    skipWs(objectJson, i);
    out->clear();
    if (i < objectJson.size() && objectJson[i] == '}')
        return true;

    while (i < objectJson.size()) {
        const std::size_t keyStart = i;
        if (!skipStringToken(objectJson, i, error))
            return false;
        std::string key;
        if (!decodeJsonString(objectJson.substr(keyStart, i - keyStart), &key, error))
            return false;

        skipWs(objectJson, i);
        if (i >= objectJson.size() || objectJson[i] != ':') {
            if (error)
                *error = "Expected ':' in JSON object";
            return false;
        }
        ++i;
        skipWs(objectJson, i);

        const std::size_t valueStart = i;
        if (!skipValueToken(objectJson, i, error))
            return false;
        const std::size_t valueEnd = i;
        out->insert_or_assign(std::move(key), trim(objectJson.substr(valueStart, valueEnd - valueStart)));

        skipWs(objectJson, i);
        if (i < objectJson.size() && objectJson[i] == ',') {
            ++i;
            skipWs(objectJson, i);
            continue;
        }
        if (i < objectJson.size() && objectJson[i] == '}')
            return true;
        if (error)
            *error = "Invalid JSON object";
        return false;
    }

    if (error)
        *error = "Unterminated JSON object";
    return false;
}

bool parseArrayElements(std::string_view arrayJson, std::vector<std::string_view> *out, std::string *error)
{
    arrayJson = trim(arrayJson);
    std::size_t i = 0;
    if (i >= arrayJson.size() || arrayJson[i] != '[') {
        if (error)
            *error = "Expected JSON array";
        return false;
    }
    ++i;
    skipWs(arrayJson, i);
    out->clear();
    if (i < arrayJson.size() && arrayJson[i] == ']')
        return true;

    while (i < arrayJson.size()) {
        const std::size_t start = i;
        if (!skipValueToken(arrayJson, i, error))
            return false;
        out->push_back(trim(arrayJson.substr(start, i - start)));
        skipWs(arrayJson, i);
        if (i < arrayJson.size() && arrayJson[i] == ',') {
            ++i;
            skipWs(arrayJson, i);
            continue;
        }
        if (i < arrayJson.size() && arrayJson[i] == ']')
            return true;
        if (error)
            *error = "Invalid JSON array";
        return false;
    }

    if (error)
        *error = "Unterminated JSON array";
    return false;
}

std::string_view member(const MemberMap &map, std::string_view key)
{
    const auto it = map.find(std::string(key));
    if (it == map.end())
        return {};
    return it->second;
}

bool parseInt64(std::string_view token, std::int64_t *value)
{
    token = trim(token);
    if (token.empty())
        return false;
    const char *begin = token.data();
    const char *end = token.data() + token.size();
    std::int64_t parsed = 0;
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc() || result.ptr != end)
        return false;
    *value = parsed;
    return true;
}

bool parseUInt64(std::string_view token, std::uint64_t *value)
{
    token = trim(token);
    if (token.empty())
        return false;
    const char *begin = token.data();
    const char *end = token.data() + token.size();
    std::uint64_t parsed = 0;
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc() || result.ptr != end)
        return false;
    *value = parsed;
    return true;
}

bool parseDouble(std::string_view token, double *value)
{
    token = trim(token);
    if (token.empty())
        return false;
    const std::string copy(token);
    char *end = nullptr;
    const double parsed = std::strtod(copy.c_str(), &end);
    if (!end || *end != '\0')
        return false;
    *value = parsed;
    return true;
}

bool parseCmdIdToken(std::string_view token, CmdId *cmdId)
{
    token = trim(token);
    if (token.empty())
        return false;
    if (token.front() == '"') {
        std::string text;
        if (!decodeJsonString(token, &text, nullptr))
            return false;
        return parseUInt64(text, cmdId);
    }
    return parseUInt64(token, cmdId);
}

bool parseScalarValueToken(std::string_view token, ScalarValue *value)
{
    token = trim(token);
    if (token.empty())
        return false;
    if (token.front() == '"') {
        std::string text;
        if (!decodeJsonString(token, &text, nullptr))
            return false;
        *value = text;
        return true;
    }
    if (token == "true") {
        *value = true;
        return true;
    }
    if (token == "false") {
        *value = false;
        return true;
    }
    if (token == "null") {
        *value = std::monostate{};
        return true;
    }
    if (token.find_first_of(".eE") != std::string_view::npos) {
        double d = 0.0;
        if (!parseDouble(token, &d))
            return false;
        *value = d;
        return true;
    }
    std::int64_t i = 0;
    if (!parseInt64(token, &i))
        return false;
    *value = i;
    return true;
}

std::string decodeStringOrDefault(std::string_view token, std::string_view def = {})
{
    if (token.empty())
        return std::string(def);
    std::string value;
    if (!decodeJsonString(token, &value, nullptr))
        return std::string(def);
    return value;
}

std::int64_t parseIntOrDefault(std::string_view token, std::int64_t fallback = 0)
{
    std::int64_t value = fallback;
    if (!token.empty())
        parseInt64(token, &value);
    return value;
}

std::string jsonQuoted(std::string_view text)
{
    std::string out;
    out.reserve(text.size() + 2);
    out.push_back('"');
    for (const char ch : text) {
        switch (ch) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(ch) < 0x20) {
                out += "\\u00";
                constexpr char kHex[] = "0123456789abcdef";
                out.push_back(kHex[(ch >> 4) & 0x0f]);
                out.push_back(kHex[ch & 0x0f]);
            } else {
                out.push_back(ch);
            }
            break;
        }
    }
    out.push_back('"');
    return out;
}

void appendFieldPrefix(std::string &out, bool &first, std::string_view key)
{
    if (!first)
        out.push_back(',');
    first = false;
    out += jsonQuoted(key);
    out.push_back(':');
}

void appendScalarJson(std::string &out, const ScalarValue &value)
{
    if (std::holds_alternative<std::monostate>(value)) {
        out += "null";
        return;
    }
    if (const auto *b = std::get_if<bool>(&value)) {
        out += (*b ? "true" : "false");
        return;
    }
    if (const auto *i = std::get_if<std::int64_t>(&value)) {
        out += std::to_string(*i);
        return;
    }
    if (const auto *d = std::get_if<double>(&value)) {
        if (std::isfinite(*d))
            out += std::to_string(*d);
        else
            out += "null";
        return;
    }
    out += jsonQuoted(std::get<std::string>(value));
}

void appendScalarListJson(std::string &out, const ScalarList &values)
{
    out.push_back('[');
    bool first = true;
    for (const ScalarValue &value : values) {
        if (!first)
            out.push_back(',');
        first = false;
        appendScalarJson(out, value);
    }
    out.push_back(']');
}

void appendMetaJson(std::string &out, const std::string &json)
{
    if (trim(json).empty()) {
        out += "{}";
        return;
    }
    out += json;
}

void appendArrayOfStrings(std::string &out, const std::vector<std::string> &values)
{
    out.push_back('[');
    bool first = true;
    for (const std::string &value : values) {
        if (!first)
            out.push_back(',');
        first = false;
        out += jsonQuoted(value);
    }
    out.push_back(']');
}

std::string deviceToJson(const Device &device)
{
    std::string out;
    out.push_back('{');
    bool first = true;
    appendFieldPrefix(out, first, "id");
    out += jsonQuoted(device.externalId);
    appendFieldPrefix(out, first, "name");
    out += jsonQuoted(device.name);
    appendFieldPrefix(out, first, "deviceClass");
    out += std::to_string(static_cast<int>(device.deviceClass));
    appendFieldPrefix(out, first, "flags");
    out += std::to_string(static_cast<int>(device.flags));
    appendFieldPrefix(out, first, "manufacturer");
    out += jsonQuoted(device.manufacturer);
    appendFieldPrefix(out, first, "firmware");
    out += jsonQuoted(device.firmware);
    appendFieldPrefix(out, first, "model");
    out += jsonQuoted(device.model);
    appendFieldPrefix(out, first, "meta");
    appendMetaJson(out, device.metaJson);
    appendFieldPrefix(out, first, "effects");
    out.push_back('[');
    bool firstEffect = true;
    for (const auto &effect : device.effects) {
        if (!firstEffect)
            out.push_back(',');
        firstEffect = false;
        out.push_back('{');
        bool firstField = true;
        appendFieldPrefix(out, firstField, "effect");
        out += std::to_string(static_cast<int>(effect.effect));
        appendFieldPrefix(out, firstField, "id");
        out += jsonQuoted(effect.id);
        appendFieldPrefix(out, firstField, "label");
        out += jsonQuoted(effect.label);
        appendFieldPrefix(out, firstField, "description");
        out += jsonQuoted(effect.description);
        appendFieldPrefix(out, firstField, "requiresParams");
        out += (effect.requiresParams ? "true" : "false");
        appendFieldPrefix(out, firstField, "meta");
        appendMetaJson(out, effect.metaJson);
        out.push_back('}');
    }
    out.push_back(']');
    out.push_back('}');
    return out;
}

std::string channelToJson(const Channel &channel)
{
    std::string out;
    out.push_back('{');
    bool first = true;
    appendFieldPrefix(out, first, "id");
    out += jsonQuoted(channel.externalId);
    appendFieldPrefix(out, first, "name");
    out += jsonQuoted(channel.name);
    appendFieldPrefix(out, first, "kind");
    out += std::to_string(static_cast<int>(channel.kind));
    appendFieldPrefix(out, first, "dataType");
    out += std::to_string(static_cast<int>(channel.dataType));
    appendFieldPrefix(out, first, "flags");
    out += std::to_string(static_cast<int>(channel.flags));
    appendFieldPrefix(out, first, "unit");
    out += jsonQuoted(channel.unit);
    appendFieldPrefix(out, first, "minValue");
    out += std::to_string(channel.minValue);
    appendFieldPrefix(out, first, "maxValue");
    out += std::to_string(channel.maxValue);
    appendFieldPrefix(out, first, "stepValue");
    out += std::to_string(channel.stepValue);
    appendFieldPrefix(out, first, "meta");
    appendMetaJson(out, channel.metaJson);
    appendFieldPrefix(out, first, "choices");
    out.push_back('[');
    bool firstChoice = true;
    for (const auto &choice : channel.choices) {
        if (!firstChoice)
            out.push_back(',');
        firstChoice = false;
        out += "{\"value\":";
        out += jsonQuoted(choice.value);
        out += ",\"label\":";
        out += jsonQuoted(choice.label);
        out.push_back('}');
    }
    out.push_back(']');
    appendFieldPrefix(out, first, "lastValue");
    appendScalarJson(out, channel.lastValue);
    appendFieldPrefix(out, first, "lastUpdateMs");
    out += std::to_string(channel.lastUpdateMs);
    appendFieldPrefix(out, first, "hasValue");
    out += (channel.hasValue ? "true" : "false");
    out.push_back('}');
    return out;
}

std::string roomToJson(const Room &room)
{
    std::string out;
    out.push_back('{');
    bool first = true;
    appendFieldPrefix(out, first, "externalId");
    out += jsonQuoted(room.externalId);
    appendFieldPrefix(out, first, "name");
    out += jsonQuoted(room.name);
    appendFieldPrefix(out, first, "zone");
    out += jsonQuoted(room.zone);
    appendFieldPrefix(out, first, "deviceExternalIds");
    appendArrayOfStrings(out, room.deviceExternalIds);
    appendFieldPrefix(out, first, "meta");
    appendMetaJson(out, room.metaJson);
    out.push_back('}');
    return out;
}

std::string groupToJson(const Group &group)
{
    std::string out;
    out.push_back('{');
    bool first = true;
    appendFieldPrefix(out, first, "id");
    out += jsonQuoted(group.externalId);
    appendFieldPrefix(out, first, "name");
    out += jsonQuoted(group.name);
    appendFieldPrefix(out, first, "zone");
    out += jsonQuoted(group.zone);
    appendFieldPrefix(out, first, "deviceExternalIds");
    appendArrayOfStrings(out, group.deviceExternalIds);
    appendFieldPrefix(out, first, "meta");
    appendMetaJson(out, group.metaJson);
    out.push_back('}');
    return out;
}

std::string sceneToJson(const Scene &scene)
{
    std::string out;
    out.push_back('{');
    bool first = true;
    appendFieldPrefix(out, first, "id");
    out += jsonQuoted(scene.externalId);
    appendFieldPrefix(out, first, "name");
    out += jsonQuoted(scene.name);
    appendFieldPrefix(out, first, "description");
    out += jsonQuoted(scene.description);
    appendFieldPrefix(out, first, "scopeId");
    out += jsonQuoted(scene.scopeExternalId);
    appendFieldPrefix(out, first, "scopeType");
    out += jsonQuoted(scene.scopeType);
    appendFieldPrefix(out, first, "avatarColor");
    out += jsonQuoted(scene.avatarColor);
    appendFieldPrefix(out, first, "image");
    out += jsonQuoted(scene.image);
    appendFieldPrefix(out, first, "presetTag");
    out += jsonQuoted(scene.presetTag);
    appendFieldPrefix(out, first, "state");
    out += std::to_string(static_cast<int>(scene.state));
    appendFieldPrefix(out, first, "flags");
    out += std::to_string(static_cast<int>(scene.flags));
    appendFieldPrefix(out, first, "meta");
    appendMetaJson(out, scene.metaJson);
    out.push_back('}');
    return out;
}

CmdResponse defaultCmdResponse(CmdId cmdId, const std::string &message)
{
    CmdResponse response;
    response.id = cmdId;
    response.status = CmdStatus::NotImplemented;
    response.error = message;
    response.tsMs = nowMs();
    return response;
}

ActionResponse defaultActionResponse(CmdId cmdId, const std::string &message)
{
    ActionResponse response;
    response.id = cmdId;
    response.status = CmdStatus::NotImplemented;
    response.error = message;
    response.resultType = ActionResultType::None;
    response.tsMs = nowMs();
    return response;
}

} // namespace

SidecarDispatcher::SidecarDispatcher(std::string socketPath)
    : m_runtime(std::move(socketPath))
{
    RuntimeCallbacks callbacks;
    callbacks.onConnected = [this]() {
        if (m_handlers.onConnected)
            m_handlers.onConnected();
    };
    callbacks.onDisconnected = [this]() {
        if (m_handlers.onDisconnected)
            m_handlers.onDisconnected();
    };
    callbacks.onFrame = [this](const phicore::adapter::v1::FrameHeader &header,
                               std::span<const std::byte> payload) {
        if (phicore::adapter::v1::messageType(header) != MessageType::Request)
            return;
        handleRequestFrame(header, payload);
    };
    m_runtime.setCallbacks(std::move(callbacks));
}

void SidecarDispatcher::setHandlers(SidecarHandlers handlers)
{
    m_handlers = std::move(handlers);
}

bool SidecarDispatcher::start(std::string *error)
{
    return m_runtime.start(error);
}

void SidecarDispatcher::stop()
{
    m_runtime.stop();
}

bool SidecarDispatcher::pollOnce(std::chrono::milliseconds timeout, std::string *error)
{
    return m_runtime.pollOnce(timeout, error);
}

bool SidecarDispatcher::handleRequestFrame(const phicore::adapter::v1::FrameHeader &header,
                                           std::span<const std::byte> payload)
{
    (void)header;

    const std::string jsonPayload(reinterpret_cast<const char *>(payload.data()), payload.size());
    MemberMap root;
    std::string parseError;
    if (!parseObjectMembers(jsonPayload, &root, &parseError)) {
        if (m_handlers.onProtocolError)
            m_handlers.onProtocolError("Invalid request JSON: " + parseError);
        return false;
    }

    const std::string method = decodeStringOrDefault(member(root, "method"));
    CmdId cmdId = 0;
    const std::string_view cmdIdToken = member(root, "cmdId");
    if (!cmdIdToken.empty())
        parseCmdIdToken(cmdIdToken, &cmdId);

    std::string_view payloadToken = member(root, "payload");
    if (payloadToken.empty())
        payloadToken = "{}";

    if (method == "sync.adapter.bootstrap") {
        if (m_handlers.onBootstrap) {
            BootstrapRequest request;
            MemberMap payloadMap;
            if (parseObjectMembers(payloadToken, &payloadMap, nullptr)) {
                request.adapterId = static_cast<int>(parseIntOrDefault(member(payloadMap, "adapterId"), 0));
                request.staticConfigJson = std::string(member(payloadMap, "staticConfig"));

                MemberMap adapterMap;
                const std::string_view adapterToken = member(payloadMap, "adapter");
                if (parseObjectMembers(adapterToken, &adapterMap, nullptr)) {
                    request.adapter.name = decodeStringOrDefault(member(adapterMap, "name"));
                    request.adapter.host = decodeStringOrDefault(member(adapterMap, "host"));
                    request.adapter.ip = decodeStringOrDefault(member(adapterMap, "ip"));
                    request.adapter.port =
                        static_cast<std::uint16_t>(parseIntOrDefault(member(adapterMap, "port"), 0));
                    request.adapter.user = decodeStringOrDefault(member(adapterMap, "user"));
                    request.adapter.password = decodeStringOrDefault(member(adapterMap, "pw"));
                    if (request.adapter.password.empty())
                        request.adapter.password = decodeStringOrDefault(member(adapterMap, "password"));
                    request.adapter.token = decodeStringOrDefault(member(adapterMap, "token"));
                    request.adapter.pluginType = decodeStringOrDefault(member(adapterMap, "plugin"));
                    if (request.adapter.pluginType.empty())
                        request.adapter.pluginType = decodeStringOrDefault(member(adapterMap, "pluginType"));
                    request.adapter.externalId = decodeStringOrDefault(member(adapterMap, "id"));
                    if (request.adapter.externalId.empty())
                        request.adapter.externalId = decodeStringOrDefault(member(adapterMap, "externalId"));
                    request.adapter.metaJson = std::string(member(adapterMap, "meta"));
                    request.adapter.flags = static_cast<phicore::adapter::v1::AdapterFlag>(
                        parseIntOrDefault(member(adapterMap, "flags"), 0));
                }
            }
            m_handlers.onBootstrap(request);
        }
        return true;
    }

    MemberMap payloadMap;
    if (!parseObjectMembers(payloadToken, &payloadMap, nullptr))
        payloadMap.clear();

    if (method == "cmd.channel.invoke") {
        ChannelInvokeRequest request;
        request.cmdId = cmdId;
        request.deviceExternalId = decodeStringOrDefault(member(payloadMap, "deviceExternalId"));
        if (request.deviceExternalId.empty())
            request.deviceExternalId = decodeStringOrDefault(member(payloadMap, "deviceId"));
        request.channelExternalId = decodeStringOrDefault(member(payloadMap, "channelExternalId"));
        if (request.channelExternalId.empty())
            request.channelExternalId = decodeStringOrDefault(member(payloadMap, "channelId"));
        const std::string_view valueToken = member(payloadMap, "value");
        request.valueJson = std::string(valueToken);
        request.hasScalarValue = parseScalarValueToken(valueToken, &request.value);

        CmdResponse response = m_handlers.onChannelInvoke
            ? m_handlers.onChannelInvoke(request)
            : defaultCmdResponse(cmdId, "Channel invoke handler not registered");
        if (response.id == 0)
            response.id = cmdId;
        return sendCmdResult(response, nullptr);
    }

    if (method == "cmd.adapter.action.invoke") {
        AdapterActionInvokeRequest request;
        request.cmdId = cmdId;
        request.actionId = decodeStringOrDefault(member(payloadMap, "actionId"));
        request.paramsJson = std::string(member(payloadMap, "params"));
        if (trim(request.paramsJson).empty())
            request.paramsJson = "{}";

        ActionResponse response = m_handlers.onAdapterActionInvoke
            ? m_handlers.onAdapterActionInvoke(request)
            : defaultActionResponse(cmdId, "Adapter action handler not registered");
        if (response.id == 0)
            response.id = cmdId;
        return sendActionResult(response, nullptr);
    }

    if (method == "cmd.device.name.update") {
        DeviceNameUpdateRequest request;
        request.cmdId = cmdId;
        request.deviceExternalId = decodeStringOrDefault(member(payloadMap, "deviceExternalId"));
        if (request.deviceExternalId.empty())
            request.deviceExternalId = decodeStringOrDefault(member(payloadMap, "deviceId"));
        request.name = decodeStringOrDefault(member(payloadMap, "name"));

        CmdResponse response = m_handlers.onDeviceNameUpdate
            ? m_handlers.onDeviceNameUpdate(request)
            : defaultCmdResponse(cmdId, "Device name update handler not registered");
        if (response.id == 0)
            response.id = cmdId;
        return sendCmdResult(response, nullptr);
    }

    if (method == "cmd.device.effect.invoke") {
        DeviceEffectInvokeRequest request;
        request.cmdId = cmdId;
        request.deviceExternalId = decodeStringOrDefault(member(payloadMap, "deviceExternalId"));
        if (request.deviceExternalId.empty())
            request.deviceExternalId = decodeStringOrDefault(member(payloadMap, "deviceId"));
        request.effect = static_cast<DeviceEffect>(parseIntOrDefault(member(payloadMap, "effect"), 0));
        request.effectId = decodeStringOrDefault(member(payloadMap, "effectId"));
        request.paramsJson = std::string(member(payloadMap, "params"));
        if (trim(request.paramsJson).empty())
            request.paramsJson = "{}";

        CmdResponse response = m_handlers.onDeviceEffectInvoke
            ? m_handlers.onDeviceEffectInvoke(request)
            : defaultCmdResponse(cmdId, "Device effect handler not registered");
        if (response.id == 0)
            response.id = cmdId;
        return sendCmdResult(response, nullptr);
    }

    if (method == "cmd.scene.invoke") {
        SceneInvokeRequest request;
        request.cmdId = cmdId;
        request.sceneExternalId = decodeStringOrDefault(member(payloadMap, "sceneExternalId"));
        if (request.sceneExternalId.empty())
            request.sceneExternalId = decodeStringOrDefault(member(payloadMap, "sceneId"));
        request.groupExternalId = decodeStringOrDefault(member(payloadMap, "groupExternalId"));
        request.action = decodeStringOrDefault(member(payloadMap, "action"));

        CmdResponse response = m_handlers.onSceneInvoke
            ? m_handlers.onSceneInvoke(request)
            : defaultCmdResponse(cmdId, "Scene invoke handler not registered");
        if (response.id == 0)
            response.id = cmdId;
        return sendCmdResult(response, nullptr);
    }

    if (m_handlers.onUnknownRequest) {
        UnknownRequest request;
        request.cmdId = cmdId;
        request.method = method;
        request.payloadJson = std::string(payloadToken);
        m_handlers.onUnknownRequest(request);
    }
    if (cmdId != 0) {
        CmdResponse response = defaultCmdResponse(cmdId, "Unhandled IPC method: " + method);
        return sendCmdResult(response, nullptr);
    }
    return true;
}

bool SidecarDispatcher::sendJson(MessageType type,
                                 CorrelationId correlationId,
                                 std::string_view json,
                                 std::string *error)
{
    const auto chars = std::span<const char>(json.data(), json.size());
    const auto bytes = std::as_bytes(chars);
    return m_runtime.send(type, correlationId, bytes, error);
}

bool SidecarDispatcher::sendCmdResult(const CmdResponse &response, std::string *error)
{
    const std::int64_t tsMs = response.tsMs > 0 ? response.tsMs : nowMs();
    std::string body;
    body.push_back('{');
    bool first = true;
    appendFieldPrefix(body, first, "kind");
    body += "\"cmdResult\"";
    appendFieldPrefix(body, first, "cmdId");
    body += jsonQuoted(std::to_string(response.id));
    appendFieldPrefix(body, first, "status");
    body += std::to_string(static_cast<int>(response.status));
    appendFieldPrefix(body, first, "error");
    body += jsonQuoted(response.error);
    appendFieldPrefix(body, first, "errorCtx");
    body += jsonQuoted(response.errorContext);
    appendFieldPrefix(body, first, "errorParams");
    appendScalarListJson(body, response.errorParams);
    appendFieldPrefix(body, first, "finalValue");
    appendScalarJson(body, response.finalValue);
    appendFieldPrefix(body, first, "tsMs");
    body += std::to_string(tsMs);
    body.push_back('}');
    return sendJson(MessageType::Response, response.id, body, error);
}

bool SidecarDispatcher::sendActionResult(const ActionResponse &response, std::string *error)
{
    const std::int64_t tsMs = response.tsMs > 0 ? response.tsMs : nowMs();
    std::string body;
    body.push_back('{');
    bool first = true;
    appendFieldPrefix(body, first, "kind");
    body += "\"actionResult\"";
    appendFieldPrefix(body, first, "cmdId");
    body += jsonQuoted(std::to_string(response.id));
    appendFieldPrefix(body, first, "status");
    body += std::to_string(static_cast<int>(response.status));
    appendFieldPrefix(body, first, "error");
    body += jsonQuoted(response.error);
    appendFieldPrefix(body, first, "errorCtx");
    body += jsonQuoted(response.errorContext);
    appendFieldPrefix(body, first, "errorParams");
    appendScalarListJson(body, response.errorParams);
    appendFieldPrefix(body, first, "resultType");
    body += std::to_string(static_cast<int>(response.resultType));
    appendFieldPrefix(body, first, "resultValue");
    appendScalarJson(body, response.resultValue);
    appendFieldPrefix(body, first, "tsMs");
    body += std::to_string(tsMs);
    body.push_back('}');
    return sendJson(MessageType::Response, response.id, body, error);
}

bool SidecarDispatcher::sendConnectionStateChanged(bool connected, std::string *error)
{
    const std::string body = std::string("{\"kind\":\"connectionStateChanged\",\"connected\":")
        + (connected ? "true" : "false")
        + "}";
    return sendJson(MessageType::Event, 0, body, error);
}

bool SidecarDispatcher::sendAdapterError(const std::string &message,
                                         const ScalarList &params,
                                         const std::string &ctx,
                                         std::string *error)
{
    std::string body;
    body.push_back('{');
    bool first = true;
    appendFieldPrefix(body, first, "kind");
    body += "\"error\"";
    appendFieldPrefix(body, first, "message");
    body += jsonQuoted(message);
    appendFieldPrefix(body, first, "ctx");
    body += jsonQuoted(ctx);
    appendFieldPrefix(body, first, "params");
    appendScalarListJson(body, params);
    body.push_back('}');
    return sendJson(MessageType::Event, 0, body, error);
}

bool SidecarDispatcher::sendAdapterMetaUpdated(const phicore::adapter::v1::JsonText &metaPatchJson,
                                               std::string *error)
{
    const std::string patch = trim(metaPatchJson).empty() ? "{}" : metaPatchJson;
    const std::string body = std::string("{\"kind\":\"adapterMetaUpdated\",\"metaPatch\":") + patch + "}";
    return sendJson(MessageType::Event, 0, body, error);
}

bool SidecarDispatcher::sendChannelStateUpdated(const phicore::adapter::v1::ExternalId &deviceExternalId,
                                                const phicore::adapter::v1::ExternalId &channelExternalId,
                                                const ScalarValue &value,
                                                std::int64_t tsMs,
                                                std::string *error)
{
    const std::int64_t timestamp = tsMs > 0 ? tsMs : nowMs();
    std::string body;
    body.push_back('{');
    bool first = true;
    appendFieldPrefix(body, first, "kind");
    body += "\"channelStateUpdated\"";
    appendFieldPrefix(body, first, "deviceExternalId");
    body += jsonQuoted(deviceExternalId);
    appendFieldPrefix(body, first, "channelExternalId");
    body += jsonQuoted(channelExternalId);
    appendFieldPrefix(body, first, "value");
    appendScalarJson(body, value);
    appendFieldPrefix(body, first, "tsMs");
    body += std::to_string(timestamp);
    body.push_back('}');
    return sendJson(MessageType::Event, 0, body, error);
}

bool SidecarDispatcher::sendDeviceUpdated(const Device &device,
                                          const ChannelList &channels,
                                          std::string *error)
{
    std::string body = "{\"kind\":\"deviceUpdated\",\"payload\":{\"device\":";
    body += deviceToJson(device);
    body += ",\"channels\":[";
    bool first = true;
    for (const Channel &channel : channels) {
        if (!first)
            body.push_back(',');
        first = false;
        body += channelToJson(channel);
    }
    body += "]}}";
    return sendJson(MessageType::Event, 0, body, error);
}

bool SidecarDispatcher::sendDeviceRemoved(const phicore::adapter::v1::ExternalId &deviceExternalId,
                                          std::string *error)
{
    const std::string body = std::string("{\"kind\":\"deviceRemoved\",\"deviceExternalId\":")
        + jsonQuoted(deviceExternalId)
        + "}";
    return sendJson(MessageType::Event, 0, body, error);
}

bool SidecarDispatcher::sendChannelUpdated(const phicore::adapter::v1::ExternalId &deviceExternalId,
                                           const Channel &channel,
                                           std::string *error)
{
    std::string body = "{\"kind\":\"channelUpdated\",\"payload\":{\"deviceExternalId\":";
    body += jsonQuoted(deviceExternalId);
    body += ",\"channel\":";
    body += channelToJson(channel);
    body += "}}";
    return sendJson(MessageType::Event, 0, body, error);
}

bool SidecarDispatcher::sendRoomUpdated(const Room &room, std::string *error)
{
    std::string body = "{\"kind\":\"roomUpdated\",\"room\":";
    body += roomToJson(room);
    body += "}";
    return sendJson(MessageType::Event, 0, body, error);
}

bool SidecarDispatcher::sendRoomRemoved(const phicore::adapter::v1::ExternalId &roomExternalId,
                                        std::string *error)
{
    const std::string body = std::string("{\"kind\":\"roomRemoved\",\"roomExternalId\":")
        + jsonQuoted(roomExternalId)
        + "}";
    return sendJson(MessageType::Event, 0, body, error);
}

bool SidecarDispatcher::sendGroupUpdated(const Group &group, std::string *error)
{
    std::string body = "{\"kind\":\"groupUpdated\",\"group\":";
    body += groupToJson(group);
    body += "}";
    return sendJson(MessageType::Event, 0, body, error);
}

bool SidecarDispatcher::sendGroupRemoved(const phicore::adapter::v1::ExternalId &groupExternalId,
                                         std::string *error)
{
    const std::string body = std::string("{\"kind\":\"groupRemoved\",\"groupExternalId\":")
        + jsonQuoted(groupExternalId)
        + "}";
    return sendJson(MessageType::Event, 0, body, error);
}

bool SidecarDispatcher::sendScenesUpdated(const SceneList &scenes, std::string *error)
{
    std::string body = "{\"kind\":\"scenesUpdated\",\"scenes\":[";
    bool first = true;
    for (const Scene &scene : scenes) {
        if (!first)
            body.push_back(',');
        first = false;
        body += sceneToJson(scene);
    }
    body += "]}";
    return sendJson(MessageType::Event, 0, body, error);
}

bool SidecarDispatcher::sendFullSyncCompleted(std::string *error)
{
    return sendJson(MessageType::Event, 0, "{\"kind\":\"fullSyncCompleted\"}", error);
}

} // namespace phicore::adapter::sdk
