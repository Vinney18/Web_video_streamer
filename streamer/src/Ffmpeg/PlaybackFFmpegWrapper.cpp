#include "Ffmpeg/PlaybackFFmpegWrapper.h"

PlaybackFFmpegWrapper::PlaybackFFmpegWrapper(string _cameraId, string _url,
		int start_seek_time,
		WebsocketDataCallback _websocketCallback,
		WebsocketSDataCallback _websocketSCallback,
		string _connectionmode,
		float playbackSpeed,
		int _requestTime,
		int _playbackFileDuration)
	: FFmpegWrapper(std::move(_cameraId), std::move(_url),
					_websocketCallback, _websocketSCallback,
					std::move(_connectionmode))
{
	initial_seek_time = start_seek_time;
	originalRequestTime = _requestTime;
	videoDuration = _playbackFileDuration;
	fastForwardFactor = playbackSpeed;
}

ProcessedPacket PlaybackFFmpegWrapper::processPacket(AVPacket& packet, int64_t firstDts, int64_t frameCount)
{
	int64_t position = round((this->inputFormatCtx->streams[videoStream]->cur_dts - firstDts) * this->inputFormatCtx->streams[videoStream]->time_base.num / this->inputFormatCtx->streams[videoStream]->time_base.den);

	if (frameCount == 1)
	{
		playbackFileStared = true;
		if (this->initial_seek_time > 0)
		{
			seek_video(this->initial_seek_time);
		}
		SendVideoStartedEvent();
	}

	// Frame timing — skip for first frame
	if (frameCount > 1)
	{
		// Get timestamp with fallback logic for files without PTS (like some .ts files)
		double currentPTS;

		if (packet.pts != AV_NOPTS_VALUE)
		{
			currentPTS = packet.pts * av_q2d(inputFormatCtx->streams[videoStream]->time_base);
		}
		else if (packet.dts != AV_NOPTS_VALUE)
		{
			currentPTS = packet.dts * av_q2d(inputFormatCtx->streams[videoStream]->time_base);
		}
		else
		{
			currentPTS = (frameCount - 1) / inputFPS;
			std::cout << "----------Warning: No PTS/DTS available, using frame count for timing" << std::endl;
		}

		if (playbackStartPTS < 0)
		{
			playbackStartPTS = currentPTS;
			playbackStartTime = std::chrono::steady_clock::now();
		}

		double relativeTime = (currentPTS - playbackStartPTS) / fastForwardFactor;
		auto targetTime = playbackStartTime + std::chrono::duration<double>(relativeTime);
		auto now = std::chrono::steady_clock::now();

		if (targetTime > now)
		{
			std::this_thread::sleep_until(targetTime);
		}
	}

	return {vector<uint8_t>(packet.data, packet.data + packet.size), position};
}

void PlaybackFFmpegWrapper::onReadLoopFinished()
{
	playbackStartPTS = -1.0;
	playbackStartTime = std::chrono::steady_clock::time_point();

	// Calculate next playback time and send as JSON
	int nextPlaybackTime = getNextPlaybackTime();

	// Send next playback time to WebSocket handler
	std::string finishMessage = "{\"event\":\"Playback_Finished\",\"cameraId\":\"" + cameraId + "\",\"nextTime\":" + std::to_string(nextPlaybackTime) + "}";

	for (webConnHdl hndl : connections)
	{
		websocketSCallback(hndl, finishMessage);
	}
	mStop = true;
}

void PlaybackFFmpegWrapper::seek_video(int time_toSeek_insec)
{
	cout << " url is " << url << " seconds " << time_toSeek_insec << endl;
	if (time_toSeek_insec > 0)
	{
		if (fileseekingstarted && !playbackFileStared)
		{
			return;
		}
		fileseekingstarted = true;
		// Seek is done on packet dts
		try
		{
			int64_t target_dts_usecs = static_cast<int64_t>(time_toSeek_insec) * 1000000;
			auto first_dts_usecs = (int64_t)round(this->inputFormatCtx->streams[videoStream]->first_dts * (double)this->inputFormatCtx->streams[videoStream]->time_base.num / this->inputFormatCtx->streams[videoStream]->time_base.den * AV_TIME_BASE);
			target_dts_usecs += first_dts_usecs;
			try
			{
				int rv = av_seek_frame(this->inputFormatCtx, -1, target_dts_usecs, AVSEEK_FLAG_FRAME);
				if (rv < 0)
				{
					fileseekingstarted = false;
				}
			}
			catch (exception ex)
			{
				cout << "my exc: " << ex.what() << endl;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(1000));
			fileseekingstarted = false;
		}
		catch (const exception &ex)
		{
			fileseekingstarted = false;
			if (logger)
			{
				logger->error("Exception while seek video: {}", ex.what());
			}
			else
			{
				std::cout << "Exception while seek video: " << ex.what() << std::endl;
			}
		}
	}
}

void PlaybackFFmpegWrapper::FastForward_video(float speed)
{
	if (speed > 0)
	{
		fastForwardFactor = speed;
		frameDuration = std::chrono::duration<double, std::milli>(1000.0 / (inputFPS * fastForwardFactor));
	}
}

int PlaybackFFmpegWrapper::getNextPlaybackTime()
{
	// Calculate the next playback time based on segment duration
	int nextTime = originalRequestTime;

	// First, check if we have duration from HTTP response (preferred method)
	if (videoDuration > 0)
	{
		// Use the duration provided from HTTP response (already in seconds)
		double segmentDuration = static_cast<double>(videoDuration);

		std::cout << "--------- Using HTTP response duration: " << segmentDuration << " seconds" << std::endl;

		// Calculate: original request time + (segment duration - seek time)
		// This gives us the timestamp where playback ended
		nextTime = originalRequestTime + static_cast<int>(segmentDuration - initial_seek_time) + 1;

		std::cout << "Calculated next playback time: original=" << originalRequestTime
				  << ", segmentDuration=" << segmentDuration
				  << ", seekTime=" << initial_seek_time
				  << ", next=" << nextTime << std::endl;
	}
	else if (inputFormatCtx && videoStream >= 0)
	{
		// Fall back to calculating from stream metadata
		double segmentDuration = 0.0;

		// Get duration from stream
		if (inputFormatCtx->streams[videoStream]->duration != AV_NOPTS_VALUE)
		{
			segmentDuration = inputFormatCtx->streams[videoStream]->duration *
							  av_q2d(inputFormatCtx->streams[videoStream]->time_base);
		}

		std::cout << "--------- Using stream metadata duration: " << segmentDuration << " seconds" << std::endl;

		// Calculate: original request time + (segment duration - seek time)
		// This gives us the timestamp where playback ended
		nextTime = originalRequestTime + static_cast<int>(segmentDuration - initial_seek_time) + 1;

		std::cout << "Calculated next playback time: original=" << originalRequestTime
				  << ", segmentDuration=" << segmentDuration
				  << ", seekTime=" << initial_seek_time
				  << ", next=" << nextTime << std::endl;
	}
	else
	{
		std::cout << "--------- Warning: No duration available, using original request time" << std::endl;
	}

	return nextTime;
}
