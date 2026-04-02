#pragma once

#include <string>
#include <memory>
#include <json/json.h>
#include <spdlog/spdlog.h>

class PlayerServerClient {
public:
    static void init();

    static Json::Value resolveStreamUrl(const Json::Value& query);
    static std::string GetLiveUrl(const std::string& cameraId, int streamType,
                                  const std::string& analyticType,
                                  const std::string& vaServerId,
                                  const std::string& vaServerPipeId);
    static std::string GetPlayBackUrl(const std::string& cameraId, int startTime,
                                      int* seekTime, float* duration = nullptr);
    static std::string GetPlayBackUrl(const std::string& cameraId, int startTime, int endTime);

private:
    static std::string addCredentialsToUrl(const std::string& url,
                                           const std::string& username,
                                           const std::string& password);

    static std::string serverIp_;
    static int serverPort_;
    static std::shared_ptr<spdlog::logger> logger_;
    static bool isVMS_;
    static int count;
    static std::string vmsUser_;
    static std::string vmsPassword_;
};
