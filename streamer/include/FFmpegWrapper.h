//#define SPDLOG_FMT_EXTERNAL

#include <string>
#include <iostream>
#include <iomanip>
#include <atomic>
#include "Thread.h"
#include <map>
#include <set>
#include <functional>
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/common/connection_hdl.hpp>
#include <websocketpp/server.hpp>
#include <websocketpp/endpoint.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/lexical_cast.hpp>
#include <cpr/cpr.h>

extern "C"
{
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <libavcodec/avcodec.h>
#include <libavcodec/vaapi.h>
#include <libswscale/swscale.h>
#include <libavformat/avio.h>
#include "libavutil/buffer.h"
#include "libavutil/imgutils.h"
#include "libavutil/error.h"
#include "libavutil/mem.h"
}

#include <spdlog/spdlog.h>

#ifndef FFMPEGWRAPPER_H
#define FFMPEGWRAPPER_H
#pragma once

using namespace std;
typedef websocketpp::connection_hdl webConnHdl;
typedef std::set<webConnHdl, std::owner_less<websocketpp::connection_hdl>> con_list;
typedef std::function<void(webConnHdl& con_hndl, vector<uint8_t>& data, int64_t timestamp)> WebsocketDataCallback;
typedef std::function<void(webConnHdl& con_hndl, string sdata)> WebsocketSDataCallback;

struct InterruptParams {
	double lastStopped;
	bool isRunning;
};

enum OutputType {
	mp4,
	rgba
};

class FFmpegWrapper : public virtual Thread
{
private:
	string cameraId;
	string url;
	string playmode;
	int initial_seek_time = 0;
	int originalRequestTime = 0;
	int videoDuration=0;         // Original requested start time for playback
	WebsocketDataCallback websocketCallback;
	WebsocketSDataCallback websocketSCallback;
	string connectionmode;
	string serverIp;
	int port = 8890;

	bool fileseekingstarted = false;
	bool playbackFileStared = false;
	std::map<OutputType, con_list> connections; //keys mp4 and rgba
	std::mutex connectionsMutex; // Mutex to protect access to connections
	std::vector<std::pair<websocketpp::connection_hdl, bool>> tempConnections;
	//std::mutex connectionlock;
	AVFormatContext* inputFormatCtx = NULL;
	AVCodecContext* inputCodecCtx = NULL;
	AVCodecID inputCodecID = AV_CODEC_ID_NONE;

	AVFrame* pFrame = NULL;
	AVFrame* rgb_frame = NULL;
	SwsContext* conversion_context = NULL;
	uint8_t* buffer = NULL;

	int inputFPS = 0;
	int videoStream = -1;
	
	AVCodec* decoderCodec = NULL;
	AVCodecContext* decoderCodecContext = NULL;
	bool isVideoStartedEventsent = false;
	InterruptParams params;

	std::shared_ptr<spdlog::logger> logger;
	std::chrono::duration<double, std::milli> frameDuration;
	float fastForwardFactor = 1;

public:
	FFmpegWrapper(string _cameraId, string _url, string _playmode, int start_seek_time, WebsocketDataCallback _websocketCallback,
			WebsocketSDataCallback _websocketSCallback, string _connectionmode, string _serverIp, int _port,
			std::shared_ptr<spdlog::logger> _logger, float playbackSpeed, int _requestTime = 0, int _playbackFileDuration=0) : Thread(), logger(std::move(_logger)) {
		cameraId = _cameraId;
		url = std::move(_url);
		playmode = _playmode;
		initial_seek_time = start_seek_time;
		originalRequestTime = _requestTime;
		videoDuration = _playbackFileDuration;
		websocketCallback = _websocketCallback;
		websocketSCallback = _websocketSCallback;
		connectionmode = _connectionmode;
		connections = { {mp4, con_list()}, { rgba , con_list()} };
		serverIp = _serverIp;
		port = _port;
		fastForwardFactor = playbackSpeed;
	}

	~FFmpegWrapper();

	void addConnection(webConnHdl connHdl);
	bool removeConnection(webConnHdl connHdl);
	void seek_video(int offset_time);
	void Pause_video();
	void SendVideoStartedEvent();
	void FastForward_video(float factor);
	int getNextPlaybackTime();  // Returns the next playback time in seconds when segment finishes


protected:
	virtual int run() override;

private:
	void addConnToList(webConnHdl hdl, OutputType outType);

	bool openInput();
	
	bool GetInputCodecInfo();
	bool createRgbaOutput();

	void readInput();

	void freeRgbaOutMemory();
	void closeInput();

	bool isLiveMode();


public:
	static double GetTickCount(void)
	{
#if __linux__
		struct timespec now;
		if (clock_gettime(CLOCK_MONOTONIC, &now))
			return 0;
		auto v = now.tv_sec * 1000.0 + now.tv_nsec / 1000000.0;
		return v;
#else		
		return GetTickCount64();
#endif
	}


	static int interrupt_cb(void* ctx)
	{
		FFmpegWrapper* thisObj = reinterpret_cast<FFmpegWrapper*>(ctx);
		if (thisObj->mStop)
		{
			return 1;
		}
		auto tickCount = GetTickCount();

		//timeout after 20 seconds of no activity
		if (thisObj->params.isRunning && (tickCount - thisObj->params.lastStopped > 20000.0))
			return 1;

		//timeout after 7 seconds of no activity
		if (!thisObj->params.isRunning && (tickCount - thisObj->params.lastStopped > 7000.0))
			return 1;

		return 0;
	}

};

#endif