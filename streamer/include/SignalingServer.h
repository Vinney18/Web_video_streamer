#pragma once

#include <rtc/rtc.hpp>
#include <string>
#include <map>
#include <mutex>
#include <memory>
#include <functional>
#include <atomic>
#include <spdlog/spdlog.h>

// Callback interface: SignalingServer -> WebRTCWrapper
struct SignalingHandler {
    std::function<void(const std::string& clientId, const std::string& query)> onRequest;
    std::function<void(const std::string& clientId, const std::string& sdp)> onAnswer;
    std::function<void(const std::string& clientId, const std::string& candidate,
                       const std::string& sdpMid, int sdpMLineIndex)> onIceCandidate;
};

class SignalingServer {
public:
    SignalingServer(int port, std::shared_ptr<spdlog::logger> logger);
    ~SignalingServer();

    void setHandler(SignalingHandler handler);
    void run();
    void stop();

    void sendMessage(const std::string& clientId, const std::string& message);
    void closeConnection(const std::string& clientId);

private:
    void onClientConnected(std::shared_ptr<rtc::WebSocket> ws);
    void onMessage(const std::string& clientId, const std::string& message);
    void onClosed(const std::string& clientId);
    void onError(const std::string& clientId, const std::string& error);

    int port_;
    std::shared_ptr<spdlog::logger> logger_;
    std::shared_ptr<rtc::WebSocketServer> server_;
    std::map<std::string, std::shared_ptr<rtc::WebSocket>> connections_;
    std::mutex connectionsMutex_;
    SignalingHandler handler_;
    std::atomic<bool> running_{false};
};
