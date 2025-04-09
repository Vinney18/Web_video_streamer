#pragma once

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <websocketpp/endpoint.hpp>
#include <string>
#include <map>
#include <mutex>
#include <spdlog/spdlog.h>
#include "FFmpegWrapper.h"
#include "common.h"

using websocketpp::connection_hdl;

class WebSocketWrapper {
public:
        WebSocketWrapper(int port, const std::string& playerIp, int playerPort, std::shared_ptr<spdlog::logger> logger, bool isVMS, const std::string& vmsUser, const std::string& vmsPassword);

    void run();
    void on_open(connection_hdl hdl);
    void on_close(connection_hdl hdl);
    void on_message(connection_hdl hdl, websocketpp::server<websocketpp::config::asio>::message_ptr msg);
    void SendData(connection_hdl& con_hndl, std::vector<uint8_t>& data, int64_t timestamp);
    void SendStringData(connection_hdl& con_hndl, std::string sdata);
    std::atomic<size_t>& getPendingOperationsCounter() { return pendingOperations; }
    size_t getMaxPendingOperations() const {
        return MAX_PENDING_OPERATIONS;
    }


private:
    int generateAndCheckRandomNumber();
    void process_request(connection_hdl hdl, std::string& query);
    std::string Get_LiveUrl(connection_hdl hdl, const std::string& cameraId, int streamtype, const std::string& analyticType, const std::string& vaServerId, const std::string& vaServerPipeId);
    std::string Get_PlayBackUrl(connection_hdl hdl, const std::string& cameraId, int start_time_ofplaybackfile, int* seekTime_ofFile);
    std::string Get_PlayBackUrl(connection_hdl hdl, const std::string& cameraId, int start_time_ofplaybackfile, int end_time_ofplaybackfile);

    void checkConfigFileIp(connection_hdl hdl, std::string& currentServerIp);
    void checkConfigFilePort(int currentServerPort);

    int websocket_server_port;
    std::string playerServerIp;
    int playerServerPort;
    std::shared_ptr<spdlog::logger> mainLogger;

    websocketpp::server<websocketpp::config::asio> websocket_server;
    std::map<std::string, std::shared_ptr<FFmpegWrapper>> ffmpegList;
    std::map<boost::asio::detail::socket_ops::shared_cancel_token_type, std::string> connectionsIdMap;

    std::mutex ffmpegListMutex;
    std::mutex connectionsIdMapMutex;

    bool isVMS;
    std::string vmsStreamUserName;
    std::string vmsStreamPassword;

    std::atomic<size_t> pendingOperations{0};
    static constexpr size_t MAX_PENDING_OPERATIONS = 30;
};
