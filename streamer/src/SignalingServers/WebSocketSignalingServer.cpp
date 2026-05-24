#include "SignalingServers/WebSocketSignalingServer.h"
#include "WebRTCWrapper.h"
#include "MjpegWebSocketWrapper.h"
#include "AppConfig.h"
#include "PlayerServerClient.h"
#include "Ffmpeg/FFmpegWrapper.h"
#include "json/json.h"
#include <boost/algorithm/string/predicate.hpp>
#include <iostream>
#include <atomic>
#include <chrono>
#include <thread>
#include <sstream>

namespace {
std::string buildJsonMessage(const std::string &type, const std::string &message)
{
    Json::Value msg;
    msg["type"] = type;
    msg["message"] = message;
    Json::StreamWriterBuilder writerBuilder;
    return Json::writeString(writerBuilder, msg);
}
}

WebSocketSignalingServer::WebSocketSignalingServer(int port, std::shared_ptr<spdlog::logger> logger)
    : port_(port), logger_(logger)
{
    SignalingTransport transport = {
        .sendMessage = [this](const std::string &id, const std::string &msg) { sendMessage(id, msg); },
        .sendBinary = [this](const std::string &id, const uint8_t *data, size_t size) { sendBinary(id, data, size); },
        .closeConnection = [this](const std::string &id) { closeConnection(id); },
        .isConnected = [this](const std::string &id) { return isConnected(id); },
        .getQuery = [this](const std::string &id) { return getClientQuery(id); }
    };

    rtcWrapper_ = std::make_unique<WebRTCWrapper>();
    rtcWrapper_->setSignalingTransport(transport);

    mjpegWsWrapper_ = std::make_unique<MjpegWebSocketWrapper>();
    mjpegWsWrapper_->setSignalingTransport(transport);
}

WebSocketSignalingServer::~WebSocketSignalingServer()
{
    stop();
}

void WebSocketSignalingServer::run()
{
    try
    {
        if (logger_)
        {
            logger_->info("Starting WebSocket signaling server on port: {}", port_);
        }

        running_ = true;

        rtc::WebSocketServer::Configuration config;
        config.port = static_cast<uint16_t>(port_);

        auto& appCfg = AppConfig::instance();
        // config.enableTls = false;
        config.enableTls = appCfg.getBool("enableTls");
        if (config.enableTls) {
            config.certificatePemFile = appCfg.get("tlsCertPath");
            config.keyPemFile = appCfg.get("tlsKeyPath");
        }

        server_ = std::make_shared<rtc::WebSocketServer>(config);

        server_->onClient([this](std::shared_ptr<rtc::WebSocket> ws)
                          { onClientConnected(ws); });

        if (logger_)
        {
            logger_->info("WebSocket signaling server started on port {}", port_);
        }

        int statsCounter = 0;
        while (running_)
        {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            statsCounter++;
            if (statsCounter >= 5)
            {
                statsCounter = 0;
                size_t wsCount = 0;
                {
                    std::lock_guard<std::mutex> lock(connectionsMutex_);
                    wsCount = connections_.size();
                }
                std::cout << "[Stats] WebSocket connections: " << wsCount << std::endl;
                if (logger_)
                {
                    logger_->info("[Stats] WebSocket connections: {}", wsCount);
                }
                rtcWrapper_->logStats();
                mjpegWsWrapper_->logStats();
            }
        }

        server_->stop();
    }
    catch (const std::exception &ex)
    {
        if (logger_)
        {
            logger_->error("Error in signaling server: {}", ex.what());
        }
        else
        {
            std::cout << "Error in signaling server: " << ex.what() << std::endl;
        }
    }
}

void WebSocketSignalingServer::stop()
{
    running_ = false;

    {
        std::lock_guard<std::mutex> lock(connectionsMutex_);
        for (auto &pair : connections_)
        {
            if (pair.second.ws)
            {
                pair.second.ws->close();
            }
        }
        connections_.clear();
    }

    if (server_)
    {
        server_->stop();
    }
}

void WebSocketSignalingServer::sendMessage(const std::string &clientId, const std::string &message)
{
    try
    {
        std::shared_ptr<rtc::WebSocket> ws;
        {
            std::lock_guard<std::mutex> lock(connectionsMutex_);
            auto it = connections_.find(clientId);
            if (it != connections_.end() && it->second.ws)
            {
                ws = it->second.ws;
            }
        }

        if (ws && ws->isOpen())
        {
            ws->send(message);
        }
    }
    catch (const std::exception &ex)
    {
        if (logger_)
        {
            logger_->error("Error sending signaling message to {}: {}", clientId, ex.what());
        }
    }
}

