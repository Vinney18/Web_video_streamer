#include "FFmpegWrapper.h"

int FFmpegWrapper::run()
{
	while (!mStop)
	{
		params.isRunning = false;
		auto opened = openInput();
		if (opened)
		{
			for (auto connHdl : connections[mjpeg])
			{
				websocketSCallback(connHdl, "connected");
			}
			params.isRunning = true;
			GetInputCodecInfo();
			createMp4Output();
			createMjpegOutput();
			if (!tempConnections.empty()) {
				for (auto el : tempConnections) {
					addConnection(el.first);
				}
				tempConnections.clear();
			}
			readInput();
			freeMp4OutMemory();
			freeMjpegOutMemory();
			closeInput();
		}

		if (!mStop) {
			for (auto connHdl : connections[mjpeg])
			{
				websocketSCallback(connHdl, "retrying");
			}
			for (auto connHdl : connections[mp4])
			{
				websocketSCallback(connHdl, "retrying");
			}
			// wait for some time before retry
			/*std::unique_lock<std::mutex> lk(mThreadMutex, std::defer_lock);
			cv.wait_for(lk, std::chrono::seconds(1));*/
			this_thread::sleep_for(std::chrono::seconds(1));
		}
	}
	for (auto connHdl : connections[mjpeg])
	{
		websocketSCallback(connHdl, "Stopped");
	}
	for (auto connHdl : connections[mp4])
	{
		websocketSCallback(connHdl, "Stopped");
	}
	return 0;
}

void FFmpegWrapper::addConnection(websocketpp::connection_hdl connHdl)
{
	if (inputCodecID == AV_CODEC_ID_NONE) {
		tempConnections.push_back(std::make_pair(connHdl, false));
	}
	else {

		if (inputCodecID != AV_CODEC_ID_H264) {
			websocketSCallback(connHdl, "mjpeg");
			addConnToList(connHdl, mjpeg);
		}
		else {
			websocketSCallback(connHdl, "mp4");
			addConnToList(connHdl, mp4);
		}
	}
}

