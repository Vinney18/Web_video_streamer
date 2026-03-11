#ifndef H265CODECHANDLER_H
#define H265CODECHANDLER_H
#pragma once

#include "CodecHandler.h"
#include <rtc/h265rtppacketizer.hpp>

class H265CodecHandler : public CodecHandler {
public:
	void addCodecToMedia(rtc::Description::Video& media) override;

	std::shared_ptr<rtc::RtpPacketizationConfig>
	setMediaHandler(std::shared_ptr<rtc::Track> track, bool isAvccFormat) override;
};

#endif
