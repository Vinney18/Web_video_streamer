#include "AppConfig.h"
#include "json.hpp"
#include "common.h"
#include <boost/filesystem.hpp>
#include <fstream>
#include <iomanip>
#include <iostream>

using json = nlohmann::json;

AppConfig& AppConfig::instance() {
    static AppConfig config;
    return config;
}

namespace {

// Default config, also written to disk when no config file exists.
json defaultConfig() {
    return {
        {"logLevel", static_cast<int>(spdlog::level::info)},
        {"websocketServer", {
            {"port", static_cast<int>(nmetics::WEBSOCKET_SERVER_PORT)}
        }},
        {"restServer", {
            {"ip", nmetics::REST_SERVER_IP},
            {"port", static_cast<int>(nmetics::REST_SERVER_PORT)}
        }},
        {"tls", {
            {"enabled", false},
            {"certPath", ""},
            {"keyPath", ""}
        }},
        {"turnServer", {
            {"enabled", false},
            {"url", ""},
            {"username", ""},
            {"password", ""}
        }}
    };
}

// Overlays user values onto defaults, section by section; keys missing from the user file keep their default.
void mergeInto(json& base, const json& overlay) {
    for (auto it = overlay.begin(); it != overlay.end(); ++it) {
        if (it.value().is_object() && base.count(it.key()) && base[it.key()].is_object()) {
            mergeInto(base[it.key()], it.value());
        } else {
            base[it.key()] = it.value();
        }
    }
}

// Flattens nested sections into dotted keys, e.g. {"tls":{"enabled":true}} -> "tls.enabled" = "true".
void flatten(const json& node, const std::string& prefix, std::map<std::string, std::string>& out) {
    for (auto it = node.begin(); it != node.end(); ++it) {
        std::string key = prefix.empty() ? it.key() : prefix + "." + it.key();
        const json& value = it.value();
        if (value.is_object()) {
            flatten(value, key, out);
        } else if (value.is_string()) {
            out[key] = value.get<std::string>();
        } else if (value.is_boolean()) {
            out[key] = value.get<bool>() ? "true" : "false";
        } else if (value.is_number_integer()) {
            out[key] = std::to_string(value.get<long long>());
        } else if (!value.is_null()) {
            out[key] = value.dump();
        }
    }
}

} // namespace

void AppConfig::load(const std::string& configFilePath) {
    json config = defaultConfig();

    if (boost::filesystem::exists(configFilePath)) {
        try {
            std::ifstream file(configFilePath);
            json userConfig;
            file >> userConfig;
            if (userConfig.is_object()) {
                mergeInto(config, userConfig);
            }
        } catch (const std::exception& ex) {
            std::cout << "[Config] Failed to parse config file, using defaults: path=" << configFilePath
                      << ", error=" << ex.what() << std::endl;
        }
    } else {
        try {
            std::ofstream file(configFilePath);
            file << std::setw(2) << config << std::endl;
        } catch (const std::exception& ex) {
            std::cout << "[Config] Failed to write default config file: path=" << configFilePath
                      << ", error=" << ex.what() << std::endl;
        }
    }

    config_.clear();
    flatten(config, "", config_);

    int level = -1;
    try { level = std::stoi(config_["logLevel"]); } catch (const std::exception&) {}
    if (level < 0 || level > 6) {
        level = static_cast<int>(spdlog::level::info);
    }
    config_["logLevel"] = std::to_string(level);
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
