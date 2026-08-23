#include "WebSocketWrapper.h"
#include "common.h"
#include "json/json.h"
#include <json/value.h>
#include "Options.h"

#include <boost/algorithm/string_regex.hpp>
#include <boost/regex.hpp>
#include <iostream>
#include <chrono>
#include <ctime>
#include <thread>
#include <Util.h>
#include <boost/filesystem.hpp>
#include <regex>

// Add mutexes for thread safety
std::mutex ffmpegListMutex;
std::mutex connectionsIdMapMutex;

string config_dir_path = i2v::Util::getConfigFolderPath();
std::string mainConfigFile_t = config_dir_path + "/mainConf.json";
WebSocketWrapper::WebSocketWrapper(int port, const std::string &playerIp, int playerPort, std::shared_ptr<spdlog::logger> logger, bool isVMS, const std::string &vmsUser, const std::string &vmsPassword)
	: websocket_server_port(port), playerServerIp(playerIp), playerServerPort(playerPort), mainLogger(logger), isVMS(isVMS), vmsStreamUserName(vmsUser), vmsStreamPassword(vmsPassword) {}
std::string addCredentialsToUrl(const std::string &url, const std::string &username, const std::string &password);

void WebSocketWrapper::run() {
	std::cout << "###############new patch" << std::endl;
    try {
        if (mainLogger) { mainLogger->info("Starting websocket server on port: {}", websocket_server_port); }
        if (mainLogger) { mainLogger->info("Player server IP is: {} and port is: {}", playerServerIp, playerServerPort); }

		websocket_server.clear_access_channels(websocketpp::log::alevel::all);
		websocket_server.init_asio();

		websocket_server.set_open_handler(bind(&WebSocketWrapper::on_open, this, websocketpp::lib::placeholders::_1));
		websocket_server.set_close_handler(bind(&WebSocketWrapper::on_close, this, websocketpp::lib::placeholders::_1));
		websocket_server.set_message_handler(bind(&WebSocketWrapper::on_message, this, websocketpp::lib::placeholders::_1, websocketpp::lib::placeholders::_2));

		websocket_server.listen(websocket_server_port);
		websocket_server.start_accept();
		websocket_server.run();
	}
	catch (const websocketpp::exception &e)
	{
		if (mainLogger)
		{
			mainLogger->error("main Error in websocket server: {}", e.what());
		}
		else
		{
			std::cout << e.what() << std::endl;
		}
	}
	catch (const std::exception &ex)
	{
		if (mainLogger)
		{
			mainLogger->error("main Error in websocket server: {}", ex.what());
		}
		else
		{
			std::cout << ex.what() << std::endl;
		}
	}
}

void WebSocketWrapper::on_open(connection_hdl hdl)
{
	std::thread t([this, hdl]()
				  {
		// Backstop: this worker runs in a detached thread. An uncaught exception
		// here (e.g. std::stoi on a non-numeric query field like startTime=NaN)
		// would call std::terminate() and kill the entire process. Catch everything
		// so one malformed request can never take down the server.
		try
		{
			if (mainLogger) { mainLogger->debug("on_open websocket connection opened"); }
			std::stringstream ss;
			ss <<"ThreadId :: "<< std::this_thread::get_id();
			std::cout <<ss.str()<<std::endl;
			auto start = std::chrono::system_clock::now();
			std::time_t end_time = std::chrono::system_clock::to_time_t(start);
			std::cout << "Connection open at " << std::ctime(&end_time);

			websocketpp::server<websocketpp::config::asio>::connection_ptr con = websocket_server.get_con_from_hdl(hdl);
			std::string query = con->get_uri()->get_query();

			if (mainLogger) { mainLogger->debug("on_open - Query -> {}", query); }
			if (!query.empty()) {
				process_request(hdl, query);
			} else {
				std::cout << "Query is Empty" << std::endl;
			}
		}
		catch (const std::exception &ex)
		{
			if (mainLogger) { mainLogger->error("on_open - Unhandled exception while processing request: {}", ex.what()); }
			else { std::cout << "on_open - Unhandled exception while processing request: " << ex.what() << std::endl; }
		}
		catch (...)
		{
			if (mainLogger) { mainLogger->error("on_open - Unhandled non-standard exception while processing request"); }
			else { std::cout << "on_open - Unhandled non-standard exception while processing request" << std::endl; }
		} });

	t.detach();
}

void WebSocketWrapper::on_close(connection_hdl hdl)
{
	// Log the connection closure
	if (mainLogger)
	{
		mainLogger->debug("on_close - WebSocket connection closed");
	}

	// Safely lock and convert the weak pointer to a shared pointer once
	auto shared_hdl = hdl.lock();
	if (!shared_hdl)
	{
		if (mainLogger)
		{
			mainLogger->error("on_close - Failed to lock connection handle, the connection may have already been closed");
		}
		return;
	}

	// Retrieve key associated with the handle
	std::string keyValue = "";
	{
		if (mainLogger)
		{
			mainLogger->debug("on_close - Acquiring connectionsIdMapMutex to retrieve keyValue");
		}
		std::lock_guard<std::mutex> guard(connectionsIdMapMutex);
		if (mainLogger)
		{
			mainLogger->debug("on_close - Acquired connectionsIdMapMutex, connectionsIdMap size: {}", connectionsIdMap.size());
		}

		// Ensure the handle exists in the map
		auto it = connectionsIdMap.find(shared_hdl);
		if (it == connectionsIdMap.end())
		{
			if (mainLogger)
			{
				mainLogger->error("on_close - Connection handle not found in connectionsIdMap, releasing connectionsIdMapMutex");
			}
			return;
		}
		keyValue = it->second;

	
	}

	// Remove connection from FFmpeg instance
	if (!keyValue.empty())
	{
		

		bool canStop = false;

		// Lock mutex for safe access to ffmpegList
		std::lock_guard<std::mutex> ffmpegGuard(ffmpegListMutex);

		

		auto ffmpegIt = ffmpegList.find(keyValue);
		if (ffmpegIt != ffmpegList.end())
		{
			canStop = ffmpegList[keyValue]->removeConnection(shared_hdl);
			if (canStop)
			{
				ffmpegList[keyValue]->stopThread();
				ffmpegList.erase(keyValue); // Safely remove the ffmpeg instance
			}
		}
		else
		{
			if (mainLogger)
			{
				mainLogger->warn("on_close keyValue not found in ffmpegList");
			}
		}

		if (mainLogger)
		{
			mainLogger->debug("on_close  Releasing ffmpegListMutex");
		}
	}

	// Remove from connections map
	{
		if (mainLogger)
		{
			mainLogger->debug("on_close -  Acquiring connectionsIdMapMutex to erase connection");
		}
		std::lock_guard<std::mutex> guard(connectionsIdMapMutex);
		if (mainLogger)
		{
			mainLogger->debug("on_close -  Acquired connectionsIdMapMutex, erasing connection");
		}
		connectionsIdMap.erase(shared_hdl); // Safely erase the handle from the map
		if (mainLogger)
		{
			mainLogger->debug("on_close -  Connection erased, connectionsIdMap size now: {}, releasing connectionsIdMapMutex", connectionsIdMap.size());
		}
	}

	if (mainLogger)
	{
		mainLogger->debug("on_close -  Finished handling connection close");
	}
}

