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

extern "C"
{
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavformat/avio.h>
}

using namespace std;
#pragma once

typedef std::set<websocketpp::connection_hdl, std::owner_less<websocketpp::connection_hdl>> con_list;
typedef std::function<void(websocketpp::connection_hdl& con_hndl, vector<uint8_t>& data)> WebsocketDataCallback;
typedef std::function<void(websocketpp::connection_hdl& con_hndl, string sdata)> WebsocketSDataCallback;

struct InterruptParams {
	int lastStopped;
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
	WebsocketDataCallback websocketCallback;
	WebsocketSDataCallback websocketSCallback;

	std::unique_ptr<Mp4frag> mp4FragCreator;
	std::map<OutputType, con_list> connections; //keys mp4 and mjpeg
	std::vector<std::pair<websocketpp::connection_hdl, bool>> tempConnections;

	//std::mutex connectionlock;

	AVFormatContext* inputFormatCtx = NULL;
	AVCodecContext* inputCodecCtx = NULL;
	AVCodecID inputCodecID = AV_CODEC_ID_NONE;
	AVFrame *pFrame = NULL;
	int inputFPS = 0;
	int videoStream = -1;

	AVCodec* decoderCodec = NULL;
	AVCodecContext* decoderCodecContext = NULL;
	AVFormatContext* mp4OutContext = NULL;

public:

	FFmpegWrapper(string _url, string _id, WebsocketDataCallback _websocketCallback, WebsocketSDataCallback _websocketSCallback) : Thread(), i2v::MjpegRoute(_id) {
		id = std::move(_id);
		url = std::move(_url);
		websocketCallback = _websocketCallback;
		websocketSCallback = _websocketSCallback;

		connections = { {mp4, con_list()}, { mjpeg , con_list()} };
	}

	~FFmpegWrapper();

	static int interrupt_cb(void *ctx)
	{
		InterruptParams* params = reinterpret_cast<InterruptParams*>(ctx);
		/*if (params->fmtCtx->start_time < 0) {
			params->num++;
		}
		else {
			params->num = 0;
		}*/

		//cout << params->num << std::endl;
		//timeout after 5 seconds of no activity
		if (!params->isRunning && (GetTickCount() - params->lastStopped >5000))
			return 1;
			//cout << "called";

		return 0;
	}

	static int ffmpegMp4Callback(void* ptr, uint8_t* buf, int buf_size) {
		auto data = buf;
		vector<uint8_t> chunk(data, data + buf_size);

		static_cast<FFmpegWrapper*>(ptr)->createMp4chunck(chunk);
		return buf_size;
	}

	void createMp4chunck(vector<uint8_t>& data) {
		mp4FragCreator->_parseChunk(data);
	}

	void addConnection(websocketpp::connection_hdl connHdl, bool useTranscoding);
	bool removeConnection(websocketpp::connection_hdl connHdl);
	InterruptParams params;

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

	int save_frame_as_jpeg(AVCodecContext *pCodecCtx, AVFrame *pFrame);
	void receiveMp4Chunk(vector<uint8_t> data);
};

