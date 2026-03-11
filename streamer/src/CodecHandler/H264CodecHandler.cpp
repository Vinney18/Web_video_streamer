#include "CodecHandler/H264CodecHandler.h"

void H264CodecHandler::addCodecToMedia(rtc::Description::Video& media)
{
	media.addH264Codec(96);
}

std::shared_ptr<rtc::RtpPacketizationConfig>
H264CodecHandler::setMediaHandler(std::shared_ptr<rtc::Track> track, bool isAvccFormat)
{
	auto rtpConfig = std::make_shared<rtc::RtpPacketizationConfig>(
		1, "video-stream", 96, rtc::H264RtpPacketizer::defaultClockRate);

	auto separator = isAvccFormat
		? rtc::H264RtpPacketizer::Separator::LongStartSequence
		: rtc::H264RtpPacketizer::Separator::StartSequence;

	track->setMediaHandler(std::make_shared<rtc::H264RtpPacketizer>(separator, rtpConfig));
	return rtpConfig;
}
