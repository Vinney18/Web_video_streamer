#include "FFmpegWrapper.h"
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
			createRgbaOutput();

			if (!tempConnections.empty())
			{

				for (auto &el : tempConnections)
				{
					if (inputCodecID == AV_CODEC_ID_NONE)
					{
						// close websocket if not able to play
						websocketSCallback(el.first, "unable_to_play");
					}
					else
					{
						addConnection(el.first);
					}
				}
				tempConnections.clear();
			}
			if (inputCodecID == AV_CODEC_ID_NONE)
			{

				mStop = true;
				closeInput();
			}
			else
			{

				readInput();

				freeRgbaOutMemory();
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
			this_thread::sleep_for(std::chrono::seconds(1));
		}
	}

	{
		std::lock_guard<std::mutex> lock(connectionsMutex); // Protect access to the connections list

		for (webConnHdl connHdl : connections)
		{
			websocketSCallback(connHdl, "Stopped");
		}
	}

	return 0;
}

FFmpegWrapper::~FFmpegWrapper()
{
	// std::cout << "FFmpegWrapper Destructor Called" << std::endl;
}

// useing pts instead of manually calculating seek time or delay
//  At start of playback
//  auto playbackStartTime = std::chrono::steady_clock::now();
//  double playbackStartPTS = -1.0;

// while (av_read_frame(this->inputFormatCtx, &packet) >= 0 && !mStop)
// {
//     if (packet.stream_index == videoStream)
//     {
//         // Get actual timestamp from packet
//         double currentPTS = packet.pts * av_q2d(inputFormatCtx->streams[videoStream]->time_base);

//         if (playbackStartPTS < 0)
//             playbackStartPTS = currentPTS;

//         // Calculate relative time
//         double relativeTime = (currentPTS - playbackStartPTS) / fastForwardFactor;

//         // Calculate target wall-clock time
//         auto targetTime = playbackStartTime + std::chrono::duration<double>(relativeTime);

//         // Sleep until target time (only for file playback, not live)
//         if (!isLiveMode())
//         {
//             std::this_thread::sleep_until(targetTime);
//         }

//         // Send packet...
//     }
// }