void WebSocketSignalingServer::sendBinary(const std::string &clientId, const uint8_t *data, size_t size)
{
    try
    {
        std::shared_ptr<rtc::WebSocket> ws;
        {
            std::lock_guard<std::mutex> lock(connectionsMutex_);
            auto it = connections_.find(clientId);
            if (it != connections_.end() && it->second.ws)
            {
                ws = it->second.ws;
            }
        }

        if (ws && ws->isOpen())
        {
            rtc::binary payload(reinterpret_cast<const std::byte *>(data),
                                reinterpret_cast<const std::byte *>(data + size));
            ws->send(std::move(payload));
        }
    }
    catch (const std::exception &ex)
    {
        if (logger_)
        {
            logger_->error("Error sending binary to {}: {}", clientId, ex.what());
        }
    }
}

void WebSocketSignalingServer::closeConnection(const std::string &clientId)
{
    std::lock_guard<std::mutex> lock(connectionsMutex_);
    auto it = connections_.find(clientId);
    if (it != connections_.end())
    {
        if (it->second.ws)
        {
            it->second.ws->close();
        }
    }
}

bool WebSocketSignalingServer::isConnected(const std::string &clientId)
{
    std::lock_guard<std::mutex> lock(connectionsMutex_);
    auto it = connections_.find(clientId);
    if (it != connections_.end())
    {
        return true;
    }
    return false;
}

void WebSocketSignalingServer::onClientConnected(std::shared_ptr<rtc::WebSocket> ws)
{
    std::string clientId = "ws_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());

    if (logger_)
    {
        logger_->info("New WebSocket connection: {}", clientId);
    }


    {
        std::lock_guard<std::mutex> lock(connectionsMutex_);
        connections_[clientId] = {ws, {}};
    }

    ws->onOpen([this, clientId]()
               {
                
        if (logger_) {
            logger_->info("WebSocket fully opened for: {}", clientId);
        } });

    ws->onClosed([this, clientId]()
                 { std::thread([this, clientId]()
                               { onClosed(clientId); })
                       .detach(); });

    ws->onError([this, clientId](std::string error)
                { onError(clientId, error); });

    ws->onMessage([this, clientId](std::variant<rtc::binary, std::string> data)
                  {
        if (std::holds_alternative<std::string>(data)) {
            std::string message = std::get<std::string>(data);
            std::thread([this, clientId, message = std::move(message)]() {
                onMessage(clientId, message);
            }).detach();
        } });
}