bool FFmpegWrapper::removeConnection(websocketpp::connection_hdl connHdl)
{
	if (!tempConnections.empty()) {
		auto foundInTemp = false;
		std::vector<std::pair<websocketpp::connection_hdl, bool>>::iterator foundPair;

		for (std::vector<std::pair<websocketpp::connection_hdl, bool>>::iterator it = tempConnections.begin(); it != tempConnections.end(); ++it) {
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
		if (connections[mp4].empty() && connections[mjpeg].empty()) {
			// no more connections so tell 
			return true;
		}
	}

	bool isFound = false;
	OutputType type;
	if (!connections[mp4].empty())
	{
		for (auto conn : connections[mp4])
		{
			if (conn.lock() == connHdl.lock())
			{
				isFound = true;
				type = mp4;
			}
		}
	}
	if (!connections[mjpeg].empty())
	{
		for (auto conn : connections[mjpeg])
		{
			if (conn.lock() == connHdl.lock())
			{
				isFound = true;
				type = mjpeg;
			}
		}
	}

	if (isFound)
	{
		//std::unique_lock<std::mutex> lock(connectionlock);
		connections[type].erase(connHdl);
		/*if (connections[type].empty()) {
			removeOutput(type);
		}*/

		if (connections[mp4].empty() && connections[mjpeg].empty()) {
			// no more connections so tell 
			return true;
		}
	}
	return false;
}

int FFmpegWrapper::save_frame_as_jpeg(AVCodecContext* pCodecCtx, AVFrame* pFrame, AVPacket* packet) {
	/*AVCodec *jpegCodec = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
	if (!jpegCodec) {
		return -1;
	}
	AVCodecContext *jpegContext = avcodec_alloc_context3(jpegCodec);
	if (!jpegContext) {
		return -1;
	}

	jpegContext->pix_fmt = AV_PIX_FMT_YUVJ420P;

	jpegContext->height = pFrame->height;
	jpegContext->width = pFrame->width;
	jpegContext->time_base.num = 1;
	jpegContext->time_base.den = 25;

	if (avcodec_open2(jpegContext, jpegCodec, NULL) < 0) {
		return -1;
	}*/
	//FILE *JPEGFile;
	//char JPEGFName[256];


	int gotFrame;

	if (avcodec_encode_video2(jpegContext, packet, pFrame, &gotFrame) < 0) {
		return -1;
	}

	vector<uint8_t> frame(packet->data, packet->data + packet->size);
	send(frame);

	/*sprintf(JPEGFName, "dvr-%06d.jpg", FrameNo);
	JPEGFile = fopen(JPEGFName, "wb");
	fwrite(packet.data, 1, packet.size, JPEGFile);
	fclose(JPEGFile);*/

	av_free_packet(packet);
	return 0;
}

bool FFmpegWrapper::createMp4Output()
{
	mp4FragCreator = make_unique<Mp4frag>(std::bind(&FFmpegWrapper::receiveMp4Chunk, this, std::placeholders::_1));

	AVOutputFormat* outFmt = av_guess_format("mp4", NULL, NULL);

	AVStream* outStrm;
	avformat_alloc_output_context2(&this->mp4OutContext, outFmt, NULL, NULL);
	if (!(outStrm = avformat_new_stream(this->mp4OutContext, 0))) {
		return -1;
	}
	AVDictionary* metadata = nullptr;
	av_dict_copy(&metadata, this->inputFormatCtx->metadata, 0);
	this->mp4OutContext->metadata = metadata;
	AVCodec* codec = NULL;
	avcodec_get_context_defaults3(outStrm->codec, codec);



	outStrm->codecpar->codec_id = this->inputFormatCtx->streams[videoStream]->codecpar->codec_id;
	outStrm->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
	outStrm->codecpar->width = this->inputFormatCtx->streams[videoStream]->codecpar->width;
	outStrm->codecpar->height = this->inputFormatCtx->streams[videoStream]->codecpar->height;
	outStrm->codecpar->format = this->inputFormatCtx->streams[videoStream]->codecpar->format;
	outStrm->codecpar->bit_rate = this->inputFormatCtx->streams[videoStream]->codecpar->bit_rate;
	outStrm->time_base = this->inputFormatCtx->streams[videoStream]->time_base;
	outStrm->codecpar->extradata = (uint8_t*)av_malloc((size_t)this->inputCodecCtx->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
	outStrm->codecpar->extradata_size = this->inputCodecCtx->extradata_size;
	memcpy(outStrm->codecpar->extradata, this->inputCodecCtx->extradata, this->inputCodecCtx->extradata_size);


	AVDictionary* options = nullptr;
	av_dict_set(&options, "movflags", "frag_keyframe+empty_moov+default_base_moof", 0);
	//av_dict_set(&options, "frag_duration", "700000", 0);
	av_dict_set(&options, "reset_timestamps", "1", 0);
	//av_dict_set(&options, "ss", "30", 0);
	//av_dict_set(&options, "b:v", "1024k", 0);

	uint8_t* buffer2 = NULL;
	int numBytes2 = 320 * 1024;
	buffer2 = (uint8_t*)av_malloc(numBytes2 * sizeof(uint8_t));
	AVIOContext* pIOCtx = avio_alloc_context(buffer2, numBytes2, 1, (void*)this, 0, ffmpegMp4Callback, 0);
	this->mp4OutContext->pb = pIOCtx;
	//avio_open(&outFmtCtx->pb, "test.mp4", AVIO_FLAG_WRITE);
	avformat_write_header(this->mp4OutContext, &options);
	return true;
}

bool FFmpegWrapper::createMjpegOutput()
{
	std::cout << "fps :" << this->inputFPS << std::endl;

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

	// Open codec
	if (avcodec_open2(this->decoderCodecContext, this->decoderCodec, NULL) < 0)
		return false; // Could not open codec



	jpegCodec = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
	if (!jpegCodec) {
		return false;
	}
	jpegContext = avcodec_alloc_context3(jpegCodec);
	if (!jpegContext) {
		return false;
	}

	jpegContext->pix_fmt = AV_PIX_FMT_YUVJ420P;

	jpegContext->height = this->inputFormatCtx->streams[videoStream]->codecpar->height;
	jpegContext->width = this->inputFormatCtx->streams[videoStream]->codecpar->width;
	jpegContext->time_base.num = 1;
	jpegContext->time_base.den = 25;

	if (avcodec_open2(jpegContext, jpegCodec, NULL) < 0) {
		return false;
	}

	/*-------------------------------------------*/
	/* open the hardware device */

	//int ret = av_hwdevice_ctx_create(&decode.hw_device_ref, AV_HWDEVICE_TYPE_QSV, "auto_any", NULL, 0);
	//if (ret < 0) {
	//	fprintf(stderr, "Cannot open the hardware device\n");
	//	return false;
	//}

	///* initialize the decoder */
	//decoder = avcodec_find_decoder_by_name("h264_qsv");
	//if (!decoder) {
	//	fprintf(stderr, "The QSV decoder is not present in libavcodec\n");
	//	return false;
	//}
	//decoder_ctx = avcodec_alloc_context3(decoder);
	//if (!decoder_ctx) {
	//	ret = AVERROR(ENOMEM);
	//	return false;
	//}
	//decoder_ctx->codec_id = AV_CODEC_ID_H264;
	//if (this->inputFormatCtx->streams[videoStream]->codecpar->extradata_size) {
	//	decoder_ctx->extradata = reinterpret_cast<uint8_t*>(av_mallocz(this->inputFormatCtx->streams[videoStream]->codecpar->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE));
	//	if (!decoder_ctx->extradata) {
	//		ret = AVERROR(ENOMEM);
	//		return false;
	//	}

	//	memcpy(decoder_ctx->extradata, this->inputFormatCtx->streams[videoStream]->codecpar->extradata,
	//		this->inputFormatCtx->streams[videoStream]->codecpar->extradata_size);
	//	decoder_ctx->extradata_size = this->inputFormatCtx->streams[videoStream]->codecpar->extradata_size;
	//}

	//decoder_ctx->refcounted_frames = 1;
	//decoder_ctx->opaque = &decode;
	//decoder_ctx->get_format = get_format;

	//ret = avcodec_open2(decoder_ctx, NULL, NULL);

	//if (ret < 0) {
	//	fprintf(stderr, "Error opening the decoder: ");
	//	return false;
	//}

	return true;
}

bool FFmpegWrapper::createOutput(OutputType outType)
{
	switch (outType)
	{
	case mp4:
		createMp4Output();
		break;
	case mjpeg:
		createMjpegOutput();
		break;
	default:
		break;
	}

	return true;
}

void FFmpegWrapper::addConnToList(websocketpp::connection_hdl connHdl, OutputType outType)
{
	//std::unique_lock<std::mutex> lock(connectionlock);	
	if (connections[outType].empty())
	{
		//createOutput(outType);
		connections[outType].insert(connHdl);
		// write header
		if (outType == mp4) {
			//AVDictionary* options = nullptr;
			//av_dict_set(&options, "movflags", "+frag_keyframe+empty_moov+default_base_moof+omit_tfhd_offset", 0);
			////av_dict_set(&options, "reset_timestamps", "1", 0);
			//av_dict_set(&options, "b:v", "1024k", 0);
			//avformat_write_header(this->mp4OutContext, &options);
			websocketCallback(connHdl, mp4FragCreator->initialization);
		}
	}
	else {

		if (outType == mp4) {
			websocketCallback(connHdl, mp4FragCreator->initialization);
		}
		connections[outType].insert(connHdl);
	}
}

void FFmpegWrapper::readInput()
{
	int frameFinished;
	AVPacket packet;

	int interval = 1;
	if (inputFPS > 7) {
		interval = inputFPS / 5;
		if (inputFPS % 5 != 0)
			interval++;
	}
	cout << "interval" << interval << endl;
	int i = 1;
	// Allocate video frame
	pFrame = av_frame_alloc();

	//Control input frame rate
	auto sleepTime = 1000 / inputFPS;
	std::mutex mut;
	std::atomic<bool> canSend = true;
	std::condition_variable condition_v;
	std::thread thread1;

	if (playmode != "Live") {
		thread1 = std::thread([&]()
			{
				while (!mStop)
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(sleepTime));
					canSend = true;
					condition_v.notify_all();
				}
			});
	}

	AVPacket packetEncoded;
	packetEncoded.data = NULL, packetEncoded.size = 0;
	av_init_packet(&packetEncoded);

	try
	{
		params.lastStopped = GetTickCount();
		int framecount = 0;
		while (av_read_frame(this->inputFormatCtx, &packet) >= 0 && !mStop) 
		{
			params.lastStopped = GetTickCount();
			// Is this a packet from the video stream?
			if (packet.stream_index == videoStream) 
			{		

				if (!connections[mjpeg].empty())
				{
					if (inputCodecID == AV_CODEC_ID_MJPEG)
					{
						vector<uint8_t> frame(packet.data, packet.data + packet.size);
						send(frame);
					}
					else {
						avcodec_decode_video2(decoderCodecContext, pFrame, &frameFinished, &packet);

						// Did we get a video frame?
						if (frameFinished) {
							framecount++;
							if (framecount == inputFPS) {
								if (this->initial_seek_time > 0) {
									seek_video(this->initial_seek_time);
								}
							}
							if (playmode != "Live")
							{
								while (!canSend && !mStop)
								{
									try
									{
										std::unique_lock<std::mutex> lok(mut);
										condition_v.wait_for(lok, std::chrono::seconds(1));
									}
									catch (const std::exception& ex)
									{
										cout << ex.what() << std::endl;
									}
								}
							}
							save_frame_as_jpeg(decoderCodecContext, pFrame, &packetEncoded);
							canSend = false;
						}
					}
				}

				if (!connections[mp4].empty()) {
					if (playmode != "Live") 
					{
						while (!canSend && !mStop)
						{
							try
							{
								std::unique_lock<std::mutex> lok(mut);
								condition_v.wait_for(lok, std::chrono::seconds(1));
							}
							catch (const std::exception& ex)
							{
								cout << ex.what() << std::endl;
							}
						}
						framecount++;
						if (framecount == inputFPS) {
							if (this->initial_seek_time > 0) {
								seek_video(this->initial_seek_time);
							}
						}
					}
					if (usejmuxer)
					{
						auto hdlList = connections[mp4];
						vector<uint8_t> chunk(packet.data, packet.data + packet.buf->size);
						for (auto hndl : hdlList) {
							websocketCallback(hndl, chunk);
						}
					}
					else
					{
						auto x = av_interleaved_write_frame(mp4OutContext, &packet);
					}
					canSend = false;
				}
			}
			// Free the packet that was allocated by av_read_frame
			av_free_packet(&packet);
		}
		if (playmode != "Live")
		{
			auto hdlList = connections[mp4];

			for (auto hndl : hdlList) {
				websocketSCallback(hndl, "Playback_Finished");
			}
			mStop = true;
			thread1.join();
			cout << "Thread 1 join";
		}
		/*stopThread2 = true;
		thread2.join();*/
	}
	catch (const exception& ex) {
		cout << ex.what() << std::endl;
	}
}


