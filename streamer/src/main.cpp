#include "CLI11.hpp"
#include "Util.h"
#include <spdlog/spdlog.h>
#include <spdlog/async.h>
#include "AppConfig.h"
#include "SignalingServers/WebSocketSignalingServer.h"

extern "C" {
#include <libavformat/avformat.h>
}
using namespace i2v;


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

	std::string config_dir_path = i2v::Util::getConfigFolderPath();
	i2v::Util::createDirectories(config_dir_path); // config directory

	std::string mainConfigFile = config_dir_path + "/mainConf.json";
	AppConfig::instance().load(mainConfigFile);

	std::string mainLogFolder = i2v::Util::getLogsFolderPath();
	i2v::Util::createDirectories(mainLogFolder); // log directory

	// create logger
	std::string logFilePrefix = "log_";
	auto logLevel = static_cast<spdlog::level::level_enum>(AppConfig::instance().getInt("logLevel"));
	auto mainLogger = i2v::Util::createAsyncLoggerAndRegister(i2v::MAIN_LOGGER_NAME, mainLogFolder, logFilePrefix, show_logs_on_console, logLevel);
	if (not mainLogger) { std::cout << "Unable to create logger !!!" << std::endl; }
	else {
		AppConfig::instance().setLogger(mainLogger);
		mainLogger->info("Logger created Successfully");
	}

	// Create a WebRTC server endpoint
	try {
		auto& cfg = AppConfig::instance();
		if (cfg.logger()) { cfg.logger()->info("Starting signaling server on port: {}", cfg.getInt("websocket_server_port")); }
		if (cfg.logger()) { cfg.logger()->info("Player server IP is: {0} and port is: {1}", cfg.get("playerServerIp"), cfg.getInt("playerServerPort")); }

		WebSocketSignalingServer server(cfg.getInt("websocket_server_port"), cfg.logger());
		server.run();

	}
	catch (const std::exception& ex) {
		auto logger = AppConfig::instance().logger();
		if (logger) { logger->error("main Error in WebRTC server: {}", ex.what()); }
		else { std::cout << ex.what() << std::endl; }
	}
}
// cd /webwork/build && cmake .. && make -j$(nproc)
// cd /webwork/build && make -j$(nproc)
// cd /webwork && rm -rf build && cmake -B build -DCMAKE_TOOLCHAIN_FILE=/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_MANIFEST_MODE=OFF -DCMAKE_PREFIX_PATH=/vcpkg/installed/x64-linux && cmake --build build
// docker build -f Dockerfile.streamer.base -t streamer_build_base:latest .

// # Check if NO_MEDIA was defined during the build
// find /vcpkg -path "*/libdatachannel/portfile.cmake" -exec cat {} \;
