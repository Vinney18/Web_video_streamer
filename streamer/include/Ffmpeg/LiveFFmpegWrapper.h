#ifndef LIVEFFMPEGWRAPPER_H
#define LIVEFFMPEGWRAPPER_H
#pragma once

#include "FFmpegWrapper.h"

class LiveFFmpegWrapper : public FFmpegWrapper
{
public:
	LiveFFmpegWrapper(const Json::Value& streamInfo,
			WebsocketDataCallback _websocketCallback,
			WebsocketSDataCallback _websocketSCallback);

	~LiveFFmpegWrapper() override = default;

protected:
	ProcessedPacket processPacket(AVPacket& packet, int64_t firstDts, int64_t frameCount) override;
	void onReadLoopFinished() override;
};

#endif
