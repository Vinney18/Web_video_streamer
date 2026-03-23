#include "WebRTCWrapper.h"
#include "AppConfig.h"
#include "common.h"
#include "json/json.h"
#include <json/value.h>
#include "Options.h"
#include "PlayerServerClient.h"
#include "Ffmpeg/LiveFFmpegWrapper.h"
#include "Ffmpeg/PlaybackFFmpegWrapper.h"

#include <boost/algorithm/string_regex.hpp>
#include <boost/regex.hpp>
#include <iostream>
#include <chrono>
#include <ctime>
#include <thread>
#include <Util.h>
#include <boost/filesystem.hpp>
#include <regex>
#include <cpr/cpr.h>

// Codec handler abstraction for RTP packetization
#include "CodecHandler/CodecHandler.h"

using namespace std;

WebRTCWrapper::WebRTCWrapper()
    : mainLogger(AppConfig::instance().logger())
{
    rtc::InitLogger(rtc::LogLevel::Warning);
    rtc::SetThreadPoolSize(2);

    PlayerServerClient::init();

    if (mainLogger)
    {
        mainLogger->info("WebRTCWrapper initialized");
    }
}

WebRTCWrapper::~WebRTCWrapper()
{
    // Clean up all WebRTC connections
    std::lock_guard<std::mutex> lock(connectionsMutex);
    connections.clear();

    if (mainLogger)
    {
        mainLogger->info("WebRTCWrapper destroyed");
    }
}

void WebRTCWrapper::setSignalingTransport(SignalingTransport transport)
{
    signalingTransport_ = std::move(transport);
}

std::string WebRTCWrapper::buildJsonMessage(const std::string &type, const std::string &message)
{
    Json::Value msg;
    msg["type"] = type;
    msg["message"] = message;
    Json::StreamWriterBuilder writerBuilder;
    return Json::writeString(writerBuilder, msg);
}

void WebRTCWrapper::logStats()
{
    std::lock_guard<std::mutex> lock(connectionsMutex);
    size_t liveCount = 0;
    {
        std::lock_guard<std::mutex> lock2(liveStreamsMutex);
        liveCount = liveStreams.size();
    }
    std::cout << "[Stats] WebRTC connections: " << connections.size()
              << ", Live FFmpeg streams: " << liveCount << std::endl;
    for (const auto &[id, conn] : connections)
    {
        if (conn->packetsSent == 0)
        {
            std::cout << "  " << id << " | packets: " << conn->packetsSent << std::endl;
        }
    }
    if (mainLogger)
    {
        mainLogger->info("[Stats] WebRTC connections: {}, Live FFmpeg streams: {}", connections.size(), liveCount);
        for (const auto &[id, conn] : connections)
        {
            mainLogger->info("  {} | packets: {}", id, conn->packetsSent);
        }
    }
}

void WebRTCWrapper::setupVideoTrack(std::shared_ptr<rtc::PeerConnection> pc,
                                    std::shared_ptr<WebRTCConnectionInfo> connInfo,
                                    const std::string &url, AVCodecID codecId)
{
    rtc::Description::Video media("video", rtc::Description::Direction::SendOnly);

    auto codecHandler = CodecHandler::create(codecId);
    codecHandler->addCodecToMedia(media);

    media.addSSRC(1, "video-stream");
    auto track = pc->addTrack(media);

    bool isAvccFormat = boost::ends_with(url, ".mp4") || boost::ends_with(url, ".mkv") || boost::ends_with(url, ".mov");

    auto rtpConfig = codecHandler->setMediaHandler(track, false);

    connInfo->videoTrack = track;
    connInfo->rtpConfig = rtpConfig;

    if (mainLogger)
    {
        mainLogger->info("Video track added: codec={}, separator={}, client={}",
                         avcodec_get_name(codecId),
                         isAvccFormat ? "AVCC/Length" : "AnnexB/StartSequence",
                         connInfo->clientId);
    }
}