void FFmpegWrapper::seek_video(int time_toSeek_insec)
{
	//Forwardseek_video(frameIndex);
	Forwardseek_video(time_toSeek_insec);
}

void FFmpegWrapper::Forwardseek_video(int time_toSeek_insec) 
{
	// Seek is done on packet dts
	try
	{
		int framerate = inputFPS;
		int frameIndex = time_toSeek_insec * framerate;
		int64_t target_dts_usecs = (int64_t)round(frameIndex * (double)this->inputFormatCtx->streams[videoStream]->r_frame_rate.den / this->inputFormatCtx->streams[videoStream]->r_frame_rate.num * AV_TIME_BASE);
		// Remove first dts: when non zero seek should be more accurate
		auto first_dts_usecs = (int64_t)round(this->inputFormatCtx->streams[videoStream]->first_dts * (double)this->inputFormatCtx->streams[videoStream]->time_base.num / this->inputFormatCtx->streams[videoStream]->time_base.den * AV_TIME_BASE);
		target_dts_usecs += first_dts_usecs;
		int rv = av_seek_frame(this->inputFormatCtx, -1, target_dts_usecs, AVSEEK_FLAG_FRAME | AVSEEK_FLAG_ANY);
		if (rv < 0)
		{
			cout << "Unable to seek video";
		}
	}
	catch (const exception & ex)
	{
		cout << "Exception while  seek video";
		cout << ex.what() << std::endl;
	}
}

