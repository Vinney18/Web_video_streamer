#include <boost/algorithm/string_regex.hpp>
#include <boost/regex.hpp>
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

using namespace i2v;
using websocketpp::connection_hdl;

#pragma once

///typedef
typedef websocketpp::server<websocketpp::config::asio>::message_ptr message_ptr;

///Variables
std::string mainConfigFile;
std::string playerServerIp;
int playerServerPort, websocket_server_port;
spdlog::level::level_enum log_level;

std::shared_ptr<spdlog::logger> mainLogger;

map<string, shared_ptr<FFmpegWrapper>> ffmpegList;
map<boost::asio::detail::socket_ops::shared_cancel_token_type, string> connectionsIdMap;

websocketpp::server<websocketpp::config::asio> websocket_server;

///Methods
int generateAndCheckRandomNumber();

// config File Related
void setDefaultValues(Options& opt);
void loadMainConfig();
// Check and Update Config File
void checkConfigFileIp(connection_hdl hdl, string currentServerIp);
void checkConfigFilePort(int currentServerPort);

// WebSocket Related 
void on_open(connection_hdl hdl);
void on_close(connection_hdl hdl);
void on_message(websocketpp::server<websocketpp::config::asio>* s, connection_hdl hdl, message_ptr msg);

// For Getting URL
string Get_LiveUrl(connection_hdl hdl, int cameraId, int streamtype, string analyticType);
string Get_PlayBackUrl(connection_hdl hdl, int cameraId, int start_time_ofplaybackfile, int* seekTime_ofFile);

// CallBack Related
void SendData(websocketpp::connection_hdl& con_hndl, vector<uint8_t>& data, int64_t timestamp);
void SendStringData(websocketpp::connection_hdl& con_hndl, string sdata);



