#include "WebRTCWrapper.h"
#include "AppConfig.h"
#include "common.h"
#include "json/json.h"
#include <json/value.h>
#include "Options.h"
#include "RestServiceClient.h"
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

    RestServiceClient::init();

    if (mainLogger)
    {
        mainLogger->info("[WebRTC] Wrapper initialized");
    }
}

WebRTCWrapper::~WebRTCWrapper()
{
    // Clean up all WebRTC connections
    // SyncHandler's own destructor releases any pending sync groups
    std::lock_guard<std::mutex> lock(connectionsMutex);
    connections.clear();

    if (mainLogger)
    {
        mainLogger->info("[WebRTC] Wrapper destroyed");
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
    std::cout << "[Stats] WebRTC: connections=" << connections.size()
              << ", liveFFmpegStreams=" << liveCount << std::endl;
    for (const auto &[id, conn] : connections)
    {
        if (conn->packetsSent == 0)
        {
            std::cout << "[Stats] WebRTC: client=" << id << ", packetsSent=" << conn->packetsSent << std::endl;
        }
    }

    // Log sync groups
    {
        std::lock_guard<std::mutex> sgLock(syncHandler_.groupsMutex_);
        std::cout << "[Stats] Sync: groups=" << syncHandler_.groups_.size() << std::endl;
        for (const auto &[groupId, info] : syncHandler_.groups_)
        {
            std::lock_guard<std::mutex> infoLock(info->mutex);
            std::cout << "[Stats] Sync: group=" << groupId
                      << ", clients=" << info->clientIds.size()
                      << ", ffmpegRefs=" << info->ffmpegRefs.size()
                      << ", released=" << (info->released ? "yes" : "no") << std::endl;
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
        mainLogger->info("[WebRTC] Video track added: codec={}, separator={}, client={}",
                         avcodec_get_name(codecId),
                         isAvccFormat ? "AVCC/Length" : "AnnexB/StartSequence",
                         connInfo->clientId);
    }
}

void WebRTCWrapper::createPeerConnection(const std::string &clientId,
                                         const std::string &url, AVCodecID codecId,
                                         const Json::Value &streamInfo)
{
    try
    {
        if (mainLogger)
        {
            mainLogger->info("[WebRTC] Creating peer connection: client={}", clientId);
        }

        // WebRTC configuration with STUN servers
        rtc::Configuration config;
        config.iceServers.emplace_back("stun:stun.l.google.com:19302");
        config.iceServers.emplace_back("stun:stun1.l.google.com:19302");

        auto &appCfg = AppConfig::instance();
        if (appCfg.getBool("turnServer.enabled"))
        {
            const std::string turnUrl = appCfg.get("turnServer.url");
            if (!turnUrl.empty())
            {
                rtc::IceServer turn(turnUrl);
                turn.username = appCfg.get("turnServer.username");
                turn.password = appCfg.get("turnServer.password");
                config.iceServers.push_back(std::move(turn));
                if (mainLogger)
                {
                    mainLogger->info("[WebRTC] TURN enabled: url={}", turnUrl);
                }
            }
            else if (mainLogger)
            {
                mainLogger->warn("[WebRTC] TURN is enabled but turnServer.url is empty, skipping TURN");
            }
        }

        // Create peer connection
        auto pc = std::make_shared<rtc::PeerConnection>(config);

        auto connInfo = std::make_shared<WebRTCConnectionInfo>();
        connInfo->clientId = clientId;
        connInfo->peerConnection = pc;
        connInfo->query = signalingTransport_.getQuery(clientId);
        connInfo->streamInfo = streamInfo;

        // Set up state change callbacks
        pc->onStateChange([this, clientId](rtc::PeerConnection::State state)
                          {
            if (mainLogger) {
                mainLogger->info("[WebRTC] Peer connection state changed: client={}, state={}", clientId, (int)state);
            }

            if (state == rtc::PeerConnection::State::Connected) {
                // PeerConnection::State::Connected means ICE is connected AND DTLS is
                // established — that is the complete precondition for sending SRTP media.
                // We deliberately do NOT wait for ICE Completed / gathering Complete:
                // those are stricter, happen later, and over a TURN relay may never fire,
                // which would leave a working connection with the stream never started.
                bool startNow = false;
                {
                    std::lock_guard<std::mutex> lock(connectionsMutex);
                    auto it = connections.find(clientId);
                    if (it != connections.end()) {
                        it->second->isConnected = true;
                        // Connected can fire again after a Disconnected->Connected recovery;
                        // only start the media pipeline the first time.
                        if (!it->second->streamStarted) {
                            it->second->streamStarted = true;
                            startNow = true;
                        }
                    }
                }
                if (startNow) {
                    onFullyConnected(clientId);
                }
            }
            else if (state == rtc::PeerConnection::State::Disconnected
                ) {
                if (mainLogger) {
                    mainLogger->info("[WebRTC] Client disconnected, cleaning up: client={}", clientId);
                }
                
            } 
            else if(state == rtc::PeerConnection::State::Closed||state == rtc::PeerConnection::State::Failed)
            {
                removeConnection(clientId);
            } });

        pc->onIceStateChange([this, clientId](rtc::PeerConnection::IceState state)
                             {
            // Logging only. Streaming starts on PeerConnection::State::Connected;
            // ICE Completed is not required (and may never arrive over a relay).
            if (mainLogger) {
                mainLogger->info("[WebRTC] ICE connection state changed: client={}, state={}", clientId, (int)state);
            } });

        pc->onGatheringStateChange([this, clientId](rtc::PeerConnection::GatheringState state)
                                   {
            // Logging only. Gathering completion is a local candidate-discovery signal and
            // is not a prerequisite for sending media (we trickle ICE), so it does not gate
            // the stream start.
            if (mainLogger) {
                mainLogger->debug("[WebRTC] ICE gathering state changed: client={}, state={}", clientId, (int)state);
            } });

        // Trickle ICE: Send candidates as they are discovered
        pc->onLocalCandidate([this, clientId](rtc::Candidate candidate)
                             {
            if (mainLogger) {
                mainLogger->debug("[WebRTC] Sending ICE candidate: client={}", clientId);
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
                mainLogger->info("[WebRTC] Sending SDP offer: client={}", clientId);
            }

            Json::Value offerMsg;
            offerMsg["type"] = "offer";
            offerMsg["sdp"] = std::string(description);

            // Advertise ICE servers to the client so it uses the same STUN/TURN set
            Json::Value iceServers(Json::arrayValue);
            Json::Value stun1; stun1["urls"] = "stun:stun.l.google.com:19302";
            Json::Value stun2; stun2["urls"] = "stun:stun1.l.google.com:19302";
            iceServers.append(stun1);
            iceServers.append(stun2);

            auto &appCfg = AppConfig::instance();
            if (appCfg.getBool("turnServer.enabled")) {
                const std::string turnUrl = appCfg.get("turnServer.url");
                if (!turnUrl.empty()) {
                    Json::Value turn;
                    turn["urls"] = turnUrl;
                    turn["username"] = appCfg.get("turnServer.username");
                    turn["credential"] = appCfg.get("turnServer.password");
                    iceServers.append(turn);
                }
            }
            offerMsg["iceServers"] = iceServers;

            Json::StreamWriterBuilder writerBuilder;
            signalingTransport_.sendMessage(clientId, Json::writeString(writerBuilder, offerMsg)); });

        // // Store connection
        // {
        //     std::lock_guard<std::mutex> lock(connectionsMutex);
        //     auto it = connections.find(clientId);
        //     if (it == connections.end())
        //     {
        //         connections[clientId] = connInfo;
        //     }
        //     else{
        //         std::cout<< "Warning: clientId " << clientId << " already exists in connections map when creating peer connection. Overwriting with new connection info." << std::endl;
        //     }
            
        // }

        std::shared_ptr<WebRTCConnectionInfo> evicted;
        {
            std::lock_guard<std::mutex> lock(connectionsMutex);
            auto it = connections.find(clientId);
            if (it != connections.end()) evicted = std::move(it->second);
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
                mainLogger->info("[WebRTC] Data channel opened: client={}", clientId);
            } });

        dc->onClosed([this, clientId]()
                     {
            if (mainLogger) {
                mainLogger->info("[WebRTC] Data channel closed: client={}", clientId);
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
        else
        {
            // Register with sync handler if this connection belongs to a sync group
            std::string syncGroupId = connInfo->query.get("syncGroup", "").asString();
            if (!syncGroupId.empty())
            {
                syncHandler_.incrementExpectedCount(syncGroupId, clientId);
                connInfo->syncGroupId = syncGroupId;
            }
        }
        if (mainLogger)
        {
            mainLogger->info("[WebRTC] Peer connection created and offer sent: client={}", clientId);
        }
    }
    catch (const std::exception &ex)
    {
        if (mainLogger)
        {
            mainLogger->error("[WebRTC] Failed to create peer connection: client={}, error={}", clientId, ex.what());
        }
        throw;
    }
}

void WebRTCWrapper::startStream(const std::string &clientId, const std::string &url,
                                AVCodecID codecId, const Json::Value &streamInfo)
{
    try
    {
        if (mainLogger)
        {
            mainLogger->info("[WebRTC] Starting stream: client={}, url={}, codec={}",
                             clientId, url, avcodec_get_name(codecId));
        }

        createPeerConnection(clientId, url, codecId, streamInfo);
    }
    catch (const std::exception &ex)
    {
        if (mainLogger)
        {
            mainLogger->error("[WebRTC] Failed to start stream: client={}, error={}", clientId, ex.what());
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
            mainLogger->info("[WebRTC] Handling SDP answer: client={}", clientId);
        }

        std::shared_ptr<WebRTCConnectionInfo> connInfo;
        {
            std::lock_guard<std::mutex> lock(connectionsMutex);
            auto it = connections.find(clientId);
            if (it == connections.end())
            {
                if (mainLogger)
                {
                    mainLogger->warn("[WebRTC] SDP answer received for unknown client: client={}", clientId);
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
            mainLogger->info("[WebRTC] Remote description (answer) set: client={}", clientId);
        }
    }
    catch (const std::exception &ex)
    {
        if (mainLogger)
        {
            mainLogger->error("[WebRTC] Failed to handle SDP answer: client={}, error={}", clientId, ex.what());
        }
    }
}

void WebRTCWrapper::handleIceCandidate(const std::string &clientId, const std::string &candidate,
                                       const std::string &sdpMid, int sdpMLineIndex)
{
    try
    {
        std::shared_ptr<rtc::PeerConnection> pc;
        {
            std::lock_guard<std::mutex> lock(connectionsMutex);
            auto it = connections.find(clientId);
            if (it == connections.end())
            {
                if (mainLogger)
                {
                    mainLogger->warn("[WebRTC] ICE candidate received for unknown client: client={}", clientId);
                }
                return;
            }
            pc = it->second->peerConnection;
        }

        pc->addRemoteCandidate(rtc::Candidate(candidate, sdpMid));

        if (mainLogger)
        {
            mainLogger->debug("[WebRTC] ICE candidate added: client={}", clientId);
        }
    }
    catch (const std::exception &ex)
    {
        if (mainLogger)
        {
            mainLogger->error("[WebRTC] Failed to add ICE candidate: client={}, error={}", clientId, ex.what());
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

        // Remove this member from sync group (group stays alive for remaining members)
        if (connInfo && !connInfo->syncGroupId.empty())
        {
            syncHandler_.removeMember(connInfo->syncGroupId, connInfo->clientId, connInfo->ffmpegWrapper.get());
        }

        // Remove from FFmpeg wrapper
        if (connInfo && connInfo->ffmpegWrapper)
        {
            // todovineet here race condition should occur
            bool canStop = connInfo->ffmpegWrapper->removeConnection(connInfo->clientId);
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
                    mainLogger->info("[WebRTC] FFmpeg instance stopped: client={}", clientId);
                }
            }
        }
        else
        {
            if (mainLogger)
            {
                mainLogger->error("[WebRTC] No FFmpeg wrapper found to detach connection from: client={}", clientId);
            }
        }

        if (mainLogger)
        {
            mainLogger->info("[WebRTC] Connection removed: client={}", clientId);
        }
    }
    catch (const std::exception &ex)
    {
        if (mainLogger)
        {
            mainLogger->error("[WebRTC] Failed to remove connection: client={}, error={}", clientId, ex.what());
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
            mainLogger->info("[WebRTC] Data channel message received: client={}, message={}", clientId, message);
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
                    mainLogger->warn("[WebRTC] No FFmpeg instance found: client={}", clientId);
                }
                return;
            }
            ffmpeg = it->second->ffmpegWrapper;
        }

        // Non-FFmpeg messages handled here; everything else passed as JSON to FFmpeg
        if (message == "Version")
        {
            webConnHdl id = clientId;
            SendStringData(id, "--version " + nmetics::VERSION);
        }
        else if (message == "Server Status")
        {
            std::string servData;
            {
                std::lock_guard<std::mutex> lock2(liveStreamsMutex);
                servData = "liveStreams.count: " + std::to_string(liveStreams.size()) + "\n";
                for (const auto &entry : liveStreams)
                {
                    servData += entry.first + "\n";
                }
            }
            {
                std::lock_guard<std::mutex> lock(connectionsMutex);
                servData += "\nconnections: " + std::to_string(connections.size()) + "\n";
            }
            webConnHdl id = clientId;
            SendStringData(id, "--servStatus " + servData);
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
            mainLogger->error("[WebRTC] Failed to handle data channel message: client={}, error={}", clientId, ex.what());
        }
    }
}

void WebRTCWrapper::SendData(webConnHdl &clientId, std::vector<uint8_t> &data)
{
    try
    {
        if (data.empty())
        {
            return;
        }

        std::shared_ptr<WebRTCConnectionInfo> connInfo;
        {
            std::lock_guard<std::mutex> lock(connectionsMutex);
            auto it = connections.find(clientId);
            if (it == connections.end())
            {
                return;
            }

            if (!it->second->videoTrack)
            {
                static bool loggedOnce = false;
                if (!loggedOnce)
                {
                    std::cout << "[WebRTC] Cannot send video: video track is null (onTrack callback never fired)" << std::endl;
                    loggedOnce = true;
                }
                return;
            }

            if (!it->second->isConnected)
            {
                static bool loggedOnce2 = false;
                if (!loggedOnce2)
                {
                    std::cout << "[WebRTC] Cannot send video: peer connection is not connected yet" << std::endl;
                    loggedOnce2 = true;
                }
                return;
            }

            connInfo = it->second;
        }

        const uint8_t *frameStart = data.data();
        size_t frameSize = data.size();

        auto now = std::chrono::steady_clock::now();
        auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(
                              now - connInfo->createdAt)
                              .count();
        auto rtpTimestamp = static_cast<uint32_t>((elapsed_us * 90) / 1000);

        if (connInfo->rtpConfig)
        {
            connInfo->rtpConfig->timestamp = rtpTimestamp;
        }

        if (connInfo->dataChannel && connInfo->dataChannel->isOpen())
        {
            Json::Value meta;
            meta["type"] = "frameMeta";
            meta["rtpTs"] = rtpTimestamp;
            meta["seq"] = (Json::UInt64)connInfo->packetsSent;
            meta["random"] = std::rand() % 100000;
            meta["serverTimeUs"] = (Json::Int64)elapsed_us;
            Json::StreamWriterBuilder w;
            connInfo->dataChannel->send(Json::writeString(w, meta));
        }

        rtc::binary rtpPayload(reinterpret_cast<const std::byte *>(frameStart),
                               reinterpret_cast<const std::byte *>(frameStart + frameSize));
        connInfo->videoTrack->send(rtpPayload);
        connInfo->packetsSent++;
    }
    catch (const std::exception &ex)
    {
        if (mainLogger)
        {
            mainLogger->error("[WebRTC] Failed to send video data: {}", ex.what());
        }
    }
}

void WebRTCWrapper::SendStringData(webConnHdl &clientId, std::string sdata)
{
    try
    {
        std::shared_ptr<rtc::DataChannel> dc;
        {
            std::lock_guard<std::mutex> lock(connectionsMutex);
            auto it = connections.find(clientId);
            if (it == connections.end() || !it->second->dataChannel)
            {
                return;
            }
            dc = it->second->dataChannel;
        }
        if (dc->isOpen())
        {
            dc->send(sdata);
        }
    }
    catch (const std::exception &ex)
    {
        if (mainLogger)
        {
            mainLogger->error("[WebRTC] Failed to send string data: {}", ex.what());
        }
    }
}

void WebRTCWrapper::closePeerConnectionIfNotConnected(const std::string &clientId)
{
    std::shared_ptr<rtc::PeerConnection> pc;
    std::string syncGroupId;
    {
        std::lock_guard<std::mutex> lock(connectionsMutex);
        auto it = connections.find(clientId);
        if (it != connections.end() && it->second->peerConnection)
        {
            if (it->second->peerConnection->state() != rtc::PeerConnection::State::Connected && it->second->peerConnection->state() != rtc::PeerConnection::State::Disconnected)
            {
                pc = it->second->peerConnection;
                syncGroupId = it->second->syncGroupId;
            }
        }
    }
    // Remove clientId from sync group before closing — FFmpeg doesn't exist yet at this point
    if (!syncGroupId.empty())
    {
        syncHandler_.removeClient(syncGroupId, clientId);
    }
    // close() outside the lock — its synchronous callbacks can safely lock connectionsMutex
    if (pc)
    {
        pc->close();
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
        std::shared_ptr<WebRTCConnectionInfo> connInfo;
        {
            std::lock_guard<std::mutex> lock(connectionsMutex);
            auto connIt = connections.find(clientId);
            if (connIt == connections.end() || !connIt->second->peerConnection)
            {
                return;
            }
            connInfo = connIt->second;
        }

        const Json::Value &query = connInfo->query;
        std::string url = connInfo->streamInfo.get("url", "").asString();
        std::string mode = query.get("mode", "Live").asString();

        Json::Value streamInfo = connInfo->streamInfo;
        streamInfo["cameraId"] = query.get("cameraId", "").asString();
        streamInfo["connectionMode"] = query.get("connectionMode", "tcp").asString();

        auto bindSendData = std::bind(&WebRTCWrapper::SendData, this,
                                      std::placeholders::_1, std::placeholders::_2);
        auto bindSendStringData = std::bind(&WebRTCWrapper::SendStringData, this,
                                            std::placeholders::_1, std::placeholders::_2);

        std::function<void(webConnHdl &, std::vector<uint8_t> &)> sendDataFunc = bindSendData;
        std::function<void(webConnHdl &, std::string)> sendStringDataFunc = bindSendStringData;

        webConnHdl handle = clientId;

        if (mode == "Live")
        {
            std::lock_guard<std::mutex> lock(liveStreamsMutex);

            auto it = liveStreams.find(url);
            if (it == liveStreams.end())
            {
                auto ffmpeg = std::make_shared<LiveFFmpegWrapper>(streamInfo,
                                                                  sendDataFunc, sendStringDataFunc);
                liveStreams[url] = ffmpeg;
                ffmpeg->startThread();
            }

            auto ffmpeg = liveStreams[url];
            if (ffmpeg != nullptr)
            {
                ffmpeg->addConnToList(handle);
            }

            connInfo->ffmpegWrapper = ffmpeg;
            connInfo->ffmpegKey = url;
        }
        else
        { // PlayBack mode
            float playbackSpeed = query.get("playbackSpeed", 1.0).asFloat();
            playbackSpeed = std::clamp(playbackSpeed, 0.5f, 5.0f);
            streamInfo["playbackSpeed"] = playbackSpeed;
            streamInfo["startTime"] = query.get("startTime", 0).asInt();

            auto ffmpeg = std::make_shared<PlaybackFFmpegWrapper>(streamInfo,
                                                                  sendDataFunc, sendStringDataFunc);

            if (!connInfo->syncGroupId.empty())
            {
                ffmpeg->setSyncInfo(&syncHandler_, connInfo->syncGroupId);
            }

            ffmpeg->startThread();
            ffmpeg->addConnToList(handle);
            connInfo->ffmpegWrapper = ffmpeg;
        }

        bool connectionStillExists;
        {
            std::lock_guard<std::mutex> lock(connectionsMutex);
            connectionStillExists = (connections.find(clientId) != connections.end());
        }

        if (!connectionStillExists)
        {
            bool canStop = connInfo->ffmpegWrapper->removeConnection(clientId);
            if (connInfo && !connInfo->syncGroupId.empty())
            {
                syncHandler_.removeMember(connInfo->syncGroupId, connInfo->clientId, connInfo->ffmpegWrapper.get());
            }
            if (canStop)
            {
                connInfo->ffmpegWrapper->stopThread();

                if (!connInfo->ffmpegKey.empty())
                {
                    std::lock_guard<std::mutex> lock2(liveStreamsMutex);
                    liveStreams.erase(connInfo->ffmpegKey);
                }

                if (mainLogger)
                {
                    mainLogger->info("[WebRTC] FFmpeg instance stopped: client={}", clientId);
                }
            }
            return;
        }

        if (mainLogger)
        {
            mainLogger->info("[WebRTC] FFmpeg wrapper created: client={}, mode={}", clientId, mode);
        }
    }
    catch (const std::exception &ex)
    {
        if (mainLogger)
        {
            mainLogger->error("[WebRTC] Failed to process stream request: client={}, error={}", clientId, ex.what());
        }
    }
}
