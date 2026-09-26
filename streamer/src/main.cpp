#include "CLI11.hpp"
#include "Util.h"
#include <spdlog/spdlog.h>
#include <spdlog/async.h>
#include <boost/filesystem.hpp>
#include "AppConfig.h"
#include "SignalingServers/WebSocketSignalingServer.h"

extern "C" {
#include <libavformat/avformat.h>
}
using namespace nmetics;


int main(int argc, char* argv[])
{
	CLI::App app{ "nmetics streamer" };
	av_register_all();
	bool show_logs_on_console = true;
	app.add_option("-s,--show_log", show_logs_on_console, "Show logs on console");
	// show_logs_on_console = true; // Default to true, can be overridden by command line argument
	CLI11_PARSE(app, argc, argv)
		spdlog::init_thread_pool(8192, 1);

	AppConfig::instance().load(nmetics::Util::getConfigFilePath());

	std::string mainLogFolder = nmetics::Util::getLogsFolderPath();
	nmetics::Util::createDirectories(mainLogFolder); // log directory

	// create logger
	std::string logFilePrefix = "log_";
	auto logLevel = static_cast<spdlog::level::level_enum>(AppConfig::instance().getInt("logLevel"));
	auto mainLogger = nmetics::Util::createAsyncLoggerAndRegister(nmetics::MAIN_LOGGER_NAME, mainLogFolder, logFilePrefix, show_logs_on_console, logLevel);
	if (not mainLogger) { std::cout << "[Main] Failed to create logger" << std::endl; }
	else {
		AppConfig::instance().setLogger(mainLogger);
		mainLogger->info("[Main] Logger created");
	}

	// Create a WebRTC server endpoint
	try {
		auto& cfg = AppConfig::instance();
		if (cfg.logger()) { cfg.logger()->info("[Main] Starting signaling server: port={}", cfg.getInt("websocketServer.port")); }
		if (cfg.logger()) { cfg.logger()->info("[Main] REST server configured: ip={}, port={}", cfg.get("restServer.ip"), cfg.getInt("restServer.port")); }

		// Validate TLS configuration
		if (cfg.getBool("tls.enabled")) {
			std::string certPath = cfg.get("tls.certPath");
			std::string keyPath = cfg.get("tls.keyPath");
			if (certPath.empty() || keyPath.empty()) {
				std::string msg = "[Main] TLS is enabled but tls.certPath or tls.keyPath is missing in config";
				if (cfg.logger()) { cfg.logger()->error(msg); }
				else { std::cout << msg << std::endl; }
				return 1;
			}
			if (!boost::filesystem::exists(certPath)) {
				std::string msg = "[Main] TLS certificate file not found: path=" + certPath;
				if (cfg.logger()) { cfg.logger()->error(msg); }
				else { std::cout << msg << std::endl; }
				return 1;
			}
			if (!boost::filesystem::exists(keyPath)) {
				std::string msg = "[Main] TLS key file not found: path=" + keyPath;
				if (cfg.logger()) { cfg.logger()->error(msg); }
				else { std::cout << msg << std::endl; }
				return 1;
			}
			if (cfg.logger()) { cfg.logger()->info("[Main] TLS enabled: cert={}, key={}", certPath, keyPath); }
		}

		WebSocketSignalingServer server(cfg.getInt("websocketServer.port"), cfg.logger());
		server.run();

	}
	catch (const std::exception& ex) {
		auto logger = AppConfig::instance().logger();
		if (logger) { logger->error("[Main] Signaling server terminated with error: {}", ex.what()); }
		else { std::cout << "[Main] Signaling server terminated with error: " << ex.what() << std::endl; }
	}}
// cd /webwork/build && cmake .. && make -j$(nproc)
// cd /webwork/build && make -j$(nproc)
// cd /webwork && rm -rf build && cmake -B build -DCMAKE_TOOLCHAIN_FILE=/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_MANIFEST_MODE=OFF -DCMAKE_PREFIX_PATH=/vcpkg/installed/x64-linux && cmake --build build
// docker build -f Dockerfile.streamer.base -t streamer_build_base:latest .

// # Check if NO_MEDIA was defined during the build
// find /vcpkg -path "*/libdatachannel/portfile.cmake" -exec cat {} \;

//[rtsp @ 000002544826b380] Multi-layer HEVC coding is not implemented. Update your FFmpeg version to the newest one from Git. If the problem still occurs, it means that your file has a feature which has not been implemented.


//openssl req -x509 -newkey rsa:2048 -keyout key.pem -out cert.pem -days 365 -nodes -subj "/CN=localhost"