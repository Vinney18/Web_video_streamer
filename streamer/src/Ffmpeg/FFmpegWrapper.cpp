#include "Ffmpeg/FFmpegWrapper.h"
#define PRINT_FFMPEG_VERSION(lib)                         \
	std::cout << #lib << " Version: "                     \
			  << AV_VERSION_MAJOR(lib##_version()) << "." \
			  << AV_VERSION_MINOR(lib##_version()) << "." \
			  << AV_VERSION_MICRO(lib##_version()) << "\n";
int FFmpegWrapper::run()
{
	while (!mStop)
	{

		params.isRunning = false;
		auto opened = openInput();

		if (opened)
		{
			params.isRunning = true;
			GetInputCodecInfo();

			if (inputCodecID == AV_CODEC_ID_NONE)
			{
				std::lock_guard<std::mutex> lock(connectionsMutex);
				for (webConnHdl connHdl : connections)
				{
					websocketSCallback(connHdl, "unable_to_play");
				}
				mStop = true;
				closeInput();
			}
			else
			{
				// Build the H.265 -> MJPEG transcoder now that the input codec
				// and stream parameters are known. Only for H.265 sources when
				// transcoding was requested (client can't decode HEVC).
				if (transcodeToMjpeg_ && inputCodecID == AV_CODEC_ID_H265 && !transcoder_)
				{
					auto t = CodecTranscoder::create(AV_CODEC_ID_H265, AV_CODEC_ID_MJPEG);
					if (t && t->init(inputFormatCtx->streams[videoStream]->codecpar))
					{
						transcoder_ = std::move(t);
					}
					else if (logger)
					{
						logger->error("[FFmpeg] Failed to initialize H265->MJPEG transcoder: camera={}", cameraId);
					}
				}

				onBeforeReadInput();
				if (mStop) { closeInput(); continue; }

				// {
				// 	auto now = std::chrono::system_clock::now();
				// 	auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
				// }
				readInput();
				closeInput();
			}
		}

		if (!mStop)
		{
			{
				std::lock_guard<std::mutex> lock(connectionsMutex);

				for (webConnHdl connHdl : connections)
				{
					websocketSCallback(connHdl, "retrying");
				}
			}
			this_thread::sleep_for(std::chrono::milliseconds(500));
		}
	}

	// {
	// 	std::lock_guard<std::mutex> lock(connectionsMutex); // Protect access to the connections list

	// 	for (webConnHdl connHdl : connections)
	// 	{
	// 		websocketSCallback(connHdl, "Stopped");
	// 	}
	// }

	return 0;
}

FFmpegWrapper::~FFmpegWrapper()
{
	// std::cout << "FFmpegWrapper Destructor Called" << std::endl;
}

void FFmpegWrapper::readInput()
{
	AVPacket packet;

	try
	{
		this->params.lastStopped = GetTickCount();
		int64_t frameCount = 0;
		int readResult;
		while ((readResult = av_read_frame(this->inputFormatCtx, &packet)) >= 0 && !mStop)
		{

			std::unique_lock<std::mutex> lck(mThreadMutex);
			cv.wait(lck, [&]()
					{ return !mPaused; });
			this->params.lastStopped = GetTickCount();

			// Is this a packet from the video stream?
			if (packet.stream_index == videoStream)
			{
				frameCount++;

				// Always run processPacket for its side effects (Playback PTS
				// pacing / seek). Its raw-byte result is only used on the
				// pass-through path; when transcoding, we emit MJPEG instead.
				vector<uint8_t> processedPacket = processPacket(packet, frameCount);

				if (transcoder_)
				{
					transcoder_->transcode(packet, [&](std::vector<uint8_t>& jpeg)
					{
						std::lock_guard<std::mutex> lock(connectionsMutex);
						for (webConnHdl hndl : connections)
						{
							websocketCallback(hndl, jpeg);
						}
					});
				}
				else
				{
					std::lock_guard<std::mutex> lock(connectionsMutex);
					for (webConnHdl hndl : connections)
					{
						websocketCallback(hndl, processedPacket);
					}
				}
			}

			// Free the packet that was allocated by av_read_frame
			av_free_packet(&packet);
		}

		// Log why we exited the read loop
		if (mStop)
		{
			std::cout << "[FFmpeg] Read loop exited, stop requested: camera=" << cameraId << ", frameCount=" << frameCount << std::endl;
		}
		else
		{
			char errbuf[AV_ERROR_MAX_STRING_SIZE];
			av_strerror(readResult, errbuf, sizeof(errbuf));
			std::cout << "[FFmpeg] Read loop exited, av_read_frame failed: camera=" << cameraId << ", code=" << readResult
					  << ", error=" << errbuf << ", frameCount=" << frameCount << std::endl;
		}

		// Check if we're stopping - don't send finish message if stopped
		if (mStop)
		{

			return;
		}

		// Hook: post-loop actions (Live is no-op, Playback sends Playback_Finished)
		onReadLoopFinished();
	}
	catch (const exception &ex)
	{
		cout << ex.what() << std::endl;
	}
}

bool FFmpegWrapper::openInput()
{
	const char *fileName = this->url.c_str();

	// Almost all cameras stream over TCP, so try TCP first and fall back to UDP.
	auto tryOpen = [&](const char *transport) -> bool
	{
		this->inputFormatCtx = avformat_alloc_context();
		this->inputFormatCtx->interrupt_callback.callback = interrupt_cb;
		this->inputFormatCtx->interrupt_callback.opaque = this;

		AVDictionary *inputOptions = nullptr;
		av_dict_set(&inputOptions, "rtsp_transport", transport, 0);
		av_dict_set(&inputOptions, "max_delay", "500000000", 0);	   // 0.5 sec
		av_dict_set(&inputOptions, "stimeout", "1500000000", 0);	   // Timeout in microseconds
		av_dict_set(&inputOptions, "analyzeduration", "1000000000", 0);
		av_dict_set(&inputOptions, "probesize", "1000000000", 0);

		this->params.lastStopped = GetTickCount();
		int ret = avformat_open_input(&this->inputFormatCtx, fileName, NULL, &inputOptions);
		av_dict_free(&inputOptions);
		return ret == 0;
	};

	return tryOpen("tcp") || tryOpen("udp");
}

bool FFmpegWrapper::GetInputCodecInfo()
{
	// Get infromation about streams
	if (avformat_find_stream_info(this->inputFormatCtx, NULL) < 0)
		return false; // Couldn't find stream information

	int i;
	// Find the first video stream
	for (i = 0; i < this->inputFormatCtx->nb_streams; i++)
		if (this->inputFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			videoStream = i;
			break;
		}
	if (videoStream == -1)
		return false; // Didn't find a video stream

	// Get a pointer to the codec context for the video stream
	this->inputCodecCtx = this->inputFormatCtx->streams[videoStream]->codec;
	this->inputCodecID = this->inputFormatCtx->streams[videoStream]->codec->codec_id;
	double r_frame_rate_fps = av_q2d(this->inputFormatCtx->streams[videoStream]->r_frame_rate);
	double avg_frame_rate_fps = av_q2d(this->inputFormatCtx->streams[videoStream]->avg_frame_rate);

	// Prefer avg_frame_rate if available
	if (avg_frame_rate_fps > 0)
	{
		this->inputFPS = avg_frame_rate_fps;
	}
	else
	{
		this->inputFPS = r_frame_rate_fps;
	}
	if (inputFPS == 0 || inputFPS < 0 || inputFPS > 100)
	{
		inputFPS = 25;
	}

	// std::cout << "-------------Input FPS: " << this->inputFPS << " url " << url << "  camera id  " << cameraId << std::endl;

	return true;
}

void FFmpegWrapper::closeInput()
{
	try
	{
		// Tear down the transcoder before the format context — the run loop
		// rebuilds it after the next openInput()/GetInputCodecInfo().
		transcoder_.reset();
		// avcodec_close(inputCodecCtx);
		inputCodecCtx = NULL;
		// Close the video file
		avformat_close_input(&this->inputFormatCtx);
		inputFormatCtx = NULL;
		inputCodecID = AV_CODEC_ID_NONE;
	}
	catch (const std::exception &ex)
	{
		std::cout << "[FFmpeg] Failed to close input: " << ex.what() << std::endl;
	}
}



bool FFmpegWrapper::removeConnection(webConnHdl connHdl)
{
	std::lock_guard<std::mutex> lock(connectionsMutex);

	if (connections.empty())
	{
		return true;
	}

	auto it = connections.find(connHdl);
	if (it != connections.end())
	{
		connections.erase(it);
		return connections.empty();
	}

	return false;
}

void FFmpegWrapper::addConnToList(webConnHdl &connHdl)
{
	std::lock_guard<std::mutex> lock(connectionsMutex);
	connections.insert(connHdl);
}


void FFmpegWrapper::Pause_video()
{
	try
	{
		mPaused = !mPaused;
		cv.notify_one();
	}
	catch (const std::exception &)
	{
		cout << "Exception while  pause video on recording server ";
	}
}

FFmpegWrapper::ProbeResult FFmpegWrapper::probeCodec(const std::string &url)
{
	ProbeResult result;

	// Mirror openInput(): most cameras stream over TCP, fall back to UDP.
	auto tryProbe = [&](const char *transport) -> bool
	{
		AVFormatContext *fmtCtx = avformat_alloc_context();
		if (!fmtCtx)
			return false;

		AVDictionary *options = nullptr;
		av_dict_set(&options, "rtsp_transport", transport, 0);
		av_dict_set(&options, "max_delay", "2000000", 0);
		av_dict_set(&options, "stimeout", "5000000", 0);
		av_dict_set(&options, "analyzeduration", "300000", 0);
		av_dict_set(&options, "probesize", "7000000", 0);

		int ret = avformat_open_input(&fmtCtx, url.c_str(), NULL, &options);
		av_dict_free(&options);
		if (ret != 0)
			return false; // could not open with this transport

		result.opened = true;

		if (avformat_find_stream_info(fmtCtx, NULL) >= 0)
		{
			for (unsigned int i = 0; i < fmtCtx->nb_streams; i++)
			{
				if (fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
				{
					result.codecId = fmtCtx->streams[i]->codecpar->codec_id;
					break;
				}
			}
		}

		avformat_close_input(&fmtCtx);
		return result.codecId != AV_CODEC_ID_NONE;
	};

	if (!tryProbe("tcp"))
		tryProbe("udp");

	return result;
}

