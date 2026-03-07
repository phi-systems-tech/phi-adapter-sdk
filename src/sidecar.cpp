#include "phi/adapter/sdk/sidecar.h"
#include "runtime_internal.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cctype>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <locale>
#include <sstream>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace phicore::adapter::sdk {

namespace {

using phicore::adapter::v1::ActionResponse;
using phicore::adapter::v1::ActionResultType;
using phicore::adapter::v1::Adapter;
using phicore::adapter::v1::AdapterActionDescriptor;
using phicore::adapter::v1::AdapterCapabilities;
using phicore::adapter::v1::Channel;
using phicore::adapter::v1::ChannelList;
using phicore::adapter::v1::CmdId;
using phicore::adapter::v1::CmdResponse;
using phicore::adapter::v1::CmdStatus;
using phicore::adapter::v1::CorrelationId;
using phicore::adapter::v1::Device;
using phicore::adapter::v1::DeviceEffect;
using phicore::adapter::v1::Group;
using phicore::adapter::v1::IpcCommand;
using phicore::adapter::v1::MessageType;
using phicore::adapter::v1::Room;
using phicore::adapter::v1::ScalarList;
using phicore::adapter::v1::ScalarValue;
using phicore::adapter::v1::Scene;
using phicore::adapter::v1::SceneList;

std::string_view logLevelName(LogLevel level)
{
    switch (level) {
    case LogLevel::Trace:
        return "trace";
    case LogLevel::Debug:
        return "debug";
    case LogLevel::Info:
        return "info";
    case LogLevel::Warn:
        return "warn";
    case LogLevel::Error:
        return "error";
    }
    return "info";
}

std::string_view logCategoryName(LogCategory category)
{
    switch (category) {
    case LogCategory::Event:
        return "event";
    case LogCategory::Lifecycle:
        return "lifecycle";
    case LogCategory::Discovery:
        return "discovery";
    case LogCategory::Network:
        return "network";
    case LogCategory::Protocol:
        return "protocol";
    case LogCategory::DeviceState:
        return "deviceState";
    case LogCategory::Config:
        return "config";
    case LogCategory::Performance:
        return "performance";
    case LogCategory::Security:
        return "security";
    case LogCategory::Internal:
        return "internal";
    }
    return "internal";
}

std::string jsonQuoted(std::string_view text);

std::string_view fileNameOnly(std::string_view path)
{
    std::size_t slashPos = path.find_last_of('/');
    std::size_t backslashPos = path.find_last_of('\\');
    std::size_t pos = std::string_view::npos;
    if (slashPos == std::string_view::npos)
        pos = backslashPos;
    else if (backslashPos == std::string_view::npos)
        pos = slashPos;
    else
        pos = std::max(slashPos, backslashPos);
    if (pos == std::string_view::npos || pos + 1 >= path.size())
        return path;
    return path.substr(pos + 1);
}

using MemberMap = std::unordered_map<std::string, std::string_view>;

std::int64_t nowMs()
{
    const auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
}

constexpr auto kExecutionBackendStopTimeout = std::chrono::seconds(3);

bool isWs(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

class DefaultInstanceExecutionBackend final : public InstanceExecutionBackend
{
public:
    ~DefaultInstanceExecutionBackend() override
    {
        phicore::adapter::v1::Utf8String ignoreError;
        stop(std::chrono::seconds(3), &ignoreError);
    }

    bool start(phicore::adapter::v1::Utf8String *error = nullptr) override
    {
        std::lock_guard<std::mutex> lock(m_lifecycleMutex);
        if (!m_state)
            m_state = std::make_shared<State>();
        if (m_thread.joinable()) {
            if (error)
                *error = "Execution backend thread still active";
            return false;
        }
        if (m_state->started)
            return true;
        m_state->stopRequested = false;
        m_state->workerExited = false;
        try {
            auto state = m_state;
            m_thread = std::thread([state]() {
                run(state);
            });
        } catch (const std::exception &ex) {
            m_state->workerExited = true;
            if (error)
                *error = std::string("Failed to create execution thread: ") + ex.what();
            return false;
        }
        m_state->started = true;
        return true;
    }

    bool execute(std::function<void()> task, phicore::adapter::v1::Utf8String *error = nullptr) override
    {
        if (!task) {
            if (error)
                *error = "Execution task is empty";
            return false;
        }
        auto state = m_state;
        if (!state) {
            if (error)
                *error = "Execution backend not initialized";
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (!state->started) {
                if (error)
                    *error = "Execution backend not started";
                return false;
            }
            if (state->stopRequested) {
                if (error)
                    *error = "Execution backend is stopping";
                return false;
            }
            state->tasks.push_back(std::move(task));
        }
        state->taskCv.notify_one();
        return true;
    }

    bool stop(std::chrono::milliseconds timeout,
              phicore::adapter::v1::Utf8String *error = nullptr) override
    {
        std::shared_ptr<State> state;
        std::thread thread;
        {
            std::lock_guard<std::mutex> lock(m_lifecycleMutex);
            state = m_state;
            if (!state)
                return true;
            if (!state->started && !m_thread.joinable())
                return true;
            {
                std::lock_guard<std::mutex> stateLock(state->mutex);
                state->stopRequested = true;
            }
            thread = std::move(m_thread);
        }
        state->taskCv.notify_one();
        if (!thread.joinable()) {
            std::lock_guard<std::mutex> stateLock(state->mutex);
            state->started = false;
            state->stopRequested = false;
            state->tasks.clear();
            state->workerExited = true;
            return true;
        }

        const auto effectiveTimeout = timeout < std::chrono::milliseconds::zero()
            ? std::chrono::milliseconds::zero()
            : timeout;
        bool exited = false;
        {
            std::unique_lock<std::mutex> stateLock(state->mutex);
            exited = state->exitCv.wait_for(stateLock, effectiveTimeout, [&state]() {
                return state->workerExited;
            });
        }

        if (!exited) {
            thread.detach();
            {
                std::lock_guard<std::mutex> lock(m_lifecycleMutex);
                if (m_state == state)
                    m_state = std::make_shared<State>();
            }
            if (error)
                *error = "Timed out waiting for execution backend stop";
            return false;
        }

        thread.join();
        {
            std::lock_guard<std::mutex> stateLock(state->mutex);
            state->started = false;
            state->stopRequested = false;
            state->tasks.clear();
        }
        return true;
    }

private:
    struct State {
        std::mutex mutex;
        std::condition_variable taskCv;
        std::condition_variable exitCv;
        std::deque<std::function<void()>> tasks;
        bool started = false;
        bool stopRequested = false;
        bool workerExited = true;
    };

    static void run(const std::shared_ptr<State> &state)
    {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(state->mutex);
                state->taskCv.wait(lock, [&state]() {
                    return state->stopRequested || !state->tasks.empty();
                });
                if (state->tasks.empty() && state->stopRequested)
                    break;
                if (state->tasks.empty())
                    continue;
                task = std::move(state->tasks.front());
                state->tasks.pop_front();
            }

            try {
                task();
            } catch (...) {
                // Adapter exceptions are contained so the runtime thread remains alive.
            }
        }
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->workerExited = true;
        }
        state->exitCv.notify_all();
    }

    std::mutex m_lifecycleMutex;
    std::shared_ptr<State> m_state = std::make_shared<State>();
    std::thread m_thread;
};

} // namespace

phicore::adapter::v1::JsonText makeSourceLocationFieldsJson(const char *file,
                                                            int line,
                                                            const char *functionName)
{
    const std::string_view fileView = file ? std::string_view(file) : std::string_view();
    const std::string_view fnView = functionName ? std::string_view(functionName) : std::string_view();
    const std::string body = std::string("{\"file\":")
        + jsonQuoted(std::string(fileNameOnly(fileView)))
        + ",\"line\":"
        + std::to_string(line > 0 ? line : 0)
        + ",\"func\":"
        + jsonQuoted(std::string(fnView))
        + "}";
    return body;
}

namespace {

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

#if defined(__cpp_lib_to_chars) && (__cpp_lib_to_chars >= 201611L)
    {
        double parsed = 0.0;
        const char *begin = token.data();
        const char *end = token.data() + token.size();
        const auto result = std::from_chars(begin, end, parsed, std::chars_format::general);
        if (result.ec == std::errc() && result.ptr == end) {
            *value = parsed;
            return true;
        }
    }
#endif

