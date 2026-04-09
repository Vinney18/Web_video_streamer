#ifndef PLAYBACKFFMPEGWRAPPER_H
#define PLAYBACKFFMPEGWRAPPER_H
#pragma once

#include "FFmpegWrapper.h"

class SyncHandler;

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

	SyncHandler* syncHandler_ = nullptr;
	std::string syncGroupId_;


	// todovineet no need to make this static
	static double getVideoDuration(const std::string& url);

public:
	PlaybackFFmpegWrapper(const Json::Value& streamInfo,
			WebsocketDataCallback _websocketCallback,
			WebsocketSDataCallback _websocketSCallback);

	~PlaybackFFmpegWrapper() override = default;

	void setSyncInfo(SyncHandler* handler, const std::string& groupId);
	void handleClientCommand(const std::string& jsonMessage) override;

protected:
	ProcessedPacket processPacket(AVPacket& packet, int64_t firstDts, int64_t frameCount) override;
	void onReadLoopFinished() override;
	void onBeforeReadInput() override;
};

#endif
