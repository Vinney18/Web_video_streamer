#include <string>
#include <iostream>
#include <atomic>

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

typedef void(*CallbackFunction)(uint8_t*, int size, string id, bool isJpeg, bool SendMjpegUrl);

class FFmpegWrapper
{
private:
	string id;
	string url;
	atomic<bool> keepRunning;
	string inputCodec;
	bool useTranscoding;
	std::thread thread;

public:
	static CallbackFunction callback;

	FFmpegWrapper(string _url, string _id, bool _useTranscoding) {
		id = std::move(_id);
		keepRunning = true;
		url = std::move(_url);
		useTranscoding = _useTranscoding;
	}

	static int ReadFunc(void* ptr, uint8_t* buf, int buf_size)
	{
		try
		{
			callback(buf, buf_size, (char*)ptr, false, false);
			//fwrite(buf, 1, buf_size, myFile);
			return buf_size;
		}
		catch (const std::exception& e)
		{
			std::cout << "Exception occured:" << e.what();
		}
	}

	int save_frame_as_jpeg(AVCodecContext *pCodecCtx, AVFrame *pFrame) {
		AVCodec *jpegCodec = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
		if (!jpegCodec) {
			return -1;
		}
		AVCodecContext *jpegContext = avcodec_alloc_context3(jpegCodec);
		if (!jpegContext) {
			return -1;
		}

		jpegContext->pix_fmt = AV_PIX_FMT_YUVJ420P;

		jpegContext->height = pFrame->height;
		jpegContext->width = pFrame->width;
		jpegContext->time_base.num = 1;
		jpegContext->time_base.den = 25;

		int x = avcodec_open2(jpegContext, jpegCodec, NULL);
		if (avcodec_open2(jpegContext, jpegCodec, NULL) < 0) {
			return -1;
		}
		//FILE *JPEGFile;
		//char JPEGFName[256];

		AVPacket packet;
		packet.data = NULL, packet.size = 0;
		av_init_packet(&packet);
		int gotFrame;

		if (avcodec_encode_video2(jpegContext, &packet, pFrame, &gotFrame) < 0) {
			return -1;
		}
		callback(packet.data, packet.size, id, true, false);
		/*sprintf(JPEGFName, "dvr-%06d.jpg", FrameNo);
		JPEGFile = fopen(JPEGFName, "wb");
		fwrite(packet.data, 1, packet.size, JPEGFile);
		fclose(JPEGFile);*/

		av_free_packet(&packet);
		avcodec_close(jpegContext);
		return 0;
	}

	int Start() {
		//cout << id << endl;
		AVFormatContext *pFormatCtx = NULL;

		std::cout << "url is: " << this->url << std::endl;
		std::cout << "url is: " << url << std::endl;

		const char *fileName = this->url.c_str();
		std::cout << "Url = " << url << "\n";
		// Open file
		AVDictionary* options1 = nullptr;
		//av_dict_set(&options1, "probesize", "5000", 0);
		//av_dict_set(&options1, "analyzeduration", "10000000", 0);
		//av_dict_set(&options1, "reorder_queue_size", "0", 0);
		av_dict_set(&options1, "rtsp_transport", "tcp", 0);

		/*auto ret = avformat_open_input(&pFormatCtx, fileName, NULL, &options1);
		cout << "error" << ret << endl;

		char* ebuff = NULL;
		size_t size = 0;
		auto xt = av_strerror(ret, ebuff, size);

		for (size_t i = 0; i < size; i++)
		{
			cout << ebuff[i];

		}*/
		cout << "Reached 2";

		if (avformat_open_input(&pFormatCtx, fileName, NULL, &options1) != 0)
		{
			AVDictionary* options1 = nullptr;
			av_dict_set(&options1, "rtsp_transport", "udp", 0);
			cout << "Reached 3";

			if (avformat_open_input(&pFormatCtx, fileName, NULL, &options1) != 0)
			{
				cout << "Reached 4";
				if (avformat_open_input(&pFormatCtx, fileName, NULL, NULL) != 0) {
					cout << "Reached 5";

					return -1;
				}
			}
		}
		cout << "Reached 6";

		// Get infromation about streams
		if (avformat_find_stream_info(pFormatCtx, NULL) < 0)
			return -1; // Couldn't find stream information

		// Dump information about file onto standard error
		av_dump_format(pFormatCtx, 0, fileName, 0);

		int i;
		AVCodecContext *pCodecCtxOrig = NULL;
		AVCodecContext *pCodecCtx = NULL;


		// Find the first video stream
		int videoStream = -1;
		for (i = 0; i < pFormatCtx->nb_streams; i++)
			if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
				videoStream = i;
				break;
			}
		if (videoStream == -1)
			return -1; // Didn't find a video stream

		  // Get a pointer to the codec context for the video stream
		pCodecCtxOrig = pFormatCtx->streams[videoStream]->codec;


		int frameFinished;
		AVPacket packet;

		AVFormatContext *outFmtCtx = NULL;
		AVFrame *pFrame = NULL;
		int videoFPS = 0;

		if ((pCodecCtxOrig->codec_id == AV_CODEC_ID_H264 || pCodecCtxOrig->codec_id == AV_CODEC_ID_MJPEG) && !useTranscoding)
		{
			inputCodec = "H264";
			callback(NULL, 0, id, false, true);
		}
		else
		{
			inputCodec = "other";
			callback(NULL, 0, id, true, true);
		}

