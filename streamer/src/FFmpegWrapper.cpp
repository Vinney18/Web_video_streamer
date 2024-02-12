#include "FFmpegWrapper.h"

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

			if (!tempConnections.empty()) {
				for (auto& el : tempConnections)
				{
					if (inputCodecID == AV_CODEC_ID_NONE)
					{
						//close websocket if not able to play
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

		if (!mStop) {
			for (webConnHdl connHdl : connections[rgba])
			{
				websocketSCallback(connHdl, "retrying");
			}
			for (webConnHdl connHdl : connections[mp4])
			{
				websocketSCallback(connHdl, "retrying");
			}
			// wait for some time before retry
			/*std::unique_lock<std::mutex> lk(mThreadMutex, std::defer_lock);
			cv.wait_for(lk, std::chrono::seconds(1));*/
			this_thread::sleep_for(std::chrono::seconds(1));
		}
	}
	for (webConnHdl connHdl : connections[rgba])
	{
		websocketSCallback(connHdl, "Stopped");
	}
	for (webConnHdl connHdl : connections[mp4])
	{
		websocketSCallback(connHdl, "Stopped");
	}
	return 0;
}

FFmpegWrapper::~FFmpegWrapper() {

}

void FFmpegWrapper::readInput()
{
	int frameFinished;
	AVPacket packet;
	AVPixelFormat pixFormat;

	//Control input frame rate
	int sleepTime = 0;
	if (inputFPS == 0 || inputFPS < 0 || inputFPS > 100) {
		inputFPS = 25;
	}

	std::mutex mut;
	std::atomic<bool> canSend = true;
	std::condition_variable condition_v;
	std::thread thread1;

	if (!isLiveMode()) {
		thread1 = std::thread([&]()
			{
				while (!mStop)
				{
					if (!fileseekingstarted)
					{
						std::this_thread::sleep_for(std::chrono::milliseconds(sleepTime));
						canSend = true;
						condition_v.notify_all();
					}
				}
			});
	}

	AVPacket packetEncoded;
	av_init_packet(&packetEncoded);

	try
	{
		this->params.lastStopped = GetTickCount();
		bool sendData = true;
		int frameNumber = 0;
		int64_t firstDts = this->inputFormatCtx->streams[videoStream]->first_dts;
		while (av_read_frame(this->inputFormatCtx, &packet) >= 0 && !mStop)
		{
			if (frameNumber < 5 * inputFPS) frameNumber++;
			std::unique_lock<std::mutex> lck(mThreadMutex);
			cv.wait(lck, [&]() { return !mPaused; });
			this->params.lastStopped = GetTickCount();

			// Is this a packet from the video stream?
			if (packet.stream_index == videoStream)
			{
				int64_t position;
				if (isLiveMode()) position = -2;
				else {
					position = round((this->inputFormatCtx->streams[videoStream]->cur_dts - firstDts) * this->inputFormatCtx->streams[videoStream]->time_base.num / this->inputFormatCtx->streams[videoStream]->time_base.den);
				}

				if (!connections[rgba].empty())
				{
					avcodec_decode_video2(decoderCodecContext, pFrame, &frameFinished, &packet);
					// avcodec_send_packet(decoderCodecContext, &packet);
					// frameFinished = avcodec_receive_frame(decoderCodecContext, pFrame);

					// Did we get a video frame?
					if (frameFinished)
					{
						if (frameNumber == 1)
						{
							switch ((AVPixelFormat)pFrame->format)
							{
							case AV_PIX_FMT_YUVJ420P:
								pixFormat = AV_PIX_FMT_YUV420P;
								break;
							case AV_PIX_FMT_YUVJ422P:
								pixFormat = AV_PIX_FMT_YUV422P;
								break;
							case AV_PIX_FMT_YUVJ444P:
								pixFormat = AV_PIX_FMT_YUV444P;
								break;
							case AV_PIX_FMT_YUVJ440P:
								pixFormat = AV_PIX_FMT_YUV440P;
								break;
							default:
								pixFormat = (AVPixelFormat)pFrame->format;
								break;
							}

							// Set up the conversion context
							conversion_context = sws_getContext(pFrame->width, pFrame->height, pixFormat,
								pFrame->width, pFrame->height, AV_PIX_FMT_RGBA, SWS_BILINEAR, NULL, NULL, NULL);

							// Allocate the buffer for the RGB frame
							int num_bytes = av_image_get_buffer_size(AV_PIX_FMT_RGBA, pFrame->width, pFrame->height, 1);
							buffer = (uint8_t*)av_malloc(num_bytes * sizeof(uint8_t));
							av_image_fill_arrays(rgb_frame->data, rgb_frame->linesize, buffer, AV_PIX_FMT_RGBA, pFrame->width, pFrame->height, 1);

							for (webConnHdl connHdl : connections[rgba])
							{
								string data = "rgba " + to_string(pFrame->width) + "x" + to_string(pFrame->height);
								websocketSCallback(connHdl, data);
							}
							if (isLiveMode()) {
								SendVideoStartedEvent();
							}
							else {
								playbackFileStared = true;
								sendData = false;
								if (this->initial_seek_time > 0) seek_video(this->initial_seek_time);
							}
						}
						if (!isLiveMode()) {
							if (frameNumber == inputFPS) { sleepTime = int(1000 / inputFPS); sendData = true; SendVideoStartedEvent(); }
							while (!canSend && !mStop)
							{
								try
								{
									std::unique_lock<std::mutex> lok(mut);
									condition_v.wait_for(lok, std::chrono::seconds(100));
								}
								catch (const std::exception& ex)
								{
									cout << ex.what() << std::endl;
								}
							}
						}
						if (sendData) {
							// Convert the YUV frame to RGB
							sws_scale(conversion_context, pFrame->data, pFrame->linesize, 0, pFrame->height, rgb_frame->data, rgb_frame->linesize);

							uint8_t* frameData = rgb_frame->data[0];
							std::vector<uint8_t> rgbData(&frameData[0], &frameData[pFrame->width * pFrame->height * 4]);
							for (webConnHdl hndl : connections[rgba])
							{
								websocketCallback(hndl, rgbData, position);
							}
							canSend = false;
						}
					}
					else
					{
						frameNumber--;
					}
				}

				if (!connections[mp4].empty())
				{
					if (frameNumber == 1)
					{
						if (isLiveMode()) {
							SendVideoStartedEvent();
						}
						else {
							playbackFileStared = true;
							sendData = false;
							if (this->initial_seek_time > 0) seek_video(this->initial_seek_time);
						}
					}

					if (!isLiveMode()) {
						if (frameNumber == inputFPS) { sleepTime = int(1000 / inputFPS); sendData = true; SendVideoStartedEvent(); }
						while (!canSend && !mStop)
						{
							try
							{
								std::unique_lock<std::mutex> lok(mut);
								condition_v.wait_for(lok, std::chrono::seconds(100));
							}
							catch (const std::exception& ex)
							{
								cout << ex.what() << std::endl;
							}
						}
					}
					if (sendData) {
						vector<uint8_t> mp4Data(packet.data, packet.data + packet.buf->size);
						for (webConnHdl hndl : connections[mp4])
						{
							websocketCallback(hndl, mp4Data, position);
						}
						canSend = false;
					}
				}
			}




			// Free the packet that was allocated by av_read_frame
			av_free_packet(&packet);
		}
		if (!isLiveMode())
		{
			for (webConnHdl hndl : connections[mp4]) {
				websocketSCallback(hndl, "Playback_Finished");
			}
			for (webConnHdl hndl1 : connections[rgba]) {
				websocketSCallback(hndl1, "Playback_Finished");
			}
			mStop = true;
			thread1.join();
			cout << "Thread 1 join";
		}
	}
	catch (const exception& ex) {
		cout << ex.what() << std::endl;
	}
}

bool FFmpegWrapper::openInput()
{
	this->inputFormatCtx = avformat_alloc_context();
	this->inputFormatCtx->interrupt_callback.callback = interrupt_cb;
	this->inputFormatCtx->interrupt_callback.opaque = this;
	const char* fileName = this->url.c_str();
	//const char* fileName = "rtsp://192.168.0.40:8556/test";
	//cout << fileName << endl;
	// Open file
	AVDictionary* options1 = nullptr;
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
		av_dict_set(&options1, "stimeout", "5000000", 0);//The unit us is 3s

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
	av_dump_format(this->inputFormatCtx, 0, fileName, 0);

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
		if (this->inputFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
			videoStream = i;
			break;
		}
	if (videoStream == -1)
		return false; // Didn't find a video stream

	// Get a pointer to the codec context for the video stream
	this->inputCodecCtx = this->inputFormatCtx->streams[videoStream]->codec;
	this->inputCodecID = this->inputFormatCtx->streams[videoStream]->codec->codec_id;
	this->inputFPS = av_q2d(this->inputFormatCtx->streams[videoStream]->r_frame_rate);
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
	if (this->decoderCodec == NULL) {
		fprintf(stderr, "Unsupported codec!\n");
		return false; // Codec not found
	}

	// Copy context
	this->decoderCodecContext = avcodec_alloc_context3(this->decoderCodec);
	if (avcodec_copy_context(this->decoderCodecContext, this->inputCodecCtx) != 0) {
		fprintf(stderr, "Couldn't copy codec context");
		return false; // Error copying codec context
	}
	//this->decoderCodecContext->
	// Open codec
	if (avcodec_open2(this->decoderCodecContext, this->decoderCodec, NULL) < 0)
		return false; // Could not open codec

	// Allocate video frame
	pFrame = av_frame_alloc();

	// Allocate the RGB frame
	rgb_frame = av_frame_alloc();
	if (rgb_frame == NULL) {
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
	try {
		avcodec_close(inputCodecCtx);
		inputCodecCtx = NULL;
		// Close the video file
		avformat_close_input(&this->inputFormatCtx);
		inputFormatCtx = NULL;
		inputCodecID = AV_CODEC_ID_NONE;
	}
	catch (const std::exception& ex) {
		std::cout << ex.what() << std::endl;
	}
}

void FFmpegWrapper::receiveMp4Chunk(vector<uint8_t> data, int64_t _vidPosition) {
	for (webConnHdl hndl : connections[mp4]) {
		websocketCallback(hndl, data, _vidPosition);
	}
}

void FFmpegWrapper::addConnection(webConnHdl connHdl)
{
	if (inputCodecID == AV_CODEC_ID_NONE)
	{
		tempConnections.push_back(std::make_pair(connHdl, false));
	}
	else {
		if (inputCodecID == AV_CODEC_ID_H264) {
			websocketSCallback(connHdl, "mp4");
			addConnToList(connHdl, mp4);
		}
		else
		{
			websocketSCallback(connHdl, "rgba");
			addConnToList(connHdl, rgba);
		}
	}
}

bool FFmpegWrapper::removeConnection(webConnHdl connHdl)
{
	if (!tempConnections.empty()) {
		auto foundInTemp = false;
		std::vector<std::pair<webConnHdl, bool>>::iterator foundPair;

		for (std::vector<std::pair<webConnHdl, bool>>::iterator it = tempConnections.begin(); it != tempConnections.end(); ++it) {
			if (it->first.lock() == connHdl.lock())
			{
				foundInTemp = true;
				foundPair = it;
			}
		}
		if (foundInTemp)
		{
			tempConnections.erase(foundPair);
		}
		if (connections[mp4].empty() && connections[rgba].empty()) {
			// no more connections so tell 
			return true;
		}
	}

	bool isFound = false;
	OutputType type;
	if (!connections[mp4].empty())
	{
		for (auto& conn : connections[mp4])
		{
			if (conn.lock() == connHdl.lock())
			{
				isFound = true;
				type = mp4;
			}
		}
	}
	if (!connections[rgba].empty())
	{
		for (auto& conn : connections[rgba])
		{
			if (conn.lock() == connHdl.lock())
			{
				isFound = true;
				type = rgba;
			}
		}
	}

	if (isFound)
	{
		connections[type].erase(connHdl);

		if (connections[mp4].empty() && connections[rgba].empty()) {
			// no more connections so tell 
			return true;
		}
	}
	return false;
}

void FFmpegWrapper::addConnToList(webConnHdl connHdl, OutputType outType)
{
	connections[outType].insert(connHdl);
}

void FFmpegWrapper::SendVideoStartedEvent()
{
	if (isVideoStartedEventsent) {
		return;
	}
	isVideoStartedEventsent = true;

	for (webConnHdl connHdl : connections[rgba])
	{
		websocketSCallback(connHdl, "Video_Started");
	}
	for (webConnHdl connHdl : connections[mp4])
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
	catch (const std::exception&)
	{
		//IsFilePaused = false;
		cout << "Exception while  pause video on recording server ";
	}
}

void FFmpegWrapper::seek_video(int time_toSeek_insec)
{
	cout << "In FFmpegWrapper::seek_video " << time_toSeek_insec << endl;
	if (isLiveMode()) { return; }

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
			try {
				avcodec_flush_buffers(this->decoderCodecContext);
				int rv = av_seek_frame(this->inputFormatCtx, -1, target_dts_usecs, AVSEEK_FLAG_FRAME);
				if (rv < 0)
				{
					fileseekingstarted = false;
					if (logger) { logger->warn("Unable to seek video"); }
				}
			}
			catch (exception ex) {
				cout << "my exc: " << ex.what() << endl;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(1000));
			fileseekingstarted = false;
		}
		catch (const exception& ex)
		{
			fileseekingstarted = false;
			if (logger) { logger->error("Exception while seek video: {}", ex.what()); }
			else { std::cout << "Exception while seek video: " << ex.what() << std::endl; }
		}
	}
}

bool FFmpegWrapper::isLiveMode()
{
	bool livemode = true;
	if (playmode != "Live") livemode = false;
	return livemode;
}