void WebSocketWrapper::on_message(connection_hdl hdl, websocketpp::server<websocketpp::config::asio>::message_ptr msg)
{
	std::string messagestring = msg->get_payload();
	if (mainLogger)
	{
		mainLogger->debug("on_message, data received: {}", messagestring);
	}

	std::string id;
	{
		std::lock_guard<std::mutex> connectionsGuard(connectionsIdMapMutex);
		auto it = connectionsIdMap.find(hdl.lock());
		if (it != connectionsIdMap.end())
		{
			id = it->second;
		}
	}

	if (boost::starts_with(messagestring, "seek_Time"))
	{
		std::string time_toseek = messagestring.substr(9);
		if (time_toseek.empty())
			return;
		int time_toseek_int = std::stoi(time_toseek);
		if (time_toseek_int >= 0)
		{
			std::lock_guard<std::mutex> ffmpegGuard(ffmpegListMutex);
			auto it = ffmpegList.find(id);
			if (it != ffmpegList.end())
			{
				it->second->seek_video(time_toseek_int);
			}
		}
	}
	else if (boost::starts_with(messagestring, "Pause"))
	{
		std::lock_guard<std::mutex> ffmpegGuard(ffmpegListMutex);
		auto it = ffmpegList.find(id);
		if (it != ffmpegList.end())
		{
			it->second->Pause_video();
		}
	}
	else if (boost::starts_with(messagestring, "Resume"))
	{
		// Resume functionality is not implemented
	}
	else if (boost::starts_with(messagestring, "Version"))
	{
		std::string version = "--version " + i2v::VERSION;
		websocket_server.send(hdl, version.c_str(), version.size(), websocketpp::frame::opcode::TEXT);
	}
	else if (boost::starts_with(messagestring, "Server Status"))
	{
		std::string servData = "ffmpegList.count: " + std::to_string(ffmpegList.size()) + "\n";
		{
			std::lock_guard<std::mutex> ffmpegGuard(ffmpegListMutex);
			for (const auto &mapKey : ffmpegList)
			{
				servData += mapKey.first + "\n";
			}
		}
		servData += "\n";

		servData += "connectionsIdMap: " + std::to_string(connectionsIdMap.size()) + "\n";
		{
			std::lock_guard<std::mutex> connectionsGuard(connectionsIdMapMutex);
			for (const auto &mapKey : connectionsIdMap)
			{
				servData += mapKey.second + "\n";
			}
		}
		std::string servStatus = "--servStatus " + servData;
		websocket_server.send(hdl, servStatus.c_str(), servStatus.size(), websocketpp::frame::opcode::TEXT);
	}
	else if (boost::starts_with(messagestring, "FastForward"))
	{
		std::string speed = messagestring.substr(11);
		if (speed.empty())
			return;
		float speed_float = std::stof(speed);
		if (speed_float >= 0)
		{
			std::lock_guard<std::mutex> ffmpegGuard(ffmpegListMutex);
			auto it = ffmpegList.find(id);
			if (it != ffmpegList.end())
			{
				it->second->FastForward_video(speed_float);
			}
		}
	}
}

