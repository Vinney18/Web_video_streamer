#pragma once

#include <rtc/rtc.hpp>
#include <string>
#include <map>
#include <mutex>
#include <memory>
#include <chrono>
#include <variant>
#include <functional>
#include <spdlog/spdlog.h>
#include "Ffmpeg/FFmpegWrapper.h"
#include "PlayerServerClient.h"
#include "SyncHandler.h"
#include "common.h"

// Forward declarations
namespace rtc {
    class PeerConnection;
    class DataChannel;
    class Track;
}

// Connection handle type for WebRTC
using rtcConnHdl = std::shared_ptr<rtc::PeerConnection>;

// Transport callbacks provided by signaling to WebRTC
struct SignalingTransport {
    std::function<void(const std::string& clientId, const std::string& message)> sendMessage;
    std::function<void(const std::string& clientId, const uint8_t* data, size_t size)> sendBinary;
    std::function<size_t(const std::string& clientId)> bufferedAmount;
    std::function<void(const std::string& clientId)> closeConnection;
    std::function<bool(const std::string& clientId)> isConnected;
    std::function<Json::Value(const std::string& clientId)> getQuery;
};

// WebRTC connection information
struct WebRTCConnectionInfo {
    std::string clientId;
    rtcConnHdl peerConnection;
    std::shared_ptr<rtc::DataChannel> dataChannel;
    std::shared_ptr<rtc::Track> videoTrack;
    std::shared_ptr<rtc::RtpPacketizationConfig> rtpConfig;  // RTP config for timestamp control
    Json::Value query;
    Json::Value streamInfo;
    std::shared_ptr<FFmpegWrapper> ffmpegWrapper;  // direct ref to this client's FFmpeg
    std::string ffmpegKey;                          // key in liveStreams map (empty for playback)
    std::string syncGroupId;                        // sync group ID (empty if not part of a group)
    bool isConnected = false;
    bool streamStarted = false;  // guards against starting the media pipeline more than once
    uint64_t packetsSent = 0;
    std::chrono::steady_clock::time_point createdAt;

    WebRTCConnectionInfo() : createdAt(std::chrono::steady_clock::now()) {}
};

class WebRTCWrapper {
public:
    WebRTCWrapper();
    ~WebRTCWrapper();

    // Signaling provides its transport capabilities
    void setSignalingTransport(SignalingTransport transport);

    // Called by signaling when messages arrive from clients
    void startStream(const std::string& clientId, const std::string& url,
                     AVCodecID codecId, const Json::Value& streamInfo);
    void handleAnswer(const std::string& clientId, const std::string& sdp);
    void handleIceCandidate(const std::string& clientId, const std::string& candidate,
                           const std::string& sdpMid, int sdpMLineIndex);

    // Callbacks for FFmpegWrapper
    void SendData(webConnHdl& clientId, std::vector<uint8_t>& data);
    void SendStringData(webConnHdl& clientId, std::string sdata);

    // Close peer connection (triggers onStateChange → removeConnection)
    void closePeerConnectionIfNotConnected(const std::string& clientId);

    // Stats logging
    void logStats();

private:
    // WebRTC peer connection management
    void createPeerConnection(const std::string& clientId,
                              const std::string& url, AVCodecID codecId,
                              const Json::Value& streamInfo);
    void removeConnection(const std::string& clientId);
    void handleDataChannelMessage(const std::string& clientId, std::variant<rtc::binary, std::string> data);

    // Query processing
    void onFullyConnected(const std::string& clientId);
    void processRequest(const std::string& clientId);

    // Video track setup (codec-specific packetizer selection)
    void setupVideoTrack(std::shared_ptr<rtc::PeerConnection> pc,
                         std::shared_ptr<WebRTCConnectionInfo> connInfo,
                         const std::string& url, AVCodecID codecId);

    // Helper to build a JSON string with "type" and "message" keys
    static std::string buildJsonMessage(const std::string& type, const std::string& message);

    // Member variables
    std::shared_ptr<spdlog::logger> mainLogger;

    // Signaling transport callbacks
    SignalingTransport signalingTransport_;

    // WebRTC connection storage
    std::map<std::string, std::shared_ptr<WebRTCConnectionInfo>> connections;
    std::mutex connectionsMutex;

    // Live stream sharing: URL → shared FFmpegWrapper (only for live mode)
    std::map<std::string, std::shared_ptr<FFmpegWrapper>> liveStreams;
    std::mutex liveStreamsMutex;

    // Sync handler for coordinated playback start
    SyncHandler syncHandler_;
};
