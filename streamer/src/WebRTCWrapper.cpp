#include "WebRTCWrapper.h"
#include "common.h"
#include "json/json.h"
#include <json/value.h>
#include "Options.h"
#include "PlayerServerClient.h"

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

// RTP packetization over WebRTC
#include <rtc/h264rtppacketizer.hpp>
#include <rtc/h265rtppacketizer.hpp>
#include <rtc/rtppacketizationconfig.hpp>

using namespace std;

WebRTCWrapper::WebRTCWrapper(const std::string &playerIp, int playerPort,
                             std::shared_ptr<spdlog::logger> logger, bool isVMS,
                             const std::string &vmsUser, const std::string &vmsPassword)
    : playerServerIp(playerIp),
      playerServerPort(playerPort),
      mainLogger(logger)
{
    PlayerServerClient::init(playerIp, playerPort, logger, isVMS, vmsUser, vmsPassword);

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

void WebRTCWrapper::tryCloseSignaling(const std::string &clientId)
{
    // Must be called with connectionsMutex already held
    auto it = connections.find(clientId);
    if (it == connections.end())
        return;

    auto &info = it->second;
    if (info->isConnected && info->iceConnected && info->gatheringComplete)
    {
        if (mainLogger)
        {
            mainLogger->info("Client {} fully connected - closing WebSocket", clientId);
        }
        signalingTransport_.closeConnection(clientId);
    }
}

void WebRTCWrapper::setupVideoTrack(std::shared_ptr<rtc::PeerConnection> pc,
                                    std::shared_ptr<WebRTCConnectionInfo> connInfo,
                                    const std::string &url, AVCodecID codecId)
{
    rtc::Description::Video media("video", rtc::Description::Direction::SendOnly);

    // Add codec to SDP based on probed codec
    switch (codecId)
    {
    case AV_CODEC_ID_H264:
        media.addH264Codec(96);
        break;
    case AV_CODEC_ID_H265:
    default:
        media.addH265Codec(96);
        break;
    }

    media.addSSRC(1, "video-stream");
    auto track = pc->addTrack(media);

    // Pick separator based on container format
    bool isAvccFormat = boost::ends_with(url, ".mp4") || boost::ends_with(url, ".mkv") || boost::ends_with(url, ".mov");

    // Create codec-specific RTP packetizer
    std::shared_ptr<rtc::RtpPacketizationConfig> rtpConfig;
    switch (codecId)
    {
    case AV_CODEC_ID_H264:
    {
        rtpConfig = std::make_shared<rtc::RtpPacketizationConfig>(
            1, "video-stream", 96, rtc::H264RtpPacketizer::defaultClockRate);
        auto separator = isAvccFormat
            ? rtc::H264RtpPacketizer::Separator::LongStartSequence
            : rtc::H264RtpPacketizer::Separator::StartSequence;
        track->setMediaHandler(std::make_shared<rtc::H264RtpPacketizer>(separator, rtpConfig));
        break;
    }
    case AV_CODEC_ID_H265:
    default:
    {
        rtpConfig = std::make_shared<rtc::RtpPacketizationConfig>(
            1, "video-stream", 96, rtc::H265RtpPacketizer::defaultClockRate);
        auto separator = isAvccFormat
            ? rtc::H265RtpPacketizer::Separator::LongStartSequence
            : rtc::H265RtpPacketizer::Separator::StartSequence;
        track->setMediaHandler(std::make_shared<rtc::H265RtpPacketizer>(separator, rtpConfig));
        break;
    }
    }

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

void WebRTCWrapper::createPeerConnection(const std::string &clientId, const std::string &query,
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
        connInfo->query = query;

        // Set up state change callbacks
        pc->onStateChange([this, clientId](rtc::PeerConnection::State state)
                          {
            if (mainLogger) {
                mainLogger->info("Client {} peer connection state: {}", clientId, (int)state);
            }

            if (state == rtc::PeerConnection::State::Connected) {
                std::lock_guard<std::mutex> lock(connectionsMutex);
                auto it = connections.find(clientId);
                if (it != connections.end()) {
                    it->second->isConnected = true;
                    tryCloseSignaling(clientId);
                }
            }
            else if (state == rtc::PeerConnection::State::Disconnected ||
                state == rtc::PeerConnection::State::Failed ||
                state == rtc::PeerConnection::State::Closed) {
                if (mainLogger) {
                    mainLogger->info("Client {} disconnected, cleaning up", clientId);
                }
                removeConnection(clientId);
            } });

        pc->onIceStateChange([this, clientId](rtc::PeerConnection::IceState state)
                             {
            if (mainLogger) {
                mainLogger->info("Client {} ICE connection state: {}", clientId, (int)state);
            }

            if (state == rtc::PeerConnection::IceState::Completed) {
                std::lock_guard<std::mutex> lock(connectionsMutex);
                auto it = connections.find(clientId);
                if (it != connections.end()) {
                    it->second->iceConnected = true;
                    tryCloseSignaling(clientId);
                }
            } });

        pc->onGatheringStateChange([this, clientId](rtc::PeerConnection::GatheringState state)
                                   {
            if (mainLogger) {
                mainLogger->debug("Client {} ICE gathering state: {}", clientId, (int)state);
            }

            if (state == rtc::PeerConnection::GatheringState::Complete) {
                std::lock_guard<std::mutex> lock(connectionsMutex);
                auto it = connections.find(clientId);
                if (it != connections.end()) {
                    it->second->gatheringComplete = true;
                    tryCloseSignaling(clientId);
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

        // Store connection
        {
            std::lock_guard<std::mutex> lock(connectionsMutex);
            connections[clientId] = connInfo;
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

void WebRTCWrapper::handleRequest(const std::string &clientId, const std::string &query)
{
    try
    {
        if (mainLogger)
        {
            mainLogger->info("Handling request from client: {}", clientId);
        }

        // 1. Resolve URL first (need it to pick correct RTP packetizer separator)
        std::string url = PlayerServerClient::resolveStreamUrl(query);

        if (url.empty() || boost::starts_with(url, "Player_Server_Not_Connected") ||
            boost::starts_with(url, "URL_Server_Not_Connected"))
        {
            if (mainLogger)
            {
                mainLogger->error("Failed to get URL for client {}: {}", clientId, url);
            }

            Json::Value errorMsg;
            errorMsg["type"] = "error";
            errorMsg["message"] = url.empty() ? "Failed to resolve stream URL" : url;

            Json::StreamWriterBuilder writerBuilder;
            signalingTransport_.sendMessage(clientId, Json::writeString(writerBuilder, errorMsg));
            return;
        }


        // 3. Probe actual codec from stream before creating peer connection
        AVCodecID codecId = FFmpegWrapper::probeCodec(url);
        if (mainLogger)
        {
            mainLogger->info("Probed codec for client {}: {} ({})", clientId,
                             avcodec_get_name(codecId), (int)codecId);
        }

        // 4. Create peer connection with correct packetizer based on actual codec
        createPeerConnection(clientId, query, url, codecId);

        // 5. Create FFmpegWrapper and start streaming
        std::string queryCopy = query;
        processRequest(clientId, queryCopy, url);
    }
    catch (const std::exception &ex)
    {
        if (mainLogger)
        {
            mainLogger->error("Error handling request from client {}: {}", clientId, ex.what());
        }

        Json::Value errorMsg;
        errorMsg["type"] = "error";
        errorMsg["message"] = ex.what();

        Json::StreamWriterBuilder writerBuilder;
        signalingTransport_.sendMessage(clientId, Json::writeString(writerBuilder, errorMsg));
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
        std::string ffmpegKey;

        // Get FFmpeg key for this client
        {
            std::lock_guard<std::mutex> lock(clientMapMutex);
            auto it = clientToFfmpegMap.find(clientId);
            if (it != clientToFfmpegMap.end())
            {
                ffmpegKey = it->second;
                clientToFfmpegMap.erase(it);
            }
        }

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
        if (!ffmpegKey.empty() && connInfo)
        {
            bool canStop = false;

            {
                std::lock_guard<std::mutex> lock(ffmpegListMutex);
                auto ffmpegIt = ffmpegList.find(ffmpegKey);
                if (ffmpegIt != ffmpegList.end())
                {
                    canStop = ffmpegIt->second->removeConnection(connInfo->peerConnection);
                    if (canStop)
                    {
                        ffmpegIt->second->stopThread();
                        ffmpegList.erase(ffmpegKey);

                        if (mainLogger)
                        {
                            mainLogger->info("Stopped FFmpeg instance for key: {}", ffmpegKey);
                        }
                    }
                }
            }
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
        std::string ffmpegKey;
        {
            std::lock_guard<std::mutex> lock(clientMapMutex);
            auto it = clientToFfmpegMap.find(clientId);
            if (it == clientToFfmpegMap.end())
            {
                if (mainLogger)
                {
                    mainLogger->warn("No FFmpeg instance for client {}", clientId);
                }
                return;
            }
            ffmpegKey = it->second;
        }

        // Get FFmpeg wrapper
        std::shared_ptr<FFmpegWrapper> ffmpeg;
        {
            std::lock_guard<std::mutex> lock(ffmpegListMutex);
            auto ffmpegIt = ffmpegList.find(ffmpegKey);
            if (ffmpegIt == ffmpegList.end())
            {
                return;
            }
            ffmpeg = ffmpegIt->second;
        }

        // Route message to FFmpegWrapper
        if (boost::starts_with(message, "seek_Time"))
        {
            std::string timeStr = message.substr(9);
            if (!timeStr.empty())
            {
                int seekTime = std::stoi(timeStr);
                if (seekTime >= 0)
                {
                    ffmpeg->seek_video(seekTime);
                }
            }
        }
        else if (message == "Pause")
        {
            ffmpeg->Pause_video();
        }
        else if (message == "Resume")
        {
            // Resume not implemented in FFmpegWrapper yet
        }
        else if (boost::starts_with(message, "FastForward"))
        {
            std::string speedStr = message.substr(11);
            if (!speedStr.empty())
            {
                float speed = std::stof(speedStr);
                if (speed >= 0)
                {
                    ffmpeg->FastForward_video(speed);
                }
            }
        }
        else if (message == "Version")
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
            std::string servData = "ffmpegList.count: " + std::to_string(ffmpegList.size()) + "\n";
            {
                std::lock_guard<std::mutex> lock(ffmpegListMutex);
                for (const auto &mapKey : ffmpegList)
                {
                    servData += mapKey.first + "\n";
                }
            }
            servData += "\nconnections: " + std::to_string(connections.size()) + "\n";

            std::lock_guard<std::mutex> lock(connectionsMutex);
            auto it = connections.find(clientId);
            if (it != connections.end())
            {
                SendStringData(it->second->peerConnection, "--servStatus " + servData);
            }
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

        if (timestamp < 0)
        {
            // Live mode — no prefix, raw H264 data
            frameStart = data.data();
            frameSize = data.size();
        }
        else if (data.size() > 8)
        {
            // Playback mode — strip 8-byte position prefix
            frameStart = data.data() + 8;
            frameSize = data.size() - 8;
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

        // Handle special case: Playback_Finished with next segment
        if (sdata.find("\"event\":\"Playback_Finished\"") != std::string::npos &&
            sdata.find("\"nextTime\":") != std::string::npos)
        {
            // Unlock before calling handlePlaybackFinished to avoid deadlock
            lock.~lock_guard();
            handlePlaybackFinished(clientId, sdata);
            return;
        }

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

void WebRTCWrapper::handlePlaybackFinished(const std::string &clientId, const std::string &jsonData)
{
    try
    {
        // Parse JSON to extract nextTime
        Json::Value root;
        Json::CharReaderBuilder builder;
        std::string errs;
        std::istringstream sstream(jsonData);

        if (!Json::parseFromStream(builder, sstream, &root, &errs))
        {
            return;
        }

        int nextTime = root["nextTime"].asInt();
        std::string cameraId = root["cameraId"].asString();

        // Find connection and FFmpeg details
        std::string keyValue;
        std::string mode;
        rtcConnHdl peerConn;

        {
            std::lock_guard<std::mutex> lock1(clientMapMutex);
            auto it = clientToFfmpegMap.find(clientId);
            if (it != clientToFfmpegMap.end())
            {
                keyValue = it->second;
            }
        }

        {
            std::lock_guard<std::mutex> lock2(connectionsMutex);
            auto it = connections.find(clientId);
            if (it != connections.end())
            {
                peerConn = it->second->peerConnection;
            }
        }

        if (keyValue.empty() || cameraId.empty() || !peerConn)
        {
            return;
        }

        // Get mode from keyValue
        std::vector<std::string> keyParts;
        boost::algorithm::split_regex(keyParts, keyValue, boost::regex("~~"));
        mode = (keyParts.size() > 1) ? keyParts[1] : "PlayBack";

        if (mode == "PlayBack")
        {
            // Get next playback URL
            int newSeekTime = 0;
            float newDuration_Minutes = 0;
            std::string nextUrl = PlayerServerClient::GetPlayBackUrl(cameraId, nextTime, &newSeekTime, &newDuration_Minutes);

            if (nextUrl.empty() || boost::starts_with(nextUrl, "Player_Server_Not_Connected") ||
                boost::starts_with(nextUrl, "URL_Server_Not_Connected"))
            {
                return;
            }

            // Stop old wrapper
            std::shared_ptr<FFmpegWrapper> oldWrapper;
            {
                std::lock_guard<std::mutex> lock(ffmpegListMutex);
                auto ffmpegIt = ffmpegList.find(keyValue);
                if (ffmpegIt != ffmpegList.end())
                {
                    oldWrapper = ffmpegIt->second;
                    ffmpegList.erase(ffmpegIt);
                }
            }

            if (oldWrapper)
            {
                std::thread([oldWrapper]()
                            { oldWrapper->stopThread(); })
                    .detach();
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(200));

            // Create new FFmpeg wrapper
            auto bindSendData = std::bind(&WebRTCWrapper::SendData, this,
                                          std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
            auto bindSendStringData = std::bind(&WebRTCWrapper::SendStringData, this,
                                                std::placeholders::_1, std::placeholders::_2);

            std::function<void(webConnHdl &, std::vector<uint8_t> &, int64_t)> sendDataFunc = bindSendData;
            std::function<void(webConnHdl &, std::string)> sendStringDataFunc = bindSendStringData;

            float playbackSpeed = 1.0;
            std::string connectionmode = "tcp";

            int random = generateAndCheckRandomNumber();
            std::string newKeyValue = std::to_string(random) + "~~" + mode;

            int newDuration_Seconds = static_cast<int>(round(newDuration_Minutes * 60));

            auto ffmpeg = std::make_shared<FFmpegWrapper>(cameraId, nextUrl, mode, newSeekTime,
                                                          sendDataFunc, sendStringDataFunc,
                                                          connectionmode, playerServerIp, playerServerPort,
                                                          mainLogger, playbackSpeed, nextTime, newDuration_Seconds);

            {
                std::lock_guard<std::mutex> lock(ffmpegListMutex);
                ffmpegList[newKeyValue] = ffmpeg;
            }

            ffmpeg->startThread();
            ffmpeg->addConnection(peerConn);

            {
                std::lock_guard<std::mutex> lock(clientMapMutex);
                clientToFfmpegMap[clientId] = newKeyValue;
            }

            if (mainLogger)
            {
                mainLogger->info("Seamlessly continued playback for client {}", clientId);
            }
        }
    }
    catch (const std::exception &ex)
    {
        if (mainLogger)
        {
            mainLogger->error("Error handling playback finished: {}", ex.what());
        }
    }
}

void WebRTCWrapper::processRequest(const std::string &clientId, std::string &query, const std::string &url)
{
    try
    {
        std::string cameraId;
        std::string mode = "Live";
        std::string connectionmode = "tcp";
        float playbackSpeed = 1.0;
        int start_time_ofplaybackfile = 0;
        int seekTime_ofFile = 0;
        float duration_in_Minutes = 0;

        // Parse query parameters
        std::vector<std::string> props;
        boost::algorithm::split_regex(props, query, boost::regex("&&"));

        for (auto const &prop : props)
        {
            std::vector<std::string> keyValue;
            boost::algorithm::split_regex(keyValue, prop, boost::regex("~~"));
            if (keyValue.size() < 2)
                continue;

            auto &key = keyValue[0];
            auto &value = keyValue[1];

            if (key == "cameraId")
                cameraId = value;
            else if (key == "mode")
                mode = value;
            else if (key == "startTime")
                start_time_ofplaybackfile = std::stoi(value);
            else if (key == "connectionMode")
                connectionmode = value;
            else if (key == "playbackSpeed" && !value.empty())
            {
                playbackSpeed = std::stof(value);
                playbackSpeed = std::clamp(playbackSpeed, 0.5f, 5.0f);
            }
        }

        // Create FFmpeg wrapper
        rtcConnHdl peerConn;
        {
            std::lock_guard<std::mutex> lock(connectionsMutex);
            auto it = connections.find(clientId);
            if (it != connections.end())
            {
                peerConn = it->second->peerConnection;
            }
        }

        if (!peerConn)
        {
            return;
        }

        auto bindSendData = std::bind(&WebRTCWrapper::SendData, this,
                                      std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
        auto bindSendStringData = std::bind(&WebRTCWrapper::SendStringData, this,
                                            std::placeholders::_1, std::placeholders::_2);

        std::function<void(webConnHdl &, std::vector<uint8_t> &, int64_t)> sendDataFunc = bindSendData;
        std::function<void(webConnHdl &, std::string)> sendStringDataFunc = bindSendStringData;

        if (mode == "Live")
        {
            std::string keyValue = cameraId + "~~" + mode + "~~" + url;

            std::lock_guard<std::mutex> lock(ffmpegListMutex);

            if (ffmpegList.find(keyValue) == ffmpegList.end())
            {
                auto ffmpeg = std::make_shared<FFmpegWrapper>(cameraId, url, mode, seekTime_ofFile,
                                                              sendDataFunc, sendStringDataFunc,
                                                              connectionmode, playerServerIp, playerServerPort,
                                                              mainLogger, playbackSpeed);
                ffmpegList[keyValue] = ffmpeg;
                ffmpeg->startThread();
            }

            auto ffmpeg = ffmpegList[keyValue];
            if (ffmpeg != nullptr)
            {
                ffmpeg->addConnection(peerConn);
            }

            {
                std::lock_guard<std::mutex> lock2(clientMapMutex);
                clientToFfmpegMap[clientId] = keyValue;
            }
        }
        else
        { // PlayBack mode
            int random = generateAndCheckRandomNumber();
            std::string keyValue = std::to_string(random) + "~~" + mode;

            int duration_in_Seconds = static_cast<int>(round(duration_in_Minutes * 60));

            auto ffmpeg = std::make_shared<FFmpegWrapper>(cameraId, url, mode, seekTime_ofFile,
                                                          sendDataFunc, sendStringDataFunc,
                                                          connectionmode, playerServerIp, playerServerPort,
                                                          mainLogger, playbackSpeed, start_time_ofplaybackfile,
                                                          duration_in_Seconds);

            {
                std::lock_guard<std::mutex> lock(ffmpegListMutex);
                ffmpegList[keyValue] = ffmpeg;
            }

            ffmpeg->startThread();
            ffmpeg->addConnection(peerConn);

            {
                std::lock_guard<std::mutex> lock(clientMapMutex);
                clientToFfmpegMap[clientId] = keyValue;
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

int WebRTCWrapper::generateAndCheckRandomNumber()
{
    bool randomValExist = false;
    int random = rand();
    if (random > 0)
    {
        random = -random;
    }

    for (auto &ffmpegListvar : ffmpegList)
    {
        try
        {
            int cameraId = std::stoi(ffmpegListvar.first.substr(0, ffmpegListvar.first.find("~~")));
            if (cameraId == random)
            {
                randomValExist = true;
                break;
            }
        }
        catch (const std::exception &ex)
        {
            continue;
        }
    }

    if (randomValExist)
    {
        return generateAndCheckRandomNumber();
    }
    else
    {
        return random;
    }
}
