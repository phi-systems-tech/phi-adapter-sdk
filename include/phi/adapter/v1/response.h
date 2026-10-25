#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "phi/adapter/v1/schema.h"

namespace phicore::adapter::v1 {

// What an adapter answers a command or an action with. Behind d-pointers like
// the entities (see channel.h for the rule); the static makers write the
// common answers in one line:
//
//     return v1::ActionResponse::success(cmdId).setData(json);
//     return v1::CmdResponse::failure(cmdId, v1::CmdStatus::InvalidArgument, "Missing '%1'", {"host"});

/// The answer to a channel write or another plain command.
class CmdResponse
{
public:
    CmdResponse();
    CmdResponse(const CmdResponse &other);
    CmdResponse(CmdResponse &&other) noexcept;
    CmdResponse &operator=(const CmdResponse &other);
    CmdResponse &operator=(CmdResponse &&other) noexcept;
    ~CmdResponse();

    static CmdResponse success(CmdId id, ScalarValue finalValue = {});
    static CmdResponse failure(CmdId id, CmdStatus status, Utf8String error, ScalarList errorParams = {},
                               Utf8String errorContext = {});

    CmdId id() const noexcept;
    CmdResponse &setId(CmdId id);
    CmdStatus status() const noexcept;
    CmdResponse &setStatus(CmdStatus status);
    /// The message, with %1.. for errorParams; errorContext names where it arose.
    const Utf8String &error() const noexcept;
    CmdResponse &setError(Utf8String error);
    const ScalarList &errorParams() const noexcept;
    CmdResponse &setErrorParams(ScalarList params);
    const Utf8String &errorContext() const noexcept;
    CmdResponse &setErrorContext(Utf8String context);
    /// The value the channel settled on, when the device says.
    const ScalarValue &finalValue() const noexcept;
    CmdResponse &setFinalValue(ScalarValue value);
    /// 0 lets the library stamp the time it sends.
    std::int64_t tsMs() const noexcept;
    CmdResponse &setTsMs(std::int64_t tsMs);

private:
    struct Private;
    const Private &p() const noexcept;
    Private &m();
    std::unique_ptr<Private> d;
};

/// What a person is shown for an action's result.
class AdapterResultDisplay
{
public:
    AdapterResultDisplay();
    /// A text, something to copy or type elsewhere (a pairing code), a QR payload.
    AdapterResultDisplay(Utf8String text, Utf8String code = {}, Utf8String qr = {});
    AdapterResultDisplay(const AdapterResultDisplay &other);
    AdapterResultDisplay(AdapterResultDisplay &&other) noexcept;
    AdapterResultDisplay &operator=(const AdapterResultDisplay &other);
    AdapterResultDisplay &operator=(AdapterResultDisplay &&other) noexcept;
    ~AdapterResultDisplay();

    const Utf8String &text() const noexcept;
    AdapterResultDisplay &setText(Utf8String text);
    const Utf8String &code() const noexcept;
    AdapterResultDisplay &setCode(Utf8String code);
    const Utf8String &qr() const noexcept;
    AdapterResultDisplay &setQr(Utf8String qr);

private:
    struct Private;
    const Private &p() const noexcept;
    Private &m();
    std::unique_ptr<Private> d;
};

/// A long action that runs on and streams what it does.
class AdapterRunHandle
{
public:
    AdapterRunHandle();
    AdapterRunHandle(const AdapterRunHandle &other);
    AdapterRunHandle(AdapterRunHandle &&other) noexcept;
    AdapterRunHandle &operator=(const AdapterRunHandle &other);
    AdapterRunHandle &operator=(AdapterRunHandle &&other) noexcept;
    ~AdapterRunHandle();

    const Utf8String &runId() const noexcept;
    AdapterRunHandle &setRunId(Utf8String runId);
    const Utf8String &streamKind() const noexcept;
    AdapterRunHandle &setStreamKind(Utf8String streamKind);
    const ChannelValueFields &streamParams() const noexcept;
    AdapterRunHandle &setStreamParams(ChannelValueFields params);
    /// The action that stops the run, with its params.
    const Utf8String &abortActionId() const noexcept;
    AdapterRunHandle &setAbortActionId(Utf8String actionId);
    const ChannelValueFields &abortParams() const noexcept;
    AdapterRunHandle &setAbortParams(ChannelValueFields params);
    bool batch() const noexcept;
    AdapterRunHandle &setBatch(bool batch);

private:
    struct Private;
    const Private &p() const noexcept;
    Private &m();
    std::unique_ptr<Private> d;
};

/// The answer to an adapter action: a status, a result in the shape
/// resultType names, and what the form should show next.
class ActionResponse
{
public:
    ActionResponse();
    ActionResponse(const ActionResponse &other);
    ActionResponse(ActionResponse &&other) noexcept;
    ActionResponse &operator=(const ActionResponse &other);
    ActionResponse &operator=(ActionResponse &&other) noexcept;
    ~ActionResponse();

    static ActionResponse success(CmdId id);
    static ActionResponse failure(CmdId id, CmdStatus status, Utf8String error, ScalarList errorParams = {},
                                  Utf8String errorContext = {});

    CmdId id() const noexcept;
    ActionResponse &setId(CmdId id);
    CmdStatus status() const noexcept;
    ActionResponse &setStatus(CmdStatus status);
    const Utf8String &error() const noexcept;
    ActionResponse &setError(Utf8String error);
    const ScalarList &errorParams() const noexcept;
    ActionResponse &setErrorParams(ScalarList params);
    const Utf8String &errorContext() const noexcept;
    ActionResponse &setErrorContext(Utf8String context);

    ActionResultType resultType() const noexcept;
    ActionResponse &setResultType(ActionResultType type);
    /// The result for the scalar result types.
    const ScalarValue &resultValue() const noexcept;
    ActionResponse &setResultValue(ScalarValue value);
    /// Value and type in one call.
    ActionResponse &setResult(ScalarValue value, ActionResultType type);
    /// The result for ActionResultType::Display; setting it sets the type.
    const AdapterResultDisplay &display() const noexcept;
    ActionResponse &setDisplay(AdapterResultDisplay display);
    /// The result for ActionResultType::Run; setting it sets the type.
    const AdapterRunHandle &run() const noexcept;
    ActionResponse &setRun(AdapterRunHandle run);
    /// The result for ActionResultType::Data, JSON a tool reads; setting it sets the type.
    const JsonText &dataJson() const noexcept;
    ActionResponse &setData(JsonText dataJson);

    const AdapterFormValues &formValues() const noexcept;
    ActionResponse &setFormValues(AdapterFormValues values);
    ActionResponse &addFormValue(Utf8String key, AdapterFormValueData data);
    const AdapterFieldChoicesList &fieldChoices() const noexcept;
    ActionResponse &setFieldChoices(AdapterFieldChoicesList choices);
    ActionResponse &addFieldChoices(Utf8String key, AdapterConfigOptionList choices);
    /// The client asks for the form again after this answer.
    bool reloadLayout() const noexcept;
    ActionResponse &setReloadLayout(bool reload);
    /// 0 lets the library stamp the time it sends.
    std::int64_t tsMs() const noexcept;
    ActionResponse &setTsMs(std::int64_t tsMs);

private:
    struct Private;
    const Private &p() const noexcept;
    Private &m();
    std::unique_ptr<Private> d;
};

} // namespace phicore::adapter::v1
