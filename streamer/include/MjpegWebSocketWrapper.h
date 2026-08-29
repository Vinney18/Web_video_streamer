#pragma once

#include "WebRTCWrapper.h"  // for SignalingTransport
#include "Ffmpeg/FFmpegWrapper.h"

#include <map>
#include <mutex>
#include <memory>
#include <string>
#include <spdlog/spdlog.h>
#include <json/json.h>

class MjpegWebSocketWrapper {
public:
    MjpegWebSocketWrapper();
    ~MjpegWebSocketWrapper();

    void setSignalingTransport(SignalingTransport transport);

    // Called by WS server when "request" arrives and codec probes as MJPEG.
    void startStream(const std::string& clientId, const std::string& url,
                     const Json::Value& streamInfo);

    // Called by WS server when the client's WS closes.
    // No-op if clientId is not an MJPEG subscriber.
    void removeClient(const std::string& clientId);

    void logStats();

private:
    // FFmpeg send callbacks
    void SendData(webConnHdl& clientId, std::vector<uint8_t>& jpeg);
    void SendStringData(webConnHdl& clientId, std::string sdata);

    std::shared_ptr<spdlog::logger> mainLogger;
    SignalingTransport signalingTransport_;

    // Live mode: url -> shared FFmpeg wrapper (one upstream per source URL,
    // fanned out to all subscribers on that URL).
    std::map<std::string, std::shared_ptr<FFmpegWrapper>> liveStreams_;
    std::mutex liveStreamsMutex_;

    // Playback mode: clientId -> per-client FFmpeg wrapper. Playback has
    // independent seek/pacing per client, so these are never shared.
    std::map<std::string, std::shared_ptr<FFmpegWrapper>> playbackStreams_;
    std::mutex playbackStreamsMutex_;

    // clientId -> url (for fast cleanup on removeClient). Empty url marks a
    // playback client (torn down via playbackStreams_ instead of liveStreams_).
    std::map<std::string, std::string> clientToUrl_;
    std::mutex clientMapMutex_;
};
