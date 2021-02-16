//#define SPDLOG_FMT_EXTERNAL

#include <boost/algorithm/string_regex.hpp>
#include <boost/regex.hpp>
#include "Mp4frag.h"
#include "MjpegServer.h"
#include <map>
#include <set>
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <websocketpp/endpoint.hpp>
#include "FFmpegWrapper.h"
#include <cpr/cpr.h>
#include <json/value.h>
#include <fstream>
#include "fmt/ostream.h"
#include "fmt/format.h"
#include <boost/filesystem.hpp>
#include "Options.h"
#include <ctime>
#include "json/json.h"
#include "CLI11.hpp"
#include "Util.h"

#include <spdlog/spdlog.h>
#include <spdlog/async.h>

using namespace std;
using websocketpp::connection_hdl;
using websocketpp::lib::bind;

#pragma once

// pull out the type of messages sent by our config
typedef websocketpp::server<websocketpp::config::asio>::message_ptr message_ptr;
unique_ptr<i2v::MjpegServer> mjpegServer;
map<boost::asio::detail::socket_ops::shared_cancel_token_type, string> m_connectionsIdMap;
map<string, shared_ptr<FFmpegWrapper>> ffmpegList;
websocketpp::server<websocketpp::config::asio> websocket_server;

void loadMainConfig();
void setDefaultValues(Options& opt);
void updateconfigFile(connection_hdl hdl , string previousServerIp, string currentServerIp);

void on_open(connection_hdl hdl);
void on_close(connection_hdl hdl);
string Get_LiveUrl(connection_hdl hdl, int cameraId, int streamtype, string analyticType);
string Get_PlayBackUrl(connection_hdl hdl, int cameraId, int start_time_ofplaybackfile, int* seekTime_ofFile, int* sessionid, bool playbackviaapache);

void on_message(websocketpp::server<websocketpp::config::asio>* s, connection_hdl hdl, message_ptr msg);
void SendData(websocketpp::connection_hdl& con_hndl, vector<uint8_t>& data);
void SendStringData(websocketpp::connection_hdl& con_hndl, string sdata);

std::string mainConfigFile;
std::string playerServerIp = "";
int playerServerPort, mjpeg_server_port, websocket_server_port;
spdlog::level::level_enum log_level;

std::shared_ptr<spdlog::logger> mainLogger;


int main(int argc, char* argv[])
{

    CLI::App app{ "i2V streamer" };

    av_register_all();

    bool show_logs_on_console = false;
    app.add_option("-s,--show_log", show_logs_on_console, "Show logs on console");

    CLI11_PARSE(app, argc, argv)

    spdlog::init_thread_pool(8192, 4);

    string config_dir_path = i2v::Util::getConfigFolderPath();
    i2v::Util::createDirectories(config_dir_path); // log directory

    mainConfigFile = config_dir_path + "/mainConf.json";
    loadMainConfig();

    std::string mainLogFolder = i2v::Util::getLogsFolderPath();
    i2v::Util::createDirectories(mainLogFolder); // log directory
    // create logger
    std::string logFilePrefix = "log_";
    mainLogger = i2v::Util::createAsyncLoggerAndRegister(i2v::MAIN_LOGGER_NAME, mainLogFolder, logFilePrefix, show_logs_on_console, log_level);

    if (not mainLogger) { std::cout << "Unable to create logger !!!" << std::endl; }
    else { mainLogger->info("Logger created Successfully"); }

    //auto isVerified = VerifySignature("7B0D0A20202244617461223A20226D797465737464617461222C0D0A2020225369676E6174757265223A2022415678643857772B6570572F4F735255663252497156686A71346C4E5839626B2F5663464A6F2B75387358776F64596667306B4656766E483253646F614238724D5670304630494865685453643776425766676B626341364F31394A54666977694C4C4B57714F442F36654671595A33325572724D376C4E77735A614B34466A55534E4F314C636A3573573658787556577972317634453157355969524A4E385366786959657367632F593D220D0A7D");
	mjpegServer = make_unique<i2v::MjpegServer>(mjpeg_server_port);
	mjpegServer->start();
    if (mainLogger) { mainLogger->info("Started mjpeg server on port: {}", mjpeg_server_port); }

	// Create a server endpoint
	try {

        if (mainLogger) { mainLogger->info("Starting websocket server on port: {}", websocket_server_port); }
        if (mainLogger) { mainLogger->info("Player server IP is: {0} and port is: {1}", playerServerIp, playerServerPort); }

		// Set logging settings
        websocket_server.clear_access_channels(websocketpp::log::alevel::all); // disable all logs
        //websocket_server.set_access_channels(websocketpp::log::alevel::connect); // enable logging of new connections

		// Initialize Asio
		websocket_server.init_asio();

		// Register our message handler
		websocket_server.set_message_handler(bind(&on_message, &websocket_server, websocketpp::lib::placeholders::_1, websocketpp::lib::placeholders::_2));
		websocket_server.set_open_handler(&on_open);
		websocket_server.set_close_handler(&on_close);

		// Listen on port 8181
		websocket_server.listen(websocket_server_port);

		// Start the server accept loop
		websocket_server.start_accept();

		// Start the ASIO io_service run loop
		websocket_server.run();
	}
	catch (const websocketpp::exception& e) {
        if (mainLogger) { mainLogger->error("main Error in websocket server: {}", e.what()); }
        else { std::cout << e.what() << std::endl; }
	}
	catch (const std::exception& ex) {
        if (mainLogger) { mainLogger->error("main Error in websocket server: {}", ex.what()); }
        else { std::cout << ex.what() << std::endl; }
	}
	mjpegServer->stop();

    if (mainLogger) { mainLogger->info("Stopped mjpeg server"); }

}

