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

using namespace std;
#pragma once

typedef std::set<websocketpp::connection_hdl, std::owner_less<websocketpp::connection_hdl>> con_list;
typedef std::function<void(websocketpp::connection_hdl& con_hndl, vector<uint8_t>& data)> WebsocketDataCallback;
typedef std::function<void(websocketpp::connection_hdl& con_hndl, string sdata)> WebsocketSDataCallback;

//typedef struct DecodeContext {
//	AVBufferRef *hw_device_ref;
//} DecodeContext;

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
	int initial_seek_time = 0;
	WebsocketDataCallback websocketCallback;
	WebsocketSDataCallback websocketSCallback;
	bool usejmuxer;
	string playmode;
	bool fileseekingstarted = false;
	bool playbackFileStared = false;
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

	AVCodec* decoderCodec = NULL;
	AVCodecContext* decoderCodecContext = NULL;
	AVFormatContext* mp4OutContext = NULL;

	AVCodec *jpegCodec;
	AVCodecContext *jpegContext;

	/*AVCodecContext *decoder_ctx = NULL;
	const AVCodec *decoder;
	AVPacket pkt = { 0 };
	AVFrame *frame = NULL, *sw_frame = NULL;
	DecodeContext decode = { NULL };*/
public:

	FFmpegWrapper(int _cameraId , string _url, string _id, int start_seek_time, WebsocketDataCallback _websocketCallback, WebsocketSDataCallback _websocketSCallback, bool _usejmuxer, string _connectionmode, bool _playbackviaapache, string _playmode, int _start_time_ofplaybackfile, string _serverIp, int _port) : Thread(), i2v::MjpegRoute(_id) {
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
		//av_log_set_level(AV_LOG_QUIET);
	}

	~FFmpegWrapper();

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
		if (thisObj->params.isRunning && (tickCount - thisObj->params.lastStopped > 20000))
			return 1;

		//timeout after 7 seconds of no activity
		if (!thisObj->params.isRunning && (tickCount - thisObj->params.lastStopped > 7000))
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

	void addConnection(websocketpp::connection_hdl connHdl);
	bool removeConnection(websocketpp::connection_hdl connHdl);
	void seek_video(int offset_time);
	InterruptParams params;


	//typedef struct DecodeContext {

	//	AVBufferRef *hw_device_ref;

	//} DecodeContext;


	//static AVPixelFormat get_format(AVCodecContext *avctx, const enum AVPixelFormat *pix_fmts)
	//{
	//	while (*pix_fmts != AV_PIX_FMT_NONE) {

	//		if (*pix_fmts == AV_PIX_FMT_QSV) {

	//			DecodeContext *decode = reinterpret_cast<DecodeContext*>(avctx->opaque);

	//			AVHWFramesContext  *frames_ctx;

	//			AVQSVFramesContext *frames_hwctx;

	//			int ret;



	//			/* create a pool of surfaces to be used by the decoder */

	//			avctx->hw_frames_ctx = av_hwframe_ctx_alloc(decode->hw_device_ref);

	//			if (!avctx->hw_frames_ctx)

	//				return AV_PIX_FMT_NONE;

	//			frames_ctx = (AVHWFramesContext*)avctx->hw_frames_ctx->data;

	//			frames_hwctx = reinterpret_cast<AVQSVFramesContext*>(frames_ctx->hwctx);



	//			frames_ctx->format = AV_PIX_FMT_QSV;

	//			frames_ctx->sw_format = avctx->sw_pix_fmt;

	//			frames_ctx->width = FFALIGN(avctx->coded_width, 32);

	//			frames_ctx->height = FFALIGN(avctx->coded_height, 32);

	//			frames_ctx->initial_pool_size = 32;



	//			frames_hwctx->frame_type = MFX_MEMTYPE_VIDEO_MEMORY_DECODER_TARGET;



	//			ret = av_hwframe_ctx_init(avctx->hw_frames_ctx);

	//			if (ret < 0)

	//				return AV_PIX_FMT_NONE;



	//			return AV_PIX_FMT_QSV;

	//		}



	//		pix_fmts++;

	//	}

	//	fprintf(stderr, "The QSV pixel format not offered in get_format()\n");
	//	return AV_PIX_FMT_NONE;
	//}

	//static int decode_packet(AVCodecContext *decoder_ctx, AVFrame *frame, AVFrame *sw_frame, AVPacket *pkt, FFmpegWrapper *wrapper)
	//{
	//	int ret = 0;



	//	ret = avcodec_send_packet(decoder_ctx, pkt);

	//	if (ret < 0) {

	//		fprintf(stderr, "Error during decoding\n");

	//		return ret;

	//	}



	//	while (ret >= 0) {

	//		int i, j;



	//		ret = avcodec_receive_frame(decoder_ctx, frame);

	//		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)

	//			break;

	//		else if (ret < 0) {

	//			fprintf(stderr, "Error during decoding\n");

	//			return ret;

	//		}


	//		/* A real program would do something useful with the decoded frame here.

	//		 * We just retrieve the raw data and write it to a file, which is rather

	//		 * useless but pedagogic. */




	//		 	ret = av_hwframe_transfer_data(sw_frame, frame, 0);

	//			 if (ret < 0) {

	//				 fprintf(stderr, "Error transferring the data to system memory\n");

	//				 goto fail;

	//			 }
	//			 //wrapper->save_frame_as_jpeg(decoder_ctx, sw_frame);
	//			 AVPacket packet;
	//			 packet.data = NULL, packet.size = 0;
	//			 av_init_packet(&packet);
	//			 int gotFrame;

	//			 if (avcodec_encode_video2(wrapper->jpegContext, &packet, sw_frame, &gotFrame) < 0) {
	//				 return -1;
	//			 }

	//		/*	 vector<uint8_t> frame1(packet.data, packet.data + packet.size);
	//			 wrapper->send(frame1);
	//			 av_free_packet(&packet);*/


	//			/* for (i = 0; i < FF_ARRAY_ELEMS(sw_frame->data) && sw_frame->data[i]; i++)

	//				 for (j = 0; j < (sw_frame->height >> (i > 0)); j++)

	//					 avio_write(output_ctx, sw_frame->data[i] + j * sw_frame->linesize[i], sw_frame->width);*/

	//	fail:

	//		av_frame_unref(sw_frame);
	//		av_frame_unref(frame);

	//		if (ret < 0)

	//			return ret;

	//	}
	//	return 0;
	//}




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
	
};

