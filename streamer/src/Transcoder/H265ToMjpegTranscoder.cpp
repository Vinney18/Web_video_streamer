#include "Transcoder/H265ToMjpegTranscoder.h"
#include "AppConfig.h"

extern "C" {
#include <libavutil/imgutils.h>
#include <libavutil/pixfmt.h>
}

// MJPEG quality. global_quality is expressed in lambda units; qscale ~4 is a
// good size/quality tradeoff (lower = better quality, larger frames).
static constexpr int kJpegQScale = 4;

H265ToMjpegTranscoder::H265ToMjpegTranscoder()
	: logger_(AppConfig::instance().logger())
{
}

H265ToMjpegTranscoder::~H265ToMjpegTranscoder()
{
	if (swsCtx_)       { sws_freeContext(swsCtx_); swsCtx_ = nullptr; }
	if (decoderCtx_)   { avcodec_free_context(&decoderCtx_); }
	if (encoderCtx_)   { avcodec_free_context(&encoderCtx_); }
	if (decodedFrame_) { av_frame_free(&decodedFrame_); }
	if (scaledFrame_)  { av_frame_free(&scaledFrame_); }
	if (jpegPacket_)   { av_packet_free(&jpegPacket_); }
}

bool H265ToMjpegTranscoder::init(const AVCodecParameters* inputPar)
{
	if (!inputPar || inputPar->width <= 0 || inputPar->height <= 0)
	{
		if (logger_) logger_->error("H265ToMjpeg: invalid input parameters");
		return false;
	}

	// --- HEVC decoder ---
	const AVCodec* dec = avcodec_find_decoder(AV_CODEC_ID_HEVC);
	if (!dec)
	{
		if (logger_) logger_->error("H265ToMjpeg: HEVC decoder not found");
		return false;
	}
	decoderCtx_ = avcodec_alloc_context3(dec);
	if (!decoderCtx_)
		return false;
	if (avcodec_parameters_to_context(decoderCtx_, inputPar) < 0)
	{
		if (logger_) logger_->error("H265ToMjpeg: failed to copy decoder parameters");
		return false;
	}
	if (avcodec_open2(decoderCtx_, dec, nullptr) < 0)
	{
		if (logger_) logger_->error("H265ToMjpeg: failed to open HEVC decoder");
		return false;
	}

	// --- MJPEG encoder ---
	const AVCodec* enc = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
	if (!enc)
	{
		if (logger_) logger_->error("H265ToMjpeg: MJPEG encoder not found");
		return false;
	}
	encoderCtx_ = avcodec_alloc_context3(enc);
	if (!encoderCtx_)
		return false;
	encoderCtx_->width       = inputPar->width;
	encoderCtx_->height      = inputPar->height;
	encoderCtx_->pix_fmt     = AV_PIX_FMT_YUVJ420P;   // full-range YUV for JPEG
	encoderCtx_->color_range = AVCOL_RANGE_JPEG;
	// MJPEG frames are independent intra JPEGs, so time_base has no effect on
	// output — but the encoder needs a non-zero value to open. Pacing is handled
	// upstream (PlaybackFFmpegWrapper); this is just a placeholder.
	encoderCtx_->time_base   = AVRational{1, 25};
	encoderCtx_->flags      |= AV_CODEC_FLAG_QSCALE;
	encoderCtx_->global_quality = FF_QP2LAMBDA * kJpegQScale;
	if (avcodec_open2(encoderCtx_, enc, nullptr) < 0)
	{
		if (logger_) logger_->error("H265ToMjpeg: failed to open MJPEG encoder");
		return false;
	}

	decodedFrame_ = av_frame_alloc();
	scaledFrame_  = av_frame_alloc();
	jpegPacket_   = av_packet_alloc();
	if (!decodedFrame_ || !scaledFrame_ || !jpegPacket_)
		return false;

	if (logger_)
		logger_->info("H265ToMjpeg: initialized {}x{}", inputPar->width, inputPar->height);
	return true;
}

bool H265ToMjpegTranscoder::ensureScaler(int width, int height, int srcFormat)
{
	if (swsCtx_ && scalerWidth_ == width && scalerHeight_ == height && scalerSrcFmt_ == srcFormat)
		return true;

	if (swsCtx_)
	{
		sws_freeContext(swsCtx_);
		swsCtx_ = nullptr;
	}

	swsCtx_ = sws_getContext(width, height, static_cast<AVPixelFormat>(srcFormat),
							 width, height, AV_PIX_FMT_YUVJ420P,
							 SWS_BILINEAR, nullptr, nullptr, nullptr);
	if (!swsCtx_)
	{
		if (logger_) logger_->error("H265ToMjpeg: failed to create scaler");
		return false;
	}

	// (Re)allocate the encoder-input frame for the new geometry.
	av_frame_unref(scaledFrame_);
	scaledFrame_->format = AV_PIX_FMT_YUVJ420P;
	scaledFrame_->width  = width;
	scaledFrame_->height = height;
	if (av_frame_get_buffer(scaledFrame_, 32) < 0)
	{
		if (logger_) logger_->error("H265ToMjpeg: failed to allocate scaled frame buffer");
		sws_freeContext(swsCtx_);
		swsCtx_ = nullptr;
		return false;
	}

	scalerWidth_  = width;
	scalerHeight_ = height;
	scalerSrcFmt_ = srcFormat;
	return true;
}

void H265ToMjpegTranscoder::transcode(const AVPacket& packet,
									  const std::function<void(std::vector<uint8_t>&)>& sink)
{
	int ret = avcodec_send_packet(decoderCtx_, &packet);
	if (ret < 0)
	{
		if (logger_) logger_->warn("H265ToMjpeg: avcodec_send_packet failed ({})", ret);
		return;
	}

	while (ret >= 0)
	{
		ret = avcodec_receive_frame(decoderCtx_, decodedFrame_);
		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
			break;
		if (ret < 0)
		{
			if (logger_) logger_->warn("H265ToMjpeg: avcodec_receive_frame failed ({})", ret);
			break;
		}

		if (ensureScaler(decodedFrame_->width, decodedFrame_->height, decodedFrame_->format))
		{
			// Make the reusable frame writable in case the encoder still holds a ref.
			if (av_frame_make_writable(scaledFrame_) >= 0)
			{
				sws_scale(swsCtx_, decodedFrame_->data, decodedFrame_->linesize, 0,
						  decodedFrame_->height, scaledFrame_->data, scaledFrame_->linesize);
				encodeFrame(scaledFrame_, sink);
			}
		}

		av_frame_unref(decodedFrame_);
	}
}

void H265ToMjpegTranscoder::encodeFrame(AVFrame* frame,
									   const std::function<void(std::vector<uint8_t>&)>& sink)
{
	frame->quality = encoderCtx_->global_quality;   // honored under AV_CODEC_FLAG_QSCALE

	int ret = avcodec_send_frame(encoderCtx_, frame);
	if (ret < 0)
	{
		if (logger_) logger_->warn("H265ToMjpeg: avcodec_send_frame failed ({})", ret);
		return;
	}

	while (ret >= 0)
	{
		ret = avcodec_receive_packet(encoderCtx_, jpegPacket_);
		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
			break;
		if (ret < 0)
		{
			if (logger_) logger_->warn("H265ToMjpeg: avcodec_receive_packet failed ({})", ret);
			break;
		}

		std::vector<uint8_t> jpeg(jpegPacket_->data, jpegPacket_->data + jpegPacket_->size);
		sink(jpeg);
		av_packet_unref(jpegPacket_);
	}
}