void WebRTCWrapper::createPeerConnection(const std::string &clientId,
                                         const std::string &url, AVCodecID codecId)
{
    try
    {
        if (mainLogger)
        {
            mainLogger->info("Creating peer connection for client: {}", clientId);
        }

        // WebRTC configuration with STUN servers
        rtc::Configuration config;
        config.iceServers.emplace_back("stun:stun.l.google.com:19302");
        config.iceServers.emplace_back("stun:stun1.l.google.com:19302");

        // Create peer connection
        auto pc = std::make_shared<rtc::PeerConnection>(config);

        auto connInfo = std::make_shared<WebRTCConnectionInfo>();
        connInfo->clientId = clientId;
        connInfo->peerConnection = pc;
        connInfo->query = signalingTransport_.getQuery(clientId);
        connInfo->url = url;

        // Set up state change callbacks
        pc->onStateChange([this, clientId](rtc::PeerConnection::State state)
                          {
            if (mainLogger) {
                mainLogger->info("Client {} peer connection state: {}", clientId, (int)state);
            }

            if (state == rtc::PeerConnection::State::Connected) {
                bool fullyConnected = false;
                {
                    std::lock_guard<std::mutex> lock(connectionsMutex);
                    auto it = connections.find(clientId);
                    if (it != connections.end()) {
                        it->second->isConnected = true;
                        fullyConnected = it->second->isConnected && it->second->iceConnected && it->second->gatheringComplete;
                    }
                }
                if (fullyConnected) {
                    onFullyConnected(clientId);
                }
            }
            else if (state == rtc::PeerConnection::State::Disconnected
                ) {
                if (mainLogger) {
                    mainLogger->info("Client {} disconnected, cleaning up", clientId);
                }
                
            } 
        else if(state == rtc::PeerConnection::State::Closed||state == rtc::PeerConnection::State::Failed)
    {
removeConnection(clientId);
    } });

        pc->onIceStateChange([this, clientId](rtc::PeerConnection::IceState state)
                             {
            if (mainLogger) {
                mainLogger->info("Client {} ICE connection state: {}", clientId, (int)state);
            }

            if (state == rtc::PeerConnection::IceState::Completed) {
                bool fullyConnected = false;
                {
                    std::lock_guard<std::mutex> lock(connectionsMutex);
                    auto it = connections.find(clientId);
                    if (it != connections.end()) {
                        it->second->iceConnected = true;
                        fullyConnected = it->second->isConnected && it->second->iceConnected && it->second->gatheringComplete;
                    }
                }
                if (fullyConnected) {
                    onFullyConnected(clientId);
                }
            } });

        pc->onGatheringStateChange([this, clientId](rtc::PeerConnection::GatheringState state)
                                   {
            if (mainLogger) {
                mainLogger->debug("Client {} ICE gathering state: {}", clientId, (int)state);
            }

            if (state == rtc::PeerConnection::GatheringState::Complete) {
                bool fullyConnected = false;
                {
                    std::lock_guard<std::mutex> lock(connectionsMutex);
                    auto it = connections.find(clientId);
                    if (it != connections.end()) {
                        it->second->gatheringComplete = true;
                        fullyConnected = it->second->isConnected && it->second->iceConnected && it->second->gatheringComplete;
                    }
                }
                if (fullyConnected) {
                    onFullyConnected(clientId);
                }
            } });

        // Trickle ICE: Send candidates as they are discovered
        pc->onLocalCandidate([this, clientId](rtc::Candidate candidate)
                             {
            if (mainLogger) {
                mainLogger->debug("Sending ICE candidate to client {}", clientId);
            }

            Json::Value msg;
            msg["type"] = "candidate";
            msg["candidate"] = candidate.candidate();
            msg["sdpMid"] = candidate.mid();

            Json::StreamWriterBuilder writerBuilder;
            signalingTransport_.sendMessage(clientId, Json::writeString(writerBuilder, msg)); });

        // Set up onLocalDescription BEFORE addTrack/createDataChannel
        pc->onLocalDescription([this, clientId](rtc::Description description)
                               {
            if (mainLogger) {
                mainLogger->info("Sending SDP offer to client: {}", clientId);
            }

            Json::Value offerMsg;
            offerMsg["type"] = "offer";
            offerMsg["sdp"] = std::string(description);

            Json::StreamWriterBuilder writerBuilder;
            signalingTransport_.sendMessage(clientId, Json::writeString(writerBuilder, offerMsg)); });

        // Store connection
        {
            std::lock_guard<std::mutex> lock(connectionsMutex);
            connections[clientId] = connInfo;
        }
        if (!signalingTransport_.isConnected(clientId))
        {
            connInfo->peerConnection->close();
            return;
        }
        // Add video track with codec-specific packetizer
        setupVideoTrack(pc, connInfo, url, codecId);

        // Create data channel (server creates it as offerer)
        auto dc = pc->createDataChannel("control");

        dc->onOpen([this, clientId]()
                   {
            if (mainLogger) {
                mainLogger->info("Data channel opened for client {}", clientId);
            } });

        dc->onClosed([this, clientId]()
                     {
            if (mainLogger) {
                mainLogger->info("Data channel closed for client {}", clientId);
            } });

        dc->onMessage([this, clientId](std::variant<rtc::binary, std::string> data)
                      { handleDataChannelMessage(clientId, data); });

        connInfo->dataChannel = dc;

        if (!signalingTransport_.isConnected(clientId))
        {
            connInfo->peerConnection->close();
            std::lock_guard<std::mutex> lock(connectionsMutex);
            connections.erase(clientId);
        }
        if (mainLogger)
        {
            mainLogger->info("Peer connection created and offer sent for client: {}", clientId);
        }
    }
    catch (const std::exception &ex)
    {
        if (mainLogger)
        {
            mainLogger->error("Error creating peer connection for client {}: {}", clientId, ex.what());
        }
        throw;
    }
}

