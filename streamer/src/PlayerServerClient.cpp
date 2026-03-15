#include "PlayerServerClient.h"
#include "AppConfig.h"
#include <cpr/cpr.h>
#include <json/json.h>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/regex.hpp>
#include <boost/regex.hpp>
#include <regex>
#include <algorithm>
#include <vector>
#include <iostream>

using std::string;

// Static member definitions
std::string PlayerServerClient::serverIp_;
int PlayerServerClient::serverPort_ = 0;
std::shared_ptr<spdlog::logger> PlayerServerClient::logger_;
bool PlayerServerClient::isVMS_ = false;
int PlayerServerClient::count = 0;
std::string PlayerServerClient::vmsUser_;
std::string PlayerServerClient::vmsPassword_;

void PlayerServerClient::init()
{
    auto& cfg = AppConfig::instance();
    serverIp_ = cfg.get("playerServerIp");
    serverPort_ = cfg.getInt("playerServerPort");
    logger_ = cfg.logger();
    isVMS_ = cfg.getBool("isVMS");
    vmsUser_ = cfg.get("vmsStreamUserName");
    vmsPassword_ = cfg.get("vmsStreamPassword");
}

std::string PlayerServerClient::resolveStreamUrl(const std::string &query)
{
    count++;
    std::string cameraId;
    std::string mode = "Live";
    int streamtype = 0;
    int start_time_ofplaybackfile = 0;
    int end_time_ofplaybackfile = 0;
    std::string analyticType = "";
    std::string vaServerId = "";
    std::string vaServerPipeId = "";

    // Parse query parameters
    std::vector<std::string> props;
    boost::algorithm::split_regex(props, query, boost::regex("&&"));

    for (auto const &prop : props)
    {
        std::vector<std::string> keyValue;
        boost::algorithm::split_regex(keyValue, prop, boost::regex("~~"));
        if (keyValue.size() < 2)
            continue;

        auto &key = keyValue[0];
        auto &value = keyValue[1];

        if (key == "cameraId")
            cameraId = value;
        else if (key == "mode")
        {
            mode=value;
            // if (count % 2 == 0)
            // {
            //     mode = value;
            // }
            // else
            // {
            //     mode = "Live";
            // }
        }
        else if (key == "streamType" || key == "streamtype")
            streamtype = std::stoi(value);
        else if (key == "startTime")
            start_time_ofplaybackfile = std::stoi(value);
        else if (key == "endTime")
            end_time_ofplaybackfile = std::stoi(value);
        else if (key == "analyticType")
            analyticType = value;
        else if (key == "vaServerId" && !value.empty())
            vaServerId = value;
        else if (key == "vaServerPipeId" && !value.empty())
            vaServerPipeId = value;
    }

    // Get URL based on mode
    // if (mode == "Live")
    // {
    //     return GetLiveUrl(cameraId, streamtype, analyticType, vaServerId, vaServerPipeId);
    // }
    // else
    // {
    //     int seekTime = 0;
    //     float duration = 0;
    //     if (end_time_ofplaybackfile == 0)
    //     {
    //         return GetPlayBackUrl(cameraId, start_time_ofplaybackfile, &seekTime, &duration);
    //     }
    //     else
    //     {
    //         return GetPlayBackUrl(cameraId, start_time_ofplaybackfile, end_time_ofplaybackfile);
    //     }
    // }

    // if (count % 2 == 0)
    // {
    //     return "/webwork/12-12-31.ts"; // For testing only, remove this line in production
    // }
    // else
    // {
    //     return "rtsp://localhost:554/cial.ts"; // For testing only, remove this line in production
    // }
    return "rtsp://localhost:554/cial.ts";
}