void on_open(connection_hdl hdl) {
	try
	{
		//websocketp get_con_from_hdl();
        if (mainLogger) { mainLogger->debug("new websocket connection"); }

		string id;
		string url;
		int start_time_ofplaybackfile = 0;
		int seekTime_ofFile = 0;
		int sessionid = 0;
		bool playbackviaapache = true;
		string analyticType = "";
		bool usejmuxer = false;
		string connectionmode = "tcp";
		int cameraId;
		int streamtype = 0;
		string mode = "Live";
		websocketpp::server<websocketpp::config::asio>::connection_ptr con = websocket_server.get_con_from_hdl(hdl);
		websocketpp::uri_ptr uri = con->get_uri();
		string query = uri->get_query();
		if (!query.empty()) {
			vector<string> props;
			boost::algorithm::split_regex(props, query, boost::regex("&&"));
			for (auto const& prop : props)
			{
				vector<string> keyValue;
				boost::algorithm::split_regex(keyValue, prop, boost::regex("~~"));
				if (keyValue.size() < 2)
					continue;
				auto key = keyValue[0];
				auto value = keyValue[1];
				if (key == "id") {
					id = value;
				}
				if (key == "mode") {
					mode = value;
				}
				else if (key == "cameraId") {
					cameraId = stoi(value);
				}
				else if (key == "streamtype") {
					streamtype = stoi(value);
				}
				else if (key == "serverIp") {
					updateconfigFile(hdl, playerServerIp, value);
				}
				else if (key == "startTime") {
					int myint1 = stoi(value);
					start_time_ofplaybackfile = myint1;
				}
				else if (key == "useJmuxer")
				{
					usejmuxer = (value == "true");

				}
				else if (key == "connectionmode")
				{
					connectionmode = value;
				}
				else if (key == "playbackviaapache")
				{
					if (value == "1") {
						playbackviaapache = true;
					}
					else
					{
						playbackviaapache = false;
					}
				}
				else if (key == "analyticType")
				{
					analyticType = value;
				}
			}
		}

			if (mode == "Live") {
				//url = Get_LiveUrl(hdl, cameraId, streamtype, analyticType);
				url = "rtsp://127.0.0.1:8554/test1";
			}
			else {
				url = Get_PlayBackUrl(hdl, cameraId, start_time_ofplaybackfile, &seekTime_ofFile, &sessionid,  playbackviaapache);
			}

            if (mainLogger) { mainLogger->debug("on_open url returned is: {}", url); }

			if (id.empty() || url.empty()) {
				websocket_server.send(hdl, "EmptyUrl", 8, websocketpp::frame::opcode::TEXT);
				websocket_server.close(hdl, 0, "EmptyUrl");
				return;
			}

			try {
				if (boost::starts_with(url, "tcp"))
				{
					//TODO
					//useTranscoding = true;
				}
			}
			catch (boost::bad_lexical_cast) {
				// bad parameter
			}

			m_connectionsIdMap.insert(std::make_pair(hdl.lock(), id));

			if (ffmpegList.count(id) > 0)
			{
				/*websocket_server.send(hdl, "mp4", 3, websocketpp::frame::opcode::TEXT);
				websocket_server.send(hdl, mp4Parsers[id]->initialization.data(), mp4Parsers[id]->initialization.size(), websocketpp::frame::opcode::BINARY);*/
				//return;
			}
			else
			{
				auto ffmpeg = make_shared<FFmpegWrapper>(cameraId , url, id, seekTime_ofFile, &SendData, &SendStringData,
				        usejmuxer, connectionmode, playbackviaapache, mode, start_time_ofplaybackfile, playerServerIp , playerServerPort, sessionid, mainLogger);
                ffmpegList.insert(std::make_pair(id, ffmpeg));
                mjpegServer->addRoute(ffmpeg);
				ffmpeg->startThread();
			}

			ffmpegList[id]->addConnection(hdl);
	}
	catch (const std::exception & ex)
	{
        if (mainLogger) { mainLogger->error("Error in on_open: {}", ex.what()); }
        else { std::cout << ex.what() << std::endl; }
	}
}

