#include <string>
#include <iostream>
#include <atomic>
#include "Thread.h"
#include "Mp4frag.h"
#include "MjpegServer.h"
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
#include "libavutil/error.h"
//#include "libavutil/hwcontext.h"
//#include "libavutil/hwcontext_qsv.h"
#include "libavutil/mem.h"
}

#ifndef FFMPEGWRAPPER_H
#define FFMPEGWRAPPER_H
#pragma once

typedef std::set<websocketpp::connection_hdl, std::owner_less<websocketpp::connection_hdl>> con_list;
typedef std::function<void(websocketpp::connection_hdl& con_hndl, vector<uint8_t>& data)> WebsocketDataCallback;
typedef std::function<void(websocketpp::connection_hdl& con_hndl, string sdata)> WebsocketSDataCallback;

//typedef struct DecodeContext {
//	AVBufferRef *hw_device_ref;
//} DecodeContext;

struct InterruptParams {
	double lastStopped;
	bool isRunning;
};

enum OutputType {
	mp4,
	mjpeg
};

class FFmpegWrapper : public virtual Thread, public virtual i2v::MjpegRoute
{
private:
	string id;
	string url;
	int initial_seek_time = 0;
	WebsocketDataCallback websocketCallback;
	WebsocketSDataCallback websocketSCallback;
	bool usejmuxer;
	string playmode;
	bool fileseekingstarted = false;
	bool playbackFileStared = false;
	bool IsFilePaused = false;
	string connectionmode;
	bool playbackviaapache;
	std::unique_ptr<Mp4frag> mp4FragCreator;
	std::map<OutputType, con_list> connections; //keys mp4 and mjpeg
	std::vector<std::pair<websocketpp::connection_hdl, bool>> tempConnections;
	int start_time_ofplaybackfile;
	//std::mutex connectionlock;
	AVFormatContext* inputFormatCtx = NULL;
	AVCodecContext* inputCodecCtx = NULL;
	AVCodecID inputCodecID = AV_CODEC_ID_NONE;
	AVFrame *pFrame = NULL;
	int inputFPS = 0;
	int videoStream = -1;
	int cameraId = 0;
	string serverIp;
	int port = 8890;
	int sessionid = 0;
	AVCodec* decoderCodec = NULL;
	AVCodecContext* decoderCodecContext = NULL;
	AVFormatContext* mp4OutContext = NULL;
	bool isVideoStartedEventsent = false;
	AVCodec *jpegCodec;
	AVCodecContext *jpegContext;
    InterruptParams params;

	/*AVCodecContext *decoder_ctx = NULL;
	const AVCodec *decoder;
	AVPacket pkt = { 0 };
	AVFrame *frame = NULL, *sw_frame = NULL;
	DecodeContext decode = { NULL };*/
public:

	FFmpegWrapper(int _cameraId , string _url, string _id, int start_seek_time, WebsocketDataCallback _websocketCallback, WebsocketSDataCallback _websocketSCallback, bool _usejmuxer, string _connectionmode, bool _playbackviaapache, string _playmode, int _start_time_ofplaybackfile, string _serverIp, int _port, int _sessionid) : Thread(), i2v::MjpegRoute(_id) {
		id = std::move(_id);
		url = std::move(_url);
		websocketCallback = _websocketCallback;
		websocketSCallback = _websocketSCallback;
		usejmuxer = _usejmuxer;
		connectionmode = _connectionmode;
		initial_seek_time = start_seek_time;
		playbackviaapache = _playbackviaapache;
		playmode = _playmode;
		connections = { {mp4, con_list()}, { mjpeg , con_list()} };
		start_time_ofplaybackfile = _start_time_ofplaybackfile;
		cameraId = _cameraId;
		serverIp = _serverIp;
		port = _port;
		sessionid = _sessionid;
		//av_log_set_level(AV_LOG_QUIET);
	}

	~FFmpegWrapper();

	void addConnection(websocketpp::connection_hdl connHdl);
	bool removeConnection(websocketpp::connection_hdl connHdl);
	void seek_video(int offset_time);
	void Pause_video(); 
	void Resume_video();
	void SendVideoStartedEvent();


protected:
	virtual int run() override;

private:
	bool openInput();
	bool GetInputCodecInfo();
	void readInput();
	void closeInput();

	void addConnToList(websocketpp::connection_hdl hdl, OutputType outType);
	bool createOutput(OutputType outType);
	bool createMp4Output();
	bool createMjpegOutput();

	bool removeOutput(OutputType outType);
	void freeMp4OutMemory();
	void freeMjpegOutMemory();

	int save_frame_as_jpeg(AVCodecContext *pCodecCtx, AVFrame *pFrame, AVPacket* packet);
	void receiveMp4Chunk(vector<uint8_t> data);
	void Backwardseek_video(int offset_time);
	bool isLiveMode();


public:
#if __linux__
    static double GetTickCount(void)
    {
        struct timespec now;
        if (clock_gettime(CLOCK_MONOTONIC, &now))
            return 0;
        auto v = now.tv_sec * 1000.0 + now.tv_nsec / 1000000.0;
        return v;
    }
#endif

    static int interrupt_cb(void *ctx)
    {
        FFmpegWrapper* thisObj = reinterpret_cast<FFmpegWrapper*>(ctx);
        /*if (params->fmtCtx->start_time < 0) {
            params->num++;
        }
        else {
            params->num = 0;
        }*/

        //cout << params->num << std::endl;
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

    static int ffmpegMp4Callback(void* ptr, uint8_t* buf, int buf_size) {
        auto data = buf;
        vector<uint8_t> chunk(data, data + buf_size);

        static_cast<FFmpegWrapper*>(ptr)->createMp4chunck(chunk);
        return buf_size;
    }

    static int ffmpegDecodeCallback(void* ptr, uint8_t* buf, int buf_size) {
        auto data = buf;
        vector<uint8_t> chunk(data, data + buf_size);

        static_cast<FFmpegWrapper*>(ptr)->createMp4chunck(chunk);
        return buf_size;
    }

    void createMp4chunck(vector<uint8_t>& data) {
        mp4FragCreator->_parseChunk(data);
    }

};

#endif