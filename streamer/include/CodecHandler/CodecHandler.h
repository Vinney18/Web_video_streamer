#ifndef CODECHANDLER_H
#define CODECHANDLER_H
#pragma once

#include <memory>
#include <rtc/rtc.hpp>
#include <rtc/rtppacketizationconfig.hpp>

extern "C" {
#include <libavcodec/avcodec.h>
}

class CodecHandler {
public:
	virtual ~CodecHandler() = default;

	virtual void addCodecToMedia(rtc::Description::Video& media) = 0;

	virtual std::shared_ptr<rtc::RtpPacketizationConfig>
	setMediaHandler(std::shared_ptr<rtc::Track> track, bool isAvccFormat) = 0;

	static std::unique_ptr<CodecHandler> create(AVCodecID codecId);
};

#endif