void WebSocketWrapper::SendData(websocketpp::connection_hdl &con_hndl, std::vector<uint8_t> &data, int64_t timestamp)
{
	try
	{
		/*if (timestamp >= 0) {
			data.insert(data.begin(), sizeof(timestamp), 0);
			for (size_t i = 0; i < sizeof(timestamp); ++i) {
				data[i] = timestamp & 0xFF;
				timestamp >>= 8;
			}
		}*/
		std::future<void> result = std::async(std::launch::async, [this, con_hndl, data, timestamp]()
											  {
			//cout << "Thread ID (SendData): " << std::this_thread::get_id() << endl;
			websocketpp::server<websocketpp::config::asio>::connection_ptr con = websocket_server.get_con_from_hdl(con_hndl);
			if (con && con->get_state() == websocketpp::session::state::open) {
				// Create the data message to send
				con->send(data.data(), data.size(), websocketpp::frame::opcode::BINARY);
				/*if (mainLogger) {
					mainLogger->debug("Sent data to connection: {}", con_hndl.lock());
				}*/
			} });
	}
	catch (const std::exception &ex)
	{
		if (mainLogger)
		{
			mainLogger->error("Error in SendData: {}", ex.what());
		}
		else
		{
			std::cout << ex.what() << std::endl;
		}
	}
}
void WebSocketWrapper::SendStringData(websocketpp::connection_hdl &con_hndl, std::string sdata)
{
	try
	{
		std::future<void> result = std::async(std::launch::async, [this, con_hndl, sdata]()
											  {
			// cout << "Thread ID (SendStringData): " << std::this_thread::get_id() << endl;

			// Check if this is a Playback_Finished message with nextTime
			if (sdata.find("\"event\":\"Playback_Finished\"") != std::string::npos &&
			    sdata.find("\"nextTime\":") != std::string::npos)
			{
				// Parse JSON to extract nextTime
				try {
					Json::Value root;
					Json::CharReaderBuilder builder;
					std::string errs;
					std::istringstream sstream(sdata);

					if (Json::parseFromStream(builder, sstream, &root, &errs))
					{
						int nextTime = root["nextTime"].asInt();
						std::string cameraId = root["cameraId"].asString();

						if (mainLogger) {
							mainLogger->info("SendStringData - [CameraID: {}] Playback finished, nextTime: {}", cameraId, nextTime);
						}

						// Find the FFmpegWrapper and its details
						std::string keyValue;
						std::string url_for_log;
						{
							if (mainLogger)
							{
								mainLogger->debug("SendStringData - [CameraID: {}] Acquiring connectionsIdMapMutex to find keyValue", cameraId);
							}
							std::lock_guard<std::mutex> connectionsGuard(connectionsIdMapMutex);
							if (mainLogger)
							{
								mainLogger->debug("SendStringData - [CameraID: {}] Acquired connectionsIdMapMutex", cameraId);
							}
							auto it = connectionsIdMap.find(con_hndl.lock());
							if (it != connectionsIdMap.end())
							{
								keyValue = it->second;
								if (mainLogger)
								{
									mainLogger->debug("SendStringData - [CameraID: {}] Found keyValue: {}", cameraId, keyValue);
								}
							}
							else
							{
								if (mainLogger)
								{
									mainLogger->warn("SendStringData - [CameraID: {}] Connection not found in connectionsIdMap", cameraId);
								}
							}
							if (mainLogger)
							{
								mainLogger->debug("SendStringData - [CameraID: {}] Releasing connectionsIdMapMutex", cameraId);
							}
						}

						if (!keyValue.empty() && !cameraId.empty())
						{
							if (mainLogger)
							{
								mainLogger->debug("SendStringData - [CameraID: {}] Processing next segment, keyValue: {}", cameraId, keyValue);
							}

							// Get mode from keyValue
							std::vector<std::string> keyParts;
							boost::algorithm::split_regex(keyParts, keyValue, boost::regex("~~"));
							std::string mode = (keyParts.size() > 1) ? keyParts[1] : "PlayBack";

							if (mainLogger)
							{
								mainLogger->debug("SendStringData - [CameraID: {}] Mode determined: {}", cameraId, mode);
							}

							if (mode == "PlayBack")
								{
									if (mainLogger)
									{
										mainLogger->debug("SendStringData - [CameraID: {}] Fetching next playback URL for nextTime: {}", cameraId, nextTime);
									}

									// Get next playback URL
									int newSeekTime = 0;
									float newDuration_Minutes = 0;
									std::string nextUrl = Get_PlayBackUrl(con_hndl, cameraId, nextTime, &newSeekTime, &newDuration_Minutes);

									if (mainLogger)
									{
										mainLogger->debug("SendStringData - [CameraID: {}] Got next URL: {}, seekTime: {}, duration: {}min",
														cameraId, nextUrl, newSeekTime, newDuration_Minutes);
									}

									if (nextUrl.empty() || boost::starts_with(nextUrl, "Player_Server_Not_Connected") || boost::starts_with(nextUrl, "URL_Server_Not_Connected"))
									{
										if (mainLogger)
										{
											mainLogger->warn("SendStringData - [CameraID: {}] Next URL unavailable or error: {}", cameraId, nextUrl);
										}

										if (boost::starts_with(nextUrl, "Player_Server_Not_Connected")) {
											if (mainLogger)
											{
												mainLogger->debug("SendStringData - [CameraID: {}] Player server not connected, closing connection", cameraId);
											}
											websocket_server.send(con_hndl, "Player_Server_Not_Connected", 27, websocketpp::frame::opcode::TEXT);
											websocket_server.pause_reading(con_hndl);
											websocket_server.close(con_hndl, 0, "Player_Server_Not_Connected");
										}
										else if (boost::starts_with(nextUrl, "URL_Server_Not_Connected")) {
											if (mainLogger)
											{
												mainLogger->debug("SendStringData - [CameraID: {}] URL server not connected, closing connection", cameraId);
											}
											websocket_server.send(con_hndl, "URL_Server_Not_Connected", 24, websocketpp::frame::opcode::TEXT);
											websocket_server.pause_reading(con_hndl);
											websocket_server.close(con_hndl, 0, "URL_Server_Not_Connected");
										}
										else
										{
											if (mainLogger)
											{
												mainLogger->debug("SendStringData - [CameraID: {}] Next URL empty, closing connection", cameraId);
											}
											websocket_server.send(con_hndl, "NextVideoEmptyUrl", 17, websocketpp::frame::opcode::TEXT);
											websocket_server.pause_reading(con_hndl);
											websocket_server.close(con_hndl, 0, "NextVideoEmptyUrl");
										}
										return;
							        }
	
									if (!nextUrl.empty() &&
									    !boost::starts_with(nextUrl, "Player_Server_Not_Connected") &&
									    !boost::starts_with(nextUrl, "URL_Server_Not_Connected"))
									{
										if (mainLogger) {
											mainLogger->info("SendStringData - [CameraID: {}] Fetched next segment: url={}, seekTime={}, duration={}min",
											                cameraId, nextUrl, newSeekTime, newDuration_Minutes);
										}

										// Stop old wrapper in a separate thread to avoid deadlock
										// (we're being called FROM the FFmpegWrapper thread via callback,
										// so we can't call stopThread()->join() here as it would deadlock)
										std::shared_ptr<FFmpegWrapper> oldWrapper;
										{
											if (mainLogger)
											{
												mainLogger->debug("SendStringData - [CameraID: {}] Acquiring ffmpegListMutex to stop old wrapper", cameraId);
											}
											std::lock_guard<std::mutex> ffmpegGuard(ffmpegListMutex);
											if (mainLogger)
											{
												mainLogger->debug("SendStringData - [CameraID: {}] Acquired ffmpegListMutex, looking for keyValue: {}", cameraId, keyValue);
											}
											auto ffmpegIt = ffmpegList.find(keyValue);
											if (ffmpegIt != ffmpegList.end())
											{
												if (mainLogger)
												{
													mainLogger->debug("SendStringData - [CameraID: {}] Found old wrapper, removing from ffmpegList", cameraId);
												}
												oldWrapper = ffmpegIt->second;
												ffmpegList.erase(ffmpegIt);
											}
											else
											{
												if (mainLogger)
												{
													mainLogger->warn("SendStringData - [CameraID: {}] Old wrapper not found in ffmpegList", cameraId);
												}
											}
											if (mainLogger)
											{
												mainLogger->debug("SendStringData - [CameraID: {}] Releasing ffmpegListMutex, ffmpegList size: {}", cameraId, ffmpegList.size());
											}
										}

										// Launch cleanup in a detached thread
										if (oldWrapper) {
											if (mainLogger)
											{
												mainLogger->info("SendStringData - [CameraID: {}] before thread detach, cameraId: {}", cameraId, cameraId);
											}
											std::thread([oldWrapper, cameraId, this]() {
												if (mainLogger)
												{
													mainLogger->debug("SendStringData - [CameraID: {}] Detached thread started, calling stopThread()", cameraId);
												}
												oldWrapper->stopThread();
												if (mainLogger)
												{
													mainLogger->debug("SendStringData - [CameraID: {}] Detached thread finished stopThread()", cameraId);
												}
											}).detach();
											if (mainLogger)
											{
												mainLogger->info("SendStringData - [CameraID: {}] after thread detach, cameraId: {}", cameraId, cameraId);
											}
										}

										// Small delay for cleanup
										if (mainLogger)
										{
											mainLogger->debug("SendStringData - [CameraID: {}] Sleeping 200ms for cleanup", cameraId);
										}
										std::this_thread::sleep_for(std::chrono::milliseconds(200));

										// Create new wrapper
										if (mainLogger)
										{
											mainLogger->debug("SendStringData - [CameraID: {}] Creating new FFmpeg wrapper for next segment", cameraId);
										}

										auto bindSendData = std::bind(&WebSocketWrapper::SendData, this,
										                              std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
										auto bindSendStringData = std::bind(&WebSocketWrapper::SendStringData, this,
										                                    std::placeholders::_1, std::placeholders::_2);
										std::function<void(webConnHdl&, std::vector<uint8_t>&, int64_t)> sendDataFunc = bindSendData;
										std::function<void(webConnHdl&, std::string)> sendStringDataFunc = bindSendStringData;

										float playbackSpeed = 1.0; // TODO: preserve original speed from query
										std::string connectionmode = "tcp"; // TODO: preserve from query

										int random = generateAndCheckRandomNumber();
										std::string newKeyValue = std::to_string(random) + "~~" + mode;

										if (mainLogger)
										{
											mainLogger->debug("SendStringData - [CameraID: {}] New keyValue: {}, nextUrl: {}, newSeekTime: {}",
															cameraId, newKeyValue, nextUrl, newSeekTime);
										}

										// Convert duration from minutes to seconds
										int newDuration_Seconds = static_cast<int>(round(newDuration_Minutes * 60));

										if (mainLogger)
										{
											mainLogger->debug("SendStringData - [CameraID: {}] Creating FFmpegWrapper instance", cameraId);
										}

										auto ffmpeg = std::make_shared<FFmpegWrapper>(cameraId, nextUrl, mode, newSeekTime,
										                                              sendDataFunc, sendStringDataFunc,
										                                              connectionmode, playerServerIp, playerServerPort,
										                                              mainLogger, playbackSpeed, nextTime, newDuration_Seconds);

										{
											if (mainLogger)
											{
												mainLogger->debug("SendStringData - [CameraID: {}] Acquiring ffmpegListMutex to add new wrapper", cameraId);
											}
											std::lock_guard<std::mutex> ffmpegGuard(ffmpegListMutex);
											if (mainLogger)
											{
												mainLogger->debug("SendStringData - [CameraID: {}] Acquired ffmpegListMutex, adding new wrapper", cameraId);
											}
											ffmpegList[newKeyValue] = ffmpeg;
											if (mainLogger)
											{
												mainLogger->debug("SendStringData - [CameraID: {}] Added new wrapper, ffmpegList size: {}, releasing ffmpegListMutex", cameraId, ffmpegList.size());
											}
										}

										if (mainLogger)
										{
											mainLogger->debug("SendStringData - [CameraID: {}] Starting FFmpeg thread for new segment", cameraId);
										}
										ffmpeg->startThread();

										if (mainLogger)
										{
											mainLogger->debug("SendStringData - [CameraID: {}] Adding connection to new FFmpeg instance", cameraId);
										}
										ffmpeg->addConnection(con_hndl);

										{
											if (mainLogger)
											{
												mainLogger->debug("SendStringData - [CameraID: {}] Acquiring connectionsIdMapMutex to update mapping", cameraId);
											}
											std::lock_guard<std::mutex> connectionsGuard(connectionsIdMapMutex);
											if (mainLogger)
											{
												mainLogger->debug("SendStringData - [CameraID: {}] Acquired connectionsIdMapMutex, updating mapping", cameraId);
											}
											connectionsIdMap[con_hndl.lock()] = newKeyValue;
											if (mainLogger)
											{
												mainLogger->debug("SendStringData - [CameraID: {}] Updated mapping, connectionsIdMap size: {}, releasing connectionsIdMapMutex", cameraId, connectionsIdMap.size());
											}
										}

										if (mainLogger) {
											mainLogger->info("SendStringData - [CameraID: {}] Successfully created new FFmpegWrapper for next segment", cameraId);
										}

										// Don't send the Playback_Finished message - continue seamlessly
										return;
									}
								else
								{
									if (mainLogger) {
										mainLogger->info("SendStringData - [CameraID: {}] No next segment available, ending playback", cameraId);
									}
								}
							}
						}
						else
						{
							if (mainLogger)
							{
								mainLogger->warn("SendStringData - Playback_Finished but keyValue or cameraId is empty");
							}
						}
					}
					else
					{
						if (mainLogger)
						{
							mainLogger->error("SendStringData - Failed to parse Playback_Finished JSON");
						}
					}
				}
				catch (const std::exception& jsonEx) {
					if (mainLogger) {
						mainLogger->error("SendStringData - Error handling Playback_Finished: {}", jsonEx.what());
					}
				}

				if (mainLogger)
				{
					mainLogger->debug("SendStringData - Playback_Finished processing complete");
				}
			}
			else
			{
				

				websocketpp::server<websocketpp::config::asio>::connection_ptr con = websocket_server.get_con_from_hdl(con_hndl);
				if (con && con->get_state() == websocketpp::session::state::open) {
					con->send(sdata, websocketpp::frame::opcode::TEXT);
					/*if (mainLogger) {
						mainLogger->debug("Sent string data to connection: {}", con_hndl.lock());
					}*/
			}
			} });
	}
	catch (const std::exception &ex)
	{
		if (mainLogger)
		{
			mainLogger->error("SendStringData - Exception in SendStringData: {}", ex.what());
		}
		else
		{
			std::cout << "SendStringData exception: " << ex.what() << std::endl;
		}
	}
}

// Ensure thread-safe access to ffmpegList and connectionsIdMap
void WebSocketWrapper::process_request(connection_hdl hdl, std::string &query)
{
	// std::cout << "Starting heavy computation on thread: " << std::this_thread::get_id() << std::endl;
	std::string cameraId;
	std::string mode = "Live";
	int streamtype = 0;
	int start_time_ofplaybackfile = 0;
	int end_time_ofplaybackfile = 0;
	std::string analyticType = "";
	std::string connectionmode = "tcp";
	std::string clVersion = "";
	float playbackSpeed = 1.0;
	std::string vaServerId = "";
	std::string vaServerPipeId = "";

	std::string url;
	int seekTime_ofFile = 0;
	float duration_in_Minutes = 0;
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
			mode = value;
		else if (key == "streamType" || key == "streamtype")
			streamtype = std::stoi(value);
		else if (key == "startTime")
			start_time_ofplaybackfile = std::stoi(value);
		else if (key == "endTime")
			end_time_ofplaybackfile = std::stoi(value);
		else if (key == "analyticType")
			analyticType = value;
		else if (key == "connectionMode")
			connectionmode = value;
		else if (key == "wServerIp")
			checkConfigFileIp(hdl, value);
		else if (key == "wServerPort")
			checkConfigFilePort(std::stoi(value));
		else if (key == "clVersion" && !value.empty())
			clVersion = value;
		else if (key == "playbackSpeed" && !value.empty())
		{
			playbackSpeed = std::stof(value);
			playbackSpeed = std::clamp(playbackSpeed, 0.5f, 5.0f);
		}
		else if (key == "vaServerId" && !value.empty())
			vaServerId = value;
		else if (key == "vaServerPipeId" && !value.empty())
			vaServerPipeId = value;
	}
	if (mode == "Live")
	{
		url = Get_LiveUrl(hdl, cameraId, streamtype, analyticType, vaServerId, vaServerPipeId);
		std::cout << "Call End\n";
	}
	else
	{
		if (end_time_ofplaybackfile == 0)
		{
			url = Get_PlayBackUrl(hdl, cameraId, start_time_ofplaybackfile, &seekTime_ofFile, &duration_in_Minutes);
		}
		else
		{
			url = Get_PlayBackUrl(hdl, cameraId, start_time_ofplaybackfile, end_time_ofplaybackfile);
		}
	}
	// cout << url << " ## " << seekTime_ofFile << endl;
	// cout << endl;

	if (mainLogger)
	{
		mainLogger->debug("on_open url returned is: {}", url);
	}

	if (url.empty() || boost::starts_with(url, "Player_Server_Not_Connected") || boost::starts_with(url, "URL_Server_Not_Connected"))
	{
		if (boost::starts_with(url, "Player_Server_Not_Connected"))
		{
			websocket_server.send(hdl, "Player_Server_Not_Connected", 27, websocketpp::frame::opcode::TEXT);
			websocket_server.pause_reading(hdl);
			websocket_server.close(hdl, 0, "Player_Server_Not_Connected");
		}
		else if (boost::starts_with(url, "URL_Server_Not_Connected"))
		{
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

	auto bindSendData = std::bind(&WebSocketWrapper::SendData, this,
								  std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
	auto bindSendStringData = std::bind(&WebSocketWrapper::SendStringData, this,
										std::placeholders::_1, std::placeholders::_2);
	std::function<void(webConnHdl &, std::vector<uint8_t> &, int64_t)> sendDataFunc = bindSendData;
	std::function<void(webConnHdl &, std::string)> sendStringDataFunc = bindSendStringData;
	if (mode == "Live")
	{
		std::string keyValue = cameraId + "~~" + mode + "~~" + url;
		if (mainLogger)
		{
			mainLogger->debug("process_request - [CameraID: {}] Live mode, keyValue: {}", cameraId, keyValue);
		}
		{
			if (mainLogger)
			{
				mainLogger->debug("process_request - [CameraID: {}] Acquiring ffmpegListMutex", cameraId);
			}
			std::lock_guard<std::mutex> ffmpegGuard(ffmpegListMutex);
			if (mainLogger)
			{
				mainLogger->debug("process_request - [CameraID: {}] Acquired ffmpegListMutex, ffmpegList size: {}", cameraId, ffmpegList.size());
			}

			if (ffmpegList.find(keyValue) == ffmpegList.end())
			{
				if (mainLogger)
				{
					mainLogger->debug("process_request - [CameraID: {}] FFmpeg instance not found, creating new one", cameraId);
				}
				auto ffmpeg = std::make_shared<FFmpegWrapper>(cameraId, url, mode, seekTime_ofFile, sendDataFunc, sendStringDataFunc,
															  connectionmode, playerServerIp, playerServerPort, mainLogger, playbackSpeed);
				ffmpegList[keyValue] = ffmpeg;
				if (mainLogger)
				{
					mainLogger->debug("process_request - [CameraID: {}] Starting FFmpeg thread", cameraId);
				}
				ffmpeg->startThread();
			}
			else
			{
				if (mainLogger)
				{
					mainLogger->debug("process_request - [CameraID: {}] FFmpeg instance already exists, reusing", cameraId);
				}
			}

			auto ffmpeg = ffmpegList[keyValue];
			if (ffmpeg != nullptr)
			{
				if (mainLogger)
				{
					mainLogger->debug("process_request - [CameraID: {}] Adding connection to FFmpeg instance", cameraId);
				}
				ffmpeg->addConnection(hdl);
			}

			if (mainLogger)
			{
				mainLogger->debug("process_request - [CameraID: {}] Releasing ffmpegListMutex", cameraId);
			}
		}

		{
			if (mainLogger)
			{
				mainLogger->debug("process_request - [CameraID: {}] Acquiring connectionsIdMapMutex to add connection mapping", cameraId);
			}
			std::lock_guard<std::mutex> connectionsGuard(connectionsIdMapMutex);
			if (mainLogger)
			{
				mainLogger->debug("process_request - [CameraID: {}] Acquired connectionsIdMapMutex, adding mapping", cameraId);
			}
			connectionsIdMap[hdl.lock()] = keyValue;
			if (mainLogger)
			{
				mainLogger->debug("process_request - [CameraID: {}] Added mapping, connectionsIdMap size: {}, releasing connectionsIdMapMutex", cameraId, connectionsIdMap.size());
			}
		}
	}
	else
	{
		int random = generateAndCheckRandomNumber();
		std::string keyValue = std::to_string(random) + "~~" + mode;

		if (mainLogger)
		{
			mainLogger->debug("process_request - [CameraID: {}] Playback mode, keyValue: {}, seekTime: {}", cameraId, keyValue, seekTime_ofFile);
		}

		// Convert duration from minutes to seconds
		int duration_in_Seconds = static_cast<int>(round(duration_in_Minutes * 60));

		if (mainLogger)
		{
			mainLogger->debug("process_request - [CameraID: {}] Creating FFmpeg instance for playback, duration: {} seconds", cameraId, duration_in_Seconds);
		}

		auto ffmpeg = std::make_shared<FFmpegWrapper>(cameraId, url, mode, seekTime_ofFile, sendDataFunc, sendStringDataFunc,
													  connectionmode, playerServerIp, playerServerPort, mainLogger, playbackSpeed, start_time_ofplaybackfile, duration_in_Seconds);
		{
			if (mainLogger)
			{
				mainLogger->debug("process_request - [CameraID: {}] Acquiring ffmpegListMutex to add playback instance", cameraId);
			}
			std::lock_guard<std::mutex> ffmpegGuard(ffmpegListMutex);
			if (mainLogger)
			{
				mainLogger->debug("process_request - [CameraID: {}] Acquired ffmpegListMutex, adding to ffmpegList", cameraId);
			}

			ffmpegList[keyValue] = ffmpeg;

			if (mainLogger)
			{
				mainLogger->debug("process_request - [CameraID: {}] Added to ffmpegList, size now: {}, releasing ffmpegListMutex", cameraId, ffmpegList.size());
			}
		}

		if (mainLogger)
		{
			mainLogger->debug("process_request - [CameraID: {}] Starting FFmpeg thread for playback", cameraId);
		}
		ffmpeg->startThread();

		if (mainLogger)
		{
			mainLogger->debug("process_request - [CameraID: {}] Adding connection to FFmpeg instance", cameraId);
		}
		ffmpeg->addConnection(hdl);

		{
			if (mainLogger)
			{
				mainLogger->debug("process_request - [CameraID: {}] Acquiring connectionsIdMapMutex to add connection mapping", cameraId);
			}
			std::lock_guard<std::mutex> connectionsGuard(connectionsIdMapMutex);
			if (mainLogger)
			{
				mainLogger->debug("process_request - [CameraID: {}] Acquired connectionsIdMapMutex, adding mapping", cameraId);
			}
			connectionsIdMap[hdl.lock()] = keyValue;
			if (mainLogger)
			{
				mainLogger->debug("process_request - [CameraID: {}] Added mapping, connectionsIdMap size: {}, releasing connectionsIdMapMutex", cameraId, connectionsIdMap.size());
			}
		}
	}
}

std::string WebSocketWrapper::Get_LiveUrl(connection_hdl hdl, const std::string &cameraId, int streamtype, const std::string &analyticType, const std::string &vaServerId, const std::string &vaServerPipeId)
{
	if (mainLogger)
	{
		mainLogger->debug("In Get_LiveUrl, cameraId: {}, streamtype: {}, analyticType: {}", cameraId, streamtype, analyticType);
	}
	string response;
	string cameraId_instring = cameraId;
	// check if any vaServerId and vaServerPipeId is provided, if it is change url, else use default url
	std::string endpoint = "";
	if (vaServerId != "" || vaServerPipeId != "")
	{
		endpoint = "/url/GetLiveVaUrl?cameraId=" + cameraId_instring + "&streamType=" + to_string(streamtype) + "&analyticType=" + analyticType + "&vaServerId=" + vaServerId + "&vaServerPipeId=" + vaServerPipeId;
	}
	else
	{
		endpoint = "/url/GetLiveUrl?cameraId=" + cameraId_instring + "&streamType=" + to_string(streamtype) + "&analyticType=" + analyticType;
	}
	if (cameraId == "")
	{
		return response;
	}
	try
	{
		// playerServerIp = "192.168.1.36";
		std::string url = "http://" + playerServerIp + ":" + to_string(playerServerPort) + endpoint;
		auto res = cpr::Get(cpr::Url{url});
		if (mainLogger)
		{
			mainLogger->debug("In Get_LiveUrl -> " + res.text);
		}

		if (res.status_code == 200)
		{
			string command = res.text;
			string c = "\\";
			command.erase(std::remove(command.begin(), command.end(), '\"'), command.end());
			command.erase(std::remove(command.begin(), command.end(), '\\'), command.end());
			if (isVMS)
			{
				command = addCredentialsToUrl(command, vmsStreamUserName, vmsStreamPassword);
			}
			response = command;
		}
		else if (res.status_code == 403)
		{
			if (mainLogger)
			{
				mainLogger->error("Get_LiveUrl Server License Expired");
			}
			else
			{
				cout << "Get_LiveUrl Server License Expired \n";
			}

			websocket_server.send(hdl, "License Expired", 15, websocketpp::frame::opcode::TEXT);
		}
		else if (res.status_code == 400)
		{
			if (mainLogger)
			{
				mainLogger->error("Get_LiveUrl Some Error occured status code: {}", res.status_code);
			}
			else
			{
				cout << "Get_LiveUrl Some Error occured status code: " << res.status_code << std::endl;
			}

			websocket_server.send(hdl, "Some problem occured", 20, websocketpp::frame::opcode::TEXT);
		}
		else
		{
			cout << res.status_code;
			if (mainLogger)
			{
				mainLogger->error("Get_LiveUrl Some Error occured status code: {}", res.status_code);
			}

			response = "Player_Server_Not_Connected";
		}
	}
	catch (const std::exception &ex)
	{
		if (mainLogger)
		{
			mainLogger->error("Error in Get_LiveUrl: {}", ex.what());
		}
		else
		{
			std::cout << ex.what() << std::endl;
		}
	}
	if (mainLogger)
	{
		mainLogger->debug("Get_LiveUrl, response: {}", response);
	}
	return response;
}

std::string WebSocketWrapper::Get_PlayBackUrl(connection_hdl hdl, const std::string &cameraId, int start_time_ofplaybackfile, int *seekTime_ofFile, float *duration_Minutes)
{
	if (mainLogger)
	{
		mainLogger->debug("Get_PlayBackUrl, cameraId: {}, start_time_ofplaybackfile: {}", cameraId, start_time_ofplaybackfile);
	}
	string response;
	string cameraId_instring = cameraId;
	string seekVideo = "";

	std::string endpoint = "/url/GetPlaybackUrl?cameraId=" + cameraId_instring + "&time=" + to_string(start_time_ofplaybackfile);
	if (cameraId == "")
	{
		return response;
	}
	try
	{
		std::string url = "http://" + playerServerIp + ":" + to_string(playerServerPort) + endpoint;
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
			cout << "Full JSON received from vms:\n"
				 << root.toStyledString() << endl;
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
				Json::Value resultValue2 = root["SessionId"];
				if (resultValue2.asString() == "")
				{
					resultValue2 = root["sessionId"];
				}
				Json::Value resultValue3 = root["duration_in_Minutes"];
				float duration_in_Minutes_value = 0;
				if (!resultValue3.isNull())
				{
					duration_in_Minutes_value = resultValue3.asFloat();
					cout << "duration_in_Minutes_value is " << duration_in_Minutes_value << std::endl;
					if (mainLogger)
					{
						mainLogger->debug("Get_PlayBackUrl, duration_in_Minutes from response: {}", duration_in_Minutes_value);
					}
				}

				/**sessionid = stoi(resultValue2.asString());*/
				*seekTime_ofFile = stoi(resultValue1.asString());

				// Set duration if pointer was provided
				if (duration_Minutes != nullptr)
				{
					*duration_Minutes = duration_in_Minutes_value;
				}

				response = resultValue.asString();
				response.erase(std::remove(response.begin(), response.end(), '\"'), response.end());
				response.erase(std::remove(response.begin(), response.end(), '\\'), response.end());
			}
			else if (res.status_code == 403)
			{
				if (mainLogger)
				{
					mainLogger->error("Get_PlayBackUrl Server License Expired");
				}
				else
				{
					cout << "Get_PlayBackUrl Server License Expired \n";
				}

				websocket_server.send(hdl, "License Expired", 15, websocketpp::frame::opcode::TEXT);
			}
			else if (res.status_code == 400)
			{
				if (mainLogger)
				{
					mainLogger->error("Get_PlayBackUrl Some Error occured status code: {}", res.status_code);
				}
				else
				{
					cout << "Get_PlayBackUrl Some Error occured status code: " << res.status_code << std::endl;
				}

				websocket_server.send(hdl, "Some problem occured", 20, websocketpp::frame::opcode::TEXT);
			}
			else
			{
				response = "Player_Server_Not_Connected";
			}
		}
		else
		{
			response = "Player_Server_Not_Connected";
		}
	}
	catch (const std::exception &ex)
	{
		response = "";

		if (mainLogger)
		{
			mainLogger->error("Error in Get_PlayBackUrl: {}", ex.what());
		}
		else
		{
			std::cout << ex.what() << std::endl;
		}
	}
	if (mainLogger)
	{
		mainLogger->debug("Get_PlayBackUrl, response: {}", response);
	}
	return response;
}

std::string WebSocketWrapper::Get_PlayBackUrl(connection_hdl hdl, const std::string &cameraId, int start_time_ofplaybackfile, int end_time_ofplaybackfile)
{
	if (mainLogger)
	{
		mainLogger->debug("Get_PlayBackUrl, cameraId: {}, start_time_ofplaybackfile: {}, end_time_ofplaybackfile: {}", cameraId, start_time_ofplaybackfile, end_time_ofplaybackfile);
	}
	// print the start and end time of playback file
	// std::cout << "start_time_ofplaybackfile: " << start_time_ofplaybackfile << ", end_time_ofplaybackfile: " << end_time_ofplaybackfile << std::endl;
	string response;
	string cameraId_instring = cameraId;
	string seekVideo = "";

	std::string endpoint = "/url/GetExportUrl?cameraId=" + cameraId_instring + "&startTime=" + to_string(start_time_ofplaybackfile) + "&endTime=" + to_string(end_time_ofplaybackfile);
	if (cameraId == "")
	{
		return response;
	}
	try
	{
		std::string url = "http://" + playerServerIp + ":" + to_string(playerServerPort) + endpoint;
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
				if (resultValue.asString() == "")
				{
					resultValue = root["ExportedVideoUrl"];
				}
				Json::Value resultValue1 = root["ExportId"];
				if (resultValue1.asString() == "")
				{
					resultValue1 = root["ExportId"];
				}

				response = resultValue.asString();
				response.erase(std::remove(response.begin(), response.end(), '\"'), response.end());
				response.erase(std::remove(response.begin(), response.end(), '\\'), response.end());
			}
			else if (res.status_code == 403)
			{
				if (mainLogger)
				{
					mainLogger->error("Get_PlayBackUrl Server License Expired");
				}
				else
				{
					cout << "Get_PlayBackUrl Server License Expired \n";
				}

				websocket_server.send(hdl, "License Expired", 15, websocketpp::frame::opcode::TEXT);
			}
			else if (res.status_code == 400)
			{
				if (mainLogger)
				{
					mainLogger->error("Get_PlayBackUrl Some Error occured status code: {}", res.status_code);
				}
				else
				{
					cout << "Get_PlayBackUrl Some Error occured status code: " << res.status_code << std::endl;
				}

				websocket_server.send(hdl, "Some problem occured", 20, websocketpp::frame::opcode::TEXT);
			}
			else
			{
				response = "Player_Server_Not_Connected";
			}
		}
		else
		{
			response = "Player_Server_Not_Connected";
		}
	}
	catch (const std::exception &ex)
	{
		response = "";

		if (mainLogger)
		{
			mainLogger->error("Error in Get_PlayBackUrl: {}", ex.what());
		}
		else
		{
			std::cout << ex.what() << std::endl;
		}
	}
	if (mainLogger)
	{
		mainLogger->debug("Get_PlayBackUrl, response: {}", response);
	}
	return response;
}