int main(int argc, char* argv[])
{
	CLI::App app{ "i2V streamer" };
	av_register_all();

	bool show_logs_on_console = false;
	app.add_option("-s,--show_log", show_logs_on_console, "Show logs on console");
	CLI11_PARSE(app, argc, argv)
		spdlog::init_thread_pool(8192, 1);

	string config_dir_path = i2v::Util::getConfigFolderPath();
	i2v::Util::createDirectories(config_dir_path); // config directory

	mainConfigFile = config_dir_path + "/mainConf.json";
	loadMainConfig();

	std::string mainLogFolder = i2v::Util::getLogsFolderPath();
	i2v::Util::createDirectories(mainLogFolder); // log directory

	// create logger
	std::string logFilePrefix = "log_";
	mainLogger = i2v::Util::createAsyncLoggerAndRegister(i2v::MAIN_LOGGER_NAME, mainLogFolder, logFilePrefix, show_logs_on_console, log_level);
	if (not mainLogger) { std::cout << "Unable to create logger !!!" << std::endl; }
	else { mainLogger->info("Logger created Successfully"); }

	// Create a server endpoint
	try {
		if (mainLogger) { mainLogger->info("Starting websocket server on port: {}", websocket_server_port); }
		if (mainLogger) { mainLogger->info("Player server IP is: {0} and port is: {1}", playerServerIp, playerServerPort); }

		// Set logging settings
		//websocket_server.set_access_channels(websocketpp::log::alevel::connect); // enable logging of new connections
		websocket_server.clear_access_channels(websocketpp::log::alevel::all); // disable all logs

		// Initialize Asio
		websocket_server.init_asio();

		// Register our message handler
		websocket_server.set_open_handler(&on_open);
		websocket_server.set_close_handler(&on_close);
		websocket_server.set_message_handler(bind(&on_message, &websocket_server, websocketpp::lib::placeholders::_1, websocketpp::lib::placeholders::_2));

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
}

void on_open(connection_hdl hdl) {
	try
	{
		//info from web socket
		int cameraId;
		string mode = "Live";
		int streamtype = 0;
		int start_time_ofplaybackfile = 0;
		string analyticType = "";
		string connectionmode = "tcp";
		string clVersion = "";

		//used to get data from methods
		string url;
		int seekTime_ofFile = 0;


		websocketpp::server<websocketpp::config::asio>::connection_ptr con = websocket_server.get_con_from_hdl(hdl);
		string query = con->get_uri()->get_query();

		cout << endl;
		cout << endl;
		cout << query << endl;
		if (mainLogger) { mainLogger->debug("on_open - Query -> " + query); }

		if (query.empty()) {
			if (mainLogger) { mainLogger->debug("on_open incoming query is empty"); }
			cout << "Query is Empty" << endl;
			return;
		}

		vector<string> props;
		boost::algorithm::split_regex(props, query, boost::regex("&&"));
		for (auto const& prop : props)
		{
			vector<string> keyValue;
			boost::algorithm::split_regex(keyValue, prop, boost::regex("~~"));
			if (keyValue.size() < 2) continue;
			auto& key = keyValue[0];
			auto& value = keyValue[1];

			if (key == "cameraId") cameraId = stoi(value);
			else if (key == "mode") mode = value;
			else if (key == "streamType") streamtype = stoi(value);
			else if (key == "startTime") start_time_ofplaybackfile = stoi(value);
			else if (key == "analyticType") analyticType = value;
			else if (key == "connectionMode") connectionmode = value;
			else if (key == "wServerIp") checkConfigFileIp(hdl, value);
			else if (key == "wServerPort") checkConfigFilePort(stoi(value));
			else if (key == "clVersion" && value != "") clVersion = value;
		}

		if (mode == "Live") {
			url = Get_LiveUrl(hdl, cameraId, streamtype, analyticType);
		}
		else {
			url = Get_PlayBackUrl(hdl, cameraId, start_time_ofplaybackfile, &seekTime_ofFile);
		}
		cout << url << " ## " << seekTime_ofFile << endl;
		cout << endl;

		if (mainLogger) { mainLogger->debug("on_open url returned is: {}", url); }

		if (url.empty() || boost::starts_with(url, "Player_Server_Not_Connected") || boost::starts_with(url, "URL_Server_Not_Connected")) {
			if (boost::starts_with(url, "Player_Server_Not_Connected")) {
				websocket_server.send(hdl, "Player_Server_Not_Connected", 27, websocketpp::frame::opcode::TEXT);
				websocket_server.pause_reading(hdl);
				websocket_server.close(hdl, 0, "Player_Server_Not_Connected");
			}
			else if (boost::starts_with(url, "URL_Server_Not_Connected")) {
				websocket_server.send(hdl, "URL_Server_Not_Connected", 24, websocketpp::frame::opcode::TEXT);
				websocket_server.pause_reading(hdl);
				websocket_server.close(hdl, 0, "URL_Server_Not_Connected");
			}
			else
			{
				websocket_server.send(hdl, "EmptyUrl", 8, websocketpp::frame::opcode::TEXT);
				websocket_server.pause_reading(hdl);
				websocket_server.close(hdl, 0, "EmptyUrl");
			}
			return;
		}

		if (mode == "Live")
		{
			string keyValue = to_string(cameraId) + "~~" + mode + "~~" + url;
			if (ffmpegList.count(keyValue) > 0) {
				//cout << "Shared FFmpegWrapper.count(cameraId)- " << ffmpegList.count(keyValue) << endl;
			}
			else {
				//cout << "CameraId- " << cameraId << ", count()- " << ffmpegList.count(keyValue) << endl;
				auto ffmpeg = make_shared<FFmpegWrapper>(cameraId, url, mode, seekTime_ofFile, &SendData, &SendStringData,
					connectionmode, playerServerIp, playerServerPort, mainLogger);
				ffmpegList.insert(std::make_pair(keyValue, ffmpeg));
				ffmpeg->startThread();
			}
			ffmpegList[keyValue]->addConnection(hdl);
			connectionsIdMap.insert(std::make_pair(hdl.lock(), keyValue));
		}
		else
		{
			int random = generateAndCheckRandomNumber();
			string keyValue = to_string(random) + "~~" + mode;
			//cout << ", Random- " << random << ", CameraId - " << cameraId << ", count() - " << ffmpegList.count(cameraId) << endl;
			auto ffmpeg = make_shared<FFmpegWrapper>(cameraId, url, mode, seekTime_ofFile, &SendData, &SendStringData,
				connectionmode, playerServerIp, playerServerPort, mainLogger);
			ffmpegList.insert(std::make_pair(keyValue, ffmpeg));
			ffmpeg->startThread();
			ffmpegList[keyValue]->addConnection(hdl);
			connectionsIdMap.insert(std::make_pair(hdl.lock(), keyValue));
		}
	}
	catch (const std::exception& ex)
	{
		if (mainLogger) { mainLogger->error("Error in on_open: {}", ex.what()); }
		else { std::cout << ex.what() << std::endl; }
	}
}

void on_close(connection_hdl hdl) {
	
	if (mainLogger) { mainLogger->debug("on_close websocket connection closed"); }
	string keyValue = connectionsIdMap[hdl.lock()];
	
	if (!keyValue.empty()) {
		bool canStop = ffmpegList[keyValue]->removeConnection(hdl);

		if (canStop) {
			ffmpegList[keyValue]->stopThread();
			ffmpegList.erase(keyValue);
		}
		connectionsIdMap.erase(hdl.lock());
	}
}

void on_message(websocketpp::server<websocketpp::config::asio>* s, connection_hdl hdl, message_ptr msg)
{
	string messagestring = msg->get_payload();
	if (mainLogger) { mainLogger->debug("on_message, data received: {}", messagestring); }
	//std::cout << "on_message" << messagestring << endl;
	if (boost::starts_with(messagestring, "seek_Time"))
	{
		string id = connectionsIdMap[hdl.lock()];

		string time_toseek = messagestring.substr(9);
		int time_toseek_int = stoi(time_toseek);
		if (time_toseek_int >= 0)
		{
			ffmpegList[id]->seek_video(time_toseek_int);
		}

	}
	else if (boost::starts_with(messagestring, "Pause"))
	{
		string id = connectionsIdMap[hdl.lock()];

		ffmpegList[id]->Pause_video();

	}
	// not called now
	else if (boost::starts_with(messagestring, "Resume"))
	{
		/*string id = connectionsIdMap[hdl.lock()];
		if (id > 0)
		{
			ffmpegList[id]->Resume_video();
		}*/
	}
	else if (boost::starts_with(messagestring, "Version"))
	{
		//call when websocket connection is open and return the version of streamer
		string version = "--version " + VERSION;
		auto  str = version.c_str();
		auto  size = version.size();
		websocket_server.send(hdl, str, size, websocketpp::frame::opcode::TEXT);
	}
	else if (boost::starts_with(messagestring, "Server Status"))
	{
		string servData = "ffmpegList.count: " + to_string(ffmpegList.size()) + "\n";
		for (auto& mapKey : ffmpegList) {
			servData = servData + mapKey.first + "\n";
		}
		servData += "\n";

		servData = servData + "connectionsIdMap: " + to_string(connectionsIdMap.size()) + "\n";
		for (auto& mapKey : connectionsIdMap) {
			servData = servData + mapKey.second + "\n";
		}
		cout << servData << endl;
		string servStatus = "--servStatus " + servData;
		auto  str = servStatus.c_str();
		auto  size = servStatus.size();
		websocket_server.send(hdl, str, size, websocketpp::frame::opcode::TEXT);
	}
}

void SendData(websocketpp::connection_hdl& con_hndl, vector<uint8_t>& data, int64_t timestamp) {
	try
	{
		if (timestamp < 0) {
			auto dataPtr = data.data();
			auto size = data.size();
			websocket_server.send(con_hndl, dataPtr, size, websocketpp::frame::opcode::BINARY);
		}
		else {
			std::vector<uint8_t> v;
			v.reserve(sizeof(timestamp));
			for (size_t i = 0; i < sizeof(timestamp); ++i) {
				v.push_back(timestamp & 0xFF);
				timestamp >>= 8;
			}
			v.insert(v.end(), data.begin(), data.end());
			websocket_server.send(con_hndl, v.data(), v.size(), websocketpp::frame::opcode::BINARY);
		}
	}
	catch (const std::exception& ex)
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
		websocket_server.send(con_hndl, str, size, websocketpp::frame::opcode::TEXT);
	}
	catch (const std::exception& ex)
	{
		if (mainLogger) { mainLogger->error("Error in SendStringData: {}", ex.what()); }
		else { std::cout << ex.what() << std::endl; }
	}
}

