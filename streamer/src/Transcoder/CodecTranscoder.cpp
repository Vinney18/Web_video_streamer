#include "Transcoder/CodecTranscoder.h"
#include "Transcoder/H265ToMjpegTranscoder.h"

std::unique_ptr<CodecTranscoder> CodecTranscoder::create(AVCodecID src, AVCodecID dst)
{
	// AV_CODEC_ID_H265 is an alias of AV_CODEC_ID_HEVC.
	if (src == AV_CODEC_ID_HEVC && dst == AV_CODEC_ID_MJPEG)
		return std::make_unique<H265ToMjpegTranscoder>();

	return nullptr;
}