void WebSocketWrapper::checkConfigFileIp(connection_hdl hdl, std::string &currentServerIp)
{
	if (mainLogger)
	{
		mainLogger->debug("In check ConfigFileIp Method, previousServerIp: {}, currentServerIp: {}", playerServerIp, currentServerIp);
	}

	if (currentServerIp == "")
		websocket_server.send(hdl, "Server_ip_not_provided", 22, websocketpp::frame::opcode::TEXT);
	transform(currentServerIp.begin(), currentServerIp.end(), currentServerIp.begin(), ::tolower);
	if (currentServerIp == "localhost")
	{
		currentServerIp = "127.0.0.1";
	}
	if (playerServerIp != currentServerIp)
	{
		Options opt;
		playerServerIp = currentServerIp;
		if (boost::filesystem::exists(mainConfigFile_t))
		{
			opt.readFile(mainConfigFile_t);
			opt.set<string>("playerServerIp", playerServerIp);
			opt.writeFile(mainConfigFile_t, true);
		}
		else
		{
			// setDefaultValues(opt);
			opt.writeFile(mainConfigFile_t, true);
		}
	}
}

void WebSocketWrapper::checkConfigFilePort(int currentServerPort)
{
	if (mainLogger)
	{
		mainLogger->debug("In check ConfigFilePort Method, previousServerPort: {}, currentServerPort: {}", playerServerPort, currentServerPort);
	}

	if (playerServerPort != currentServerPort)
	{
		Options opt;
		if (boost::filesystem::exists(mainConfigFile_t))
		{
			opt.readFile(mainConfigFile_t);
			opt.set<int>("playerServerPort", currentServerPort);
			opt.writeFile(mainConfigFile_t, true);
		}
		else
		{
			// setDefaultValues(opt);
			opt.writeFile(mainConfigFile_t, true);
		}
	}
}

int WebSocketWrapper::generateAndCheckRandomNumber()
{
	bool randomValExist = false;
	int random = rand();
	if (random > 0)
	{
		random = -random;
	}
	for (auto &ffmpegListvar : ffmpegList)
	{
		try
		{
			int cameraId = stoi(ffmpegListvar.first.substr(0, ffmpegListvar.first.find("~~")));
			if (cameraId == random)
			{
				randomValExist = true;
				break;
			}
		}
		catch (const std::exception &ex)
		{
			// if unable to convert camera Id into string
			// hence camera id is string so ignore this case
			continue;
		}
	}
	if (randomValExist)
	{
		return generateAndCheckRandomNumber();
	}
	else
	{
		return random;
	}
}

// Method to check and add credentials to the URL if they are not already present
std::string addCredentialsToUrl(const std::string &url, const std::string &username, const std::string &password)
{
	// Regular expression to check if the URL already contains credentials (e.g., "username:password@")
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
			// Insert credentials after the protocol (e.g., "://")
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
