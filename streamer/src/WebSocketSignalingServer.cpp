#include "WebSocketSignalingServer.h"
#include "WebRTCWrapper.h"
#include "json/json.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <sstream>

WebSocketSignalingServer::WebSocketSignalingServer(int port, const std::string& playerIp, int playerPort,
                                                   std::shared_ptr<spdlog::logger> logger, bool isVMS,
                                                   const std::string& vmsUser, const std::string& vmsPassword)
    : port_(port), logger_(logger)
{
    rtcWrapper_ = std::make_unique<WebRTCWrapper>(playerIp, playerPort, logger, isVMS, vmsUser, vmsPassword);
    rtcWrapper_->setSignalingTransport({
        .sendMessage = [this](const std::string& id, const std::string& msg) { sendMessage(id, msg); },
        .closeConnection = [this](const std::string& id) { closeConnection(id); }
    });
}

WebSocketSignalingServer::~WebSocketSignalingServer()
{
    stop();
}

void WebSocketSignalingServer::run()
{
    try
    {
        std::cout << "Starting WebSocket signaling server on port: " << port_ << std::endl;
        if (logger_)
        {
            logger_->info("Starting WebSocket signaling server on port: {}", port_);
        }

        running_ = true;

        rtc::WebSocketServer::Configuration config;
        config.port = static_cast<uint16_t>(port_);
        config.enableTls = false;

        server_ = std::make_shared<rtc::WebSocketServer>(config);

        server_->onClient([this](std::shared_ptr<rtc::WebSocket> ws)
                          { onClientConnected(ws); });

        if (logger_)
        {
            logger_->info("WebSocket signaling server started on port {}", port_);
        }

        while (running_)
        {
            std::this_thread::sleep_for(std::chrono::seconds(1));
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
            if (pair.second)
            {
                pair.second->close();
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
            if (it != connections_.end() && it->second)
            {
                ws = it->second;
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
        if (it->second)
        {
            std::cout << "Closing WebSocket for client: " << clientId << std::endl;
            it->second->close();
            std::cout << "Closed WebSocket for client: " << clientId << std::endl;
        }
    }
}

void WebSocketSignalingServer::onClientConnected(std::shared_ptr<rtc::WebSocket> ws)
{
    std::string clientId = "ws_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());

    if (logger_)
    {
        logger_->info("New WebSocket connection: {}", clientId);
    }

    std::cout << "New WebSocket connection: " << clientId << std::endl;

    {
        std::lock_guard<std::mutex> lock(connectionsMutex_);
        connections_[clientId] = ws;
    }

    ws->onOpen([this, clientId]()
               {
        if (logger_) {
            logger_->info("WebSocket fully opened for: {}", clientId);
        } });

    ws->onClosed([this, clientId]()
                 { onClosed(clientId); });

    ws->onError([this, clientId](std::string error)
                { onError(clientId, error); });

    ws->onMessage([this, clientId](std::variant<rtc::binary, std::string> data)
                  {
        if (std::holds_alternative<std::string>(data)) {
            onMessage(clientId, std::get<std::string>(data));
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
            std::string query = root.get("query", "").asString();
            rtcWrapper_->handleRequest(clientId, query);
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
    std::cout << "WebSocket closed for client: " << clientId << std::endl;
    if (logger_)
    {
        logger_->info("WebSocket closed for client: {}", clientId);
    }

    {
        std::lock_guard<std::mutex> lock(connectionsMutex_);
        connections_.erase(clientId);
    }
}

void WebSocketSignalingServer::onError(const std::string &clientId, const std::string &error)
{
    if (logger_)
    {
        logger_->error("WebSocket error for client {}: {}", clientId, error);
    }
}
