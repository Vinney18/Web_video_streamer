#include "RestServiceClient.h"
#include "AppConfig.h"
#include <cpr/cpr.h>
#include <json/json.h>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/regex.hpp>
#include <boost/regex.hpp>
#include <algorithm>
#include <vector>
#include <iostream>
#include <ctime>

using std::string;

// Static member definitions
std::string RestServiceClient::serverIp_;
int RestServiceClient::serverPort_ = 0;
std::shared_ptr<spdlog::logger> RestServiceClient::logger_;
std::atomic<int> RestServiceClient::count{0};

void RestServiceClient::init()
{
    auto& cfg = AppConfig::instance();
    serverIp_ = cfg.get("restServer.ip");
    serverPort_ = cfg.getInt("restServer.port");
    logger_ = cfg.logger();
}

// Performs a GET against the RestService. Throws if the server is unreachable.
std::string RestServiceClient::get(const std::string &path, const cpr::Parameters &params, long &status)
{
    std::string url = "http://" + serverIp_ + ":" + std::to_string(serverPort_) + path;
    // Bounded timeouts: callers run on per-message detached threads, so a hung RestService must not pin them.
    auto res = cpr::Get(cpr::Url{url}, params, cpr::ConnectTimeout{3000}, cpr::Timeout{10000});
    if (res.error)
    {
        throw std::runtime_error("RestService not reachable at " + url + ": " + res.error.message);
    }
    status = res.status_code;
    return res.text;
}

// RestService expects playback time as local "MM/dd/yyyy, hh:mm:ss tt".
std::string RestServiceClient::localDateTime(int epochSeconds)
{
    std::time_t t = epochSeconds;
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%m/%d/%Y, %I:%M:%S %p", &tm);
    return buf;
}

// RestService may return URLs pointing at its own loopback; make them reachable from the streamer.
std::string RestServiceClient::replaceLoopbackHost(std::string url)
{
    if (serverIp_ == "127.0.0.1" || serverIp_ == "localhost")
    {
        return url;
    }
    boost::replace_all(url, "127.0.0.1", serverIp_);
    boost::replace_all(url, "localhost", serverIp_);
    return url;
}

Json::Value RestServiceClient::resolveStreamUrl(const Json::Value &query)
{
    count++;
    std::string cameraId = query.get("cameraId", "").asString();
    std::string mode = query.get("mode", "Live").asString();
    int streamtype = query.get("streamType", 0).asInt();
    int start_time_ofplaybackfile = query.get("startTime", 0).asInt();
    int end_time_ofplaybackfile = query.get("endTime", 0).asInt();
    std::string analyticType = query.get("analyticType", "").asString();
    std::string vaServerId = query.get("vaServerId", "").asString();
    std::string vaServerPipeId = query.get("vaServerPipeId", "").asString();

    Json::Value result;

    if (mode == "Live")
    {
        result["url"] = GetLiveUrl(cameraId, streamtype, analyticType, vaServerId, vaServerPipeId);
    }
    else
    {
        int seekTime = 0;
        float duration = 0;
        if (end_time_ofplaybackfile == 0)
        {
            result["url"] = GetPlayBackUrl(cameraId, start_time_ofplaybackfile, &seekTime, &duration);
            result["seekTime"] = seekTime;
            result["durationMinutes"] = duration;
            // result["url"] = "rtsp://admin:Prama123@192.168.2.248/Streaming/Channels/301";
        }
        else
        {
            result["url"] = GetPlayBackUrl(cameraId, start_time_ofplaybackfile, end_time_ofplaybackfile);
        }
    }

    // std::cout<< "Resolved stream URL for cameraId: " << cameraId << ", mode: " << mode << ", url: " << result["url"].asString() << std::endl;
    return result;


}

