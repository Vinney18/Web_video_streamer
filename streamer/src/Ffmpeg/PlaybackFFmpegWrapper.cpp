#include "Ffmpeg/PlaybackFFmpegWrapper.h"
#include "SyncHandler.h"
#include "json/json.h"

PlaybackFFmpegWrapper::PlaybackFFmpegWrapper(const Json::Value &streamInfo,
											 WebsocketDataCallback _websocketCallback,
											 WebsocketSDataCallback _websocketSCallback)
	: FFmpegWrapper(streamInfo, _websocketCallback, _websocketSCallback)
{
	initial_seek_time = streamInfo.get("seekTime", 0).asInt();
	originalRequestTime = streamInfo.get("startTime", 0).asInt();
	videoDuration = static_cast<int>(round(streamInfo.get("durationMinutes", 0).asFloat() * 60));
	fastForwardFactor = streamInfo.get("playbackSpeed", 1.0).asFloat();
}

vector<uint8_t> PlaybackFFmpegWrapper::processPacket(AVPacket &packet, int64_t frameCount)
{
	if (frameCount == 1)
	{
		videoDuration = getVideoDuration(url);

		playbackFileStared = true;
		if (this->initial_seek_time > 0)
		{
			seek_video(this->initial_seek_time);
		}
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
			std::cout << "[Playback] No PTS/DTS available, using frame count for timing" << std::endl;
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

	return vector<uint8_t>(packet.data, packet.data + packet.size);
}

// todovineet extra code is written
void PlaybackFFmpegWrapper::onReadLoopFinished()
{
	int existingVideoDuration = static_cast<int>(videoDuration);

	auto currentVideoDuration = getVideoDuration(url);
	if (currentVideoDuration > existingVideoDuration + 1)
	{
		int difference = existingVideoDuration - initial_seek_time;
		if (difference < 0)
		{
			difference = 0;
		}
		originalRequestTime = originalRequestTime + difference + 1;
		initial_seek_time = initial_seek_time + difference + 1;
		videoDuration = currentVideoDuration;
		logger->error("[Playback] Server-reported duration differs from stream duration: serverDuration={}, streamDuration={}, originalRequestTime={}", existingVideoDuration, currentVideoDuration, originalRequestTime);
		// std::cout << "##########Warning: HTTP duration " << existingVideoDuration << " differs from stream duration " << currentVideoDuration << std::endl;
		return;
	}
	else
	{
		logger->error("[Playback] Server-reported duration matches stream duration: durationSeconds={}", existingVideoDuration);
	}

	playbackStartPTS = -1.0;
	playbackStartTime = std::chrono::steady_clock::time_point();

	// Calculate next playback time and send as JSON
	int nextPlaybackTime = getNextPlaybackTime();

	Json::Value msg;
	msg["type"] = "Playback_Finished";
	msg["time"] = nextPlaybackTime;
	Json::StreamWriterBuilder writerBuilder;
	std::string finishMessage = Json::writeString(writerBuilder, msg);

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
				logger->error("[Playback] Seek failed: {}", ex.what());
			}
			else
			{
				std::cout << "[Playback] Seek failed: " << ex.what() << std::endl;
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

	// Use the duration provided from HTTP response (already in seconds)
	int segmentDuration = static_cast<int>(videoDuration);

	// Calculate: original request time + (segment duration - seek time)
	// This gives us the timestamp where playback ended
	auto difference = segmentDuration - initial_seek_time;
	if (difference < 0)
	{
		difference = 0;
	}
	int nextTime = originalRequestTime + difference + 1;

	std::cout << "[Playback] Next playback time calculated: original=" << originalRequestTime
			  << ", segmentDuration=" << segmentDuration
			  << ", seekTime=" << initial_seek_time
			  << ", next=" << nextTime << std::endl;
	return nextTime;
}

void PlaybackFFmpegWrapper::handleClientCommand(const std::string &jsonMessage)
{
	Json::Value root;
	Json::Reader reader;
	if (!reader.parse(jsonMessage, root))
	{
		if (logger)
		{
			logger->warn("[Playback] Invalid JSON command: {}", jsonMessage);
		}
		return;
	}

	if (root.isMember("seek_Time"))
	{
		int seekTime = root["seek_Time"].asInt();
		if (seekTime >= 0)
		{
			seek_video(seekTime);
		}
	}
	else if (root.isMember("FastForward"))
	{
		float speed = root["FastForward"].asFloat();
		if (speed >= 0)
		{
			FastForward_video(speed);
		}
	}
	else if (root.isMember("Pause"))
	{
		Pause_video();
	}
}

void PlaybackFFmpegWrapper::setSyncInfo(SyncHandler* handler, const std::string& groupId)
{
	syncHandler_ = handler;
	syncGroupId_ = groupId;
}

void PlaybackFFmpegWrapper::onBeforeReadInput()
{
	if (syncHandler_ && !syncGroupId_.empty())
	{
		syncHandler_->waitForReady(syncGroupId_, this);
	}
}

double PlaybackFFmpegWrapper::getVideoDuration(const std::string &url)
{
	AVFormatContext *fmtCtx = avformat_alloc_context();

	if (!fmtCtx)
		return AV_CODEC_ID_NONE;

	AVDictionary *options = nullptr;
	av_dict_set(&options, "rtsp_transport", "tcp", 0);

	av_dict_set(&options, "max_delay", "2000000", 0);
	av_dict_set(&options, "stimeout", "5000000", 0);
	av_dict_set(&options, "analyzeduration", "300000", 0);
	av_dict_set(&options, "probesize", "7000000", 0);

	if (avformat_open_input(&fmtCtx, url.c_str(), NULL, &options) != 0)
	{
		return AV_CODEC_ID_NONE;
	}

	if (avformat_find_stream_info(fmtCtx, NULL) < 0)
	{
		avformat_close_input(&fmtCtx);
		return AV_CODEC_ID_NONE;
	}
	auto currentVideoDuration = (double)fmtCtx->duration / AV_TIME_BASE;
	avformat_close_input(&fmtCtx);
	return currentVideoDuration;
}