std::string PlayerServerClient::GetLiveUrl(const std::string &cameraId, int streamtype,
                                           const std::string &analyticType, const std::string &vaServerId,
                                           const std::string &vaServerPipeId)
{
    string response;
    string cameraId_instring = cameraId;
    std::string endpoint = "";

    if (vaServerId != "" || vaServerPipeId != "")
    {
        endpoint = "/url/GetLiveVaUrl?cameraId=" + cameraId_instring + "&streamType=" + std::to_string(streamtype) +
                   "&analyticType=" + analyticType + "&vaServerId=" + vaServerId + "&vaServerPipeId=" + vaServerPipeId;
    }
    else
    {
        endpoint = "/url/GetLiveUrl?cameraId=" + cameraId_instring + "&streamType=" + std::to_string(streamtype) +
                   "&analyticType=" + analyticType;
    }

    if (cameraId == "")
    {
        return response;
    }

    try
    {
        std::string url = "http://" + serverIp_ + ":" + std::to_string(serverPort_) + endpoint;
        auto res = cpr::Get(cpr::Url{url});

        if (res.status_code == 200)
        {
            string command = res.text;
            command.erase(std::remove(command.begin(), command.end(), '\"'), command.end());
            command.erase(std::remove(command.begin(), command.end(), '\\'), command.end());
            if (isVMS_)
            {
                command = addCredentialsToUrl(command, vmsUser_, vmsPassword_);
            }
            response = command;
        }
        else if (res.status_code == 403)
        {
            if (logger_)
            {
                logger_->error("GetLiveUrl Server License Expired");
            }
        }
        else if (res.status_code == 400)
        {
            if (logger_)
            {
                logger_->error("GetLiveUrl Some Error occurred status code: {}", res.status_code);
            }
        }
        else
        {
            if (logger_)
            {
                logger_->error("GetLiveUrl Some Error occurred status code: {}", res.status_code);
            }
            response = "Player_Server_Not_Connected";
        }
    }
    catch (const std::exception &ex)
    {
        if (logger_)
        {
            logger_->error("Error in GetLiveUrl: {}", ex.what());
        }
    }
    return response;
}

std::string PlayerServerClient::GetPlayBackUrl(const std::string &cameraId, int startTime,
                                               int *seekTime, float *duration)
{
    string response;
    string cameraId_instring = cameraId;

    std::string endpoint = "/url/GetPlaybackUrl?cameraId=" + cameraId_instring + "&time=" + std::to_string(startTime);
    if (cameraId == "")
    {
        return response;
    }

    try
    {
        std::string url = "http://" + serverIp_ + ":" + std::to_string(serverPort_) + endpoint;
        auto res = cpr::Get(cpr::Url{url});

        if (res.status_code == 200)
        {
            if (res.text == "URL_Server_Not_Connected")
            {
                return "URL_Server_Not_Connected";
            }

            string json = res.text;
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

                if (duration != nullptr)
                {
                    *duration = duration_in_Minutes_value;
                }

                response = resultValue.asString();
                response.erase(std::remove(response.begin(), response.end(), '\"'), response.end());
                response.erase(std::remove(response.begin(), response.end(), '\\'), response.end());
            }
        }
        else
        {
            response = "Player_Server_Not_Connected";
        }
    }
    catch (const std::exception &ex)
    {
        if (logger_)
        {
            logger_->error("Error in GetPlayBackUrl: {}", ex.what());
        }
        response = "";
    }
    return response;
}

std::string PlayerServerClient::GetPlayBackUrl(const std::string &cameraId, int startTime, int endTime)
{
    string response;
    string cameraId_instring = cameraId;

    std::string endpoint = "/url/GetExportUrl?cameraId=" + cameraId_instring + "&startTime=" +
                           std::to_string(startTime) + "&endTime=" + std::to_string(endTime);
    if (cameraId == "")
    {
        return response;
    }

    try
    {
        std::string url = "http://" + serverIp_ + ":" + std::to_string(serverPort_) + endpoint;
        auto res = cpr::Get(cpr::Url{url});

        if (res.status_code == 200)
        {
            if (res.text == "URL_Server_Not_Connected")
            {
                return "URL_Server_Not_Connected";
            }

            string json = res.text;
            Json::Reader reader;
            Json::Value root;
            bool parseSuccess = reader.parse(json, root, false);

            if (parseSuccess)
            {
                Json::Value resultValue = root["ExportedVideoUrl"];
                response = resultValue.asString();
                response.erase(std::remove(response.begin(), response.end(), '\"'), response.end());
                response.erase(std::remove(response.begin(), response.end(), '\\'), response.end());
            }
        }
        else
        {
            response = "Player_Server_Not_Connected";
        }
    }
    catch (const std::exception &ex)
    {
        if (logger_)
        {
            logger_->error("Error in GetPlayBackUrl: {}", ex.what());
        }
        response = "";
    }
    return response;
}

std::string PlayerServerClient::addCredentialsToUrl(const std::string &url, const std::string &username, const std::string &password)
{
    std::regex credentialsRegex(R"([^:]+:[^@]+@)");

    if (std::regex_search(url, credentialsRegex))
    {
        return url;
    }
    else
    {
        size_t prefixPos = url.find("://");
        if (prefixPos != std::string::npos)
        {
            std::string credentials = username + ":" + password + "@";
            std::string newUrl = url.substr(0, prefixPos + 3) + credentials + url.substr(prefixPos + 3);
            return newUrl;
        }
        else
        {
            throw std::invalid_argument("Invalid/Unexpected URL: " + url);
        }
    }
}
