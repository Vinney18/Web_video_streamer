#include "Ffmpeg/LiveFFmpegWrapper.h"

LiveFFmpegWrapper::LiveFFmpegWrapper(string _cameraId, string _url,
		WebsocketDataCallback _websocketCallback,
		WebsocketSDataCallback _websocketSCallback,
		string _connectionmode)
	: FFmpegWrapper(std::move(_cameraId), std::move(_url),
					_websocketCallback, _websocketSCallback,
					std::move(_connectionmode))
{
}

ProcessedPacket LiveFFmpegWrapper::processPacket(AVPacket& packet, int64_t /*firstDts*/, int64_t frameCount)
{
	if (frameCount == 1)
	{
		SendVideoStartedEvent();
	}

	return {vector<uint8_t>(packet.data, packet.data + packet.size), -2};
}

void LiveFFmpegWrapper::onReadLoopFinished()
{
	// Live mode: nothing to do on read loop exit — retry logic in run() handles reconnect
}
