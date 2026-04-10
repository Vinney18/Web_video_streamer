#include "SignalingServers/WebSocketSignalingServer.h"
#include "WebRTCWrapper.h"
#include "AppConfig.h"
#include "json/json.h"
#include <iostream>
#include <atomic>
#include <chrono>
#include <thread>
#include <sstream>

WebSocketSignalingServer::WebSocketSignalingServer(int port, std::shared_ptr<spdlog::logger> logger)
    : port_(port), logger_(logger)
{
    rtcWrapper_ = std::make_unique<WebRTCWrapper>();
    rtcWrapper_->setSignalingTransport({.sendMessage = [this](const std::string &id, const std::string &msg)
                                        { sendMessage(id, msg); },
                                        .closeConnection = [this](const std::string &id)
                                        { closeConnection(id); },
                                        .isConnected = [this](const std::string &id)
                                        { return isConnected(id); },
                                        .getQuery = [this](const std::string &id)
                                        { return getClientQuery(id); }});
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
            rtcWrapper_->handleRequest(clientId);
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
