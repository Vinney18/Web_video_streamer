#include "Ffmpeg/LiveFFmpegWrapper.h"

LiveFFmpegWrapper::LiveFFmpegWrapper(const Json::Value& streamInfo,
		WebsocketDataCallback _websocketCallback,
		WebsocketSDataCallback _websocketSCallback)
	: FFmpegWrapper(streamInfo, _websocketCallback, _websocketSCallback)
{
	interruptTimeoutMs = 3000.0;  // 3s timeout for live streams
}

vector<uint8_t> LiveFFmpegWrapper::processPacket(AVPacket& packet, int64_t frameCount)
{
	if (frameCount == 1)
	{
		SendVideoStartedEvent();
	}

	return vector<uint8_t>(packet.data, packet.data + packet.size);
}

void LiveFFmpegWrapper::onReadLoopFinished()
{
	// Live mode: nothing to do on read loop exit — retry logic in run() handles reconnect
}