void WebRTCWrapper::handleRequest(const std::string &clientId)
{
    try
    {
        if (mainLogger)
        {
            mainLogger->info("Handling request from client: {}", clientId);
        }

        // Fetch query from signaling transport (stored in websocket dictionary)
        Json::Value query = signalingTransport_.getQuery(clientId);

        signalingTransport_.sendMessage(clientId, buildJsonMessage("status", "Connecting Player Server"));

        // 1. Resolve URL first (need it to pick correct RTP packetizer separator)
        std::string url = PlayerServerClient::resolveStreamUrl(query);

        if (url.empty() || boost::starts_with(url, "Player_Server_Not_Connected") ||
            boost::starts_with(url, "URL_Server_Not_Connected"))
        {
            if (mainLogger)
            {
                mainLogger->error("Failed to get URL for client {}: {}", clientId, url);
            }

            signalingTransport_.sendMessage(clientId, buildJsonMessage("error", url.empty() ? "Failed to resolve stream URL" : url));
            return;
        }

        // 3. Probe actual codec from stream before creating peer connection
        signalingTransport_.sendMessage(clientId, buildJsonMessage("status", "fetching codec Information"));
        AVCodecID codecId = FFmpegWrapper::probeCodec(url);
        if (mainLogger)
        {
            mainLogger->info("Probed codec for client {}: {} ({})", clientId,
                             avcodec_get_name(codecId), (int)codecId);
        }

        // 4. Reject unsupported codecs
        if (codecId != AV_CODEC_ID_H264 && codecId != AV_CODEC_ID_H265)
        {
            if (mainLogger)
            {
                mainLogger->error("Unsupported codec for client {}: {}", clientId, avcodec_get_name(codecId));
            }

            signalingTransport_.sendMessage(clientId, buildJsonMessage("error", std::string("Unsupported codec: ") + avcodec_get_name(codecId)));
            return;
        }

        // 5. Create peer connection with correct packetizer based on actual codec
        createPeerConnection(clientId, url, codecId);
    }
    catch (const std::exception &ex)
    {
        if (mainLogger)
        {
            mainLogger->error("Error handling request from client {}: {}", clientId, ex.what());
        }

        signalingTransport_.sendMessage(clientId, buildJsonMessage("error", ex.what()));
    }
}

void WebRTCWrapper::handleAnswer(const std::string &clientId, const std::string &sdp)
{
    try
    {
        if (mainLogger)
        {
            mainLogger->info("Handling answer from client: {}", clientId);
        }

        std::shared_ptr<WebRTCConnectionInfo> connInfo;
        {
            std::lock_guard<std::mutex> lock(connectionsMutex);
            auto it = connections.find(clientId);
            if (it == connections.end())
            {
                if (mainLogger)
                {
                    mainLogger->warn("Answer for unknown client: {}", clientId);
                }
                return;
            }
            connInfo = it->second;
        }

        auto pc = connInfo->peerConnection;
        rtc::Description answer(sdp, "answer");
        pc->setRemoteDescription(answer);

        if (mainLogger)
        {
            mainLogger->info("Remote description (answer) set for client: {}", clientId);
        }
    }
    catch (const std::exception &ex)
    {
        if (mainLogger)
        {
            mainLogger->error("Error handling answer from client {}: {}", clientId, ex.what());
        }
    }
}

