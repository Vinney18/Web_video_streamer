#ifndef LIVEFFMPEGWRAPPER_H
#define LIVEFFMPEGWRAPPER_H
#pragma once

#include "FFmpegWrapper.h"

class LiveFFmpegWrapper : public FFmpegWrapper
{
public:
	LiveFFmpegWrapper(string _cameraId, string _url,
			WebsocketDataCallback _websocketCallback,
			WebsocketSDataCallback _websocketSCallback,
			string _connectionmode, string _serverIp, int _port,
			std::shared_ptr<spdlog::logger> _logger);

	~LiveFFmpegWrapper() override = default;

protected:
	ProcessedPacket processPacket(AVPacket& packet, int64_t firstDts, int64_t frameCount) override;
	void onReadLoopFinished() override;
};

#endif