void on_close(connection_hdl hdl) {
	//hdl.lock();
    if (mainLogger) { mainLogger->debug("on_close websocket connection closed"); }
	auto id = m_connectionsIdMap[hdl.lock()];
	if (id != "")
	{
		bool canStop = ffmpegList[id]->removeConnection(hdl);

		if (canStop) {
			ffmpegList[id]->stopThread();
			ffmpegList[id]->stop(); // stop route
			mjpegServer->removeRoute(id);
			ffmpegList.erase(id);
		}
		m_connectionsIdMap.erase(hdl.lock());
	}
}

void on_message(websocketpp::server<websocketpp::config::asio>* s, connection_hdl hdl, message_ptr msg)
{
	string messagestring = msg->get_payload();
	if (boost::starts_with(messagestring, "seek_Time"))
	{
		auto id = m_connectionsIdMap[hdl.lock()];
		if (id != "")
		{
			 string time_toseek = messagestring.substr(9);
			 int time_toseek_int  = stoi(time_toseek);
			 if (time_toseek_int >= 0)
			 {
				 ffmpegList[id]->seek_video(time_toseek_int);
			 }
		}
	}
	else if (boost::starts_with(messagestring, "Pause"))
	{
		auto id = m_connectionsIdMap[hdl.lock()];
		if (id != "")
		{
			ffmpegList[id]->Pause_video();

		}
	}
	else if (boost::starts_with(messagestring, "Resume"))
	{
		auto id = m_connectionsIdMap[hdl.lock()];
		if (id != "")
		{
			ffmpegList[id]->Resume_video();

		}
	}
	//std::cout << "on_message called with hdl: " << hdl.lock().get()
	//	<< " and message: " << msg->get_payload()
	//	<< std::endl;

	//// check for a special command to instruct the server to stop listening so
	//// it can be cleanly exited.
	//if (msg->get_payload() == "stop-listening") {
	//	s->stop_listening();
	//	return;
	//}

	//try {
	//	s->send(hdl, msg->get_payload(), msg->get_opcode());
	//}
	//catch (websocketpp::exception const & e) {
	//	std::cout << "Echo failed because: "
	//		<< "(" << e.what() << ")" << std::endl;
	//}
}

void SendData(websocketpp::connection_hdl& con_hndl, vector<uint8_t>& data) {
	try
	{
		auto dataPtr = data.data();
		auto size = data.size();
		websocket_server.send(con_hndl, dataPtr, size, websocketpp::frame::opcode::BINARY);
	}
	catch (const std::exception & ex)
	{
        if (mainLogger) { mainLogger->error("Error in SendData: {}", ex.what()); }
        else { std::cout << ex.what() << std::endl; }
	}
}

void SendStringData(websocketpp::connection_hdl& con_hndl, string sdata) {
	try
	{
		auto  str = sdata.c_str();
		auto  size = sdata.size();
		//websocket_server.send(con_hndl, "mp4", 3, websocketpp::frame::opcode::TEXT);
		websocket_server.send(con_hndl, str, size, websocketpp::frame::opcode::TEXT);
	}
	catch (const std::exception & ex)
	{
        if (mainLogger) { mainLogger->error("Error in SendStringData: {}", ex.what()); }
        else { std::cout << ex.what() << std::endl; }
	}
}

