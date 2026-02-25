#include "WebRTCWrapper.h"
#include "common.h"
#include "json/json.h"
#include <json/value.h>
#include "Options.h"

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

// RTP packetization for H.264 over WebRTC
#include <rtc/h264rtppacketizer.hpp>
#include <rtc/rtppacketizationconfig.hpp>

using namespace std;

// Utility function to add credentials to URL
std::string addCredentialsToUrl(const std::string &url, const std::string &username, const std::string &password);

WebRTCWrapper::WebRTCWrapper(int wsPort, const std::string &playerIp, int playerPort,
                             std::shared_ptr<spdlog::logger> logger, bool isVMS,
                             const std::string &vmsUser, const std::string &vmsPassword)
    : ws_signaling_port(wsPort),
      playerServerIp(playerIp),
      playerServerPort(playerPort),
      mainLogger(logger),
      isVMS(isVMS),
      vmsStreamUserName(vmsUser),
      vmsStreamPassword(vmsPassword),
      running(false)
{
    if (mainLogger)
    {
        mainLogger->info("WebRTCWrapper initialized on port: {}", ws_signaling_port);
    }
}

WebRTCWrapper::~WebRTCWrapper()
{
    running = false;

    // Close all WebSocket connections
    {
        std::lock_guard<std::mutex> lock(wsConnectionsMutex);
        for (auto &pair : wsConnections)
        {
            if (pair.second->webSocket)
            {
                pair.second->webSocket->close();
            }
        }
        wsConnections.clear();
    }

    // Stop WebSocket server
    if (wsServer)
    {
        wsServer->stop();
    }

    // Clean up all WebRTC connections
    std::lock_guard<std::mutex> lock(connectionsMutex);
    connections.clear();

    if (mainLogger)
    {
        mainLogger->info("WebRTCWrapper destroyed");
    }
}