		if (inputCodec == "H264")
		{
			AVOutputFormat * outFmt = av_guess_format("mp4", NULL, NULL);

			AVStream * outStrm;
			avformat_alloc_output_context2(&outFmtCtx, outFmt, NULL, NULL);
			if (!(outStrm = avformat_new_stream(outFmtCtx, 0))) {
				return -1;
			}
			outFmtCtx->metadata = pFormatCtx->metadata;
			AVCodec * codec = NULL;
			avcodec_get_context_defaults3(outStrm->codec, codec);



			outStrm->codecpar->codec_id = pFormatCtx->streams[videoStream]->codecpar->codec_id;
			outStrm->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
			outStrm->codecpar->width = pFormatCtx->streams[videoStream]->codecpar->width;
			outStrm->codecpar->height = pFormatCtx->streams[videoStream]->codecpar->height;
			outStrm->codecpar->format = pFormatCtx->streams[videoStream]->codecpar->format;
			outStrm->codecpar->bit_rate = pFormatCtx->streams[videoStream]->codecpar->bit_rate;
			outStrm->time_base = pFormatCtx->streams[videoStream]->time_base;
			outStrm->codecpar->extradata = (uint8_t*)av_malloc(pCodecCtxOrig->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
			outStrm->codecpar->extradata_size = pCodecCtxOrig->extradata_size;
			memcpy(outStrm->codecpar->extradata, pCodecCtxOrig->extradata, pCodecCtxOrig->extradata_size);


			AVDictionary* options = nullptr;
			av_dict_set(&options, "movflags", "+frag_keyframe+empty_moov+default_base_moof+omit_tfhd_offset", 0);
			//av_dict_set(&options, "reset_timestamps", "1", 0);
			av_dict_set(&options, "b:v", "1024k", 0);

			uint8_t *buffer2 = NULL;
			int numBytes2 = 320 * 1024;
			buffer2 = (uint8_t *)av_malloc(numBytes2 * sizeof(uint8_t));
			AVIOContext* pIOCtx = avio_alloc_context(buffer2, numBytes2, 1, (void *)id.c_str(), 0, ReadFunc, 0);
			outFmtCtx->pb = pIOCtx;
			//avio_open(&outFmtCtx->pb, "test.mp4", AVIO_FLAG_WRITE);
			avformat_write_header(outFmtCtx, &options);
		}
		else if (inputCodec == "other") {
			AVCodec *pCodec = NULL;
			videoFPS = av_q2d(pFormatCtx->streams[videoStream]->r_frame_rate);
			std::cout << "fps :" << videoFPS << std::endl;
			// Find the decoder for the video stream
			pCodec = avcodec_find_decoder(pCodecCtxOrig->codec_id);
			if (pCodec == NULL) {
				fprintf(stderr, "Unsupported codec!\n");
				return -1; // Codec not found
			}
			// Copy context
			pCodecCtx = avcodec_alloc_context3(pCodec);
			if (avcodec_copy_context(pCodecCtx, pCodecCtxOrig) != 0) {
				fprintf(stderr, "Couldn't copy codec context");
				return -1; // Error copying codec context
			}
			// Open codec
			if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0)
				return -1; // Could not open codec



			// Allocate video frame
			pFrame = av_frame_alloc();

		}
		int interval = 1;
		if (videoFPS > 7) {
			interval = videoFPS / 5;
			if (videoFPS % 5 != 0)
				interval++;
		}
		cout << "interval" << interval << endl;
		i = 1;
		try
		{
			while (av_read_frame(pFormatCtx, &packet) >= 0 && keepRunning) {
				// Is this a packet from the video stream?
				if (packet.stream_index == videoStream) {
					//av_write_frame(outFmtCtx, &packet);
					if (inputCodec == "other")
					{
						avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);

						// Did we get a video frame?
						if (frameFinished) {
							/*if (++i <= 10)*/
							cout << i << endl;
							if (i == 1)
								save_frame_as_jpeg(pCodecCtx, pFrame);
							if (i++ >= interval)
								i = 1;
						}
					}
					else if (inputCodec == "H264") {
						auto x = av_interleaved_write_frame(outFmtCtx, &packet);
					}
					else {
						keepRunning = false;
					}
				}
				// Free the packet that was allocated by av_read_frame
				av_free_packet(&packet);

			}

			cout << "While stopped and isRunning: " << keepRunning << "+++++++++++++++++++++++++++++++++++++++++" << endl;

			if (!inputCodec.empty())
			{
				if (inputCodec == "H264")
				{
					//For mp4
						//av_write_trailer(outFmtCtx);
					avio_context_free(&outFmtCtx->pb);
					//avio_close(outFmtCtx->pb);
					avformat_free_context(outFmtCtx);


					// Close the codecs
					avcodec_close(pCodecCtxOrig);

					// Close the video file
				}
				else if (inputCodec == "other") {

					// Free the YUV frame
					av_free(pFrame);

					// Close the codecs
					avcodec_close(pCodecCtx);
					avcodec_close(pCodecCtxOrig);

					// Close the video file
				}
			}

			avformat_close_input(&pFormatCtx);

		}
		catch (const exception& ex)
		{
			cout << "Exception Occured @ FFmpegWrapper" << ex.what() << endl;
		}
		return 0;
	}

	void Stop() {
		keepRunning = false;
		cout << keepRunning << "+++++++++++++++++++++++++++++++++++++++++" << endl;
	}

	~FFmpegWrapper() {

	}

};