string GetDateStringFormat_ByUnix(int start_time_ofplaybackfile) {
	time_t t = start_time_ofplaybackfile;
	struct tm* tm = localtime(&t);
	char date_string[100];
	char time_string[100];
	string am_PM = "AM";
	strftime(date_string, sizeof(date_string), "%m/%d/%Y", tm);
	strftime(time_string, sizeof(time_string), "%T", tm);
	string hour = to_string(tm->tm_hour);
	string minute = to_string(tm->tm_min);
	string second = to_string(tm->tm_sec);

	if (tm->tm_hour > 12) {
		int time = tm->tm_hour - 12;
		hour = to_string(time);
	}
	if (stoi(hour) < 10) {
		hour = "0" + hour;
	}
	if (tm->tm_min < 10) {
		minute = "0" + to_string(tm->tm_min);
	}
	if (tm->tm_sec < 10) {
		second = "0" + to_string(tm->tm_sec);
	}

	if (tm->tm_hour >= 12) {
		am_PM = "PM";
	}
	string date_string1, time_string1;
	date_string1 = string(date_string);
	time_string1 = string(time_string);
	string startTime = date_string1 + ", " + hour + ":" + minute + ":" + second + " " + am_PM;
	boost::replace_all(startTime, " ", "%20");
	return startTime;
}

string Get_PlayBackUrl(connection_hdl hdl, int cameraId, int start_time_ofplaybackfile, int* seekTime_ofFile, int*sessionid, bool streamviaapache) {
	string response;
	string cameraId_instring = to_string(cameraId);
	string seekVideo = "";
	string playviaapache = "false";
		if (streamviaapache) {
			playviaapache = "true";
		}
	std::string endpoint = "/url/GetPlaybackUrl?cameraId=" + cameraId_instring + "&time=" + to_string(start_time_ofplaybackfile)+"&streamviaapache=" + playviaapache;
	if (cameraId == -1) {
		return response;
	}
	try
	{
		std::string url = "http://" + playerServerIp + ":" + to_string(playerServerPort) + endpoint;
		auto res = cpr::Get(cpr::Url{ url });

		if (res.status_code == 200) {
			string json = res.text;
			Json::Reader reader;
			Json::Value root;
			bool parseSuccess = reader.parse(json, root, false);
			if (parseSuccess)
			{
				Json::Value resultValue = root["GetEventPlaybackUrlResult"];
				if (resultValue.asString() == "") {
					resultValue = root["getEventPlaybackUrlResult"];
				}
				Json::Value resultValue1 = root["Seek_Time_InSeconds"];
				if (resultValue1.asString() == "") {
					resultValue1 = root["seek_Time_InSeconds"];
				}
				Json::Value resultValue2 = root["SessionId"];
				if (resultValue2.asString() == "") {
					resultValue2 = root["sessionId"];
				}

				*sessionid = stoi(resultValue2.asString());
				*seekTime_ofFile = stoi(resultValue1.asString());

				response = resultValue.asString();
				response.erase(std::remove(response.begin(), response.end(), '\"'), response.end());
				response.erase(std::remove(response.begin(), response.end(), '\\'), response.end());
			}
			else if (res.status_code == 403) {
                if (mainLogger) { mainLogger->error("Get_PlayBackUrl Server License Expired"); }
				else { cout << "Get_PlayBackUrl Server License Expired \n"; }

				websocket_server.send(hdl, "License Expired", 15, websocketpp::frame::opcode::TEXT);
			}
			else if (res.status_code == 400) {
                if (mainLogger) { mainLogger->error("Get_PlayBackUrl Some Error occured status code: {}", res.status_code); }
                else { cout << "Get_PlayBackUrl Some Error occured status code: " << res.status_code <<std::endl; }

                websocket_server.send(hdl, "Some problem occured", 20, websocketpp::frame::opcode::TEXT);
			}
			else {
				response = "";
			}
		}
	}
	catch (const std::exception & ex)
	{
		response = "";

        if (mainLogger) { mainLogger->error("Error in Get_PlayBackUrl: {}", ex.what()); }
        else { std::cout << ex.what() << std::endl; }
	}
	return response;
}

