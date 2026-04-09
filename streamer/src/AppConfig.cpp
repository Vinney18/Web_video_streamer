#include "AppConfig.h"
#include "Options.h"
#include "common.h"
#include <boost/filesystem.hpp>

AppConfig& AppConfig::instance() {
    static AppConfig config;
    return config;
}

void AppConfig::load(const std::string& configFilePath) {
    Options configoptions;

    if (boost::filesystem::exists(configFilePath)) {
        configoptions.readFile(configFilePath);

        config_["websocket_server_port"] = std::to_string(configoptions.get<int>("websocket_server_port", static_cast<int>(i2v::WEBSOCKET_SERVER_PORT)));
        config_["playerServerIp"] = configoptions.get<std::string>("playerServerIp", i2v::PLAYER_SERVER_IP);
        config_["playerServerPort"] = std::to_string(configoptions.get<int>("playerServerPort", static_cast<int>(i2v::PLAYER_SERVER_PORT)));
        config_["isVMS"] = configoptions.get<bool>("isVMS", false) ? "true" : "false";
        config_["vmsStreamUserName"] = configoptions.get<std::string>("vmsStreamUserName", i2v::VMS_STREAM_USERNAME);
        config_["vmsStreamPassword"] = configoptions.get<std::string>("vmsStreamPassword", i2v::VMS_STREAM_PASSWORD);

        config_["enableTls"] = configoptions.get<bool>("enableTls", false) ? "true" : "false";
        config_["tlsCertPath"] = configoptions.get<std::string>("tlsCertPath", "");
        config_["tlsKeyPath"] = configoptions.get<std::string>("tlsKeyPath", "");

        int level = configoptions.get<int>("logLevel", -1);
        if (level < 0 || level > 6) {
            level = static_cast<int>(spdlog::level::info);
        }
        config_["logLevel"] = std::to_string(level);
    } else {
        // Write default config file
        config_["websocket_server_port"] = std::to_string(i2v::WEBSOCKET_SERVER_PORT);
        config_["playerServerIp"] = i2v::PLAYER_SERVER_IP;
        config_["playerServerPort"] = std::to_string(i2v::PLAYER_SERVER_PORT);
        config_["logLevel"] = std::to_string(static_cast<int>(spdlog::level::info));
        config_["isVMS"] = "false";
        config_["vmsStreamUserName"] = i2v::VMS_STREAM_USERNAME;
        config_["vmsStreamPassword"] = i2v::VMS_STREAM_PASSWORD;
        config_["enableTls"] = "false";
        config_["tlsCertPath"] = "";
        config_["tlsKeyPath"] = "";

        configoptions.add("websocket_server_port", static_cast<int>(i2v::WEBSOCKET_SERVER_PORT));
        configoptions.add("playerServerIp", i2v::PLAYER_SERVER_IP);
        configoptions.add("playerServerPort", static_cast<int>(i2v::PLAYER_SERVER_PORT));
        configoptions.add("logLevel", static_cast<int>(spdlog::level::info));
        configoptions.add("isVMS", false);
        configoptions.add("vmsStreamUserName", i2v::VMS_STREAM_USERNAME);
        configoptions.add("vmsStreamPassword", i2v::VMS_STREAM_PASSWORD);
        configoptions.add("enableTls", false);
        configoptions.add("tlsCertPath", std::string(""));
        configoptions.add("tlsKeyPath", std::string(""));
        configoptions.writeFile(configFilePath, true);
    }
}

std::string AppConfig::get(const std::string& key) const {
    auto it = config_.find(key);
    if (it != config_.end()) {
        return it->second;
    }
    return "";
}

int AppConfig::getInt(const std::string& key) const {
    auto it = config_.find(key);
    if (it != config_.end()) {
        return std::stoi(it->second);
    }
    return 0;
}

bool AppConfig::getBool(const std::string& key) const {
    auto it = config_.find(key);
    if (it != config_.end()) {
        return it->second == "true";
    }
    return false;
}

void AppConfig::setLogger(std::shared_ptr<spdlog::logger> logger) {
    logger_ = std::move(logger);
}