    std::istringstream iss{std::string(token)};
    iss.imbue(std::locale::classic());
    double parsed = 0.0;
    iss >> parsed;
    if (iss.fail())
        return false;
    iss >> std::ws;
    if (!iss.eof())
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

bool parseIpcCommandToken(std::string_view token, IpcCommand *command)
{
    if (!command)
        return false;
    std::int64_t raw = 0;
    if (!parseInt64(token, &raw))
        return false;
    if (raw <= 0 || raw > 0xFFFF)
        return false;
    *command = static_cast<IpcCommand>(static_cast<std::uint16_t>(raw));
    return true;
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

std::string toLowerAscii(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (const unsigned char ch : text)
        out.push_back(static_cast<char>(std::tolower(ch)));
    return out;
}

std::string normalizeCategoryToken(std::string_view text)
{
    const std::string lower = toLowerAscii(trim(text));
    std::string out;
    out.reserve(lower.size());
    for (const char ch : lower) {
        if (ch == '_' || ch == '-' || ch == ' ' || ch == '.')
            continue;
        out.push_back(ch);
    }
    return out;
}

int logLevelPriority(LogLevel level)
{
    switch (level) {
    case LogLevel::Trace:
        return 0;
    case LogLevel::Debug:
        return 1;
    case LogLevel::Info:
        return 2;
    case LogLevel::Warn:
        return 3;
    case LogLevel::Error:
        return 4;
    }
    return 2;
}

bool parseLogLevelFilter(std::string_view text, LogLevel *out)
{
    if (!out)
        return false;
    const std::string value = toLowerAscii(trim(text));
    if (value == "trace") {
        *out = LogLevel::Trace;
        return true;
    }
    if (value == "debug") {
        *out = LogLevel::Debug;
        return true;
    }
    if (value == "info") {
        *out = LogLevel::Info;
        return true;
    }
    if (value == "warn" || value == "warning") {
        *out = LogLevel::Warn;
        return true;
    }
    if (value == "error" || value == "critical") {
        *out = LogLevel::Error;
        return true;
    }
    return false;
}

int logCategoryIndex(LogCategory category)
{
    switch (category) {
    case LogCategory::Event:
        return 0;
    case LogCategory::Lifecycle:
        return 1;
    case LogCategory::Discovery:
        return 2;
    case LogCategory::Network:
        return 3;
    case LogCategory::Protocol:
        return 4;
    case LogCategory::DeviceState:
        return 5;
    case LogCategory::Config:
        return 6;
    case LogCategory::Performance:
        return 7;
    case LogCategory::Security:
        return 8;
    case LogCategory::Internal:
        return 9;
    }
    return -1;
}

bool parseLogCategoryFilter(std::string_view text, LogCategory *out)
{
    if (!out)
        return false;
    const std::string value = normalizeCategoryToken(text);
    if (value == "event") {
        *out = LogCategory::Event;
        return true;
    }
    if (value == "lifecycle") {
        *out = LogCategory::Lifecycle;
        return true;
    }
    if (value == "discovery") {
        *out = LogCategory::Discovery;
        return true;
    }
    if (value == "network") {
        *out = LogCategory::Network;
        return true;
    }
    if (value == "protocol") {
        *out = LogCategory::Protocol;
        return true;
    }
    if (value == "devicestate") {
        *out = LogCategory::DeviceState;
        return true;
    }
    if (value == "config") {
        *out = LogCategory::Config;
        return true;
    }
    if (value == "performance") {
        *out = LogCategory::Performance;
        return true;
    }
    if (value == "security") {
        *out = LogCategory::Security;
        return true;
    }
    if (value == "internal") {
        *out = LogCategory::Internal;
        return true;
    }
    return false;
}

struct LogFilterConfig {
    LogLevel minLevel = LogLevel::Debug;
    bool allowAllCategories = true;
    std::array<bool, 10> allowedCategories{};
};

bool parseLogFilterConfig(std::string_view metaJson, LogFilterConfig *out)
{
    if (!out)
        return false;
    *out = LogFilterConfig{};

    const std::string_view metaToken = trim(metaJson);
    if (metaToken.empty())
        return true;

    MemberMap metaMap;
    if (!parseObjectMembers(metaToken, &metaMap, nullptr))
        return false;

    const std::string_view loggingToken = member(metaMap, "logging");
    if (loggingToken.empty())
        return true;

    MemberMap loggingMap;
    if (!parseObjectMembers(loggingToken, &loggingMap, nullptr))
        return false;

    const std::string minLevel = decodeStringOrDefault(member(loggingMap, "minLevel"));
    if (!minLevel.empty()) {
        LogLevel parsedLevel = out->minLevel;
        if (parseLogLevelFilter(minLevel, &parsedLevel))
            out->minLevel = parsedLevel;
    }

    const std::string_view categoriesToken = member(loggingMap, "categories");
    if (categoriesToken.empty())
        return true;

    std::vector<std::string_view> categories;
    if (!parseArrayElements(categoriesToken, &categories, nullptr))
        return false;

    out->allowAllCategories = false;
    out->allowedCategories.fill(false);

    for (const std::string_view itemToken : categories) {
        const std::string item = decodeStringOrDefault(itemToken);
        if (item.empty())
            continue;
        if (normalizeCategoryToken(item) == "all") {
            out->allowAllCategories = true;
            break;
        }
        LogCategory category = LogCategory::Internal;
        if (!parseLogCategoryFilter(item, &category))
            continue;
        const int idx = logCategoryIndex(category);
        if (idx >= 0)
            out->allowedCategories[static_cast<std::size_t>(idx)] = true;
    }

    return true;
}

bool shouldForwardLog(const phicore::adapter::v1::Adapter &adapter,
                      bool hasConfig,
                      LogLevel level,
                      LogCategory category)
{
    if (level == LogLevel::Error)
        return true;
    if (!hasConfig)
        return true;
    if (!phicore::adapter::v1::hasFlag(adapter.flags, phicore::adapter::v1::AdapterFlag::EnableLogs))
        return false;

    LogFilterConfig filter;
    if (!parseLogFilterConfig(adapter.metaJson, &filter))
        filter = LogFilterConfig{};

    if (logLevelPriority(level) < logLevelPriority(filter.minLevel))
        return false;
    if (filter.allowAllCategories)
        return true;

    const int idx = logCategoryIndex(category);
    if (idx < 0)
        return false;
    return filter.allowedCategories[static_cast<std::size_t>(idx)];
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

void appendCommandField(std::string &out, bool &first, IpcCommand command)
{
    appendFieldPrefix(out, first, "command");
    out += std::to_string(phicore::adapter::v1::toUint16(command));
}

void appendDoubleJson(std::string &out, double value)
{
    if (!std::isfinite(value)) {
        out += "null";
        return;
    }

#if defined(__cpp_lib_to_chars) && (__cpp_lib_to_chars >= 201611L)
    {
        std::array<char, 64> buf{};
        const auto result = std::to_chars(
            buf.data(), buf.data() + buf.size(), value, std::chars_format::general, 15);
        if (result.ec == std::errc()) {
            out.append(buf.data(), static_cast<std::size_t>(result.ptr - buf.data()));
            return;
        }
    }
#endif

    std::ostringstream oss;
    oss.imbue(std::locale::classic());
    oss << std::setprecision(15) << value;
    out += oss.str();
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
        appendDoubleJson(out, *d);
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
    appendFieldPrefix(out, first, "externalId");
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
    appendFieldPrefix(out, first, "externalId");
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
    appendDoubleJson(out, channel.minValue);
    appendFieldPrefix(out, first, "maxValue");
    appendDoubleJson(out, channel.maxValue);
    appendFieldPrefix(out, first, "stepValue");
    appendDoubleJson(out, channel.stepValue);
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

std::string jsonTokenOrDefault(const std::string &json, std::string_view fallback)
{
    const std::string_view token = trim(json);
    if (token.empty())
        return std::string(fallback);
    return std::string(token);
}

std::string actionToJson(const AdapterActionDescriptor &action)
{
    std::string out;
    out.push_back('{');
    bool first = true;
    appendFieldPrefix(out, first, "id");
    out += jsonQuoted(action.id);
    appendFieldPrefix(out, first, "label");
    out += jsonQuoted(action.label);
    appendFieldPrefix(out, first, "description");
    out += jsonQuoted(action.description);
    appendFieldPrefix(out, first, "hasForm");
    out += (action.hasForm ? "true" : "false");
    appendFieldPrefix(out, first, "danger");
    out += (action.danger ? "true" : "false");
    appendFieldPrefix(out, first, "cooldownMs");
    out += std::to_string(action.cooldownMs);
    if (!trim(action.confirmJson).empty()) {
        appendFieldPrefix(out, first, "confirm");
        out += jsonTokenOrDefault(action.confirmJson, "{}");
    }
    if (!trim(action.metaJson).empty()) {
        appendFieldPrefix(out, first, "meta");
        out += jsonTokenOrDefault(action.metaJson, "{}");
    }
    out.push_back('}');
    return out;
}

std::string actionListToJson(const std::vector<AdapterActionDescriptor> &actions)
{
    std::string out;
    out.push_back('[');
    bool first = true;
    for (const AdapterActionDescriptor &action : actions) {
        if (action.id.empty())
            continue;
        if (!first)
            out.push_back(',');
        first = false;
        out += actionToJson(action);
    }
    out.push_back(']');
    return out;
}

std::string capabilitiesToJson(const AdapterCapabilities &caps)
{
    std::string out;
    out.push_back('{');
    bool first = true;
    appendFieldPrefix(out, first, "required");
    out += std::to_string(static_cast<int>(caps.required));
    appendFieldPrefix(out, first, "optional");
    out += std::to_string(static_cast<int>(caps.optional));
    appendFieldPrefix(out, first, "flags");
    out += std::to_string(static_cast<int>(caps.flags));
    appendFieldPrefix(out, first, "factoryActions");
    out += actionListToJson(caps.factoryActions);
    appendFieldPrefix(out, first, "instanceActions");
    out += actionListToJson(caps.instanceActions);
    if (!trim(caps.defaultsJson).empty()) {
        appendFieldPrefix(out, first, "defaults");
        out += jsonTokenOrDefault(caps.defaultsJson, "{}");
    }
    out.push_back('}');
    return out;
}

std::string descriptorToJson(const AdapterDescriptor &descriptor)
{
    std::string out;
    out.push_back('{');
    bool first = true;
    appendFieldPrefix(out, first, "pluginType");
    out += jsonQuoted(descriptor.pluginType);
    appendFieldPrefix(out, first, "displayName");
    out += jsonQuoted(descriptor.displayName);
    appendFieldPrefix(out, first, "description");
    out += jsonQuoted(descriptor.description);
    appendFieldPrefix(out, first, "apiVersion");
    out += jsonQuoted(descriptor.apiVersion);
    appendFieldPrefix(out, first, "iconSvg");
    out += jsonQuoted(descriptor.iconSvg);
    appendFieldPrefix(out, first, "imageBase64");
    out += jsonQuoted(descriptor.imageBase64);
    appendFieldPrefix(out, first, "timeoutMs");
    out += std::to_string(descriptor.timeoutMs);
    appendFieldPrefix(out, first, "maxInstances");
    out += std::to_string(descriptor.maxInstances);
    appendFieldPrefix(out, first, "capabilities");
    out += capabilitiesToJson(descriptor.capabilities);
    appendFieldPrefix(out, first, "configSchema");
    out += trim(descriptor.configSchemaJson).empty()
        ? "null"
        : jsonTokenOrDefault(descriptor.configSchemaJson, "{}");
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

CmdResponse invalidArgumentCmdResponse(CmdId cmdId, const std::string &message)
{
    CmdResponse response;
    response.id = cmdId;
    response.status = CmdStatus::InvalidArgument;
    response.error = message;
    response.tsMs = nowMs();
    return response;
}

ActionResponse invalidArgumentActionResponse(CmdId cmdId, const std::string &message)
{
    ActionResponse response;
    response.id = cmdId;
    response.status = CmdStatus::InvalidArgument;
    response.error = message;
    response.resultType = ActionResultType::None;
    response.tsMs = nowMs();
    return response;
}

} // namespace

SidecarDispatcher::SidecarDispatcher(phicore::adapter::v1::Utf8String socketPath)
    : m_runtime(std::make_unique<SidecarRuntime>(std::move(socketPath)))
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
    m_runtime->setCallbacks(std::move(callbacks));
}

SidecarDispatcher::~SidecarDispatcher() = default;

void SidecarDispatcher::setHandlers(SidecarHandlers handlers)
{
    m_handlers = std::move(handlers);
}

bool SidecarDispatcher::start(phicore::adapter::v1::Utf8String *error)
{
    {
        std::lock_guard<std::mutex> lock(m_runtimeMutex);
        if (!m_runtime->start(error))
            return false;
    }
    m_started.store(true, std::memory_order_release);
    return true;
}

void SidecarDispatcher::stop()
{
    m_started.store(false, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(m_sendQueueMutex);
        m_sendQueue.clear();
    }
    std::lock_guard<std::mutex> lock(m_runtimeMutex);
    m_runtime->stop();
}

bool SidecarDispatcher::pollOnce(std::chrono::milliseconds timeout, phicore::adapter::v1::Utf8String *error)
{
    if (!m_started.load(std::memory_order_acquire)) {
        if (error)
            *error = "dispatcher not started";
        return false;
    }
    flushSendQueue(nullptr);
    bool ok = false;
    {
        std::lock_guard<std::mutex> lock(m_runtimeMutex);
        ok = m_runtime->pollOnce(timeout, error);
    }
    flushSendQueue(nullptr);
    return ok;
}

bool SidecarDispatcher::handleRequestFrame(const phicore::adapter::v1::FrameHeader &header,
                                           std::span<const std::byte> payload)
{
    const std::string jsonPayload(reinterpret_cast<const char *>(payload.data()), payload.size());
    MemberMap root;
    std::string parseError;
    if (!parseObjectMembers(jsonPayload, &root, &parseError)) {
        if (m_handlers.onProtocolError)
            m_handlers.onProtocolError("Invalid request JSON: " + parseError);
        return false;
    }

    IpcCommand command = IpcCommand::SyncAdapterBootstrap;
    const std::string_view commandToken = member(root, "command");
    if (!parseIpcCommandToken(commandToken, &command)) {
        if (m_handlers.onProtocolError)
            m_handlers.onProtocolError("Request missing/invalid command");
        return false;
    }

    CmdId cmdId = 0;
    const std::string_view cmdIdToken = member(root, "cmdId");
    if (!cmdIdToken.empty()) {
        if (!parseCmdIdToken(cmdIdToken, &cmdId))
            cmdId = header.correlationId;
    } else {
        cmdId = header.correlationId;
    }

    std::string_view payloadToken = member(root, "payload");
    if (payloadToken.empty())
        payloadToken = "{}";

    MemberMap payloadMap;
    if (!parseObjectMembers(payloadToken, &payloadMap, nullptr))
        payloadMap.clear();

    auto parseAdapterFromMap = [](const MemberMap &adapterMap) {
        phicore::adapter::v1::Adapter adapter;
        adapter.name = decodeStringOrDefault(member(adapterMap, "name"));
        adapter.host = decodeStringOrDefault(member(adapterMap, "host"));
        adapter.ip = decodeStringOrDefault(member(adapterMap, "ip"));
        adapter.port =
            static_cast<std::uint16_t>(parseIntOrDefault(member(adapterMap, "port"), 0));
        adapter.user = decodeStringOrDefault(member(adapterMap, "user"));
        adapter.password = decodeStringOrDefault(member(adapterMap, "password"));
        adapter.token = decodeStringOrDefault(member(adapterMap, "token"));
        adapter.pluginType = decodeStringOrDefault(member(adapterMap, "pluginType"));
        adapter.externalId = decodeStringOrDefault(member(adapterMap, "externalId"));
        adapter.metaJson = std::string(member(adapterMap, "meta"));
        adapter.flags = static_cast<phicore::adapter::v1::AdapterFlag>(
            parseIntOrDefault(member(adapterMap, "flags"), 0));
        return adapter;
    };

    if (command == IpcCommand::SyncAdapterBootstrap) {
        if (m_handlers.onBootstrap) {
            BootstrapRequest request;
            request.cmdId = cmdId;
            request.correlationId = header.correlationId;
            request.adapterId = static_cast<int>(parseIntOrDefault(member(payloadMap, "adapterId"), 0));
            request.staticConfigJson = std::string(member(payloadMap, "staticConfig"));
            request.adapter.externalId = decodeStringOrDefault(member(payloadMap, "externalId"));
            request.adapter.pluginType = decodeStringOrDefault(member(payloadMap, "pluginType"));

            MemberMap adapterMap;
            const std::string_view adapterToken = member(payloadMap, "adapter");
            if (parseObjectMembers(adapterToken, &adapterMap, nullptr)) {
                request.adapter = parseAdapterFromMap(adapterMap);
            }
            if (request.adapter.pluginType.empty())
                request.adapter.pluginType = decodeStringOrDefault(member(payloadMap, "pluginType"));
            if (request.adapter.externalId.empty())
                request.adapter.externalId = decodeStringOrDefault(member(payloadMap, "externalId"));
            m_handlers.onBootstrap(request);
        }
        return true;
    }

    if (command == IpcCommand::SyncAdapterConfigChanged) {
        if (m_handlers.onConfigChanged) {
            ConfigChangedRequest request;
            request.cmdId = cmdId;
            request.correlationId = header.correlationId;
            request.adapterId = static_cast<int>(parseIntOrDefault(member(payloadMap, "adapterId"), 0));
            request.staticConfigJson = std::string(member(payloadMap, "staticConfig"));
            request.adapter.externalId = decodeStringOrDefault(member(payloadMap, "externalId"));
            request.adapter.pluginType = decodeStringOrDefault(member(payloadMap, "pluginType"));

            MemberMap adapterMap;
            if (parseObjectMembers(member(payloadMap, "adapter"), &adapterMap, nullptr))
                request.adapter = parseAdapterFromMap(adapterMap);
            if (request.adapter.pluginType.empty())
                request.adapter.pluginType = decodeStringOrDefault(member(payloadMap, "pluginType"));
            if (request.adapter.externalId.empty())
                request.adapter.externalId = decodeStringOrDefault(member(payloadMap, "externalId"));
            m_handlers.onConfigChanged(request);
        }
        return true;
    }

    if (command == IpcCommand::SyncAdapterInstanceRemoved) {
        if (m_handlers.onInstanceRemoved) {
            InstanceRemovedRequest request;
            request.cmdId = cmdId;
            request.correlationId = header.correlationId;
            request.adapterId = static_cast<int>(parseIntOrDefault(member(payloadMap, "adapterId"), 0));
            request.pluginType = decodeStringOrDefault(member(payloadMap, "pluginType"));
            request.externalId = decodeStringOrDefault(member(payloadMap, "externalId"));
            m_handlers.onInstanceRemoved(request);
        }
        return true;
    }

    if (command == IpcCommand::CmdChannelInvoke) {
        ChannelInvokeRequest request;
        request.cmdId = cmdId;
        request.externalId = decodeStringOrDefault(member(payloadMap, "externalId"));
        request.deviceExternalId = decodeStringOrDefault(member(payloadMap, "deviceExternalId"));
        request.channelExternalId = decodeStringOrDefault(member(payloadMap, "channelExternalId"));
        const std::string_view valueToken = member(payloadMap, "value");
        request.valueJson = std::string(valueToken);
        request.hasScalarValue = parseScalarValueToken(valueToken, &request.value);

        if (m_handlers.onChannelInvoke) {
            m_handlers.onChannelInvoke(request);
            return true;
        }
        CmdResponse response = defaultCmdResponse(cmdId, "Channel invoke handler not registered");
        return sendCmdResult(response, nullptr);
    }

    if (command == IpcCommand::CmdAdapterActionInvoke) {
        AdapterActionInvokeRequest request;
        request.cmdId = cmdId;
        request.externalId = decodeStringOrDefault(member(payloadMap, "externalId"));
        request.actionId = decodeStringOrDefault(member(payloadMap, "actionId"));
        request.paramsJson = std::string(member(payloadMap, "params"));
        if (trim(request.paramsJson).empty())
            request.paramsJson = "{}";

        if (m_handlers.onAdapterActionInvoke) {
            m_handlers.onAdapterActionInvoke(request);
            return true;
        }
        ActionResponse response = defaultActionResponse(cmdId, "Adapter action handler not registered");
        return sendActionResult(response, nullptr);
    }

    if (command == IpcCommand::CmdDeviceNameUpdate) {
        DeviceNameUpdateRequest request;
        request.cmdId = cmdId;
        request.externalId = decodeStringOrDefault(member(payloadMap, "externalId"));
        request.deviceExternalId = decodeStringOrDefault(member(payloadMap, "deviceExternalId"));
        request.name = decodeStringOrDefault(member(payloadMap, "name"));

        if (m_handlers.onDeviceNameUpdate) {
            m_handlers.onDeviceNameUpdate(request);
            return true;
        }
        CmdResponse response = defaultCmdResponse(cmdId, "Device name update handler not registered");
        return sendCmdResult(response, nullptr);
    }

    if (command == IpcCommand::CmdDeviceEffectInvoke) {
        DeviceEffectInvokeRequest request;
        request.cmdId = cmdId;
        request.externalId = decodeStringOrDefault(member(payloadMap, "externalId"));
        request.deviceExternalId = decodeStringOrDefault(member(payloadMap, "deviceExternalId"));
        request.effect = static_cast<DeviceEffect>(parseIntOrDefault(member(payloadMap, "effect"), 0));
        request.effectId = decodeStringOrDefault(member(payloadMap, "effectId"));
        request.paramsJson = std::string(member(payloadMap, "params"));
        if (trim(request.paramsJson).empty())
            request.paramsJson = "{}";

        if (m_handlers.onDeviceEffectInvoke) {
            m_handlers.onDeviceEffectInvoke(request);
            return true;
        }
        CmdResponse response = defaultCmdResponse(cmdId, "Device effect handler not registered");
        return sendCmdResult(response, nullptr);
    }

    if (command == IpcCommand::CmdSceneInvoke) {
        SceneInvokeRequest request;
        request.cmdId = cmdId;
        request.externalId = decodeStringOrDefault(member(payloadMap, "externalId"));
        request.sceneExternalId = decodeStringOrDefault(member(payloadMap, "sceneExternalId"));
        request.groupExternalId = decodeStringOrDefault(member(payloadMap, "groupExternalId"));
        request.action = decodeStringOrDefault(member(payloadMap, "action"));

        if (m_handlers.onSceneInvoke) {
            m_handlers.onSceneInvoke(request);
            return true;
        }
        CmdResponse response = defaultCmdResponse(cmdId, "Scene invoke handler not registered");
        return sendCmdResult(response, nullptr);
    }

    if (m_handlers.onUnknownRequest) {
        UnknownRequest request;
        request.cmdId = cmdId;
        request.externalId = decodeStringOrDefault(member(payloadMap, "externalId"));
        request.command = phicore::adapter::v1::toUint16(command);
        request.payloadJson = std::string(payloadToken);
        m_handlers.onUnknownRequest(request);
    }
    if (cmdId != 0) {
        CmdResponse response = defaultCmdResponse(
            cmdId, "Unhandled IPC command: " + std::to_string(phicore::adapter::v1::toUint16(command)));
        return sendCmdResult(response, nullptr);
    }
    return true;
}

bool SidecarDispatcher::sendJson(MessageType type,
                                 CorrelationId correlationId,
                                 std::string_view json,
                                 phicore::adapter::v1::Utf8String *error)
{
    OutboundFrame frame;
    frame.type = type;
    frame.correlationId = correlationId;
    frame.payload.assign(json.data(), json.size());
    return queueOutboundFrame(std::move(frame), error);
}

bool SidecarDispatcher::queueOutboundFrame(OutboundFrame frame, phicore::adapter::v1::Utf8String *error)
{
    if (!m_started.load(std::memory_order_acquire)) {
        if (error)
            *error = "dispatcher not started";
        return false;
    }
    if (frame.payload.empty()) {
        if (error)
            *error = "outbound payload must not be empty";
        return false;
    }
    std::lock_guard<std::mutex> lock(m_sendQueueMutex);
    m_sendQueue.push_back(std::move(frame));
    return true;
}

bool SidecarDispatcher::flushSendQueue(phicore::adapter::v1::Utf8String *error)
{
    std::deque<OutboundFrame> localQueue;
    {
        std::lock_guard<std::mutex> lock(m_sendQueueMutex);
        if (m_sendQueue.empty())
            return true;
        localQueue.swap(m_sendQueue);
    }

    for (auto &frame : localQueue) {
        const auto chars = std::span<const char>(frame.payload.data(), frame.payload.size());
        const auto bytes = std::as_bytes(chars);
        phicore::adapter::v1::Utf8String sendError;
        bool ok = false;
        {
            std::lock_guard<std::mutex> lock(m_runtimeMutex);
            ok = m_runtime->send(frame.type, frame.correlationId, bytes, &sendError);
        }
        if (!ok) {
            if (error && error->empty())
                *error = "Failed to send outbound frame: " + sendError;
            if (m_handlers.onProtocolError)
                m_handlers.onProtocolError("Failed to send outbound frame: " + sendError);
        }
    }
    return true;
}

bool SidecarDispatcher::sendCmdResult(const CmdResponse &response, phicore::adapter::v1::Utf8String *error)
{
    const std::int64_t tsMs = response.tsMs > 0 ? response.tsMs : nowMs();
    std::string body;
    body.push_back('{');
    bool first = true;
    appendCommandField(body, first, IpcCommand::ResultCmd);
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

bool SidecarDispatcher::sendActionResult(const ActionResponse &response, phicore::adapter::v1::Utf8String *error)
{
    const std::int64_t tsMs = response.tsMs > 0 ? response.tsMs : nowMs();
    const auto formValues = trim(response.formValuesJson);
    const auto fieldChoices = trim(response.fieldChoicesJson);
    std::string body;
    body.push_back('{');
    bool first = true;
    appendCommandField(body, first, IpcCommand::ResultAction);
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
    if (!formValues.empty()) {
        appendFieldPrefix(body, first, "formValues");
        appendMetaJson(body, std::string(formValues));
    }
    if (!fieldChoices.empty()) {
        appendFieldPrefix(body, first, "fieldChoices");
        appendMetaJson(body, std::string(fieldChoices));
    }
    if (response.reloadLayout) {
        appendFieldPrefix(body, first, "reloadLayout");
        body += "true";
    }
    appendFieldPrefix(body, first, "tsMs");
    body += std::to_string(tsMs);
    body.push_back('}');
    return sendJson(MessageType::Response, response.id, body, error);
}

bool SidecarDispatcher::sendConnectionStateChanged(const phicore::adapter::v1::ExternalId &externalId,
                                                   bool connected,
                                                   phicore::adapter::v1::Utf8String *error)
{
    const std::string body = std::string("{\"command\":")
        + std::to_string(phicore::adapter::v1::toUint16(IpcCommand::EventConnectionStateChanged))
        + ",\"externalId\":"
        + jsonQuoted(externalId)
        + ",\"connected\":"
        + (connected ? "true" : "false")
        + "}";
    return sendJson(MessageType::Event, 0, body, error);
}

bool SidecarDispatcher::sendError(const phicore::adapter::v1::ExternalId &externalId,
                                  const phicore::adapter::v1::Utf8String &message,
                                  const ScalarList &params,
                                  const phicore::adapter::v1::Utf8String &ctx,
                                  phicore::adapter::v1::Utf8String *error)
{
    std::string body;
    body.push_back('{');
    bool first = true;
    appendCommandField(body, first, IpcCommand::EventError);
    appendFieldPrefix(body, first, "externalId");
    body += jsonQuoted(externalId);
    appendFieldPrefix(body, first, "message");
    body += jsonQuoted(message);
    appendFieldPrefix(body, first, "ctx");
    body += jsonQuoted(ctx);
    appendFieldPrefix(body, first, "params");
    appendScalarListJson(body, params);
    body.push_back('}');
    return sendJson(MessageType::Event, 0, body, error);
}

bool SidecarDispatcher::sendLog(const phicore::adapter::v1::ExternalId &externalId,
                                const phicore::adapter::v1::Utf8String &pluginType,
                                const LogEntry &entry,
                                phicore::adapter::v1::Utf8String *error)
{
    const std::int64_t tsMs = entry.tsMs > 0 ? entry.tsMs : nowMs();
    const std::string fields = trim(entry.fieldsJson).empty() ? "{}" : entry.fieldsJson;
    std::string body;
    body.push_back('{');
    bool first = true;
    appendCommandField(body, first, IpcCommand::EventLog);
    appendFieldPrefix(body, first, "externalId");
    body += jsonQuoted(externalId);
    appendFieldPrefix(body, first, "pluginType");
    body += jsonQuoted(pluginType);
    appendFieldPrefix(body, first, "level");
    body += jsonQuoted(std::string(logLevelName(entry.level)));
    appendFieldPrefix(body, first, "category");
    body += jsonQuoted(std::string(logCategoryName(entry.category)));
    appendFieldPrefix(body, first, "message");
    body += jsonQuoted(entry.message);
    appendFieldPrefix(body, first, "ctx");
    body += jsonQuoted(entry.ctx);
    appendFieldPrefix(body, first, "params");
    appendScalarListJson(body, entry.params);
    appendFieldPrefix(body, first, "fields");
    body += jsonTokenOrDefault(fields, "{}");
    appendFieldPrefix(body, first, "tsMs");
    body += std::to_string(tsMs);
    body.push_back('}');
    return sendJson(MessageType::Event, 0, body, error);
}

bool SidecarDispatcher::sendAdapterMetaUpdated(const phicore::adapter::v1::ExternalId &externalId,
                                               const phicore::adapter::v1::JsonText &metaPatchJson,
                                               phicore::adapter::v1::Utf8String *error)
{
    const std::string patch = trim(metaPatchJson).empty() ? "{}" : metaPatchJson;
    const std::string body = std::string("{\"command\":")
        + std::to_string(phicore::adapter::v1::toUint16(IpcCommand::EventAdapterMetaUpdated))
        + ",\"externalId\":"
        + jsonQuoted(externalId)
        + ",\"metaPatch\":"
        + patch
        + "}";
    return sendJson(MessageType::Event, 0, body, error);
}

bool SidecarDispatcher::sendAdapterDescriptor(const phicore::adapter::v1::ExternalId &externalId,
                                              const AdapterDescriptor &descriptor,
                                              CorrelationId correlationId,
                                              phicore::adapter::v1::Utf8String *error)
{
    const std::string body = std::string("{\"command\":")
        + std::to_string(phicore::adapter::v1::toUint16(IpcCommand::EventFactoryDescriptor))
        + ",\"externalId\":"
        + jsonQuoted(externalId)
        + ",\"descriptor\":"
        + descriptorToJson(descriptor)
        + "}";
    return sendJson(MessageType::Response, correlationId, body, error);
}

bool SidecarDispatcher::sendAdapterDescriptorUpdated(const phicore::adapter::v1::ExternalId &externalId,
                                                     const AdapterDescriptor &descriptor,
                                                     phicore::adapter::v1::Utf8String *error)
{
    const std::string body = std::string("{\"command\":")
        + std::to_string(phicore::adapter::v1::toUint16(IpcCommand::EventFactoryDescriptorUpdated))
        + ",\"externalId\":"
        + jsonQuoted(externalId)
        + ",\"descriptor\":"
        + descriptorToJson(descriptor)
        + "}";
    return sendJson(MessageType::Event, 0, body, error);
}

bool SidecarDispatcher::sendChannelStateUpdated(const phicore::adapter::v1::ExternalId &externalId,
                                                const phicore::adapter::v1::ExternalId &deviceExternalId,
                                                const phicore::adapter::v1::ExternalId &channelExternalId,
                                                const ScalarValue &value,
                                                std::int64_t tsMs,
                                                phicore::adapter::v1::Utf8String *error)
{
    const std::int64_t timestamp = tsMs > 0 ? tsMs : nowMs();
    std::string body;
    body.push_back('{');
    bool first = true;
    appendCommandField(body, first, IpcCommand::EventChannelStateUpdated);
    appendFieldPrefix(body, first, "externalId");
    body += jsonQuoted(externalId);
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

bool SidecarDispatcher::sendDeviceUpdated(const phicore::adapter::v1::ExternalId &externalId,
                                          const Device &device,
                                          const ChannelList &channels,
                                          phicore::adapter::v1::Utf8String *error)
{
    std::string body = std::string("{\"command\":")
        + std::to_string(phicore::adapter::v1::toUint16(IpcCommand::EventDeviceUpdated))
        + ",\"externalId\":";
    body += jsonQuoted(externalId);
    body += ",\"payload\":{\"device\":";
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

bool SidecarDispatcher::sendDeviceRemoved(const phicore::adapter::v1::ExternalId &externalId,
                                          const phicore::adapter::v1::ExternalId &deviceExternalId,
                                          phicore::adapter::v1::Utf8String *error)
{
    const std::string body = std::string("{\"command\":")
        + std::to_string(phicore::adapter::v1::toUint16(IpcCommand::EventDeviceRemoved))
        + ",\"externalId\":"
        + jsonQuoted(externalId)
        + ",\"deviceExternalId\":"
        + jsonQuoted(deviceExternalId)
        + "}";
    return sendJson(MessageType::Event, 0, body, error);
}

bool SidecarDispatcher::sendChannelUpdated(const phicore::adapter::v1::ExternalId &externalId,
                                           const phicore::adapter::v1::ExternalId &deviceExternalId,
                                           const Channel &channel,
                                           phicore::adapter::v1::Utf8String *error)
{
    std::string body = std::string("{\"command\":")
        + std::to_string(phicore::adapter::v1::toUint16(IpcCommand::EventChannelUpdated))
        + ",\"externalId\":";
    body += jsonQuoted(externalId);
    body += ",\"payload\":{\"deviceExternalId\":";
    body += jsonQuoted(deviceExternalId);
    body += ",\"channel\":";
    body += channelToJson(channel);
    body += "}}";
    return sendJson(MessageType::Event, 0, body, error);
}

bool SidecarDispatcher::sendRoomUpdated(const phicore::adapter::v1::ExternalId &externalId,
                                        const Room &room,
                                        phicore::adapter::v1::Utf8String *error)
{
    std::string body = std::string("{\"command\":")
        + std::to_string(phicore::adapter::v1::toUint16(IpcCommand::EventRoomUpdated))
        + ",\"externalId\":";
    body += jsonQuoted(externalId);
    body += ",\"room\":";
    body += roomToJson(room);
    body += "}";
    return sendJson(MessageType::Event, 0, body, error);
}

bool SidecarDispatcher::sendRoomRemoved(const phicore::adapter::v1::ExternalId &externalId,
                                        const phicore::adapter::v1::ExternalId &roomExternalId,
                                        phicore::adapter::v1::Utf8String *error)
{
    const std::string body = std::string("{\"command\":")
        + std::to_string(phicore::adapter::v1::toUint16(IpcCommand::EventRoomRemoved))
        + ",\"externalId\":"
        + jsonQuoted(externalId)
        + ",\"roomExternalId\":"
        + jsonQuoted(roomExternalId)
        + "}";
    return sendJson(MessageType::Event, 0, body, error);
}

bool SidecarDispatcher::sendGroupUpdated(const phicore::adapter::v1::ExternalId &externalId,
                                         const Group &group,
                                         phicore::adapter::v1::Utf8String *error)
{
    std::string body = std::string("{\"command\":")
        + std::to_string(phicore::adapter::v1::toUint16(IpcCommand::EventGroupUpdated))
        + ",\"externalId\":";
    body += jsonQuoted(externalId);
    body += ",\"group\":";
    body += groupToJson(group);
    body += "}";
    return sendJson(MessageType::Event, 0, body, error);
}

bool SidecarDispatcher::sendGroupRemoved(const phicore::adapter::v1::ExternalId &externalId,
                                         const phicore::adapter::v1::ExternalId &groupExternalId,
                                         phicore::adapter::v1::Utf8String *error)
{
    const std::string body = std::string("{\"command\":")
        + std::to_string(phicore::adapter::v1::toUint16(IpcCommand::EventGroupRemoved))
        + ",\"externalId\":"
        + jsonQuoted(externalId)
        + ",\"groupExternalId\":"
        + jsonQuoted(groupExternalId)
        + "}";
    return sendJson(MessageType::Event, 0, body, error);
}

bool SidecarDispatcher::sendSceneUpdated(const phicore::adapter::v1::ExternalId &externalId,
                                         const Scene &scene,
                                         phicore::adapter::v1::Utf8String *error)
{
    std::string body = std::string("{\"command\":")
        + std::to_string(phicore::adapter::v1::toUint16(IpcCommand::EventSceneUpdated))
        + ",\"externalId\":";
    body += jsonQuoted(externalId);
    body += ",\"scene\":";
    body += sceneToJson(scene);
    body += "}";
    return sendJson(MessageType::Event, 0, body, error);
}

bool SidecarDispatcher::sendSceneRemoved(const phicore::adapter::v1::ExternalId &externalId,
                                         const phicore::adapter::v1::ExternalId &sceneExternalId,
                                         phicore::adapter::v1::Utf8String *error)
{
    const std::string body = std::string("{\"command\":")
        + std::to_string(phicore::adapter::v1::toUint16(IpcCommand::EventSceneRemoved))
        + ",\"externalId\":"
        + jsonQuoted(externalId)
        + ",\"sceneExternalId\":"
        + jsonQuoted(sceneExternalId)
        + "}";
    return sendJson(MessageType::Event, 0, body, error);
}

const BootstrapRequest &AdapterFactory::bootstrap() const
{
    return m_bootstrap;
}

bool AdapterFactory::hasBootstrap() const
{
    return m_hasBootstrap;
}

bool AdapterFactory::log(LogLevel level,
                         LogCategory category,
                         const phicore::adapter::v1::Utf8String &message,
                         const phicore::adapter::v1::Utf8String &ctx,
                         const phicore::adapter::v1::ScalarList &params,
                         const phicore::adapter::v1::JsonText &fieldsJson,
                         std::int64_t tsMs,
                         phicore::adapter::v1::Utf8String *error)
{
    if (!m_dispatcher) {
        if (error)
            *error = "Dispatcher not bound";
        return false;
    }
    if (!shouldForwardLog(m_factoryConfig.adapter, m_hasFactoryConfig, level, category)) {
        return true;
    }
    LogEntry entry;
    entry.level = level;
    entry.category = category;
    entry.message = message;
    entry.ctx = ctx;
    entry.params = params;
    entry.fieldsJson = fieldsJson;
    entry.tsMs = tsMs;
    return m_dispatcher->sendLog({}, hostPluginType(), entry, error);
}

phicore::adapter::v1::Utf8String AdapterFactory::displayName() const { return {}; }
phicore::adapter::v1::Utf8String AdapterFactory::description() const { return {}; }
phicore::adapter::v1::Utf8String AdapterFactory::apiVersion() const { return {}; }
phicore::adapter::v1::Utf8String AdapterFactory::iconSvg() const { return {}; }
phicore::adapter::v1::Utf8String AdapterFactory::imageBase64() const { return {}; }
int AdapterFactory::timeoutMs() const { return 50; }
int AdapterFactory::maxInstances() const { return 0; }
phicore::adapter::v1::AdapterCapabilities AdapterFactory::capabilities() const { return {}; }
phicore::adapter::v1::JsonText AdapterFactory::configSchemaJson() const { return {}; }

AdapterDescriptor AdapterFactory::descriptor() const
{
    AdapterDescriptor out;
    out.pluginType = pluginType();
    out.displayName = displayName();
    out.description = description();
    out.apiVersion = apiVersion();
    out.iconSvg = iconSvg();
    out.imageBase64 = imageBase64();
    out.timeoutMs = timeoutMs();
    out.maxInstances = maxInstances();
    out.capabilities = capabilities();
    out.configSchemaJson = configSchemaJson();
    return out;
}

std::unique_ptr<InstanceExecutionBackend> AdapterFactory::createInstanceExecutionBackend(
    const phicore::adapter::v1::ExternalId &externalId)
{
    (void)externalId;
    return std::make_unique<DefaultInstanceExecutionBackend>();
}

void AdapterFactory::destroyInstance(std::unique_ptr<AdapterInstance> instance) { (void)instance; }
void AdapterFactory::onFactoryActionInvoke(const AdapterActionInvokeRequest &request)
{
    phicore::adapter::v1::Utf8String err;
    sendResult(defaultActionResponse(request.cmdId, "Factory action handler not implemented"), &err);
    if (!err.empty())
        onProtocolError("Failed to send default factory action result: " + err);
}
void AdapterFactory::onFactoryConfigChanged(const ConfigChangedRequest &request) { (void)request; }
void AdapterFactory::onConnected() {}
void AdapterFactory::onDisconnected() {}
void AdapterFactory::onProtocolError(const phicore::adapter::v1::Utf8String &message)
{
    std::cerr << "[sidecar][protocolError][host] " << message << std::endl;
}
void AdapterFactory::onBootstrap(const BootstrapRequest &request) { (void)request; }

bool AdapterFactory::sendConnectionStateChanged(bool connected, phicore::adapter::v1::Utf8String *error)
{
    return m_dispatcher ? m_dispatcher->sendConnectionStateChanged({}, connected, error) : false;
}
bool AdapterFactory::sendError(const phicore::adapter::v1::Utf8String &message,
                               const ScalarList &params,
                               const phicore::adapter::v1::Utf8String &ctx,
                               phicore::adapter::v1::Utf8String *error)
{
    if (!m_dispatcher) {
        if (error)
            *error = "Dispatcher not bound";
        return false;
    }
    if (!m_dispatcher->sendError({}, message, params, ctx, error))
        return false;
    LogEntry mirrored;
    mirrored.level = LogLevel::Error;
    mirrored.category = LogCategory::Event;
    mirrored.message = message;
    mirrored.ctx = ctx;
    mirrored.params = params;
    mirrored.fieldsJson = R"({"source":"event.error"})";
    return m_dispatcher->sendLog({}, hostPluginType(), mirrored, error);
}
bool AdapterFactory::sendAdapterMetaUpdated(const phicore::adapter::v1::JsonText &metaPatchJson,
                                            phicore::adapter::v1::Utf8String *error)
{
    return m_dispatcher ? m_dispatcher->sendAdapterMetaUpdated({}, metaPatchJson, error) : false;
}
AdapterDescriptor AdapterFactory::factoryDescriptor() const
{
    AdapterDescriptor descriptorValue = descriptor();
    if (descriptorValue.pluginType.empty())
        descriptorValue.pluginType = pluginType();
    return descriptorValue;
}
bool AdapterFactory::sendFactoryDescriptorUpdated(phicore::adapter::v1::Utf8String *error)
{
    if (!m_dispatcher) {
        if (error)
            *error = "Dispatcher not bound";
        return false;
    }
    return m_dispatcher->sendAdapterDescriptorUpdated({}, factoryDescriptor(), error);
}
bool AdapterFactory::sendFactoryDescriptorUpdated(const AdapterDescriptor &descriptor,
                                                  phicore::adapter::v1::Utf8String *error)
{
    return m_dispatcher ? m_dispatcher->sendAdapterDescriptorUpdated({}, descriptor, error) : false;
}
bool AdapterFactory::sendResult(const phicore::adapter::v1::ActionResponse &response,
                                phicore::adapter::v1::Utf8String *error)
{
    if (!m_actionResultSubmitter) {
        if (error)
            *error = "Factory action result submitter not bound";
        return false;
    }
    if (response.id == 0) {
        if (error)
            *error = "ActionResponse.id must be > 0";
        return false;
    }
    m_actionResultSubmitter(response);
    return true;
}

void AdapterFactory::bindDispatcher(SidecarDispatcher *dispatcher) { m_dispatcher = dispatcher; }
void AdapterFactory::bindResultSubmitter(std::function<void(const phicore::adapter::v1::ActionResponse &)> actionSubmitter)
{
    m_actionResultSubmitter = std::move(actionSubmitter);
}
void AdapterFactory::cacheBootstrap(const BootstrapRequest &request)
{
    m_bootstrap = request;
    m_hasBootstrap = true;
}
void AdapterFactory::cacheFactoryConfig(const ConfigChangedRequest &request)
{
    m_factoryConfig = request;
    m_hasFactoryConfig = true;
}
phicore::adapter::v1::Utf8String AdapterFactory::hostPluginType() const { return pluginType(); }
AdapterDescriptor AdapterFactory::hostDescriptor() const { return descriptor(); }
std::unique_ptr<InstanceExecutionBackend> AdapterFactory::hostCreateInstanceExecutionBackend(
    const phicore::adapter::v1::ExternalId &externalId)
{
    return createInstanceExecutionBackend(externalId);
}
std::unique_ptr<AdapterInstance> AdapterFactory::hostCreateInstance(const phicore::adapter::v1::ExternalId &externalId)
{
    return createInstance(externalId);
}
void AdapterFactory::hostDestroyInstance(std::unique_ptr<AdapterInstance> instance)
{
    destroyInstance(std::move(instance));
}
void AdapterFactory::hostOnFactoryActionInvoke(const AdapterActionInvokeRequest &request)
{
    onFactoryActionInvoke(request);
}
void AdapterFactory::hostOnConnected() { onConnected(); }
void AdapterFactory::hostOnDisconnected() { onDisconnected(); }
void AdapterFactory::hostOnProtocolError(const phicore::adapter::v1::Utf8String &message) { onProtocolError(message); }
void AdapterFactory::hostOnBootstrap(const BootstrapRequest &request)
{
    cacheBootstrap(request);
    onBootstrap(request);
}
void AdapterFactory::hostOnFactoryConfigChanged(const ConfigChangedRequest &request)
{
    cacheFactoryConfig(request);
    onFactoryConfigChanged(request);
}

int AdapterInstance::adapterId() const { return m_adapterId; }
const phicore::adapter::v1::Utf8String &AdapterInstance::pluginType() const { return m_pluginType; }
const phicore::adapter::v1::ExternalId &AdapterInstance::externalId() const { return m_externalId; }
const ConfigChangedRequest &AdapterInstance::config() const { return m_config; }
bool AdapterInstance::hasConfig() const { return m_hasConfig; }

bool AdapterInstance::log(LogLevel level,
                          LogCategory category,
                          const phicore::adapter::v1::Utf8String &message,
                          const phicore::adapter::v1::Utf8String &ctx,
                          const phicore::adapter::v1::ScalarList &params,
                          const phicore::adapter::v1::JsonText &fieldsJson,
                          std::int64_t tsMs,
                          phicore::adapter::v1::Utf8String *error)
{
    if (!m_dispatcher) {
        if (error)
            *error = "Dispatcher not bound";
        return false;
    }
    if (!shouldForwardLog(m_config.adapter, m_hasConfig, level, category)) {
        return true;
    }
    LogEntry entry;
    entry.level = level;
    entry.category = category;
    entry.message = message;
    entry.ctx = ctx;
    entry.params = params;
    entry.fieldsJson = fieldsJson;
    entry.tsMs = tsMs;
    return m_dispatcher->sendLog(m_externalId, m_pluginType, entry, error);
}

bool AdapterInstance::start() { return true; }
void AdapterInstance::stop() {}
bool AdapterInstance::restart()
{
    stop();
    return start();
}
void AdapterInstance::onConnected() {}
void AdapterInstance::onDisconnected() {}
void AdapterInstance::onProtocolError(const phicore::adapter::v1::Utf8String &message)
{
    std::cerr << "[sidecar][protocolError][instance] externalId=" << m_externalId
              << " message=" << message << std::endl;
}
void AdapterInstance::onConfigChanged(const ConfigChangedRequest &request) { (void)request; }
void AdapterInstance::onChannelInvoke(const ChannelInvokeRequest &request)
{
    phicore::adapter::v1::Utf8String err;
    sendResult(defaultCmdResponse(request.cmdId, "Channel invoke handler not implemented"), &err);
    if (!err.empty())
        onProtocolError("Failed to send default channel invoke result: " + err);
}
void AdapterInstance::onAdapterActionInvoke(const AdapterActionInvokeRequest &request)
{
    phicore::adapter::v1::Utf8String err;
    sendResult(defaultActionResponse(request.cmdId, "Adapter action handler not implemented"), &err);
    if (!err.empty())
        onProtocolError("Failed to send default adapter action result: " + err);
}
void AdapterInstance::onDeviceNameUpdate(const DeviceNameUpdateRequest &request)
{
    phicore::adapter::v1::Utf8String err;
    sendResult(defaultCmdResponse(request.cmdId, "Device name update handler not implemented"), &err);
    if (!err.empty())
        onProtocolError("Failed to send default device name update result: " + err);
}
void AdapterInstance::onDeviceEffectInvoke(const DeviceEffectInvokeRequest &request)
{
    phicore::adapter::v1::Utf8String err;
    sendResult(defaultCmdResponse(request.cmdId, "Device effect handler not implemented"), &err);
    if (!err.empty())
        onProtocolError("Failed to send default device effect result: " + err);
}
void AdapterInstance::onSceneInvoke(const SceneInvokeRequest &request)
{
    phicore::adapter::v1::Utf8String err;
    sendResult(defaultCmdResponse(request.cmdId, "Scene invoke handler not implemented"), &err);
    if (!err.empty())
        onProtocolError("Failed to send default scene invoke result: " + err);
}
void AdapterInstance::onUnknownRequest(const UnknownRequest &request) { (void)request; }

bool AdapterInstance::sendConnectionStateChanged(bool connected, phicore::adapter::v1::Utf8String *error)
{
    return m_dispatcher ? m_dispatcher->sendConnectionStateChanged(m_externalId, connected, error) : false;
}
bool AdapterInstance::sendError(const phicore::adapter::v1::Utf8String &message,
                                const ScalarList &params,
                                const phicore::adapter::v1::Utf8String &ctx,
                                phicore::adapter::v1::Utf8String *error)
{
    if (!m_dispatcher) {
        if (error)
            *error = "Dispatcher not bound";
        return false;
    }
    if (!m_dispatcher->sendError(m_externalId, message, params, ctx, error))
        return false;
    LogEntry mirrored;
    mirrored.level = LogLevel::Error;
    mirrored.category = LogCategory::Event;
    mirrored.message = message;
    mirrored.ctx = ctx;
    mirrored.params = params;
    mirrored.fieldsJson = R"({"source":"event.error"})";
    return m_dispatcher->sendLog(m_externalId, m_pluginType, mirrored, error);
}
bool AdapterInstance::sendAdapterMetaUpdated(const phicore::adapter::v1::JsonText &metaPatchJson,
                                             phicore::adapter::v1::Utf8String *error)
{
    return m_dispatcher ? m_dispatcher->sendAdapterMetaUpdated(m_externalId, metaPatchJson, error) : false;
}
bool AdapterInstance::sendChannelStateUpdated(const phicore::adapter::v1::ExternalId &deviceExternalId,
                                              const phicore::adapter::v1::ExternalId &channelExternalId,
                                              const ScalarValue &value,
                                              std::int64_t tsMs,
                                              phicore::adapter::v1::Utf8String *error)
{
    return m_dispatcher
        ? m_dispatcher->sendChannelStateUpdated(m_externalId, deviceExternalId, channelExternalId, value, tsMs, error)
        : false;
}
bool AdapterInstance::sendDeviceUpdated(const Device &device,
                                        const ChannelList &channels,
                                        phicore::adapter::v1::Utf8String *error)
{
    return m_dispatcher ? m_dispatcher->sendDeviceUpdated(m_externalId, device, channels, error) : false;
}
bool AdapterInstance::sendDeviceRemoved(const phicore::adapter::v1::ExternalId &deviceExternalId,
                                        phicore::adapter::v1::Utf8String *error)
{
    return m_dispatcher ? m_dispatcher->sendDeviceRemoved(m_externalId, deviceExternalId, error) : false;
}
bool AdapterInstance::sendChannelUpdated(const phicore::adapter::v1::ExternalId &deviceExternalId,
                                         const Channel &channel,
                                         phicore::adapter::v1::Utf8String *error)
{
    return m_dispatcher ? m_dispatcher->sendChannelUpdated(m_externalId, deviceExternalId, channel, error) : false;
}
bool AdapterInstance::sendRoomUpdated(const Room &room, phicore::adapter::v1::Utf8String *error)
{
    return m_dispatcher ? m_dispatcher->sendRoomUpdated(m_externalId, room, error) : false;
}
bool AdapterInstance::sendRoomRemoved(const phicore::adapter::v1::ExternalId &roomExternalId,
                                      phicore::adapter::v1::Utf8String *error)
{
    return m_dispatcher ? m_dispatcher->sendRoomRemoved(m_externalId, roomExternalId, error) : false;
}
bool AdapterInstance::sendGroupUpdated(const Group &group, phicore::adapter::v1::Utf8String *error)
{
    return m_dispatcher ? m_dispatcher->sendGroupUpdated(m_externalId, group, error) : false;
}
bool AdapterInstance::sendGroupRemoved(const phicore::adapter::v1::ExternalId &groupExternalId,
                                       phicore::adapter::v1::Utf8String *error)
{
    return m_dispatcher ? m_dispatcher->sendGroupRemoved(m_externalId, groupExternalId, error) : false;
}
bool AdapterInstance::sendSceneUpdated(const Scene &scene, phicore::adapter::v1::Utf8String *error)
{
    return m_dispatcher ? m_dispatcher->sendSceneUpdated(m_externalId, scene, error) : false;
}
bool AdapterInstance::sendSceneRemoved(const phicore::adapter::v1::ExternalId &sceneExternalId,
                                       phicore::adapter::v1::Utf8String *error)
{
    return m_dispatcher ? m_dispatcher->sendSceneRemoved(m_externalId, sceneExternalId, error) : false;
}
bool AdapterInstance::sendResult(const phicore::adapter::v1::CmdResponse &response,
                                 phicore::adapter::v1::Utf8String *error)
{
    if (!m_cmdResultSubmitter) {
        if (error)
            *error = "Command result submitter not bound";
        return false;
    }
    if (response.id == 0) {
        if (error)
            *error = "CmdResponse.id must be > 0";
        return false;
    }
    m_cmdResultSubmitter(response);
    return true;
}
bool AdapterInstance::sendResult(const phicore::adapter::v1::ActionResponse &response,
                                 phicore::adapter::v1::Utf8String *error)
{
    if (!m_actionResultSubmitter) {
        if (error)
            *error = "Action result submitter not bound";
        return false;
    }
    if (response.id == 0) {
        if (error)
            *error = "ActionResponse.id must be > 0";
        return false;
    }
    m_actionResultSubmitter(response);
    return true;
}

void AdapterInstance::bindDispatcher(SidecarDispatcher *dispatcher) { m_dispatcher = dispatcher; }
void AdapterInstance::bindResultSubmitters(
    std::function<void(const phicore::adapter::v1::CmdResponse &)> cmdSubmitter,
    std::function<void(const phicore::adapter::v1::ActionResponse &)> actionSubmitter)
{
    m_cmdResultSubmitter = std::move(cmdSubmitter);
    m_actionResultSubmitter = std::move(actionSubmitter);
}
void AdapterInstance::bindContext(int adapterId,
                                  phicore::adapter::v1::Utf8String pluginType,
                                  phicore::adapter::v1::ExternalId externalId)
{
    m_adapterId = adapterId;
    m_pluginType = std::move(pluginType);
    m_externalId = std::move(externalId);
}
void AdapterInstance::cacheConfig(const ConfigChangedRequest &request)
{
    m_config = request;
    m_hasConfig = true;
}
bool AdapterInstance::hostStart() { return start(); }
void AdapterInstance::hostStop() { stop(); }
bool AdapterInstance::hostRestart() { return restart(); }
void AdapterInstance::hostOnConnected() { onConnected(); }
void AdapterInstance::hostOnDisconnected() { onDisconnected(); }
void AdapterInstance::hostOnProtocolError(const phicore::adapter::v1::Utf8String &message) { onProtocolError(message); }
void AdapterInstance::hostOnConfigChanged(const ConfigChangedRequest &request)
{
    cacheConfig(request);
    onConfigChanged(request);
}
void AdapterInstance::hostOnChannelInvoke(const ChannelInvokeRequest &request) { onChannelInvoke(request); }
void AdapterInstance::hostOnAdapterActionInvoke(const AdapterActionInvokeRequest &request) { onAdapterActionInvoke(request); }
void AdapterInstance::hostOnDeviceNameUpdate(const DeviceNameUpdateRequest &request) { onDeviceNameUpdate(request); }
void AdapterInstance::hostOnDeviceEffectInvoke(const DeviceEffectInvokeRequest &request) { onDeviceEffectInvoke(request); }
void AdapterInstance::hostOnSceneInvoke(const SceneInvokeRequest &request) { onSceneInvoke(request); }
void AdapterInstance::hostOnUnknownRequest(const UnknownRequest &request) { onUnknownRequest(request); }


SidecarHost::SidecarHost(phicore::adapter::v1::Utf8String socketPath, std::unique_ptr<AdapterFactory> factory)
    : m_dispatcher(std::move(socketPath))
    , m_ownedFactory(std::move(factory))
    , m_factory(m_ownedFactory.get())
{
    if (m_factory) {
        m_factory->bindDispatcher(&m_dispatcher);
        m_factory->bindResultSubmitter([this](const phicore::adapter::v1::ActionResponse &response) {
            queueDeferredResult(DeferredActionResult{normalizeActionResponse(response)});
        });
    }
    wireHandlers();
}

SidecarHost::SidecarHost(phicore::adapter::v1::Utf8String socketPath, AdapterFactory &factory)
    : m_dispatcher(std::move(socketPath))
    , m_factory(&factory)
{
    m_factory->bindDispatcher(&m_dispatcher);
    m_factory->bindResultSubmitter([this](const phicore::adapter::v1::ActionResponse &response) {
        queueDeferredResult(DeferredActionResult{normalizeActionResponse(response)});
    });
    wireHandlers();
}

SidecarHost::~SidecarHost()
{
    stop();
}

bool SidecarHost::start(phicore::adapter::v1::Utf8String *error)
{
    if (!m_factory) {
        if (error)
            *error = "SidecarHost has no factory";
        return false;
    }
    return m_dispatcher.start(error);
}

void SidecarHost::stop()
{
    stopAndDestroyInstances();
    {
        std::lock_guard<std::mutex> lock(m_resultMutex);
        m_resultQueue.clear();
    }
    if (m_factory)
        m_factory->bindResultSubmitter({});
    m_dispatcher.stop();
}

bool SidecarHost::pollOnce(std::chrono::milliseconds timeout, phicore::adapter::v1::Utf8String *error)
{
    drainDeferredResults();
    m_dispatcher.flushSendQueue(nullptr);
    const bool ok = m_dispatcher.pollOnce(timeout, error);
    drainDeferredResults();
    m_dispatcher.flushSendQueue(nullptr);
    return ok;
}

AdapterFactory *SidecarHost::factory() { return m_factory; }
const AdapterFactory *SidecarHost::factory() const { return m_factory; }
AdapterInstance *SidecarHost::instance(const phicore::adapter::v1::ExternalId &externalId) { return findInstance(externalId); }
const AdapterInstance *SidecarHost::instance(const phicore::adapter::v1::ExternalId &externalId) const { return findInstance(externalId); }
SidecarDispatcher *SidecarHost::dispatcher() { return &m_dispatcher; }
const SidecarDispatcher *SidecarHost::dispatcher() const { return &m_dispatcher; }

phicore::adapter::v1::CmdResponse SidecarHost::normalizeCmdResponse(const phicore::adapter::v1::CmdResponse &response)
{
    CmdResponse out = response;
    if (out.tsMs <= 0)
        out.tsMs = nowMs();
    return out;
}

phicore::adapter::v1::ActionResponse SidecarHost::normalizeActionResponse(const phicore::adapter::v1::ActionResponse &response)
{
    ActionResponse out = response;
    if (out.tsMs <= 0)
        out.tsMs = nowMs();
    return out;
}

AdapterInstance *SidecarHost::findInstance(const phicore::adapter::v1::ExternalId &externalId)
{
    const auto it = m_instances.find(externalId);
    if (it == m_instances.end() || !it->second)
        return nullptr;
    return it->second->instance.get();
}

const AdapterInstance *SidecarHost::findInstance(const phicore::adapter::v1::ExternalId &externalId) const
{
    const auto it = m_instances.find(externalId);
    if (it == m_instances.end() || !it->second)
        return nullptr;
    return it->second->instance.get();
}

AdapterInstance *SidecarHost::ensureInstance(const ConfigChangedRequest &request)
{
    if (!m_factory || request.adapter.externalId.empty())
        return nullptr;
    if (!findRuntime(request.adapter.externalId)) {
        phicore::adapter::v1::Utf8String error;
        if (!createInstanceRuntime(request, &error)) {
            m_factory->hostOnProtocolError("Failed to create instance runtime for externalId='"
                                           + request.adapter.externalId + "': " + error);
            return nullptr;
        }
    }
    return findInstance(request.adapter.externalId);
}

bool SidecarHost::createInstanceRuntime(const ConfigChangedRequest &request, phicore::adapter::v1::Utf8String *error)
{
    if (!m_factory) {
        if (error)
            *error = "Factory not available";
        return false;
    }
    if (request.adapter.externalId.empty()) {
        if (error)
            *error = "externalId is required";
        return false;
    }
    if (findRuntime(request.adapter.externalId))
        return true;

    const AdapterDescriptor descriptor = m_factory->hostDescriptor();
    if (descriptor.maxInstances > 0
        && static_cast<int>(m_instances.size()) >= descriptor.maxInstances) {
        if (error)
            *error = "maxInstances reached";
        return false;
    }

    std::unique_ptr<AdapterInstance> createdInstance = m_factory->hostCreateInstance(request.adapter.externalId);
    if (!createdInstance) {
        if (error)
            *error = "Factory createInstance returned null";
        return false;
    }

    std::unique_ptr<InstanceExecutionBackend> execution
        = m_factory->hostCreateInstanceExecutionBackend(request.adapter.externalId);
    if (!execution) {
        if (error)
            *error = "Factory createInstanceExecutionBackend returned null";
        m_factory->hostDestroyInstance(std::move(createdInstance));
        return false;
    }

    phicore::adapter::v1::Utf8String backendError;
    if (!execution->start(&backendError)) {
        if (error)
            *error = "Failed to start instance execution backend: " + backendError;
        m_factory->hostDestroyInstance(std::move(createdInstance));
        return false;
    }

    createdInstance->bindDispatcher(&m_dispatcher);
    createdInstance->bindResultSubmitters(
        [this](const phicore::adapter::v1::CmdResponse &response) {
            queueDeferredResult(DeferredCmdResult{normalizeCmdResponse(response)});
        },
        [this](const phicore::adapter::v1::ActionResponse &response) {
            queueDeferredResult(DeferredActionResult{normalizeActionResponse(response)});
        });
    createdInstance->bindContext(request.adapterId, request.adapter.pluginType, request.adapter.externalId);

    auto runtime = std::make_unique<InstanceRuntime>();
    runtime->externalId = request.adapter.externalId;
    runtime->instance = std::move(createdInstance);
    runtime->execution = std::move(execution);

    bool started = false;
    bool startDone = false;
    std::mutex startMutex;
    std::condition_variable startCv;
    AdapterInstance *instance = runtime->instance.get();
    backendError.clear();
    if (!runtime->execution->execute(
            [instance, &started, &startDone, &startMutex, &startCv]() {
                const bool ok = instance->hostStart();
                if (!ok)
                    instance->hostOnProtocolError("Instance start() failed");
                {
                    std::lock_guard<std::mutex> lock(startMutex);
                    started = ok;
                    startDone = true;
                }
                startCv.notify_one();
            },
            &backendError)) {
        phicore::adapter::v1::Utf8String stopError;
        if (!runtime->execution->stop(kExecutionBackendStopTimeout, &stopError) && m_factory)
            m_factory->hostOnProtocolError("Execution backend stop failed after start scheduling error: " + stopError);
        if (error)
            *error = "Failed to schedule instance start on execution backend: " + backendError;
        m_factory->hostDestroyInstance(std::move(runtime->instance));
        return false;
    }
    constexpr auto kInstanceStartTimeout = std::chrono::seconds(2);
    {
        std::unique_lock<std::mutex> lock(startMutex);
        if (!startCv.wait_for(lock, kInstanceStartTimeout, [&startDone]() {
                return startDone;
            })) {
            phicore::adapter::v1::Utf8String stopError;
            if (!runtime->execution->stop(kExecutionBackendStopTimeout, &stopError) && m_factory)
                m_factory->hostOnProtocolError("Execution backend stop failed after start timeout: " + stopError);
            if (error)
                *error = "Timed out waiting for instance start completion for externalId='" + request.adapter.externalId + "'";
            m_factory->hostDestroyInstance(std::move(runtime->instance));
            return false;
        }
    }
    if (!started) {
        phicore::adapter::v1::Utf8String stopError;
        if (!runtime->execution->stop(kExecutionBackendStopTimeout, &stopError) && m_factory)
            m_factory->hostOnProtocolError("Execution backend stop failed after instance start failure: " + stopError);
        if (error)
            *error = "Instance start() failed for externalId='" + request.adapter.externalId + "'";
        m_factory->hostDestroyInstance(std::move(runtime->instance));
        return false;
    }

    auto [it, inserted] = m_instances.try_emplace(request.adapter.externalId, nullptr);
    if (!inserted) {
        if (runtime && runtime->execution) {
            phicore::adapter::v1::Utf8String ignoreError;
            if (runtime->instance) {
                runtime->execution->execute([instance = runtime->instance.get()]() {
                    instance->hostStop();
                }, &ignoreError);
            }
            phicore::adapter::v1::Utf8String stopError;
            if (!runtime->execution->stop(kExecutionBackendStopTimeout, &stopError) && m_factory)
                m_factory->hostOnProtocolError("Execution backend stop failed for duplicate instance rollback: " + stopError);
        }
        if (runtime && m_factory && runtime->instance)
            m_factory->hostDestroyInstance(std::move(runtime->instance));
        if (error)
            *error = "Instance runtime already exists";
        return false;
    }
    it->second = std::move(runtime);
    return true;
}

SidecarHost::InstanceRuntime *SidecarHost::findRuntime(const phicore::adapter::v1::ExternalId &externalId)
{
    const auto it = m_instances.find(externalId);
    return it == m_instances.end() ? nullptr : it->second.get();
}

const SidecarHost::InstanceRuntime *SidecarHost::findRuntime(const phicore::adapter::v1::ExternalId &externalId) const
{
    const auto it = m_instances.find(externalId);
    return it == m_instances.end() ? nullptr : it->second.get();
}

bool SidecarHost::executeOnRuntime(const phicore::adapter::v1::ExternalId &externalId,
                                   std::function<void()> task,
                                   phicore::adapter::v1::Utf8String *error)
{
    InstanceRuntime *runtime = findRuntime(externalId);
    if (!runtime || !runtime->execution) {
        if (error)
            *error = "Unknown instance externalId: " + externalId;
        return false;
    }
    return runtime->execution->execute(std::move(task), error);
}

void SidecarHost::executeOnAllRuntimes(const std::function<void(AdapterInstance &)> &fn)
{
    for (const auto &entry : m_instances) {
        InstanceRuntime *runtime = entry.second.get();
        if (!runtime || !runtime->instance || !runtime->execution)
            continue;
        AdapterInstance *instance = runtime->instance.get();
        phicore::adapter::v1::Utf8String error;
        if (!runtime->execution->execute([instance, fn]() {
                fn(*instance);
            }, &error) && m_factory) {
            m_factory->hostOnProtocolError("Failed to dispatch runtime callback for externalId='"
                                           + runtime->externalId + "': " + error);
        }
    }
}

void SidecarHost::queueDeferredResult(DeferredResult result)
{
    std::lock_guard<std::mutex> lock(m_resultMutex);
    m_resultQueue.push_back(std::move(result));
}

void SidecarHost::drainDeferredResults()
{
    std::deque<DeferredResult> local;
    {
        std::lock_guard<std::mutex> lock(m_resultMutex);
        local.swap(m_resultQueue);
    }

    for (auto &result : local) {
        if (auto *cmd = std::get_if<DeferredCmdResult>(&result)) {
            if (cmd->response.id == 0) {
                if (m_factory)
                    m_factory->hostOnProtocolError("Dropped command result with id=0");
                continue;
            }
            phicore::adapter::v1::Utf8String sendError;
            if (!m_dispatcher.sendCmdResult(cmd->response, &sendError) && m_factory)
                m_factory->hostOnProtocolError("Failed to send command result: " + sendError);
            continue;
        }
        if (auto *action = std::get_if<DeferredActionResult>(&result)) {
            if (action->response.id == 0) {
                if (m_factory)
                    m_factory->hostOnProtocolError("Dropped action result with id=0");
                continue;
            }
            phicore::adapter::v1::Utf8String sendError;
            if (!m_dispatcher.sendActionResult(action->response, &sendError) && m_factory)
                m_factory->hostOnProtocolError("Failed to send action result: " + sendError);
        }
    }
}

void SidecarHost::stopAndDestroyInstance(const phicore::adapter::v1::ExternalId &externalId)
{
    const auto it = m_instances.find(externalId);
    if (it == m_instances.end())
        return;

    std::unique_ptr<InstanceRuntime> runtime = std::move(it->second);
    m_instances.erase(it);
    if (!runtime)
        return;

    if (runtime->execution && runtime->instance) {
        phicore::adapter::v1::Utf8String error;
        if (!runtime->execution->execute([instance = runtime->instance.get()]() {
                instance->hostStop();
            }, &error) && m_factory) {
            m_factory->hostOnProtocolError("Failed to schedule stop for externalId='"
                                           + externalId + "': " + error);
        }
    }

    if (runtime->execution) {
        phicore::adapter::v1::Utf8String stopError;
        if (!runtime->execution->stop(kExecutionBackendStopTimeout, &stopError) && m_factory) {
            m_factory->hostOnProtocolError("Execution backend stop timed out for externalId='"
                                           + externalId + "': " + stopError);
        }
    }
    if (m_factory && runtime->instance)
        m_factory->hostDestroyInstance(std::move(runtime->instance));
}

void SidecarHost::stopAndDestroyInstances()
{
    std::vector<phicore::adapter::v1::ExternalId> externalIds;
    externalIds.reserve(m_instances.size());
    for (const auto &entry : m_instances)
        externalIds.push_back(entry.first);

    for (const auto &externalId : externalIds)
        stopAndDestroyInstance(externalId);
}

void SidecarHost::wireHandlers()
{
    SidecarHandlers handlers;
    handlers.onConnected = [this]() {
        if (m_factory)
            m_factory->hostOnConnected();
        executeOnAllRuntimes([](AdapterInstance &instance) {
            instance.hostOnConnected();
        });
    };
    handlers.onDisconnected = [this]() {
        if (m_factory)
            m_factory->hostOnDisconnected();
        executeOnAllRuntimes([](AdapterInstance &instance) {
            instance.hostOnDisconnected();
        });
    };
    handlers.onProtocolError = [this](const phicore::adapter::v1::Utf8String &message) {
        if (m_factory)
            m_factory->hostOnProtocolError(message);
        executeOnAllRuntimes([message](AdapterInstance &instance) {
            instance.hostOnProtocolError(message);
        });
    };
    handlers.onBootstrap = [this](const BootstrapRequest &request) {
        if (!m_factory)
            return;
        if (!request.adapter.externalId.empty()) {
            m_factory->hostOnProtocolError("Bootstrap must target factory scope (externalId must be empty)");
            return;
        }
        BootstrapRequest normalized = request;
        if (normalized.adapter.pluginType.empty())
            normalized.adapter.pluginType = m_factory->hostPluginType();
        m_factory->hostOnBootstrap(normalized);

        AdapterDescriptor descriptor = m_factory->hostDescriptor();
        if (descriptor.pluginType.empty())
            descriptor.pluginType = normalized.adapter.pluginType;
        phicore::adapter::v1::Utf8String err;
        if (!m_dispatcher.sendAdapterDescriptor({}, descriptor, normalized.correlationId, &err))
            m_factory->hostOnProtocolError("Failed to send bootstrap descriptor: " + err);
    };
    handlers.onConfigChanged = [this](const ConfigChangedRequest &request) {
        if (!m_factory)
            return;
        ConfigChangedRequest normalized = request;
        if (normalized.adapter.pluginType.empty())
            normalized.adapter.pluginType = m_factory->hostPluginType();
        if (normalized.adapter.externalId.empty()) {
            m_factory->hostOnFactoryConfigChanged(normalized);
            return;
        }
        if (!ensureInstance(normalized))
            return;
        phicore::adapter::v1::Utf8String error;
        if (!executeOnRuntime(normalized.adapter.externalId,
                              [instance = findInstance(normalized.adapter.externalId), normalized]() {
                                  if (instance)
                                      instance->hostOnConfigChanged(normalized);
                              },
                              &error)) {
            m_factory->hostOnProtocolError("Failed to dispatch config.changed for externalId='"
                                           + normalized.adapter.externalId + "': " + error);
        }
    };
    handlers.onInstanceRemoved = [this](const InstanceRemovedRequest &request) {
        if (!m_factory)
            return;
        if (request.externalId.empty()) {
            m_factory->hostOnProtocolError("InstanceRemoved must target instance scope (externalId required)");
            return;
        }
        stopAndDestroyInstance(request.externalId);
    };
    handlers.onChannelInvoke = [this](const ChannelInvokeRequest &request) {
        CmdResponse response;
        response.id = request.cmdId;
        response.tsMs = nowMs();
        if (request.externalId.empty()) {
            response.status = CmdStatus::InvalidArgument;
            response.error = "externalId is required for CmdChannelInvoke";
            m_dispatcher.sendCmdResult(response, nullptr);
            return;
        }
        phicore::adapter::v1::Utf8String dispatchError;
        if (!executeOnRuntime(request.externalId,
                              [instance = findInstance(request.externalId), request]() {
                                  if (instance)
                                      instance->hostOnChannelInvoke(request);
                              },
                              &dispatchError)) {
            response.status = CmdStatus::InvalidArgument;
            response.error = "Failed to dispatch channel invoke: " + dispatchError;
            m_dispatcher.sendCmdResult(response, nullptr);
        }
    };
    handlers.onAdapterActionInvoke = [this](const AdapterActionInvokeRequest &request) {
        ActionResponse response;
        response.id = request.cmdId;
        response.resultType = ActionResultType::None;
        response.tsMs = nowMs();
        if (!m_factory) {
            response.status = CmdStatus::Failure;
            response.error = "Adapter factory not available";
            m_dispatcher.sendActionResult(response, nullptr);
            return;
        }
        if (request.externalId.empty()) {
            m_factory->hostOnFactoryActionInvoke(request);
            return;
        }
        phicore::adapter::v1::Utf8String dispatchError;
        if (!executeOnRuntime(request.externalId,
                              [instance = findInstance(request.externalId), request]() {
                                  if (instance)
                                      instance->hostOnAdapterActionInvoke(request);
                              },
                              &dispatchError)) {
            response.status = CmdStatus::InvalidArgument;
            response.error = "Failed to dispatch adapter action: " + dispatchError;
            m_dispatcher.sendActionResult(response, nullptr);
        }
    };
    handlers.onDeviceNameUpdate = [this](const DeviceNameUpdateRequest &request) {
        CmdResponse response;
        response.id = request.cmdId;
        response.tsMs = nowMs();
        if (request.externalId.empty()) {
            response.status = CmdStatus::InvalidArgument;
            response.error = "externalId is required for CmdDeviceNameUpdate";
            m_dispatcher.sendCmdResult(response, nullptr);
            return;
        }
        phicore::adapter::v1::Utf8String dispatchError;
        if (!executeOnRuntime(request.externalId,
                              [instance = findInstance(request.externalId), request]() {
                                  if (instance)
                                      instance->hostOnDeviceNameUpdate(request);
                              },
                              &dispatchError)) {
            response.status = CmdStatus::InvalidArgument;
            response.error = "Failed to dispatch device.name.update: " + dispatchError;
            m_dispatcher.sendCmdResult(response, nullptr);
        }
    };
    handlers.onDeviceEffectInvoke = [this](const DeviceEffectInvokeRequest &request) {
        CmdResponse response;
        response.id = request.cmdId;
        response.tsMs = nowMs();
        if (request.externalId.empty()) {
            response.status = CmdStatus::InvalidArgument;
            response.error = "externalId is required for CmdDeviceEffectInvoke";
            m_dispatcher.sendCmdResult(response, nullptr);
            return;
        }
        phicore::adapter::v1::Utf8String dispatchError;
        if (!executeOnRuntime(request.externalId,
                              [instance = findInstance(request.externalId), request]() {
                                  if (instance)
                                      instance->hostOnDeviceEffectInvoke(request);
                              },
                              &dispatchError)) {
            response.status = CmdStatus::InvalidArgument;
            response.error = "Failed to dispatch device.effect.invoke: " + dispatchError;
            m_dispatcher.sendCmdResult(response, nullptr);
        }
    };
    handlers.onSceneInvoke = [this](const SceneInvokeRequest &request) {
        CmdResponse response;
        response.id = request.cmdId;
        response.tsMs = nowMs();
        if (request.externalId.empty()) {
            response.status = CmdStatus::InvalidArgument;
            response.error = "externalId is required for CmdSceneInvoke";
            m_dispatcher.sendCmdResult(response, nullptr);
            return;
        }
        phicore::adapter::v1::Utf8String dispatchError;
        if (!executeOnRuntime(request.externalId,
                              [instance = findInstance(request.externalId), request]() {
                                  if (instance)
                                      instance->hostOnSceneInvoke(request);
                              },
                              &dispatchError)) {
            response.status = CmdStatus::InvalidArgument;
            response.error = "Failed to dispatch scene invoke: " + dispatchError;
            m_dispatcher.sendCmdResult(response, nullptr);
        }
    };
    handlers.onUnknownRequest = [this](const UnknownRequest &request) {
        if (!m_factory)
            return;
        if (request.externalId.empty()) {
            m_factory->hostOnProtocolError("Unhandled IPC command: " + std::to_string(request.command));
            return;
        }
        phicore::adapter::v1::Utf8String dispatchError;
        if (!executeOnRuntime(request.externalId,
                              [instance = findInstance(request.externalId), request]() {
                                  if (instance)
                                      instance->hostOnUnknownRequest(request);
                              },
                              &dispatchError)) {
            m_factory->hostOnProtocolError("Failed to dispatch unknown request for externalId='"
                                           + request.externalId + "': " + dispatchError);
        }
    };
    m_dispatcher.setHandlers(std::move(handlers));
}

} // namespace phicore::adapter::sdk