string Get_LiveUrl(connection_hdl hdl, int cameraId, int streamtype, string analyticType) {

	string response;
	string cameraId_instring = to_string(cameraId);

	std::string endpoint = "/url/GetLiveUrl?cameraId=" + cameraId_instring + "&streamType=" + to_string(streamtype) + "&analyticType=" + analyticType;
	if (cameraId == -1) {
		return response;
	}
	try
	{
		std::string url = "http://" + playerServerIp + ":" + to_string(playerServerPort) + endpoint;
		auto res = cpr::Get(cpr::Url{ url });

		if (res.status_code == 200) {
			string command = res.text;
			string c = "\\";
			command.erase(std::remove(command.begin(), command.end(), '\"'), command.end());
			command.erase(std::remove(command.begin(), command.end(), '\\'), command.end());

			response = command;
		}
		else if (res.status_code == 403) {
            if (mainLogger) { mainLogger->error("Get_LiveUrl Server License Expired"); }
            else { cout << "Get_LiveUrl Server License Expired \n"; }

			websocket_server.send(hdl, "License Expired", 15, websocketpp::frame::opcode::TEXT);
		}
		else if (res.status_code == 400) {
            if (mainLogger) { mainLogger->error("Get_LiveUrl Some Error occured status code: {}", res.status_code); }
            else { cout << "Get_LiveUrl Some Error occured status code: " << res.status_code <<std::endl; }

			websocket_server.send(hdl, "Some problem occured", 20, websocketpp::frame::opcode::TEXT);
		}
	}
	catch (const std::exception & ex)
	{
        if (mainLogger) { mainLogger->error("Error in Get_LiveUrl: {}", ex.what()); }
        else { std::cout << ex.what() << std::endl; }
	}
	return response;
}


/// config related

void loadMainConfig()
{
    Options configoptions;
    if (boost::filesystem::exists(mainConfigFile)) {

        configoptions.readFile(mainConfigFile);

        mjpeg_server_port = configoptions.get<int>("mjpeg_server_port", i2v::MJPEG_SERVER_PORT);
        websocket_server_port = configoptions.get<int>("websocket_server_port", i2v::WEBSOCKET_SERVER_PORT);
        playerServerIp = configoptions.get<std::string>("playerServerIp", i2v::PLAYER_SERVER_IP);
        playerServerPort = configoptions.get<int>("playerServerPort", i2v::PLAYER_SERVER_PORT);

        // log level
        int level = configoptions.get<int>("logLevel", -1);
        if (level < 0 or level > 6) {
            level = static_cast<int>(spdlog::level::level_enum::info);
        }
        log_level = static_cast<spdlog::level::level_enum>(level);
    }
    else
    {
        setDefaultValues(configoptions);
        configoptions.writeFile(mainConfigFile, true);
    }
}

void setDefaultValues(Options& opt)
{
    mjpeg_server_port = i2v::MJPEG_SERVER_PORT;
    websocket_server_port = i2v::WEBSOCKET_SERVER_PORT;
    playerServerIp = i2v::PLAYER_SERVER_IP;
    playerServerPort = i2v::PLAYER_SERVER_PORT;
    log_level = spdlog::level::level_enum::info;

    opt.add("mjpeg_server_port", mjpeg_server_port);
    opt.add("websocket_server_port", websocket_server_port);
    opt.add("playerServerIp", playerServerIp);
    opt.add("playerServerPort", playerServerPort);
    opt.add("logLevel", static_cast<int>(log_level));
}

void updateconfigFile(connection_hdl hdl , string previousServerIp, string currentServerIp)
{
    if (currentServerIp == "")
    {
        websocket_server.send(hdl, "Server_ip_not_provided", 22, websocketpp::frame::opcode::TEXT);
    }
    transform(currentServerIp.begin(), currentServerIp.end(), currentServerIp.begin(), ::tolower);
    if (currentServerIp == "localhost")
    {
        currentServerIp = "127.0.0.1";
    }
    else if (previousServerIp != currentServerIp) {
        Options opt;
        playerServerIp = currentServerIp;
        if (boost::filesystem::exists(mainConfigFile))
        {
            opt.readFile(mainConfigFile);
            opt.set<string>("playerServerIp", playerServerIp);
            opt.set<int>("playerServerPort", playerServerPort);
            opt.writeFile(mainConfigFile, true);
        }
        else {
            opt.add("playerServerIp", i2v::PLAYER_SERVER_IP);
            opt.add("playerServerPort", i2v::PLAYER_SERVER_PORT);
            opt.writeFile(mainConfigFile, true);
        }
    }
}