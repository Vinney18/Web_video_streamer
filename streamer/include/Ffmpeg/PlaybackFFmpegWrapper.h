#ifndef PLAYBACKFFMPEGWRAPPER_H
#define PLAYBACKFFMPEGWRAPPER_H
#pragma once

#include "FFmpegWrapper.h"

class PlaybackFFmpegWrapper : public FFmpegWrapper
{
private:
	int initial_seek_time = 0;
	int originalRequestTime = 0;
	int videoDuration = 0;
	bool fileseekingstarted = false;
	bool playbackFileStared = false;
	float fastForwardFactor = 1.0;
	std::chrono::duration<double, std::milli> frameDuration;
	double playbackStartPTS = -1.0;
	std::chrono::steady_clock::time_point playbackStartTime;

	void seek_video(int offset_time);
	void FastForward_video(float factor);
	int getNextPlaybackTime();

public:
	PlaybackFFmpegWrapper(string _cameraId, string _url,
			int start_seek_time,
			WebsocketDataCallback _websocketCallback,
			WebsocketSDataCallback _websocketSCallback,
			string _connectionmode,
			float playbackSpeed,
			int _requestTime = 0,
			int _playbackFileDuration = 0);

	~PlaybackFFmpegWrapper() override = default;

	void handleClientCommand(const std::string& jsonMessage) override;

protected:
	ProcessedPacket processPacket(AVPacket& packet, int64_t firstDts, int64_t frameCount) override;
	void onReadLoopFinished() override;
};

#endif
