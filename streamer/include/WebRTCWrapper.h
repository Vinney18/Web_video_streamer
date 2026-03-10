#pragma once

#include <rtc/rtc.hpp>
#include <string>
#include <map>
#include <mutex>
#include <memory>
#include <chrono>
#include <variant>
#include <spdlog/spdlog.h>
#include "FFmpegWrapper.h"
#include "PlayerServerClient.h"
#include "common.h"

// Forward declarations
namespace rtc {
    class PeerConnection;
    class DataChannel;
    class Track;
    class WebSocket;
    class WebSocketServer;
}

// Connection handle type for WebRTC
using rtcConnHdl = std::shared_ptr<rtc::PeerConnection>;

// WebSocket connection info for signaling
struct WebSocketConnectionInfo {
    std::string clientId;
    std::shared_ptr<rtc::WebSocket> webSocket;
    std::weak_ptr<rtc::PeerConnection> peerConnection;
};

// WebRTC connection information
struct WebRTCConnectionInfo {
    std::string clientId;
    rtcConnHdl peerConnection;
    std::shared_ptr<rtc::DataChannel> dataChannel;
    std::shared_ptr<rtc::Track> videoTrack;
    std::shared_ptr<rtc::RtpPacketizationConfig> rtpConfig;  // RTP config for timestamp control
    std::string query;
    bool isConnected = false;
    bool iceConnected = false;
    bool gatheringComplete = false;
    std::chrono::steady_clock::time_point createdAt;

    WebRTCConnectionInfo() : createdAt(std::chrono::steady_clock::now()) {}
};

class WebRTCWrapper {
public:
    WebRTCWrapper(int wsPort, const std::string& playerIp, int playerPort,
                  std::shared_ptr<spdlog::logger> logger, bool isVMS,
                  const std::string& vmsUser, const std::string& vmsPassword);
    ~WebRTCWrapper();

    void run();  // Start WebSocket signaling server

    // Callbacks for FFmpegWrapper
    void SendData(rtcConnHdl& conn, std::vector<uint8_t>& data, int64_t timestamp);
    void SendStringData(rtcConnHdl& conn, std::string sdata);

private:
    // WebSocket signaling handlers
    void onWebSocketOpen(std::shared_ptr<rtc::WebSocket> ws);
    void onWebSocketClosed(const std::string& clientId);
    void onWebSocketMessage(const std::string& clientId, const std::string& message);
    void onWebSocketError(const std::string& clientId, const std::string& error);

    // Send message via WebSocket
    void sendSignalingMessage(const std::string& clientId, const std::string& message);
    void sendSignalingJson(const std::string& clientId, const std::string& type,
                          const std::map<std::string, std::string>& data);

    // WebRTC peer connection management
    void createPeerConnection(const std::string& clientId, const std::string& query, const std::string& url);
    void handleRequest(const std::string& clientId, const std::string& query);
    void handleAnswer(const std::string& clientId, const std::string& sdp);
    void handleIceCandidate(const std::string& clientId, const std::string& candidate,
                           const std::string& sdpMid, int sdpMLineIndex);
    void removeConnection(const std::string& clientId);
    void handleDataChannelMessage(const std::string& clientId, std::variant<rtc::binary, std::string> data);

    // Query processing (adapted from WebSocketWrapper)
    void processRequest(const std::string& clientId, std::string& query, const std::string& url);

    // WebSocket lifecycle
    void closeWebSocket(const std::string& clientId);
    void tryCloseWebSocket(const std::string& clientId);  // call with connectionsMutex held

    // Utility methods
    int generateAndCheckRandomNumber();
    void handlePlaybackFinished(const std::string& clientId, const std::string& jsonData);

    // Member variables
    int ws_signaling_port;
    std::string playerServerIp;
    int playerServerPort;
    std::shared_ptr<spdlog::logger> mainLogger;

    // WebSocket server for signaling
    std::shared_ptr<rtc::WebSocketServer> wsServer;

    // WebSocket connections (clientId -> WebSocket)
    std::map<std::string, std::shared_ptr<WebSocketConnectionInfo>> wsConnections;
    std::mutex wsConnectionsMutex;

    // WebRTC connection storage
    std::map<std::string, std::shared_ptr<WebRTCConnectionInfo>> connections;
    std::mutex connectionsMutex;

    // FFmpeg wrapper instances
    std::map<std::string, std::shared_ptr<FFmpegWrapper>> ffmpegList;
    std::mutex ffmpegListMutex;

    // Client ID to FFmpeg key mapping
    std::map<std::string, std::string> clientToFfmpegMap;
    std::mutex clientMapMutex;

    std::atomic<bool> running;
};