void WebRTCWrapper::handleIceCandidate(const std::string &clientId, const std::string &candidate,
                                       const std::string &sdpMid, int sdpMLineIndex)
{
    try
    {
        std::lock_guard<std::mutex> lock(connectionsMutex);

        auto it = connections.find(clientId);
        if (it == connections.end())
        {
            if (mainLogger)
            {
                mainLogger->warn("ICE candidate for unknown client: {}", clientId);
            }
            return;
        }

        auto pc = it->second->peerConnection;
        pc->addRemoteCandidate(rtc::Candidate(candidate, sdpMid));

        if (mainLogger)
        {
            mainLogger->debug("Added ICE candidate for client: {}", clientId);
        }
    }
    catch (const std::exception &ex)
    {
        if (mainLogger)
        {
            mainLogger->error("Error adding ICE candidate for client {}: {}", clientId, ex.what());
        }
    }
}

void WebRTCWrapper::removeConnection(const std::string &clientId)
{
    try
    {
        // Remove from connections
        std::shared_ptr<WebRTCConnectionInfo> connInfo;
        {
            std::lock_guard<std::mutex> lock(connectionsMutex);
            auto it = connections.find(clientId);
            if (it != connections.end())
            {
                connInfo = it->second;
                connections.erase(it);
            }
        }

        
        // Remove from FFmpeg wrapper
        if (connInfo && connInfo->ffmpegWrapper)
        {
            //todovineet here race condition should occur
            bool canStop = connInfo->ffmpegWrapper->removeConnection(connInfo->peerConnection);
            if (canStop)
            {
                connInfo->ffmpegWrapper->stopThread();

                // Remove from liveStreams if it's a live stream
                if (!connInfo->ffmpegKey.empty())
                {
                    std::lock_guard<std::mutex> lock(liveStreamsMutex);
                    liveStreams.erase(connInfo->ffmpegKey);
                }

                if (mainLogger)
                {
                    mainLogger->info("Stopped FFmpeg instance for client: {}", clientId);
                }
            }
        }
        else
        {
            std::cout << "No FFmpeg wrapper to remove connection from for client: " << clientId << std::endl;
        }

        if (mainLogger)
        {
            mainLogger->info("Removed connection for client: {}", clientId);
        }
    }
    catch (const std::exception &ex)
    {
        if (mainLogger)
        {
            mainLogger->error("Error removing connection for client {}: {}", clientId, ex.what());
        }
    }
}

void WebRTCWrapper::handleDataChannelMessage(const std::string &clientId, std::variant<rtc::binary, std::string> data)
{
    try
    {
        std::string message;

        // Convert to string
        if (std::holds_alternative<std::string>(data))
        {
            message = std::get<std::string>(data);
        }
        else
        {
            auto binary = std::get<rtc::binary>(data);
            message = std::string(reinterpret_cast<const char *>(binary.data()), binary.size());
        }

        if (mainLogger)
        {
            mainLogger->info("Client {} sent message: {}", clientId, message);
        }

        // Find FFmpeg instance for this client
        std::shared_ptr<FFmpegWrapper> ffmpeg;
        {
            std::lock_guard<std::mutex> lock(connectionsMutex);
            auto it = connections.find(clientId);
            if (it == connections.end() || !it->second->ffmpegWrapper)
            {
                if (mainLogger)
                {
                    mainLogger->warn("No FFmpeg instance for client {}", clientId);
                }
                return;
            }
            ffmpeg = it->second->ffmpegWrapper;
        }

        // Non-FFmpeg messages handled here; everything else passed as JSON to FFmpeg
        if (message == "Version")
        {
            std::lock_guard<std::mutex> lock(connectionsMutex);
            auto it = connections.find(clientId);
            if (it != connections.end())
            {
                SendStringData(it->second->peerConnection, "--version " + i2v::VERSION);
            }
        }
        else if (message == "Server Status")
        {
            std::lock_guard<std::mutex> lock(connectionsMutex);
            std::string servData = "liveStreams.count: " + std::to_string(liveStreams.size()) + "\n";
            {
                std::lock_guard<std::mutex> lock2(liveStreamsMutex);
                for (const auto &entry : liveStreams)
                {
                    servData += entry.first + "\n";
                }
            }
            servData += "\nconnections: " + std::to_string(connections.size()) + "\n";

            auto it = connections.find(clientId);
            if (it != connections.end())
            {
                SendStringData(it->second->peerConnection, "--servStatus " + servData);
            }
        }
        else
        {
            // All other messages are JSON commands — pass directly to FFmpeg
            ffmpeg->handleClientCommand(message);
        }
    }
    catch (const std::exception &ex)
    {
        if (mainLogger)
        {
            mainLogger->error("Error handling data channel message from client {}: {}", clientId, ex.what());
        }
    }
}