void WebSocketSignalingServer::onMessage(const std::string &clientId, const std::string &message)
{
    try
    {
        if (logger_)
        {
            logger_->debug("Received WebSocket message from {}: {}", clientId, message);
        }

        Json::Value root;
        Json::CharReaderBuilder builder;
        std::string errs;
        std::istringstream sstream(message);

        if (!Json::parseFromStream(builder, sstream, &root, &errs))
        {
            if (logger_)
            {
                logger_->error("Invalid JSON from client {}: {}", clientId, errs);
            }
            return;
        }

        std::string type = root["type"].asString();

        if (type == "request")
        {
            Json::Value query = root["query"];
            {
                std::lock_guard<std::mutex> lock(connectionsMutex_);
                auto it = connections_.find(clientId);
                if (it != connections_.end())
                {
                    it->second.requestQuery = query;
                }
            }

            sendMessage(clientId, buildJsonMessage("status", "Connecting Player Server"));
            Json::Value streamInfo = PlayerServerClient::resolveStreamUrl(query);
            std::string url = streamInfo.get("url", "").asString();

            if (url.empty() ||
                boost::starts_with(url, "Player_Server_Not_Connected") ||
                boost::starts_with(url, "URL_Server_Not_Connected"))
            {
                if (logger_) logger_->error("Failed to get URL for client {}: {}", clientId, url);
                sendMessage(clientId, buildJsonMessage("error",
                    url.empty() ? "Failed to resolve stream URL" : url));
                return;
            }

            sendMessage(clientId, buildJsonMessage("status", "fetching codec Information"));
            auto probe = FFmpegWrapper::probeCodec(url);

            if (!probe.opened)
            {
                if (logger_) logger_->error("Unable to locate video for client {}: {}", clientId, url);
                sendMessage(clientId, buildJsonMessage("error", "Unable to locate video: " + url));
                return;
            }
            if (probe.codecId == AV_CODEC_ID_NONE)
            {
                if (logger_) logger_->error("Unable to fetch codec for client {}: {}", clientId, url);
                sendMessage(clientId, buildJsonMessage("error", "Unable to fetch codec from video: " + url));
                return;
            }

            if (logger_)
            {
                logger_->info("Probed codec for client {}: {}", clientId, avcodec_get_name(probe.codecId));
            }

            // Reject unsupported codecs first so we don't announce a codec we can't actually serve.
            if (probe.codecId != AV_CODEC_ID_H264 &&
                probe.codecId != AV_CODEC_ID_H265 &&
                probe.codecId != AV_CODEC_ID_MJPEG)
            {
                if (logger_)
                {
                    logger_->error("Unsupported codec for client {}: {}",
                                   clientId, avcodec_get_name(probe.codecId));
                }
                sendMessage(clientId, buildJsonMessage("error",
                    std::string("Unsupported codec: ") + avcodec_get_name(probe.codecId)));
                return;
            }

            // Announce the codec to the client so it can construct the right
            // media element (img for MJPEG, video for H.264/H.265) before any
            // SDP offer or JPEG frame arrives.
            {
                Json::Value codecMsg;
                codecMsg["type"] = "codec";
                codecMsg["codec"] = avcodec_get_name(probe.codecId);
                Json::StreamWriterBuilder w;
                sendMessage(clientId, Json::writeString(w, codecMsg));
            }

            if (probe.codecId == AV_CODEC_ID_H264 || probe.codecId == AV_CODEC_ID_H265)
            {
                sendMessage(clientId, buildJsonMessage("status", "creating peer connection"));
                rtcWrapper_->startStream(clientId, url, probe.codecId, streamInfo);
            }
            else // AV_CODEC_ID_MJPEG
            {
                sendMessage(clientId, buildJsonMessage("status", "starting MJPEG stream"));
                Json::Value enriched = streamInfo;
                enriched["cameraId"] = query.get("cameraId", "").asString();
                enriched["connectionMode"] = query.get("connectionMode", "tcp").asString();
                mjpegWsWrapper_->startStream(clientId, url, enriched);
            }
        }
        else if (type == "answer")
        {
            std::string sdp = root["sdp"].asString();
            rtcWrapper_->handleAnswer(clientId, sdp);
        }
        else if (type == "candidate" || type == "ice")
        {
            std::string candidate = root["candidate"].asString();
            std::string sdpMid = root.get("sdpMid", "").asString();
            int sdpMLineIndex = root.get("sdpMLineIndex", 0).asInt();
            rtcWrapper_->handleIceCandidate(clientId, candidate, sdpMid, sdpMLineIndex);
        }
        else
        {
            if (logger_)
            {
                logger_->warn("Unknown message type from client {}: {}", clientId, type);
            }
        }
    }
    catch (const std::exception &ex)
    {
        if (logger_)
        {
            logger_->error("Error processing WebSocket message from {}: {}", clientId, ex.what());
        }
    }
}

void WebSocketSignalingServer::onClosed(const std::string &clientId)
{
    if (logger_)
    {
        logger_->info("WebSocket closed for client: {}", clientId);
    }

    {
        std::lock_guard<std::mutex> lock(connectionsMutex_);
        connections_.erase(clientId);
    }

    rtcWrapper_->closePeerConnectionIfNotConnected(clientId);
    mjpegWsWrapper_->removeClient(clientId);
}

void WebSocketSignalingServer::onError(const std::string &clientId, const std::string &error)
{
    if (logger_)
    {
        logger_->error("WebSocket error for client {}: {}", clientId, error);
    }
}

Json::Value WebSocketSignalingServer::getClientQuery(const std::string &clientId)
{
    std::lock_guard<std::mutex> lock(connectionsMutex_);
    auto it = connections_.find(clientId);
    if (it != connections_.end())
    {
        return it->second.requestQuery;
    }
    throw std::runtime_error("No query found for client: " + clientId);
}