void WebRTCWrapper::run()
{
    try
    {
        std::cout << "Starting WebRTC WebSocket signaling server on port: " << ws_signaling_port << std::endl;
        std::cout << "Starting WebRTC WebSocket signaling server on port: " << ws_signaling_port << std::endl;
        // rtc::WebSocket
        if (mainLogger)
        {
            mainLogger->info("Starting WebRTC WebSocket signaling server on port: {}", ws_signaling_port);
            mainLogger->info("Player server IP is: {} and port is: {}", playerServerIp, playerServerPort);
        }

        running = true;

        // Create WebSocket server using libdatachannel
        rtc::WebSocketServer::Configuration config;
        config.port = static_cast<uint16_t>(ws_signaling_port);
        config.enableTls = false; // Use ws:// not wss://

        wsServer = std::make_shared<rtc::WebSocketServer>(config);

        wsServer->onClient([this](std::shared_ptr<rtc::WebSocket> ws)
                           { onWebSocketOpen(ws); });

        if (mainLogger)
        {
            mainLogger->info("WebRTC WebSocket signaling server started successfully on port {}", ws_signaling_port);
        }

        // Keep running
        while (running)
        {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        wsServer->stop();
    }
    catch (const std::exception &ex)
    {
        if (mainLogger)
        {
            mainLogger->error("Error in WebRTC server: {}", ex.what());
        }
        else
        {
            std::cout << "Error in WebRTC server: " << ex.what() << std::endl;
        }
    }
}

void WebRTCWrapper::onWebSocketOpen(std::shared_ptr<rtc::WebSocket> ws)
{
    // Generate a temporary client ID (will be replaced when client sends its ID)
    std::string tempId = "ws_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());

    if (mainLogger)
    {
        mainLogger->info("New WebSocket connection: {}", tempId);
    }

    auto wsInfo = std::make_shared<WebSocketConnectionInfo>();
    wsInfo->clientId = tempId;
    wsInfo->webSocket = ws;

    {
        std::lock_guard<std::mutex> lock(wsConnectionsMutex);
        wsConnections[tempId] = wsInfo;
    }

    // Capture shared_ptr to prevent premature destruction
    std::weak_ptr<WebSocketConnectionInfo> weakWsInfo = wsInfo;

    ws->onOpen([this, weakWsInfo]()
               {
        auto wsInfo = weakWsInfo.lock();
        if (wsInfo && mainLogger) {
            mainLogger->info("WebSocket fully opened for: {}", wsInfo->clientId);
        } });

    ws->onClosed([this, tempId]()
                 { onWebSocketClosed(tempId); });

    ws->onError([this, tempId](std::string error)
                { onWebSocketError(tempId, error); });

    ws->onMessage([this, tempId](std::variant<rtc::binary, std::string> data)
                  {
        if (std::holds_alternative<std::string>(data)) {
            onWebSocketMessage(tempId, std::get<std::string>(data));
        } });
}

void WebRTCWrapper::onWebSocketClosed(const std::string &clientId)
{
    if (mainLogger)
    {
        mainLogger->info("WebSocket closed for client: {}", clientId);
    }

    // Find actual client ID (may have been updated)
    std::string actualClientId = clientId;
    {
        std::lock_guard<std::mutex> lock(wsConnectionsMutex);
        auto it = wsConnections.find(clientId);
        if (it != wsConnections.end())
        {
            actualClientId = it->second->clientId;
            wsConnections.erase(it);
        }
    }

    // Remove WebRTC connection
    removeConnection(actualClientId);
}

void WebRTCWrapper::onWebSocketError(const std::string &clientId, const std::string &error)
{
    if (mainLogger)
    {
        mainLogger->error("WebSocket error for client {}: {}", clientId, error);
    }
}

void WebRTCWrapper::onWebSocketMessage(const std::string &wsId, const std::string &message)
{
    try
    {
        if (mainLogger)
        {
            mainLogger->debug("Received WebSocket message from {}: {}", wsId, message);
        }

        // Parse JSON message
        Json::Value root;
        Json::CharReaderBuilder builder;
        std::string errs;
        std::istringstream sstream(message);

        if (!Json::parseFromStream(builder, sstream, &root, &errs))
        {
            if (mainLogger)
            {
                mainLogger->error("Invalid JSON from client {}: {}", wsId, errs);
            }
            return;
        }

        std::string type = root["type"].asString();
        std::string clientId = root.get("clientId", wsId).asString();

        // Update clientId mapping if client provides its own ID
        std::shared_ptr<rtc::WebSocket> ws;
        {
            std::lock_guard<std::mutex> lock(wsConnectionsMutex);
            auto it = wsConnections.find(wsId);
            if (it != wsConnections.end())
            {
                ws = it->second->webSocket;

                // If client sends a different ID, update mapping
                if (clientId != wsId && clientId != it->second->clientId)
                {
                    it->second->clientId = clientId;
                    wsConnections[clientId] = it->second;
                    // Keep old mapping for cleanup purposes
                }
            }
        }

        // Handle message based on type
        if (type == "offer")
        {
            std::string sdp = root["sdp"].asString();
            std::string sdpType = root.get("sdpType", "offer").asString();
            std::string query = root.get("query", "").asString();

            handleOffer(clientId, sdp, sdpType, query);
        }
        else if (type == "candidate" || type == "ice")
        {
            std::string candidate = root["candidate"].asString();
            std::string sdpMid = root.get("sdpMid", "").asString();
            int sdpMLineIndex = root.get("sdpMLineIndex", 0).asInt();

            handleIceCandidate(clientId, candidate, sdpMid, sdpMLineIndex);
        }
        else
        {
            if (mainLogger)
            {
                mainLogger->warn("Unknown message type from client {}: {}", clientId, type);
            }
        }
    }
    catch (const std::exception &ex)
    {
        if (mainLogger)
        {
            mainLogger->error("Error processing WebSocket message from {}: {}", wsId, ex.what());
        }
    }
}

void WebRTCWrapper::sendSignalingMessage(const std::string &clientId, const std::string &message)
{
    try
    {
        std::shared_ptr<rtc::WebSocket> ws;
        {
            std::lock_guard<std::mutex> lock(wsConnectionsMutex);
            auto it = wsConnections.find(clientId);
            if (it != wsConnections.end() && it->second->webSocket)
            {
                ws = it->second->webSocket;
            }
        }

        if (ws && ws->isOpen())
        {
            ws->send(message);
        }
    }
    catch (const std::exception &ex)
    {
        if (mainLogger)
        {
            mainLogger->error("Error sending signaling message to {}: {}", clientId, ex.what());
        }
    }
}

void WebRTCWrapper::sendSignalingJson(const std::string &clientId, const std::string &type,
                                      const std::map<std::string, std::string> &data)
{
    Json::Value root;
    root["type"] = type;
    for (const auto &pair : data)
    {
        root[pair.first] = pair.second;
    }

    Json::StreamWriterBuilder writerBuilder;
    std::string message = Json::writeString(writerBuilder, root);
    sendSignalingMessage(clientId, message);
}

void WebRTCWrapper::createPeerConnection(const std::string &clientId, const std::string &query)
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
        connInfo->isConnected = false;

        // Store WebSocket reference for signaling
        {
            std::lock_guard<std::mutex> lock(wsConnectionsMutex);
            auto wsIt = wsConnections.find(clientId);
            if (wsIt != wsConnections.end())
            {
                connInfo->signalingWs = wsIt->second->webSocket;
            }
        }

        // Set up state change callbacks
        pc->onStateChange([this, clientId](rtc::PeerConnection::State state)
                          {
            if (mainLogger) {
                mainLogger->info("Client {} peer connection state: {}", clientId, (int)state);
            }

            if (state == rtc::PeerConnection::State::Disconnected ||
                state == rtc::PeerConnection::State::Failed ||
                state == rtc::PeerConnection::State::Closed) {
                if (mainLogger) {
                    mainLogger->info("Client {} disconnected, cleaning up", clientId);
                }
                removeConnection(clientId);
            } });

        pc->onGatheringStateChange([this, clientId](rtc::PeerConnection::GatheringState state)
                                   {
            if (mainLogger) {
                mainLogger->debug("Client {} ICE gathering state: {}", clientId, (int)state);
            } });

        // Trickle ICE: Send candidates as they are discovered (no waiting!)
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
            sendSignalingMessage(clientId, Json::writeString(writerBuilder, msg)); });

        // Accept data channel created by the client (server is the answerer)
        pc->onDataChannel([this, clientId](std::shared_ptr<rtc::DataChannel> dc)
                          {
            if (mainLogger) {
                mainLogger->info("Data channel received from client {}: {}", clientId, dc->label());
            }

            dc->onOpen([this, clientId]()
                       {
                if (mainLogger) {
                    mainLogger->info("Data channel opened for client {}", clientId);
                }

                std::lock_guard<std::mutex> lock(connectionsMutex);
                auto it = connections.find(clientId);
                if (it != connections.end()) {
                    it->second->isConnected = true;
                } });

            dc->onClosed([this, clientId]()
                         {
                if (mainLogger) {
                    mainLogger->info("Data channel closed for client {}", clientId);
                } });

            dc->onMessage([this, clientId](std::variant<rtc::binary, std::string> data)
                          { handleDataChannelMessage(clientId, data); });

            // Store the data channel
            {
                std::lock_guard<std::mutex> lock(connectionsMutex);
                auto it = connections.find(clientId);
                if (it != connections.end()) {
                    it->second->dataChannel = dc;
                }
            } });

        // Capture the video track from the offer's video m-line (server is answerer)
        pc->onTrack([this, clientId](std::shared_ptr<rtc::Track> track)
                     {
            if (mainLogger) {
                mainLogger->info("Track received from offer for client {}: mid={}", clientId, track->mid());
            }

            // Set up H.264 RTP packetizer for proper WebRTC video delivery
            // Use the H264 payload type from the SDP offer (stored in connection info)
            int h264PT = 109; // H264 Constrained Baseline, packetization-mode=1
            {
                std::lock_guard<std::mutex> lock(connectionsMutex);
                auto it = connections.find(clientId);
                if (it != connections.end()) {
                    h264PT = it->second->h264PayloadType;
                }
            }
            uint32_t ssrc = 1;
            auto rtpConfig = std::make_shared<rtc::RtpPacketizationConfig>(
                ssrc, "video-send", h264PT, rtc::H264RtpPacketizer::defaultClockRate);
            auto packetizer = std::make_shared<rtc::H264RtpPacketizer>(
                rtc::H264RtpPacketizer::Separator::LongStartSequence, rtpConfig);
            track->setMediaHandler(packetizer);

            if (mainLogger) {
                mainLogger->info("H264 RTP packetizer configured for client {}", clientId);
            }

            std::lock_guard<std::mutex> lock(connectionsMutex);
            auto it = connections.find(clientId);
            if (it != connections.end()) {
                it->second->videoTrack = track;
                it->second->rtpConfig = rtpConfig;
            } });

        // Store connection
        {
            std::lock_guard<std::mutex> lock(connectionsMutex);
            connections[clientId] = connInfo;
        }

        if (mainLogger)
        {
            mainLogger->info("Peer connection created successfully for client: {}", clientId);
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

void WebRTCWrapper::handleOffer(const std::string &clientId, const std::string &sdp,
                                const std::string &type, const std::string &query)
{
    try
    {
        if (mainLogger)
        {
            mainLogger->info("Handling offer from client: {}", clientId);
        }

        // Create peer connection if it doesn't exist
        {
            bool needsCreation = false;
            {
                std::lock_guard<std::mutex> lock(connectionsMutex);
                needsCreation = (connections.find(clientId) == connections.end());
            }
            if (needsCreation)
            {
                createPeerConnection(clientId, query);
            }
        }

        std::shared_ptr<WebRTCConnectionInfo> connInfo;
        {
            std::lock_guard<std::mutex> lock(connectionsMutex);
            connInfo = connections[clientId];
        }

        auto pc = connInfo->peerConnection;

        // Set remote description (offer) — puts state into have-remote-offer
        rtc::Description offer(sdp, type);
        pc->setRemoteDescription(offer);

        // Video track is captured via onTrack callback (set up in createPeerConnection)
        // Do NOT call addTrack() here — it would create a new m-line and break m-line order

        // Process request to create FFmpegWrapper
        std::string queryCopy = query;
        processRequest(clientId, queryCopy);

        // Set up callback to send answer when local description is ready
        // This is called asynchronously - no blocking wait needed!
        pc->onLocalDescription([this, clientId](rtc::Description description)
                               {
            if (mainLogger) {
                mainLogger->info("Sending SDP answer to client: {}", clientId);
            }

            // Fix SDP: answerer must use "active" or "passive", not "actpass"
            std::string sdpStr = std::string(description);
            std::string::size_type pos = 0;
            while ((pos = sdpStr.find("a=setup:actpass", pos)) != std::string::npos) {
                sdpStr.replace(pos, 15, "a=setup:active");
                pos += 14;
            }

            Json::Value answerMsg;
            answerMsg["type"] = "answer";
            answerMsg["sdp"] = sdpStr;

            Json::StreamWriterBuilder writerBuilder;
            sendSignalingMessage(clientId, Json::writeString(writerBuilder, answerMsg)); });

        // Generate answer - this triggers onLocalDescription callback
        // ICE candidates will be sent via onLocalCandidate as they're discovered
        std::cout << "Creating answer for client: " << clientId << std::endl;
        pc->setLocalDescription();
        std::cout << "Creating answer for client: " << clientId << std::endl;
        // No waiting needed! Everything is async via callbacks
    }
    catch (const std::exception &ex)
    {
        if (mainLogger)
        {
            mainLogger->error("Error handling offer from client {}: {}", clientId, ex.what());
        }

        // Send error to client
        Json::Value errorMsg;
        errorMsg["type"] = "error";
        errorMsg["message"] = ex.what();

        Json::StreamWriterBuilder writerBuilder;
        sendSignalingMessage(clientId, Json::writeString(writerBuilder, errorMsg));
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
            if (!loggedOnce) {
                std::cout << "SendData: videoTrack is NULL - onTrack callback never fired" << std::endl;
                loggedOnce = true;
            }
            return;
        }

        if (!it->second->isConnected)
        {
            static bool loggedOnce2 = false;
            if (!loggedOnce2) {
                std::cout << "SendData: isConnected=false, data channel not open yet" << std::endl;
                loggedOnce2 = true;
            }
            return;
        }

        auto track = it->second->videoTrack;

        // Remove 8-byte timestamp prefix that FFmpegWrapper adds
        std::vector<uint8_t> frameData;
        if (data.size() > 8)
        {
            frameData.assign(data.begin() + 8, data.end());
        }
        else
        {
            frameData = data;
        }

        // Send H.264 frame — the H264RtpPacketizer (set on the track via setMediaHandler)
        // automatically handles: RTP headers, timestamps, FU-A fragmentation for large NALs.
        // FFmpeg provides Annex B format (00 00 00 01 start codes), which the packetizer
        // splits into individual NAL units and packetizes into RTP.
        if (!frameData.empty())
        {
            // Convert PTS (microseconds) to RTP timestamp (90kHz clock)
            auto rtpTimestamp = static_cast<uint32_t>((timestamp * 90000) / 1000000);

            // Set the timestamp on the RTP config before sending
            if (it->second->rtpConfig) {
                it->second->rtpConfig->timestamp = rtpTimestamp;
            }

            rtc::binary rtpPayload(reinterpret_cast<const std::byte *>(frameData.data()),
                                   reinterpret_cast<const std::byte *>(frameData.data() + frameData.size()));
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
            return; // Don't send to client, seamless continuation
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
            std::string nextUrl = Get_PlayBackUrl(cameraId, nextTime, &newSeekTime, &newDuration_Minutes);

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

void WebRTCWrapper::processRequest(const std::string &clientId, std::string &query)
{
    try
    {
        std::string cameraId;
        std::string mode = "Live";
        int streamtype = 0;
        int start_time_ofplaybackfile = 0;
        int end_time_ofplaybackfile = 0;
        std::string analyticType = "";
        std::string connectionmode = "tcp";
        float playbackSpeed = 1.0;
        std::string vaServerId = "";
        std::string vaServerPipeId = "";

        std::string url;
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
            else if (key == "streamType" || key == "streamtype")
                streamtype = std::stoi(value);
            else if (key == "startTime")
                start_time_ofplaybackfile = std::stoi(value);
            else if (key == "endTime")
                end_time_ofplaybackfile = std::stoi(value);
            else if (key == "analyticType")
                analyticType = value;
            else if (key == "connectionMode")
                connectionmode = value;
            else if (key == "playbackSpeed" && !value.empty())
            {
                playbackSpeed = std::stof(value);
                playbackSpeed = std::clamp(playbackSpeed, 0.5f, 5.0f);
            }
            else if (key == "vaServerId" && !value.empty())
                vaServerId = value;
            else if (key == "vaServerPipeId" && !value.empty())
                vaServerPipeId = value;
        }

        // Get URL based on mode
        // if (mode == "Live")
        // {
        //     url = Get_LiveUrl(cameraId, streamtype, analyticType, vaServerId, vaServerPipeId);
        // }
        // else
        // {
        //     if (end_time_ofplaybackfile == 0)
        //     {
        //         url = Get_PlayBackUrl(cameraId, start_time_ofplaybackfile, &seekTime_ofFile, &duration_in_Minutes);
        //     }
        //     else
        //     {
        //         url = Get_PlayBackUrl(cameraId, start_time_ofplaybackfile, end_time_ofplaybackfile);
        //     }
        // }
        url = "rtsp://192.168.29.227:554/output.mp4"; // For testing only, remove this line in production

        if (url.empty() || boost::starts_with(url, "Player_Server_Not_Connected") ||
            boost::starts_with(url, "URL_Server_Not_Connected"))
        {
            if (mainLogger)
            {
                mainLogger->error("Failed to get URL for client {}: {}", clientId, url);
            }
            return;
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

std::string WebRTCWrapper::Get_LiveUrl(const std::string &cameraId, int streamtype,
                                       const std::string &analyticType, const std::string &vaServerId,
                                       const std::string &vaServerPipeId)
{
    string response;
    string cameraId_instring = cameraId;
    std::string endpoint = "";

    if (vaServerId != "" || vaServerPipeId != "")
    {
        endpoint = "/url/GetLiveVaUrl?cameraId=" + cameraId_instring + "&streamType=" + std::to_string(streamtype) +
                   "&analyticType=" + analyticType + "&vaServerId=" + vaServerId + "&vaServerPipeId=" + vaServerPipeId;
    }
    else
    {
        endpoint = "/url/GetLiveUrl?cameraId=" + cameraId_instring + "&streamType=" + std::to_string(streamtype) +
                   "&analyticType=" + analyticType;
    }

    if (cameraId == "")
    {
        return response;
    }

    try
    {
        std::string url = "http://" + playerServerIp + ":" + std::to_string(playerServerPort) + endpoint;
        auto res = cpr::Get(cpr::Url{url});

        if (res.status_code == 200)
        {
            string command = res.text;
            command.erase(std::remove(command.begin(), command.end(), '\"'), command.end());
            command.erase(std::remove(command.begin(), command.end(), '\\'), command.end());
            if (isVMS)
            {
                command = addCredentialsToUrl(command, vmsStreamUserName, vmsStreamPassword);
            }
            response = command;
        }
        else if (res.status_code == 403)
        {
            if (mainLogger)
            {
                mainLogger->error("Get_LiveUrl Server License Expired");
            }
        }
        else if (res.status_code == 400)
        {
            if (mainLogger)
            {
                mainLogger->error("Get_LiveUrl Some Error occurred status code: {}", res.status_code);
            }
        }
        else
        {
            if (mainLogger)
            {
                mainLogger->error("Get_LiveUrl Some Error occurred status code: {}", res.status_code);
            }
            response = "Player_Server_Not_Connected";
        }
    }
    catch (const std::exception &ex)
    {
        if (mainLogger)
        {
            mainLogger->error("Error in Get_LiveUrl: {}", ex.what());
        }
    }
    return response;
}

std::string WebRTCWrapper::Get_PlayBackUrl(const std::string &cameraId, int start_time,
                                           int *seekTime, float *duration)
{
    string response;
    string cameraId_instring = cameraId;

    std::string endpoint = "/url/GetPlaybackUrl?cameraId=" + cameraId_instring + "&time=" + std::to_string(start_time);
    if (cameraId == "")
    {
        return response;
    }

    try
    {
        std::string url = "http://" + playerServerIp + ":" + std::to_string(playerServerPort) + endpoint;
        auto res = cpr::Get(cpr::Url{url});

        if (res.status_code == 200)
        {
            if (res.text == "URL_Server_Not_Connected")
            {
                return "URL_Server_Not_Connected";
            }

            string json = res.text;
            Json::Reader reader;
            Json::Value root;
            bool parseSuccess = reader.parse(json, root, false);

            if (parseSuccess)
            {
                Json::Value resultValue = root["GetEventPlaybackUrlResult"];
                if (resultValue.asString() == "")
                {
                    resultValue = root["getEventPlaybackUrlResult"];
                }

                Json::Value resultValue1 = root["Seek_Time_InSeconds"];
                if (resultValue1.asString() == "")
                {
                    resultValue1 = root["seek_Time_InSeconds"];
                }

                Json::Value resultValue3 = root["duration_in_Minutes"];
                float duration_in_Minutes_value = 0;
                if (!resultValue3.isNull())
                {
                    duration_in_Minutes_value = resultValue3.asFloat();
                }

                *seekTime = std::stoi(resultValue1.asString());

                if (duration != nullptr)
                {
                    *duration = duration_in_Minutes_value;
                }

                response = resultValue.asString();
                response.erase(std::remove(response.begin(), response.end(), '\"'), response.end());
                response.erase(std::remove(response.begin(), response.end(), '\\'), response.end());
            }
        }
        else
        {
            response = "Player_Server_Not_Connected";
        }
    }
    catch (const std::exception &ex)
    {
        if (mainLogger)
        {
            mainLogger->error("Error in Get_PlayBackUrl: {}", ex.what());
        }
        response = "";
    }
    return response;
}

std::string WebRTCWrapper::Get_PlayBackUrl(const std::string &cameraId, int start_time, int end_time)
{
    string response;
    string cameraId_instring = cameraId;

    std::string endpoint = "/url/GetExportUrl?cameraId=" + cameraId_instring + "&startTime=" +
                           std::to_string(start_time) + "&endTime=" + std::to_string(end_time);
    if (cameraId == "")
    {
        return response;
    }

    try
    {
        std::string url = "http://" + playerServerIp + ":" + std::to_string(playerServerPort) + endpoint;
        auto res = cpr::Get(cpr::Url{url});

        if (res.status_code == 200)
        {
            if (res.text == "URL_Server_Not_Connected")
            {
                return "URL_Server_Not_Connected";
            }

            string json = res.text;
            Json::Reader reader;
            Json::Value root;
            bool parseSuccess = reader.parse(json, root, false);

            if (parseSuccess)
            {
                Json::Value resultValue = root["ExportedVideoUrl"];
                response = resultValue.asString();
                response.erase(std::remove(response.begin(), response.end(), '\"'), response.end());
                response.erase(std::remove(response.begin(), response.end(), '\\'), response.end());
            }
        }
        else
        {
            response = "Player_Server_Not_Connected";
        }
    }
    catch (const std::exception &ex)
    {
        if (mainLogger)
        {
            mainLogger->error("Error in Get_PlayBackUrl: {}", ex.what());
        }
        response = "";
    }
    return response;
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

// Utility function to add credentials to URL
std::string addCredentialsToUrl(const std::string &url, const std::string &username, const std::string &password)
{
    std::regex credentialsRegex(R"([^:]+:[^@]+@)");

    if (std::regex_search(url, credentialsRegex))
    {
        return url;
    }
    else
    {
        size_t prefixPos = url.find("://");
        if (prefixPos != std::string::npos)
        {
            std::string credentials = username + ":" + password + "@";
            std::string newUrl = url.substr(0, prefixPos + 3) + credentials + url.substr(prefixPos + 3);
            return newUrl;
        }
        else
        {
            throw std::invalid_argument("Invalid/Unexpected URL: " + url);
        }
    }
}