void FFmpegWrapper::Backwardseek_video(int frameIndex)
{
	// Seek is done on packet dts
	try
	{

		int64_t target_dts_usecs = (int64_t)round(frameIndex * (double)this->inputFormatCtx->streams[videoStream]->r_frame_rate.den / this->inputFormatCtx->streams[videoStream]->r_frame_rate.num * AV_TIME_BASE);
		// Remove first dts: when non zero seek should be more accurate
		auto first_dts_usecs = (int64_t)round(this->inputFormatCtx->streams[videoStream]->first_dts * (double)this->inputFormatCtx->streams[videoStream]->time_base.num / this->inputFormatCtx->streams[videoStream]->time_base.den * AV_TIME_BASE);
		target_dts_usecs += first_dts_usecs;
		int rv = av_seek_frame(this->inputFormatCtx, -1, target_dts_usecs, AVSEEK_FLAG_BACKWARD | AVSEEK_FLAG_ANY);
		avcodec_flush_buffers(this->decoderCodecContext);
		avcodec_flush_buffers(this->jpegContext);

		if (rv < 0)
		{
			cout << "Unable to seek video";
		}

	}
	catch (const exception & ex)
	{
		cout << "Exception while  seek video";
		cout << ex.what() << std::endl;
	}
}


bool FFmpegWrapper::removeOutput(OutputType outType)
{
	switch (outType)
	{
	case mp4:
		freeMp4OutMemory();
		break;
	case mjpeg:
		freeMjpegOutMemory();
		break;
	default:
		break;
	}
	return true;
}