std::string RestServiceClient::GetLiveUrl(const std::string &cameraId, int streamtype,
                                           const std::string &analyticType, const std::string &vaServerId,
                                           const std::string &vaServerPipeId)
{
    string response;

    if (cameraId == "")
    {
        return response;
    }

    std::string path = "/RestService/server/LiveUrl";
    cpr::Parameters params{{"cameraId", cameraId}, {"streamType", std::to_string(streamtype)}, {"analyticType", analyticType}};
    if (vaServerId != "" || vaServerPipeId != "")
    {
        path = "/RestService/server/LiveVaUrl";
        params.Add({"vaServerId", vaServerId});
        params.Add({"vaServerPipeId", vaServerPipeId});
    }

    try
    {
        long status = 0;
        string command = get(path, params, status);

        if (status == 200)
        {
            command.erase(std::remove(command.begin(), command.end(), '\"'), command.end());
            command.erase(std::remove(command.begin(), command.end(), '\\'), command.end());
            response = replaceLoopbackHost(command);
        }
        else
        {
            if (logger_)
            {
                logger_->error("[RestClient] Live URL request returned non-OK status: status={}", status);
            }
        }
    }
    catch (const std::exception &ex)
    {
        if (logger_)
        {
            logger_->error("[RestClient] Live URL request failed: {}", ex.what());
        }
        response = "URL_Server_Not_Connected";
    }
    return response;
}

std::string RestServiceClient::GetPlayBackUrl(const std::string &cameraId, int startTime,
                                               int *seekTime, float *duration)
{
    string response;
    string cameraId_instring = cameraId;

    return "/webwork/cial.ts";
    if (cameraId == "")
    {
        return response;
    }

    try
    {
        long status = 0;
        string json = get("/RestService/server/EventPlaybackUrl",
                          cpr::Parameters{{"cameraId", cameraId_instring},
                                          {"eventDateTime", localDateTime(startTime)},
                                          {"streamviaapache", "false"}},
                          status);

        if (status == 200)
        {
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

                auto x=resultValue.asString();
                auto y= resultValue1.asString();
                std::cout << "[RestClient] Playback URL resolved: url=" << x << ", seekTime=" << y << std::endl;
                if (duration != nullptr)
                {
                    *duration = duration_in_Minutes_value;
                }

                response = resultValue.asString();
                response.erase(std::remove(response.begin(), response.end(), '\"'), response.end());
                response.erase(std::remove(response.begin(), response.end(), '\\'), response.end());
                response = replaceLoopbackHost(response);
            }
        }
        else
        {
            if (logger_)
            {
                logger_->error("[RestClient] Playback URL request returned non-OK status: status={}", status);
            }
        }
    }
    catch (const std::runtime_error &ex)
    {
        if (logger_)
        {
            logger_->error("[RestClient] Playback URL request failed: {}", ex.what());
        }
        response = "URL_Server_Not_Connected";
    }
    catch (const std::exception &ex)
    {
        if (logger_)
        {
            logger_->error("[RestClient] Playback URL request failed: {}", ex.what());
        }
        response = "";
    }
    return response;
}

std::string RestServiceClient::GetPlayBackUrl(const std::string &cameraId, int startTime, int endTime)
{
    string response;

    if (cameraId == "")
    {
        return response;
    }

    try
    {
        long status = 0;
        string json = get("/RestService/server/GetExportVideoUrl",
                          cpr::Parameters{{"cameraId", cameraId},
                                          {"startTime", std::to_string(startTime)},
                                          {"endTime", std::to_string(endTime)},
                                          {"fileFormat", "ts"}},
                          status);

        if (status == 200)
        {
            Json::Reader reader;
            Json::Value root;
            bool parseSuccess = reader.parse(json, root, false);

            if (parseSuccess)
            {
                if (root.isMember("IsSuccess") && !root["IsSuccess"].asBool())
                {
                    if (logger_)
                    {
                        logger_->error("[RestClient] Export URL request rejected by server: {}", root.get("ErrorMessage", "").asString());
                    }
                    return response;
                }

                response = root["ExportedVideoUrl"].asString();
                response.erase(std::remove(response.begin(), response.end(), '\"'), response.end());
                response.erase(std::remove(response.begin(), response.end(), '\\'), response.end());
                response = replaceLoopbackHost(response);
            }
        }
        else
        {
            if (logger_)
            {
                logger_->error("[RestClient] Export URL request returned non-OK status: status={}", status);
            }
        }
    }
    catch (const std::runtime_error &ex)
    {
        if (logger_)
        {
            logger_->error("[RestClient] Export URL request failed: {}", ex.what());
        }
        response = "URL_Server_Not_Connected";
    }
    catch (const std::exception &ex)
    {
        if (logger_)
        {
            logger_->error("[RestClient] Export URL request failed: {}", ex.what());
        }
        response = "";
    }
    return response;
}