void WebRTCWrapper::SendData(rtcConnHdl &conn, std::vector<uint8_t> &data, int64_t timestamp)
{
    try
    {
        std::lock_guard<std::mutex> lock(connectionsMutex);

        // Find connection by PeerConnection pointer
        auto it = std::find_if(connections.begin(), connections.end(),
                               [&conn](const auto &pair)
                               {
                                   return pair.second->peerConnection.get() == conn.get();
                               });

        if (it == connections.end())
        {
            return;
        }

        if (!it->second->videoTrack)
        {
            static bool loggedOnce = false;
            if (!loggedOnce)
            {
                std::cout << "SendData: videoTrack is NULL - onTrack callback never fired" << std::endl;
                loggedOnce = true;
            }
            return;
        }

        if (!it->second->isConnected)
        {
            static bool loggedOnce2 = false;
            if (!loggedOnce2)
            {
                std::cout << "SendData: isConnected=false, peer connection not connected yet" << std::endl;
                loggedOnce2 = true;
            }
            return;
        }

        auto track = it->second->videoTrack;

        const uint8_t *frameStart;
        size_t frameSize;

        if (data.size() > 0)
        {
            // Playback mode — strip 8-byte position prefix
            frameStart = data.data();
            frameSize = data.size();
        }
        else
        {
            return; // Too small, skip
        }

        if (frameSize > 0)
        {
            // Generate RTP timestamp from wall clock (90kHz RTP clock)
            auto now = std::chrono::steady_clock::now();
            auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
                                  now - it->second->createdAt)
                                  .count();
            auto rtpTimestamp = static_cast<uint32_t>((elapsed_us * 90) / 1000);

            if (it->second->rtpConfig)
            {
                it->second->rtpConfig->timestamp = rtpTimestamp;
            }

            rtc::binary rtpPayload(reinterpret_cast<const std::byte *>(frameStart),
                                   reinterpret_cast<const std::byte *>(frameStart + frameSize));
            track->send(rtpPayload);
            it->second->packetsSent++;
        }
    }
    catch (const std::exception &ex)
    {
        if (mainLogger)
        {
            mainLogger->error("Error sending video data: {}", ex.what());
        }
    }
}

void WebRTCWrapper::SendStringData(rtcConnHdl &conn, std::string sdata)
{
    try
    {
        std::lock_guard<std::mutex> lock(connectionsMutex);

        // Find connection by PeerConnection pointer
        auto it = std::find_if(connections.begin(), connections.end(),
                               [&conn](const auto &pair)
                               {
                                   return pair.second->peerConnection.get() == conn.get();
                               });

        if (it == connections.end() || !it->second->dataChannel)
        {
            return;
        }

        auto dc = it->second->dataChannel;
        std::string clientId = it->second->clientId;

        // Send string message via data channel
        if (dc->isOpen())
        {
            dc->send(sdata);
        }
    }
    catch (const std::exception &ex)
    {
        if (mainLogger)
        {
            mainLogger->error("Error sending string data: {}", ex.what());
        }
    }
}

void WebRTCWrapper::closePeerConnectionIfNotConnected(const std::string &clientId)
{
    std::lock_guard<std::mutex> lock(connectionsMutex);
    auto it = connections.find(clientId);
    if (it != connections.end() && it->second->peerConnection)
    {
        if (it->second->peerConnection->state() != rtc::PeerConnection::State::Connected && it->second->peerConnection->state() != rtc::PeerConnection::State::Disconnected)
        {
            it->second->peerConnection->close();
        }
    }
}

