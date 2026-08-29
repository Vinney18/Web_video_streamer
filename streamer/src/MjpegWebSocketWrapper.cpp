#include "MjpegWebSocketWrapper.h"
#include "AppConfig.h"
#include "Ffmpeg/LiveFFmpegWrapper.h"
#include "Ffmpeg/PlaybackFFmpegWrapper.h"

#include <json/json.h>

MjpegWebSocketWrapper::MjpegWebSocketWrapper()
    : mainLogger(AppConfig::instance().logger())
{
    if (mainLogger)
    {
        mainLogger->info("MjpegWebSocketWrapper initialized");
    }
}

MjpegWebSocketWrapper::~MjpegWebSocketWrapper()
{
    {
        std::lock_guard<std::mutex> lock(liveStreamsMutex_);
        liveStreams_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(playbackStreamsMutex_);
        playbackStreams_.clear();
    }
    if (mainLogger)
    {
        mainLogger->info("MjpegWebSocketWrapper destroyed");
    }
}

void MjpegWebSocketWrapper::setSignalingTransport(SignalingTransport transport)
{
    signalingTransport_ = std::move(transport);
}

void MjpegWebSocketWrapper::SendData(webConnHdl &clientId, std::vector<uint8_t> &jpeg)
{
    // Drop frames when the WS outgoing queue grows past this threshold.
    // MJPEG is I-frame-only, so dropping is safe — the next frame is fully decodable.
    // Matches the WS maxMessageSize (16 MB) so a single max-size message can never
    // trip the threshold against the next frame.
    constexpr size_t kBackpressureLimitBytes = 16 * 1024 * 1024;  // 16 MB

    try
    {
        if (jpeg.empty()) return;
        if (!signalingTransport_.isConnected(clientId)) return;

        const size_t buffered = signalingTransport_.bufferedAmount(clientId);
        if (buffered > kBackpressureLimitBytes)
        {
            if (mainLogger)
            {
                mainLogger->warn("MJPEG: data not sent for {} due to buffer overflow (buffered={} bytes, limit={} bytes, frame={} bytes)",
                                 clientId, buffered, kBackpressureLimitBytes, jpeg.size());
            }
            return;
        }

        signalingTransport_.sendBinary(clientId, jpeg.data(), jpeg.size());
    }
    catch (const std::exception &ex)
    {
        if (mainLogger)
        {
            mainLogger->error("MjpegWebSocketWrapper::SendData error for {}: {}", clientId, ex.what());
        }
    }
}

void MjpegWebSocketWrapper::SendStringData(webConnHdl &clientId, std::string sdata)
{
    try
    {
        if (!signalingTransport_.isConnected(clientId)) return;
        Json::Value msg;
        msg["type"] = "mjpegInfo";
        msg["message"] = sdata;
        Json::StreamWriterBuilder w;
        signalingTransport_.sendMessage(clientId, Json::writeString(w, msg));
    }
    catch (const std::exception &ex)
    {
        if (mainLogger)
        {
            mainLogger->error("MjpegWebSocketWrapper::SendStringData error for {}: {}", clientId, ex.what());
        }
    }
}

