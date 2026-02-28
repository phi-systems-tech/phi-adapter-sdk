#pragma once

#include <chrono>
#include <functional>
#include <string>

#include "phi/adapter/sdk/runtime.h"
#include "phi/adapter/v1/contract.h"

namespace phicore::adapter::sdk {

struct BootstrapRequest {
    int adapterId = 0;
    phicore::adapter::v1::Adapter adapter;
    phicore::adapter::v1::JsonText staticConfigJson;
};

struct ChannelInvokeRequest {
    phicore::adapter::v1::CmdId cmdId = 0;
    phicore::adapter::v1::ExternalId deviceExternalId;
    phicore::adapter::v1::ExternalId channelExternalId;
    phicore::adapter::v1::ScalarValue value;
    phicore::adapter::v1::JsonText valueJson;
    bool hasScalarValue = false;
};

struct AdapterActionInvokeRequest {
    phicore::adapter::v1::CmdId cmdId = 0;
    phicore::adapter::v1::Utf8String actionId;
    phicore::adapter::v1::JsonText paramsJson;
};

struct DeviceNameUpdateRequest {
    phicore::adapter::v1::CmdId cmdId = 0;
    phicore::adapter::v1::ExternalId deviceExternalId;
    phicore::adapter::v1::Utf8String name;
};

struct DeviceEffectInvokeRequest {
    phicore::adapter::v1::CmdId cmdId = 0;
    phicore::adapter::v1::ExternalId deviceExternalId;
    phicore::adapter::v1::DeviceEffect effect = phicore::adapter::v1::DeviceEffect::None;
    phicore::adapter::v1::Utf8String effectId;
    phicore::adapter::v1::JsonText paramsJson;
};

struct SceneInvokeRequest {
    phicore::adapter::v1::CmdId cmdId = 0;
    phicore::adapter::v1::ExternalId sceneExternalId;
    phicore::adapter::v1::ExternalId groupExternalId;
    phicore::adapter::v1::Utf8String action;
};

struct UnknownRequest {
    phicore::adapter::v1::CmdId cmdId = 0;
    phicore::adapter::v1::Utf8String method;
    phicore::adapter::v1::JsonText payloadJson;
};

struct SidecarHandlers {
    std::function<void()> onConnected;
    std::function<void()> onDisconnected;
    std::function<void(const std::string &)> onProtocolError;

    std::function<void(const BootstrapRequest &)> onBootstrap;
    std::function<phicore::adapter::v1::CmdResponse(const ChannelInvokeRequest &)> onChannelInvoke;
    std::function<phicore::adapter::v1::ActionResponse(const AdapterActionInvokeRequest &)> onAdapterActionInvoke;
    std::function<phicore::adapter::v1::CmdResponse(const DeviceNameUpdateRequest &)> onDeviceNameUpdate;
    std::function<phicore::adapter::v1::CmdResponse(const DeviceEffectInvokeRequest &)> onDeviceEffectInvoke;
    std::function<phicore::adapter::v1::CmdResponse(const SceneInvokeRequest &)> onSceneInvoke;
    std::function<void(const UnknownRequest &)> onUnknownRequest;
};

class SidecarDispatcher
{
public:
    explicit SidecarDispatcher(std::string socketPath);

    void setHandlers(SidecarHandlers handlers);

    bool start(std::string *error = nullptr);
    void stop();
    bool pollOnce(std::chrono::milliseconds timeout, std::string *error = nullptr);

    bool sendCmdResult(const phicore::adapter::v1::CmdResponse &response, std::string *error = nullptr);
    bool sendActionResult(const phicore::adapter::v1::ActionResponse &response, std::string *error = nullptr);
    bool sendConnectionStateChanged(bool connected, std::string *error = nullptr);
    bool sendAdapterError(const std::string &message,
                          const phicore::adapter::v1::ScalarList &params = {},
                          const std::string &ctx = {},
                          std::string *error = nullptr);
    bool sendAdapterMetaUpdated(const phicore::adapter::v1::JsonText &metaPatchJson,
                                std::string *error = nullptr);
    bool sendChannelStateUpdated(const phicore::adapter::v1::ExternalId &deviceExternalId,
                                 const phicore::adapter::v1::ExternalId &channelExternalId,
                                 const phicore::adapter::v1::ScalarValue &value,
                                 std::int64_t tsMs = 0,
                                 std::string *error = nullptr);
    bool sendDeviceUpdated(const phicore::adapter::v1::Device &device,
                           const phicore::adapter::v1::ChannelList &channels,
                           std::string *error = nullptr);
    bool sendDeviceRemoved(const phicore::adapter::v1::ExternalId &deviceExternalId,
                           std::string *error = nullptr);
    bool sendChannelUpdated(const phicore::adapter::v1::ExternalId &deviceExternalId,
                            const phicore::adapter::v1::Channel &channel,
                            std::string *error = nullptr);
    bool sendRoomUpdated(const phicore::adapter::v1::Room &room, std::string *error = nullptr);
    bool sendRoomRemoved(const phicore::adapter::v1::ExternalId &roomExternalId, std::string *error = nullptr);
    bool sendGroupUpdated(const phicore::adapter::v1::Group &group, std::string *error = nullptr);
    bool sendGroupRemoved(const phicore::adapter::v1::ExternalId &groupExternalId, std::string *error = nullptr);
    bool sendScenesUpdated(const phicore::adapter::v1::SceneList &scenes, std::string *error = nullptr);
    bool sendFullSyncCompleted(std::string *error = nullptr);

private:
    bool handleRequestFrame(const phicore::adapter::v1::FrameHeader &header,
                            std::span<const std::byte> payload);
    bool sendJson(phicore::adapter::v1::MessageType type,
                  phicore::adapter::v1::CorrelationId correlationId,
                  std::string_view json,
                  std::string *error);

    SidecarRuntime m_runtime;
    SidecarHandlers m_handlers;
};

} // namespace phicore::adapter::sdk
