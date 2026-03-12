//#define SPDLOG_FMT_EXTERNAL

#include <string>
#include <iostream>
#include <iomanip>
#include <atomic>
#include "Thread.h"
#include <map>
#include <set>
#include <functional>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/lexical_cast.hpp>
#include <cpr/cpr.h>

extern "C"
{
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <libavcodec/avcodec.h>
#include <libavcodec/vaapi.h>
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

// Forward declaration for WebRTC
namespace rtc {
	class PeerConnection;
}

typedef std::shared_ptr<rtc::PeerConnection> webConnHdl;
typedef std::set<webConnHdl> con_list;
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

struct ProcessedPacket {
	vector<uint8_t> mp4Data;
	int64_t position;
};

class FFmpegWrapper : public virtual Thread
{
protected:
	string cameraId;
	string url;
	string connectionmode;
	string serverIp;
	int port = 8890;

	WebsocketDataCallback websocketCallback;
	WebsocketSDataCallback websocketSCallback;

	OutputType outputType;
	con_list connections;
	std::mutex connectionsMutex;

	AVFormatContext* inputFormatCtx = NULL;
	AVCodecContext* inputCodecCtx = NULL;
	AVCodecID inputCodecID = AV_CODEC_ID_NONE;

	int inputFPS = 0;
	int videoStream = -1;

	bool isVideoStartedEventsent = false;
	InterruptParams params;

	std::shared_ptr<spdlog::logger> logger;

public:
	FFmpegWrapper(string _cameraId, string _url,
			WebsocketDataCallback _websocketCallback,
			WebsocketSDataCallback _websocketSCallback,
			string _connectionmode, string _serverIp, int _port,
			std::shared_ptr<spdlog::logger> _logger) : Thread(), logger(std::move(_logger)) {
		cameraId = _cameraId;
		url = std::move(_url);
		websocketCallback = _websocketCallback;
		websocketSCallback = _websocketSCallback;
		connectionmode = _connectionmode;
		outputType = mp4;
		serverIp = _serverIp;
		port = _port;
	}

	virtual ~FFmpegWrapper();

	bool removeConnection(webConnHdl connHdl);
	void Pause_video();
	void SendVideoStartedEvent();

	// Playback-specific — no-op defaults so callers don't need dynamic_cast
	virtual void seek_video(int offset_time) { (void)offset_time; }
	virtual void FastForward_video(float factor) { (void)factor; }
	virtual int getNextPlaybackTime() { return 0; }

	// Lightweight probe: opens stream, detects video codec, closes. Returns AV_CODEC_ID_NONE on failure.
	static AVCodecID probeCodec(const std::string& url);

protected:
	int run() override;

	// Virtual hooks called by readInput() — subclasses implement mode-specific behavior
	virtual ProcessedPacket processPacket(AVPacket& packet, int64_t firstDts, int64_t frameCount) = 0;
	virtual void onReadLoopFinished() = 0;

public:
	void addConnToList(webConnHdl &hdl);
	void sendVideoInformation(webConnHdl &connHdl);

	bool openInput();

	bool GetInputCodecInfo();

	void readInput();

	void closeInput();

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

		//timeout after 2 seconds of no activity
		if (thisObj->params.isRunning && (tickCount - thisObj->params.lastStopped > 2000.0))
			return 1;


		return 0;
	}

};

#endif