void MjpegWebSocketWrapper::startStream(const std::string &clientId, const std::string &url,
                                       const Json::Value &streamInfo)
{
    try
    {
        if (mainLogger)
        {
            mainLogger->info("Starting MJPEG stream for client: {} url={}", clientId, url);
        }

        // Tell the client to switch to MJPEG mode BEFORE attaching to FFmpeg.
        Json::Value status;
        status["type"] = "mjpegStream";
        status["url"] = url;
        status["cameraId"] = streamInfo.get("cameraId", "").asString();
        Json::StreamWriterBuilder w;
        signalingTransport_.sendMessage(clientId, Json::writeString(w, status));

        auto bindSendData = std::bind(&MjpegWebSocketWrapper::SendData, this,
                                      std::placeholders::_1, std::placeholders::_2);
        auto bindSendStringData = std::bind(&MjpegWebSocketWrapper::SendStringData, this,
                                            std::placeholders::_1, std::placeholders::_2);

        std::function<void(webConnHdl &, std::vector<uint8_t> &)> sendDataFunc = bindSendData;
        std::function<void(webConnHdl &, std::string)> sendStringDataFunc = bindSendStringData;

        Json::Value enrichedInfo = streamInfo;
        enrichedInfo["url"] = url;
        if (!enrichedInfo.isMember("cameraId"))
            enrichedInfo["cameraId"] = streamInfo.get("cameraId", "").asString();
        if (!enrichedInfo.isMember("connectionMode"))
            enrichedInfo["connectionMode"] = streamInfo.get("connectionMode", "tcp").asString();

        const std::string mode = streamInfo.get("mode", "Live").asString();
        webConnHdl handle = clientId;

        if (mode == "PlayBack")
        {
            // Playback needs independent seek/pacing per client, so each client
            // gets its own wrapper (never shared/deduplicated).
            auto ffmpeg = std::make_shared<PlaybackFFmpegWrapper>(enrichedInfo, sendDataFunc, sendStringDataFunc);
            ffmpeg->addConnToList(handle);
            {
                std::lock_guard<std::mutex> lock(playbackStreamsMutex_);
                playbackStreams_[clientId] = ffmpeg;
            }
            ffmpeg->startThread();
            {
                // Empty url marks this client as a playback subscriber.
                std::lock_guard<std::mutex> lock(clientMapMutex_);
                clientToUrl_[clientId] = "";
            }
            if (mainLogger)
            {
                mainLogger->info("MJPEG: started playback FFmpeg for client {} url={}", clientId, url);
            }
        }
        else
        {
            // Live: one shared upstream per URL, fanned out to all subscribers.
            std::shared_ptr<FFmpegWrapper> ffmpeg;
            {
                std::lock_guard<std::mutex> lock(liveStreamsMutex_);
                auto it = liveStreams_.find(url);
                if (it == liveStreams_.end())
                {
                    ffmpeg = std::make_shared<LiveFFmpegWrapper>(enrichedInfo, sendDataFunc, sendStringDataFunc);
                    liveStreams_[url] = ffmpeg;
                    ffmpeg->startThread();
                    if (mainLogger)
                    {
                        mainLogger->info("MJPEG: started upstream FFmpeg for {}", url);
                    }
                }
                else
                {
                    ffmpeg = it->second;
                }
            }

            ffmpeg->addConnToList(handle);

            {
                std::lock_guard<std::mutex> lock(clientMapMutex_);
                clientToUrl_[clientId] = url;
            }
        }
    }
    catch (const std::exception &ex)
    {
        if (mainLogger)
        {
            mainLogger->error("MJPEG startStream error for {}: {}", clientId, ex.what());
        }
        Json::Value err;
        err["type"] = "error";
        err["message"] = ex.what();
        Json::StreamWriterBuilder w;
        signalingTransport_.sendMessage(clientId, Json::writeString(w, err));
    }
}

void MjpegWebSocketWrapper::removeClient(const std::string &clientId)
{
    try
    {
        std::string url;
    {
        std::lock_guard<std::mutex> lock(clientMapMutex_);
        auto it = clientToUrl_.find(clientId);
        if (it == clientToUrl_.end()) {
            return;  // not an MJPEG client
        }
        url = it->second;
        clientToUrl_.erase(it);
    }

    // Empty url marks a playback client: its wrapper is per-client, so tear it
    // down directly rather than ref-counting a shared live upstream.
    if (url.empty())
    {
        std::shared_ptr<FFmpegWrapper> ffmpeg;
        {
            std::lock_guard<std::mutex> lock(playbackStreamsMutex_);
            auto it = playbackStreams_.find(clientId);
            if (it == playbackStreams_.end()) {
                return;
            }
            ffmpeg = it->second;
            playbackStreams_.erase(it);
        }
        ffmpeg->stopThread();
        if (mainLogger)
        {
            mainLogger->info("MJPEG: torn down playback FFmpeg for client {}", clientId);
        }
        return;
    }

    std::shared_ptr<FFmpegWrapper> ffmpeg;
    {
        std::lock_guard<std::mutex> lock(liveStreamsMutex_);
        auto it = liveStreams_.find(url);
        if (it == liveStreams_.end()) {
            return;
        }
        ffmpeg = it->second;
    }

    bool canStop = ffmpeg->removeConnection(clientId);

    if (canStop)
    {
        ffmpeg->stopThread();
        {
            std::lock_guard<std::mutex> lock(liveStreamsMutex_);
            liveStreams_.erase(url);
        }
        if (mainLogger)
        {
            mainLogger->info("MJPEG: torn down upstream FFmpeg for {}", url);
        }
    }
    else
    {
        if (mainLogger)
        {
            mainLogger->info("MJPEG: client {} unsubscribed from {} (other subs remain)", clientId, url);
        }
    }
    }
    catch(const std::exception& e)
    {
        std::cout << e.what() << '\n';
    }
    
    
}

void MjpegWebSocketWrapper::logStats()
{
    size_t streamCount = 0;
    size_t clientCount = 0;
    {
        std::lock_guard<std::mutex> lock(liveStreamsMutex_);
        streamCount = liveStreams_.size();
    }
    {
        std::lock_guard<std::mutex> lock(clientMapMutex_);
        clientCount = clientToUrl_.size();
    }
    if (mainLogger)
    {
        mainLogger->info("[Stats] MJPEG: streams={}, subscribers={}", streamCount, clientCount);
    }
}
