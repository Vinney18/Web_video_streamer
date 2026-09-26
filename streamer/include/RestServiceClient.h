#pragma once

#include <string>
#include <memory>
#include <atomic>
#include <json/json.h>
#include <cpr/cpr.h>
#include <spdlog/spdlog.h>

class RestServiceClient {
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
    static std::string get(const std::string& path, const cpr::Parameters& params, long& status);
    static std::string localDateTime(int epochSeconds);
    static std::string replaceLoopbackHost(std::string url);

    static std::string serverIp_;
    static int serverPort_;
    static std::shared_ptr<spdlog::logger> logger_;
    static std::atomic<int> count;
};