void FFmpegWrapper::readInput()
{
	int frameFinished;
	AVPacket packet;
	AVPixelFormat pixFormat;

	if (inputFPS == 0 || inputFPS < 0 || inputFPS > 100)
	{
		inputFPS = 25;
	}

	std::mutex mut;

	AVPacket packetEncoded;
	av_init_packet(&packetEncoded);

	try
	{
		this->params.lastStopped = GetTickCount();
		bool sendData = true;
		int64_t firstDts = this->inputFormatCtx->streams[videoStream]->first_dts;
		auto startTime = std::chrono::steady_clock::now();
		int64_t frameCount = 0;
		double playbackStartPTS = -1.0; // Track the first PTS for playback sync
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
				int64_t position;
				if (isLiveMode())
					position = -2;
				else
				{
					position = round((this->inputFormatCtx->streams[videoStream]->cur_dts - firstDts) * this->inputFormatCtx->streams[videoStream]->time_base.num / this->inputFormatCtx->streams[videoStream]->time_base.den);
				}

				// h265
				if (outputType == rgba && !connections.empty())
				{
					
					
				}
				// h264
				if (outputType == mp4 && !connections.empty())
				{
					if (frameCount == 1)
					{
						if (isLiveMode())
						{
							SendVideoStartedEvent();
						}
						else
						{
							playbackFileStared = true;
							sendData = false;
							if (this->initial_seek_time > 0)
							{
								seek_video(this->initial_seek_time);
							}
							SendVideoStartedEvent();
							sendData = true;
						}
					}

					if (!isLiveMode() && frameCount > 1)
					{
						// Get timestamp with fallback logic for files without PTS (like some .ts files)
						double currentPTS;

						if (packet.pts != AV_NOPTS_VALUE)
						{
							// Use PTS if available (preferred)
							currentPTS = packet.pts * av_q2d(inputFormatCtx->streams[videoStream]->time_base);
						}
						else if (packet.dts != AV_NOPTS_VALUE)
						{
							// Fallback to DTS if PTS not available
							currentPTS = packet.dts * av_q2d(inputFormatCtx->streams[videoStream]->time_base);
						}
						else
						{
							// Fallback to frame-counting if no timestamps available
							currentPTS = (frameCount - 1) / inputFPS;
							std::cout << "----------Warning: No PTS/DTS available, using frame count for timing" << std::endl;
						}

						// Initialize playback start PTS on first frame with actual data to send
						// (after seek completes if there was an initial seek)
						if (playbackStartPTS < 0 && sendData)
						{
							playbackStartPTS = currentPTS;
							startTime = std::chrono::steady_clock::now(); // Reset start time after seek
						}

						// Calculate relative time from start, adjusted for playback speed
						double relativeTime = (currentPTS - playbackStartPTS) / fastForwardFactor;

						// Calculate target wall-clock time
						auto targetTime = startTime + std::chrono::duration<double>(relativeTime);

						// Debug timing info
						auto now = std::chrono::steady_clock::now();
						double elapsed = std::chrono::duration<double>(now - startTime).count();
						double sleepTime = std::chrono::duration<double>(targetTime - now).count();

						// std::cout << "[" << cameraId << "] F#" << frameCount
						// 		  << " PTS:" << std::fixed << std::setprecision(3) << currentPTS
						// 		  << " RelT:" << relativeTime
						// 		  << " Elap:" << elapsed
						// 		  << " Sleep:" << sleepTime << "s" << std::endl;

						// Sleep until target time
						if (targetTime > now)
						{
							std::this_thread::sleep_until(targetTime);
						}
					}
					if (sendData)
					{
						vector<uint8_t> mp4Data(packet.data, packet.data + packet.size);
						int64_t savedPosition = position;

						if (savedPosition >= 0)
						{
							mp4Data.insert(mp4Data.begin(), sizeof(savedPosition), 0);
							int64_t tempPos = savedPosition;
							for (size_t i = 0; i < sizeof(tempPos); ++i)
							{
								mp4Data[i] = tempPos & 0xFF;
								tempPos >>= 8;
							}
						}

						{
							std::lock_guard<std::mutex> lock(connectionsMutex);
							for (webConnHdl hndl : connections)
							{
								websocketCallback(hndl, mp4Data, savedPosition);
							}
						}
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
					  << " (" << errbuf << "), frameCount=" << frameCount << std::endl;
		}

		// Check if we're stopping - don't send finish message if stopped
		if (mStop)
		{

			return;
		}

		if (!isLiveMode())
		{
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
	}
	catch (const exception &ex)
	{
		cout << ex.what() << std::endl;
	}
}

bool FFmpegWrapper::openInput()
{
	// std::cout << "FFmpeg Version Info:\n";

	// std::cout << "  libavcodec  : "
	//           << AV_VERSION_MAJOR(avcodec_version()) << "."
	//           << AV_VERSION_MINOR(avcodec_version()) << "."
	//           << AV_VERSION_MICRO(avcodec_version()) << "\n";

	// std::cout << "  libavformat : "
	//           << AV_VERSION_MAJOR(avformat_version()) << "."
	//           << AV_VERSION_MINOR(avformat_version()) << "."
	//           << AV_VERSION_MICRO(avformat_version()) << "\n";

	// std::cout << "  libavutil   : "
	//           << AV_VERSION_MAJOR(avutil_version()) << "."
	//           << AV_VERSION_MINOR(avutil_version()) << "."
	//           << AV_VERSION_MICRO(avutil_version()) << "\n";

	this->inputFormatCtx = avformat_alloc_context();
	this->inputFormatCtx->interrupt_callback.callback = interrupt_cb;
	this->inputFormatCtx->interrupt_callback.opaque = this;
	const char *fileName = this->url.c_str();
	// const char* fileName = "rtsp://192.168.0.40:8556/test";
	// cout << fileName << endl;
	//  Open file
	AVDictionary *options1 = nullptr;
	try
	{
		if (connectionmode == "tcp")
		{
			av_dict_set(&options1, "rtsp_transport", "tcp", 0);
		}
		else if (connectionmode == "udp")
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
		// bad parameter
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

	// Dump information about file onto standard error
	// av_dump_format(this->inputFormatCtx, 0, fileName, 0);

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

	std::cout << "-------------Input FPS: " << this->inputFPS << " url " << url << "  camera id  " << cameraId << std::endl;

	return true;
}

bool FFmpegWrapper::createRgbaOutput()
{
	if (this->inputCodecCtx == NULL)
	{
		fprintf(stderr, "Unsupported codec!\n");
		return false;
	}
	// Find the decoder for the video stream
	this->decoderCodec = avcodec_find_decoder(this->inputCodecCtx->codec_id);
	if (this->decoderCodec == NULL)
	{
		fprintf(stderr, "Unsupported codec!\n");
		return false; // Codec not found
	}

	// Copy context
	this->decoderCodecContext = avcodec_alloc_context3(this->decoderCodec);
	if (avcodec_copy_context(this->decoderCodecContext, this->inputCodecCtx) != 0)
	{
		fprintf(stderr, "Couldn't copy codec context");
		return false; // Error copying codec context
	}
	// this->decoderCodecContext->
	//  Open codec
	if (avcodec_open2(this->decoderCodecContext, this->decoderCodec, NULL) < 0)
		return false; // Could not open codec

	// Allocate video frame
	pFrame = av_frame_alloc();

	// Allocate the RGB frame
	rgb_frame = av_frame_alloc();
	if (rgb_frame == NULL)
	{
		std::cerr << "Error allocating RGB frame" << std::endl;
		return false;
	}

	return true;
}

void FFmpegWrapper::freeRgbaOutMemory()
{
	// Free the YUV frame
	av_frame_free(&pFrame);

	// Clean up
	av_free(buffer);
	sws_freeContext(conversion_context);
	av_frame_free(&rgb_frame);

	// Close the codecs
	avcodec_close(decoderCodecContext);
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

void FFmpegWrapper::addConnection(webConnHdl connHdl)
{
	if (inputCodecID == AV_CODEC_ID_NONE)
	{
		tempConnections.push_back(std::make_pair(connHdl, false));
	}
	else
	{
		if (inputFPS == 0 || inputFPS < 0 || inputFPS > 100)
		{
			inputFPS = 25;
		}
		string data = to_string(pFrame->width) + "x" + to_string(pFrame->height) + "x" + to_string(inputFPS);

		if (inputCodecID == AV_CODEC_ID_H264 || inputCodecID == AV_CODEC_ID_H265)
		{
			outputType = mp4;
			websocketSCallback(connHdl, "mp4");
			websocketSCallback(connHdl, "mp4 " + data);
			if (isLiveMode())
			{
				SendVideoStartedEvent();
			}
			addConnToList(connHdl);
		}
		else
		{
			outputType = rgba;
			// string data = "rgba " + to_string(pFrame->width) + "x" + to_string(pFrame->height) + "x" + to_string(inputFPS);
			websocketSCallback(connHdl, "rgba");
			// send only if pframe width is not 0, i.e this stream is getting played for first time
			// any subsequent connection will get the data from the first connection, thus width will not be 0
			// need to send this data to client for each connection to make the canvas
			if (pFrame->width != 0)
			{
				websocketSCallback(connHdl, "rgba " + data);
			}
			addConnToList(connHdl);
		}
	}
}

bool FFmpegWrapper::removeConnection(webConnHdl connHdl)
{

	std::lock_guard<std::mutex> lock(connectionsMutex); // Protect shared resources

	// Remove from temporary connections
	auto foundPair = std::find_if(tempConnections.begin(), tempConnections.end(),
								  [&](const std::pair<webConnHdl, bool> &p)
								  { return p.first.get() == connHdl.get(); });

	if (foundPair != tempConnections.end())
	{
		tempConnections.erase(foundPair);
	}

	if (connections.empty())
	{
		return true;
	}

	// Remove from connections list
	auto it = std::find_if(connections.begin(), connections.end(),
						   [&](const webConnHdl &conn)
						   { return conn.get() == connHdl.get(); });
	if (it != connections.end())
	{
		connections.erase(it);
		return connections.empty();
	}

	return false;
}

void FFmpegWrapper::addConnToList(webConnHdl connHdl)
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
		// IsFilePaused = false;
		cout << "Exception while  pause video on recording server ";
	}
}

void FFmpegWrapper::seek_video(int time_toSeek_insec)
{
	cout << " url is " << url << " seconds " << time_toSeek_insec << endl;
	if (isLiveMode())
	{
		return;
	}

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
				avcodec_flush_buffers(this->decoderCodecContext);
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

// FastForward_video
void FFmpegWrapper::FastForward_video(float speed)
{
	if (speed > 0)
	{
		fastForwardFactor = speed;
		frameDuration = std::chrono::duration<double, std::milli>(1000.0 / (inputFPS * fastForwardFactor));

		// set sleepTime
		// sleepTime = int(1000 / (inputFPS * fastForwardFactor));
	}
}

int FFmpegWrapper::getNextPlaybackTime()
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

bool FFmpegWrapper::isLiveMode()
{
	bool livemode = true;
	if (playmode != "Live")
		livemode = false;
	return livemode;
}

AVCodecID FFmpegWrapper::probeCodec(const std::string& url)
{
	AVFormatContext* fmtCtx = avformat_alloc_context();
	if (!fmtCtx)
		return AV_CODEC_ID_NONE;

	AVDictionary* options = nullptr;
		av_dict_set(&options, "rtsp_transport", "tcp", 0);

	av_dict_set(&options, "max_delay", "500000000", 0);
	av_dict_set(&options, "stimeout", "1500000000", 0);
	av_dict_set(&options, "analyzeduration", "1000000000", 0);
	av_dict_set(&options, "probesize", "1000000000", 0);

	if (avformat_open_input(&fmtCtx, url.c_str(), NULL, &options) != 0)
	{
		return AV_CODEC_ID_NONE;
	}

	if (avformat_find_stream_info(fmtCtx, NULL) < 0)
	{
		avformat_close_input(&fmtCtx);
		return AV_CODEC_ID_NONE;
	}

	AVCodecID codecId = AV_CODEC_ID_NONE;
	for (unsigned int i = 0; i < fmtCtx->nb_streams; i++)
	{
		if (fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			codecId = fmtCtx->streams[i]->codecpar->codec_id;
			break;
		}
	}

	avformat_close_input(&fmtCtx);
	return codecId;
}