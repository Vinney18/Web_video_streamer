#include "CodecHandler/CodecHandler.h"
#include "CodecHandler/H264CodecHandler.h"
#include "CodecHandler/H265CodecHandler.h"

std::unique_ptr<CodecHandler> CodecHandler::create(AVCodecID codecId)
{
	switch (codecId)
	{
	case AV_CODEC_ID_H264:
		return std::make_unique<H264CodecHandler>();
	case AV_CODEC_ID_H265:
	default:
		return std::make_unique<H265CodecHandler>();
	}
}
