#include <boost/algorithm/string_regex.hpp>
#include <boost/regex.hpp>
#include <map>
#include <set>
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
#include "WebRTCWrapper.h" // Include WebRTCWrapper header file
using namespace i2v;

#pragma once

///Variables
std::string mainConfigFile;
std::string playerServerIp;
int playerServerPort, websocket_server_port;
spdlog::level::level_enum log_level;

std::shared_ptr<spdlog::logger> mainLogger;

map<string, shared_ptr<FFmpegWrapper>> ffmpegList;

// config File Related
void setDefaultValues(Options& opt);
void loadMainConfig();

bool isVMS = false;
std::string vmsStreamUserName = "";
std::string vmsStreamPassword = "";


int main(int argc, char* argv[])
{
	CLI::App app{ "i2V streamer" };
	av_register_all();
	std::cout << "i2V Streamer starting..." << std::endl;
	bool show_logs_on_console = false;
	app.add_option("-s,--show_log", show_logs_on_console, "Show logs on console");
	// show_logs_on_console = true; // Default to true, can be overridden by command line argument
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

	// Create a WebRTC server endpoint
	try {
		rtc::InitLogger(rtc::LogLevel::Warning);
		rtc::SetThreadPoolSize(2);

		if (mainLogger) { mainLogger->info("Starting WebRTC signaling server on port: {}", websocket_server_port); }
		if (mainLogger) { mainLogger->info("Player server IP is: {0} and port is: {1}", playerServerIp, playerServerPort); }

		WebRTCWrapper rtc_wrapper(websocket_server_port, playerServerIp, playerServerPort, mainLogger, isVMS, vmsStreamUserName, vmsStreamPassword);
		rtc_wrapper.run();

	}
	catch (const std::exception& ex) {
		if (mainLogger) { mainLogger->error("main Error in WebRTC server: {}", ex.what()); }
		else { std::cout << ex.what() << std::endl; }
	}
}




/// config related
void setDefaultValues(Options& opt) {
	websocket_server_port = i2v::WEBSOCKET_SERVER_PORT;
	playerServerIp = i2v::PLAYER_SERVER_IP;
	playerServerPort = i2v::PLAYER_SERVER_PORT;
	log_level = spdlog::level::level_enum::info;
	vmsStreamUserName = i2v::VMS_STREAM_USERNAME;
	vmsStreamPassword = i2v::VMS_STREAM_PASSWORD;

	opt.add("websocket_server_port", websocket_server_port);
	opt.add("playerServerIp", playerServerIp);
	opt.add("playerServerPort", playerServerPort);
	opt.add("logLevel", static_cast<int>(log_level));
	opt.add("isVMS", false);
	opt.add("vmsStreamUserName", vmsStreamUserName);
	opt.add("vmsStreamPassword", vmsStreamPassword);
}

void loadMainConfig() {
	Options configoptions;
	if (boost::filesystem::exists(mainConfigFile)) {

		configoptions.readFile(mainConfigFile);

		websocket_server_port = configoptions.get<int>("websocket_server_port", i2v::WEBSOCKET_SERVER_PORT);
		playerServerIp = configoptions.get<std::string>("playerServerIp", i2v::PLAYER_SERVER_IP);
		playerServerPort = configoptions.get<int>("playerServerPort", i2v::PLAYER_SERVER_PORT);
		isVMS = configoptions.get<bool>("isVMS", false);
		vmsStreamUserName = configoptions.get<std::string>("vmsStreamUserName", i2v::VMS_STREAM_USERNAME);
		vmsStreamPassword = configoptions.get<std::string>("vmsStreamPassword", i2v::VMS_STREAM_PASSWORD);

		// log level
		int level = configoptions.get<int>("logLevel", -1);
		// level = 0; // Force log level to info for now, can be changed later if needed
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

// cd /webwork/build && make -j$(nproc)
// cd /webwork && rm -rf build && cmake -B build -DCMAKE_TOOLCHAIN_FILE=/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_MANIFEST_MODE=OFF -DCMAKE_PREFIX_PATH=/vcpkg/installed/x64-linux && cmake --build build
// docker build -f Dockerfile.streamer.base -t streamer_build_base:latest .

// # Check if NO_MEDIA was defined during the build
// find /vcpkg -path "*/libdatachannel/portfile.cmake" -exec cat {} \;