string Get_LiveUrl(connection_hdl hdl, int cameraId, int streamtype, string analyticType) {

	if (mainLogger) { mainLogger->debug("In Get_LiveUrl, cameraId: {}, streamtype: {}, analyticType: {}", cameraId, streamtype, analyticType); }
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
		if (mainLogger) { mainLogger->debug("In Get_LiveUrl -> " + res.text); }

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
			else { cout << "Get_LiveUrl Some Error occured status code: " << res.status_code << std::endl; }

			websocket_server.send(hdl, "Some problem occured", 20, websocketpp::frame::opcode::TEXT);
		}
		else {
			cout << res.status_code;
			if (mainLogger) { mainLogger->error("Get_LiveUrl Some Error occured status code: {}", res.status_code); }

			response = "Player_Server_Not_Connected";
		}
	}
	catch (const std::exception& ex)
	{
		if (mainLogger) { mainLogger->error("Error in Get_LiveUrl: {}", ex.what()); }
		else { std::cout << ex.what() << std::endl; }
	}
	if (mainLogger) { mainLogger->debug("Get_LiveUrl, response: {}", response); }
	return response;
}

string Get_PlayBackUrl(connection_hdl hdl, int cameraId, int start_time_ofplaybackfile, int* seekTime_ofFile) {

	if (mainLogger) { mainLogger->debug("Get_PlayBackUrl, cameraId: {}, start_time_ofplaybackfile: {}", cameraId, start_time_ofplaybackfile); }
	string response;
	string cameraId_instring = to_string(cameraId);
	string seekVideo = "";

	std::string endpoint = "/url/GetPlaybackUrl?cameraId=" + cameraId_instring + "&time=" + to_string(start_time_ofplaybackfile);
	if (cameraId == -1) {
		return response;
	}
	try
	{
		std::string url = "http://" + playerServerIp + ":" + to_string(playerServerPort) + endpoint;
		auto res = cpr::Get(cpr::Url{ url });

		if (res.status_code == 200) {
			if (res.text == "URL_Server_Not_Connected") { return "URL_Server_Not_Connected"; }
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

				/**sessionid = stoi(resultValue2.asString());*/
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
				else { cout << "Get_PlayBackUrl Some Error occured status code: " << res.status_code << std::endl; }

				websocket_server.send(hdl, "Some problem occured", 20, websocketpp::frame::opcode::TEXT);
			}
			else {
				response = "Player_Server_Not_Connected";
			}
		}
		else {
			response = "Player_Server_Not_Connected";
		}
	}
	catch (const std::exception& ex)
	{
		response = "";

		if (mainLogger) { mainLogger->error("Error in Get_PlayBackUrl: {}", ex.what()); }
		else { std::cout << ex.what() << std::endl; }
	}
	if (mainLogger) { mainLogger->debug("Get_PlayBackUrl, response: {}", response); }
	return response;
}

