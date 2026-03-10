#pragma once

#include <string>
#include <functional>

// Callback interface: SignalingServer -> WebRTCWrapper
struct SignalingHandler {
    std::function<void(const std::string& clientId, const std::string& query)> onRequest;
    std::function<void(const std::string& clientId, const std::string& sdp)> onAnswer;
    std::function<void(const std::string& clientId, const std::string& candidate,
                       const std::string& sdpMid, int sdpMLineIndex)> onIceCandidate;
};

// Abstract interface for signaling transport (WebSocket, HTTP, etc.)
class ISignalingServer {
public:
    virtual ~ISignalingServer() = default;

    virtual void setHandler(SignalingHandler handler) = 0;
    virtual void run() = 0;
    virtual void stop() = 0;

    virtual void sendMessage(const std::string& clientId, const std::string& message) = 0;
    virtual void closeConnection(const std::string& clientId) = 0;
};