void WebRTCWrapper::onFullyConnected(const std::string &clientId)
{
    std::thread([this, clientId]()
                {
                    signalingTransport_.sendMessage(clientId, buildJsonMessage("status", "starting video"));
        processRequest(clientId);
        signalingTransport_.closeConnection(clientId); })
        .detach();
}

void WebRTCWrapper::processRequest(const std::string &clientId)
{
    try
    {
        // Look up connection info first (has query and url stored from createPeerConnection)
        std::shared_ptr<WebRTCConnectionInfo> connInfo;
        rtcConnHdl peerConn;
        {
            std::lock_guard<std::mutex> lock(connectionsMutex);
            auto connIt = connections.find(clientId);
            if (connIt == connections.end() || !connIt->second->peerConnection)
            {
                return;
            }
            connInfo = connIt->second;
            peerConn = connInfo->peerConnection;
        }

        const Json::Value &query = connInfo->query;
        const std::string &url = connInfo->url;

        std::string cameraId = query.get("cameraId", "").asString();
        std::string mode = query.get("mode", "Live").asString();
        std::string connectionmode = query.get("connectionMode", "tcp").asString();
        float playbackSpeed = query.get("playbackSpeed", 1.0).asFloat();
        playbackSpeed = std::clamp(playbackSpeed, 0.5f, 5.0f);
        int start_time_ofplaybackfile = query.get("startTime", 0).asInt();
        int seekTime_ofFile = 0;
        float duration_in_Minutes = 0;

        auto bindSendData = std::bind(&WebRTCWrapper::SendData, this,
                                      std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
        auto bindSendStringData = std::bind(&WebRTCWrapper::SendStringData, this,
                                            std::placeholders::_1, std::placeholders::_2);

        std::function<void(webConnHdl &, std::vector<uint8_t> &, int64_t)> sendDataFunc = bindSendData;
        std::function<void(webConnHdl &, std::string)> sendStringDataFunc = bindSendStringData;

        if (mode == "Live")
        {
            std::lock_guard<std::mutex> lock(liveStreamsMutex);

            auto it = liveStreams.find(url);
            if (it == liveStreams.end())
            {
                auto ffmpeg = std::make_shared<LiveFFmpegWrapper>(cameraId, url,
                                                                  sendDataFunc, sendStringDataFunc,
                                                                  connectionmode);
                liveStreams[url] = ffmpeg;
                ffmpeg->startThread();
            }

            auto ffmpeg = liveStreams[url];
            if (ffmpeg != nullptr)
            {
                ffmpeg->addConnToList(peerConn);
            }

            connInfo->ffmpegWrapper = ffmpeg;
            connInfo->ffmpegKey = url;
        }
        else
        { // PlayBack mode
            int duration_in_Seconds = static_cast<int>(round(duration_in_Minutes * 60));

            auto ffmpeg = std::make_shared<PlaybackFFmpegWrapper>(cameraId, url, seekTime_ofFile,
                                                                  sendDataFunc, sendStringDataFunc,
                                                                  connectionmode, playbackSpeed, start_time_ofplaybackfile,
                                                                  duration_in_Seconds);

            ffmpeg->startThread();
            ffmpeg->addConnToList(peerConn);

            connInfo->ffmpegWrapper = ffmpeg;
        }

        // Check if connection was removed while we were setting up FFmpeg
        {
            std::lock_guard<std::mutex> lock(connectionsMutex);
            if (connections.find(clientId) == connections.end())
            {
                bool canStop = connInfo->ffmpegWrapper->removeConnection(peerConn);
                if (canStop)
                {
                    connInfo->ffmpegWrapper->stopThread();

                    // Remove from liveStreams if it's a live stream
                    if (!connInfo->ffmpegKey.empty())
                    {
                        std::lock_guard<std::mutex> lock(liveStreamsMutex);
                        liveStreams.erase(connInfo->ffmpegKey);
                    }

                    if (mainLogger)
                    {
                        mainLogger->info("Stopped FFmpeg instance for client: {}", clientId);
                    }
                }
                return;
            }
        }

        if (mainLogger)
        {
            mainLogger->info("Created FFmpeg wrapper for client {} in {} mode", clientId, mode);
        }
    }
    catch (const std::exception &ex)
    {
        if (mainLogger)
        {
            mainLogger->error("Error processing request for client {}: {}", clientId, ex.what());
        }
    }
}
