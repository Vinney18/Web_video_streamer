#ifndef H265TOMJPEGTRANSCODER_H
#define H265TOMJPEGTRANSCODER_H
#pragma once

#include "Transcoder/CodecTranscoder.h"

#include <memory>
#include <spdlog/spdlog.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libswscale/swscale.h>
}

// Decodes an HEVC (H.265) elementary stream and re-encodes each frame as a
// standalone JPEG (MJPEG). Used to serve H.265 sources to browsers that cannot
// decode HEVC, delivering the JPEG frames over the WebSocket relay.
class H265ToMjpegTranscoder : public CodecTranscoder {
public:
	H265ToMjpegTranscoder();
	~H265ToMjpegTranscoder() override;

	bool init(const AVCodecParameters* inputPar) override;

	void transcode(const AVPacket& packet,
				   const std::function<void(std::vector<uint8_t>&)>& sink) override;

private:
	// Lazily (re)build the scaler once the decoded frame geometry is known.
	bool ensureScaler(int width, int height, int srcFormat);
	// Encode one scaled frame to JPEG and hand the bytes to sink.
	void encodeFrame(AVFrame* frame,
					 const std::function<void(std::vector<uint8_t>&)>& sink);

	AVCodecContext* decoderCtx_ = nullptr;
	AVCodecContext* encoderCtx_ = nullptr;
	SwsContext*     swsCtx_     = nullptr;

	AVFrame*  decodedFrame_ = nullptr;  // decoder output
	AVFrame*  scaledFrame_  = nullptr;  // encoder input (YUVJ420P)
	AVPacket* jpegPacket_   = nullptr;  // encoder output

	int scalerWidth_  = 0;
	int scalerHeight_ = 0;
	int scalerSrcFmt_ = -1;             // AVPixelFormat the scaler was built for

	std::shared_ptr<spdlog::logger> logger_;
};

#endif
