#ifndef H264CODECHANDLER_H
#define H264CODECHANDLER_H
#pragma once

#include "CodecHandler.h"
#include <rtc/h264rtppacketizer.hpp>

class H264CodecHandler : public CodecHandler {
public:
	void addCodecToMedia(rtc::Description::Video& media) override;

	std::shared_ptr<rtc::RtpPacketizationConfig>
	setMediaHandler(std::shared_ptr<rtc::Track> track, bool isAvccFormat) override;
};

#endif
