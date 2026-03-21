#pragma once

#include <string>
#include <json/json.h>

// Abstract interface for signaling transport (WebSocket, HTTP, etc.)
class ISignalingServer {
public:
    virtual ~ISignalingServer() = default;

    virtual void run() = 0;
    virtual void stop() = 0;

    virtual void sendMessage(const std::string& clientId, const std::string& message) = 0;
    virtual void closeConnection(const std::string& clientId) = 0;
    virtual Json::Value getClientQuery(const std::string& clientId)=0;
};