void FFmpegWrapper::freeMp4OutMemory()
{
	try
	{
		//For mp4
	//av_write_trailer(outFmtCtx);
		av_free(mp4OutContext->pb->buffer);
		avio_context_free(&mp4OutContext->pb);
		mp4OutContext->pb = NULL;
		//avio_close(outFmtCtx->pb);
		//avcodec_close(mp4OutContext->streams[0]->codec);
		avformat_free_context(mp4OutContext);
		mp4OutContext = NULL;
	}
	catch (const std::exception& ex) {
		std::cout << ex.what() << std::endl;
	}
}

void FFmpegWrapper::freeMjpegOutMemory()
{
	// Free the YUV frame
	av_free(pFrame);

	// Close the codecs
	avcodec_close(decoderCodecContext);

	avcodec_close(jpegContext);
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

bool FFmpegWrapper::openInput()
{
	this->inputFormatCtx = avformat_alloc_context();
	this->inputFormatCtx->interrupt_callback.callback = interrupt_cb;
	this->inputFormatCtx->interrupt_callback.opaque = this;
	const char* fileName = this->url.c_str();
	// Open file
	AVDictionary* options1 = nullptr;
	try {
		av_dict_set(&options1, "rtsp_transport", "tcp", 0);
		if (connectionmode == "udp") 
		{
			av_dict_set(&options1, "rtsp_transport", "udp", 0);
		}

	}
	catch (boost::bad_lexical_cast) {
		// bad parameter
	}


	params.lastStopped = GetTickCount();

	if (avformat_open_input(&this->inputFormatCtx, fileName, NULL, &options1) != 0)
	{
		return false;
		//this->inputFormatCtx = avformat_alloc_context();
		//this->inputFormatCtx->interrupt_callback.callback = interrupt_cb;
		//this->inputFormatCtx->interrupt_callback.opaque = this;
		////AVDictionary* options1 = nullptr;
		////av_dict_set(&options1, "rtsp_transport", "tcp", 0);
		////av_dict_set(&options1, "use_wallclock_as_timestamps", "1", 0);

		//params.lastStopped = GetTickCount();
		//if (avformat_open_input(&this->inputFormatCtx, fileName, NULL, &options1) != 0)
		//{
		//	this->inputFormatCtx = avformat_alloc_context();
		//	this->inputFormatCtx->interrupt_callback.callback = interrupt_cb;
		//	this->inputFormatCtx->interrupt_callback.opaque = this;
		//	//AVDictionary* options1 = nullptr;

		//	params.lastStopped = GetTickCount();
		//	if (avformat_open_input(&this->inputFormatCtx, fileName, NULL, &options1) != 0) {
		//		return false;
		//	}
		//}
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

FFmpegWrapper::~FFmpegWrapper() {

}

void FFmpegWrapper::receiveMp4Chunk(vector<uint8_t> data) {
	auto hdlList = connections[mp4];

	//auto dataPtr = data.data();
	//auto size = data.size();
	//auto c = 0;
	for (auto hndl : hdlList) {
		//cout << "Counter Count:" << ++c << endl;
		//echo_server.send(*it, dataPtr, size, websocketpp::frame::opcode::BINARY);
		websocketCallback(hndl, data);
	}
}