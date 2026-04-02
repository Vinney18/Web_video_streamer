#pragma once

#include "SignalingServers/ISignalingServer.h"
#include <rtc/rtc.hpp>
#include <string>
#include <map>
#include <mutex>
#include <memory>
#include <atomic>
#include <spdlog/spdlog.h>
#include <json/json.h>

class WebRTCWrapper;

struct WsClientData {
    std::shared_ptr<rtc::WebSocket> ws;
    Json::Value requestQuery;  // stored when "request" message arrives
};

class WebSocketSignalingServer : public ISignalingServer {
public:
    WebSocketSignalingServer(int port, std::shared_ptr<spdlog::logger> logger);
    ~WebSocketSignalingServer() override;

    void run() override;
    void stop() override;

    void sendMessage(const std::string& clientId, const std::string& message) override;
    void closeConnection(const std::string& clientId) override;
    Json::Value getClientQuery(const std::string& clientId) override;
    bool isConnected(const std::string& clientId);

private:
    void onClientConnected(std::shared_ptr<rtc::WebSocket> ws);
    void onMessage(const std::string& clientId, const std::string& message);
    void onClosed(const std::string& clientId);
    void onError(const std::string& clientId, const std::string& error);

    int port_;
    std::shared_ptr<spdlog::logger> logger_;
    std::shared_ptr<rtc::WebSocketServer> server_;
    std::map<std::string, WsClientData> connections_;
    std::mutex connectionsMutex_;
    std::atomic<bool> running_{false};

    std::unique_ptr<WebRTCWrapper> rtcWrapper_;
};
