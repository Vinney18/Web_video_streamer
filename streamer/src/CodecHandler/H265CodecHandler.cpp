#include "CodecHandler/H265CodecHandler.h"

void H265CodecHandler::addCodecToMedia(rtc::Description::Video& media)
{
	media.addH265Codec(96);
}

std::shared_ptr<rtc::RtpPacketizationConfig>
H265CodecHandler::setMediaHandler(std::shared_ptr<rtc::Track> track, bool isAvccFormat)
{
	auto rtpConfig = std::make_shared<rtc::RtpPacketizationConfig>(
		1, "video-stream", 96, rtc::H265RtpPacketizer::defaultClockRate);

	auto separator = isAvccFormat
		? rtc::H265RtpPacketizer::Separator::LongStartSequence
		: rtc::H265RtpPacketizer::Separator::StartSequence;

	track->setMediaHandler(std::make_shared<rtc::H265RtpPacketizer>(separator, rtpConfig));
	return rtpConfig;
}