int generateAndCheckRandomNumber() {
	bool randomValExist = false;
	int random = rand();
	if (random > 0) {
		random = -random;
	}
	for (auto& ffmpegListvar : ffmpegList) {
		int cameraId = stoi(ffmpegListvar.first.substr(0, ffmpegListvar.first.find("!!")));
		if (cameraId == random) {
			randomValExist = true;
			break;
		}
	}
	if (randomValExist) {
		return generateAndCheckRandomNumber();
	}
	else {
		return random;
	}
}


/// config related
void setDefaultValues(Options& opt) {
	websocket_server_port = i2v::WEBSOCKET_SERVER_PORT;
	playerServerIp = i2v::PLAYER_SERVER_IP;
	playerServerPort = i2v::PLAYER_SERVER_PORT;
	log_level = spdlog::level::level_enum::info;

	opt.add("websocket_server_port", websocket_server_port);
	opt.add("playerServerIp", playerServerIp);
	opt.add("playerServerPort", playerServerPort);
	opt.add("logLevel", static_cast<int>(log_level));
}

void loadMainConfig() {
	Options configoptions;
	if (boost::filesystem::exists(mainConfigFile)) {

		configoptions.readFile(mainConfigFile);

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

void checkConfigFileIp(connection_hdl hdl, string currentServerIp) {
	if (mainLogger) { mainLogger->debug("In check ConfigFileIp Method, previousServerIp: {}, currentServerIp: {}", playerServerIp, currentServerIp); }

	if (currentServerIp == "") websocket_server.send(hdl, "Server_ip_not_provided", 22, websocketpp::frame::opcode::TEXT);
	transform(currentServerIp.begin(), currentServerIp.end(), currentServerIp.begin(), ::tolower);
	if (currentServerIp == "localhost") currentServerIp = "127.0.0.1";

	if (playerServerIp != currentServerIp) {
		Options opt;
		playerServerIp = currentServerIp;
		if (boost::filesystem::exists(mainConfigFile))
		{
			opt.readFile(mainConfigFile);
			opt.set<string>("playerServerIp", playerServerIp);
			opt.writeFile(mainConfigFile, true);
		}
		else {
			setDefaultValues(opt);
			opt.writeFile(mainConfigFile, true);
		}
	}
}

void checkConfigFilePort(int currentServerPort) {
	if (mainLogger) { mainLogger->debug("In check ConfigFilePort Method, previousServerPort: {}, currentServerPort: {}", playerServerPort, currentServerPort); }

	if (playerServerPort != currentServerPort) {
		Options opt;
		if (boost::filesystem::exists(mainConfigFile))
		{
			opt.readFile(mainConfigFile);
			opt.set<int>("playerServerPort", currentServerPort);
			opt.writeFile(mainConfigFile, true);
		}
		else {
			setDefaultValues(opt);
			opt.writeFile(mainConfigFile, true);
		}
	}
}