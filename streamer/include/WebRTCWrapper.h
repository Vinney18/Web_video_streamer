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
    std::function<void(const std::string& clientId)> closeConnection;
    std::function<bool(const std::string& clientId)> isConnected;
};

// WebRTC connection information
struct WebRTCConnectionInfo {
    std::string clientId;
    rtcConnHdl peerConnection;
    std::shared_ptr<rtc::DataChannel> dataChannel;
    std::shared_ptr<rtc::Track> videoTrack;
    std::shared_ptr<rtc::RtpPacketizationConfig> rtpConfig;  // RTP config for timestamp control
    std::string query;
    std::shared_ptr<FFmpegWrapper> ffmpegWrapper;  // direct ref to this client's FFmpeg
    std::string ffmpegKey;                          // key in liveStreams map (empty for playback)
    bool isConnected = false;
    bool iceConnected = false;
    bool gatheringComplete = false;
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
    void handleRequest(const std::string& clientId, const std::string& query);
    void handleAnswer(const std::string& clientId, const std::string& sdp);
    void handleIceCandidate(const std::string& clientId, const std::string& candidate,
                           const std::string& sdpMid, int sdpMLineIndex);

    // Callbacks for FFmpegWrapper
    void SendData(rtcConnHdl& conn, std::vector<uint8_t>& data, int64_t timestamp);
    void SendStringData(rtcConnHdl& conn, std::string sdata);

    // Close peer connection (triggers onStateChange → removeConnection)
    void closePeerConnectionIfNotConnected(const std::string& clientId);

    // Stats logging
    void logStats();

private:
    // WebRTC peer connection management
    void createPeerConnection(const std::string& clientId, const std::string& query,
                              const std::string& url, AVCodecID codecId);
    void removeConnection(const std::string& clientId);
    void handleDataChannelMessage(const std::string& clientId, std::variant<rtc::binary, std::string> data);

    // Query processing
    void onFullyConnected(const std::string& clientId, const std::string& query, const std::string& url);
    void processRequest(const std::string& clientId,const std::string& query, const std::string& url);

    // Video track setup (codec-specific packetizer selection)
    void setupVideoTrack(std::shared_ptr<rtc::PeerConnection> pc,
                         std::shared_ptr<WebRTCConnectionInfo> connInfo,
                         const std::string& url, AVCodecID codecId);

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
};
