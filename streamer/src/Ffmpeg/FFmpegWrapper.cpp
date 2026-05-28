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
				onBeforeReadInput();
				if (mStop) { closeInput(); continue; }

				{
					auto now = std::chrono::system_clock::now();
					auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
				}
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
		int64_t firstDts = this->inputFormatCtx->streams[videoStream]->first_dts;
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

				ProcessedPacket processed = processPacket(packet, firstDts, frameCount);

				{
					std::lock_guard<std::mutex> lock(connectionsMutex);
					for (webConnHdl hndl : connections)
					{
						websocketCallback(hndl, processed.mp4Data, processed.position);
					}
				}
			}

			// Free the packet that was allocated by av_read_frame
			av_free_packet(&packet);
		}

		// Log why we exited the read loop
		if (mStop)
		{
			std::cout << "[" << cameraId << "] Read loop exited: mStop was set (frameCount=" << frameCount << ")" << std::endl;
		}
		else
		{
			char errbuf[AV_ERROR_MAX_STRING_SIZE];
			av_strerror(readResult, errbuf, sizeof(errbuf));
			std::cout << "[" << cameraId << "] Read loop exited: av_read_frame returned " << readResult
					  << " (" << errbuf << "), frameCount= " << frameCount << std::endl;
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
	this->inputFormatCtx = avformat_alloc_context();
	this->inputFormatCtx->interrupt_callback.callback = interrupt_cb;
	this->inputFormatCtx->interrupt_callback.opaque = this;
	const char *fileName = this->url.c_str();
	AVDictionary *options1 = nullptr;
	try
	{
		if (connectionmode == "udp")
		{
			av_dict_set(&options1, "rtsp_transport", "udp", 0);
		}
		else
		{
			av_dict_set(&options1, "rtsp_transport", "tcp", 0);
		}
		av_dict_set(&options1, "max_delay", "500000000", 0);		// 0.5 sec
		av_dict_set(&options1, "stimeout", "1500000000", 0);		// Timeout in microseconds
		av_dict_set(&options1, "analyzeduration", "1000000000", 0); // 20 seconds
		av_dict_set(&options1, "probesize", "1000000000", 0);		// 10 MB
	}
	catch (boost::bad_lexical_cast)
	{
		std::cout << "Invalid connection mode: " << connectionmode << ". Defaulting to TCP." << std::endl;
	}

	this->params.lastStopped = GetTickCount();

	if (avformat_open_input(&this->inputFormatCtx, fileName, NULL, &options1) != 0)
	{
		if (connectionmode == "")
		{
			av_dict_set(&options1, "rtsp_transport", "udp", 0);
			if (avformat_open_input(&this->inputFormatCtx, fileName, NULL, &options1) != 0)
			{
				return false;
			}
		}
		else
		{
			return false;
		}
	}

	return true;
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
		avcodec_close(inputCodecCtx);
		inputCodecCtx = NULL;
		// Close the video file
		avformat_close_input(&this->inputFormatCtx);
		inputFormatCtx = NULL;
		inputCodecID = AV_CODEC_ID_NONE;
	}
	catch (const std::exception &ex)
	{
		std::cout << ex.what() << std::endl;
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

void FFmpegWrapper::SendVideoStartedEvent()
{
	if (isVideoStartedEventsent)
	{
		return;
	}
	isVideoStartedEventsent = true;

	for (webConnHdl connHdl : connections)
	{
		websocketSCallback(connHdl, "Video_Started");
	}
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

	AVFormatContext *fmtCtx = avformat_alloc_context();
	if (!fmtCtx)
		return result;

	AVDictionary *options = nullptr;
	av_dict_set(&options, "rtsp_transport", "tcp", 0);

	av_dict_set(&options, "max_delay", "2000000", 0);
	av_dict_set(&options, "stimeout", "5000000", 0);
	av_dict_set(&options, "analyzeduration", "300000", 0);
	av_dict_set(&options, "probesize", "7000000", 0);

	if (avformat_open_input(&fmtCtx, url.c_str(), NULL, &options) != 0)
	{
		return result; // opened = false
	}

	result.opened = true;

	if (avformat_find_stream_info(fmtCtx, NULL) < 0)
	{
		avformat_close_input(&fmtCtx);
		return result; // opened = true, codecId = NONE
	}

	for (unsigned int i = 0; i < fmtCtx->nb_streams; i++)
	{
		if (fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			result.codecId = fmtCtx->streams[i]->codecpar->codec_id;
			break;
		}
	}

	avformat_close_input(&fmtCtx);
	return result;
}

