#pragma once

#include <string>
#include <map>
#include <memory>
#include <spdlog/spdlog.h>

class AppConfig {
public:
    static AppConfig& instance();

    void load(const std::string& configFilePath);

    // Generic key-value access
    std::string get(const std::string& key) const;
    int getInt(const std::string& key) const;
    bool getBool(const std::string& key) const;

    // Logger (not a simple config value, kept separate)
    void setLogger(std::shared_ptr<spdlog::logger> logger);
    std::shared_ptr<spdlog::logger> logger() const { return logger_; }

    AppConfig(const AppConfig&) = delete;
    AppConfig& operator=(const AppConfig&) = delete;

private:
    AppConfig() = default;

    std::map<std::string, std::string> config_;
    std::shared_ptr<spdlog::logger> logger_;
};
