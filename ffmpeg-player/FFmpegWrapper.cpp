#include "FFmpegWrapper.h"

int FFmpegWrapper::run()
{
	while (!mStop)
	{
		auto opened = openInput();
		if (opened)
		{
			GetInputCodecInfo();
			if (!tempConnections.empty()) {
				for (auto el : tempConnections) {
					addConnection(el.first, el.second);
				}
				tempConnections.clear();
			}
			readInput();
			closeInput();
		}

		if (!mStop) {
			// wait for some time before retry
			std::unique_lock<std::mutex> lk(mThreadMutex);
			cv.wait_for(lk, std::chrono::seconds(5));
		}
	}	
	return 0;
}

void FFmpegWrapper::addConnection(websocketpp::connection_hdl connHdl, bool useTranscoding)
{
	if (inputCodecID == AV_CODEC_ID_NONE) {
		tempConnections.push_back(std::make_pair(connHdl, useTranscoding));
	}
	else {
		
		if (inputCodecID != AV_CODEC_ID_H264 || useTranscoding) {
			addConnToList(connHdl, mjpeg);
		}
		else {
			addConnToList(connHdl, mp4);
		}
	}
}

bool FFmpegWrapper::removeConnection(websocketpp::connection_hdl connHdl)
{
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
		connections[type].erase(connHdl);
		if (connections[type].empty()) {		
			removeOutput(type);
		}

		if (connections[mp4].empty() && connections[mjpeg].empty()){
			// no more connections so tell 
			return true;
		}
	}
	return false;
}

int FFmpegWrapper::save_frame_as_jpeg(AVCodecContext *pCodecCtx, AVFrame *pFrame) {
	AVCodec *jpegCodec = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
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
	}
	//FILE *JPEGFile;
	//char JPEGFName[256];

	AVPacket packet;
	packet.data = NULL, packet.size = 0;
	av_init_packet(&packet);
	int gotFrame;

	if (avcodec_encode_video2(jpegContext, &packet, pFrame, &gotFrame) < 0) {
		return -1;
	}

	vector<uint8_t> frame(packet.data, packet.data + packet.size);
	send(frame);

	/*sprintf(JPEGFName, "dvr-%06d.jpg", FrameNo);
	JPEGFile = fopen(JPEGFName, "wb");
	fwrite(packet.data, 1, packet.size, JPEGFile);
	fclose(JPEGFile);*/

	av_free_packet(&packet);
	avcodec_close(jpegContext);
	return 0;
}

bool FFmpegWrapper::createMp4Output()
{
	mp4FragCreator = make_unique<Mp4frag>(std::bind(&FFmpegWrapper::receiveMp4Chunk, this, std::placeholders::_1));

	AVOutputFormat * outFmt = av_guess_format("mp4", NULL, NULL);

	AVStream * outStrm;
	avformat_alloc_output_context2(&this->mp4OutContext, outFmt, NULL, NULL);
	if (!(outStrm = avformat_new_stream(this->mp4OutContext, 0))) {
		return -1;
	}
	this->mp4OutContext->metadata = this->inputFormatCtx->metadata;
	AVCodec * codec = NULL;
	avcodec_get_context_defaults3(outStrm->codec, codec);



	outStrm->codecpar->codec_id = this->inputFormatCtx->streams[videoStream]->codecpar->codec_id;
	outStrm->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
	outStrm->codecpar->width = this->inputFormatCtx->streams[videoStream]->codecpar->width;
	outStrm->codecpar->height = this->inputFormatCtx->streams[videoStream]->codecpar->height;
	outStrm->codecpar->format = this->inputFormatCtx->streams[videoStream]->codecpar->format;
	outStrm->codecpar->bit_rate = this->inputFormatCtx->streams[videoStream]->codecpar->bit_rate;
	outStrm->time_base = this->inputFormatCtx->streams[videoStream]->time_base;
	outStrm->codecpar->extradata = (uint8_t*)av_malloc(this->inputCodecCtx->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
	outStrm->codecpar->extradata_size = this->inputCodecCtx->extradata_size;
	memcpy(outStrm->codecpar->extradata, this->inputCodecCtx->extradata, this->inputCodecCtx->extradata_size);


	AVDictionary* options = nullptr;
	av_dict_set(&options, "movflags", "+frag_keyframe+empty_moov+default_base_moof+omit_tfhd_offset", 0);
	//av_dict_set(&options, "reset_timestamps", "1", 0);
	av_dict_set(&options, "b:v", "1024k", 0);

	uint8_t *buffer2 = NULL;
	int numBytes2 = 320 * 1024;
	buffer2 = (uint8_t *)av_malloc(numBytes2 * sizeof(uint8_t));
	AVIOContext* pIOCtx = avio_alloc_context(buffer2, numBytes2, 1, (void *)this, 0, ffmpegMp4Callback, 0);
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
		return -1; // Codec not found
	}

	// Copy context
	this->decoderCodecContext = avcodec_alloc_context3(this->decoderCodec);
	if (avcodec_copy_context(this->decoderCodecContext, this->inputCodecCtx) != 0) {
		fprintf(stderr, "Couldn't copy codec context");
		return -1; // Error copying codec context
	}

	// Open codec
	if (avcodec_open2(this->decoderCodecContext, this->decoderCodec, NULL) < 0)
		return -1; // Could not open codec


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
	if (connections[outType].empty())
	{
		createOutput(outType);
	}
	connections[outType].insert(connHdl);

}

void FFmpegWrapper::readInput()
{
	int frameFinished;
	AVPacket packet;


	int videoFPS = 0;

	int interval = 1;
	if (videoFPS > 7) {
		interval = videoFPS / 5;
		if (videoFPS % 5 != 0)
			interval++;
	}
	cout << "interval" << interval << endl;
	int i = 1;
	// Allocate video frame
	pFrame = av_frame_alloc();
	try
	{
		while (av_read_frame(this->inputFormatCtx, &packet) >= 0 && !mStop) {
			// Is this a packet from the video stream?
			if (packet.stream_index == videoStream) {
				//av_write_frame(outFmtCtx, &packet);
				if (!connections[mjpeg].empty())
				{
					avcodec_decode_video2(decoderCodecContext, pFrame, &frameFinished, &packet);

					// Did we get a video frame?
					if (frameFinished) {
						/*if (++i <= 10)*/
						//cout << i << endl;
						if (i == 1)
							save_frame_as_jpeg(decoderCodecContext, pFrame);
						if (i++ >= interval)
							i = 1;
					}
				}
				if (!connections[mp4].empty()) {
					auto x = av_interleaved_write_frame(mp4OutContext, &packet);
				}
			}
			// Free the packet that was allocated by av_read_frame
			av_free_packet(&packet);
		}

	}
	catch (const exception& ex) {

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
	//For mp4
	//av_write_trailer(outFmtCtx);
	avio_context_free(&mp4OutContext->pb);
	//avio_close(outFmtCtx->pb);
	avformat_free_context(mp4OutContext);
}

void FFmpegWrapper::freeMjpegOutMemory()
{
	// Free the YUV frame
	av_free(pFrame);

	// Close the codecs
	avcodec_close(decoderCodecContext);
}

void FFmpegWrapper::closeInput()
{
	avcodec_close(inputCodecCtx);

	// Close the video file
	avformat_close_input(&this->inputFormatCtx);
}

bool FFmpegWrapper::openInput()
{
	this->inputFormatCtx = avformat_alloc_context();;
	const char *fileName = this->url.c_str();
	// Open file
	AVDictionary* options1 = nullptr;
	//av_dict_set(&options1, "probesize", "5000", 0);
	//av_dict_set(&options1, "analyzeduration", "10000000", 0);
	//av_dict_set(&options1, "reorder_queue_size", "0", 0);
	av_dict_set(&options1, "rtsp_transport", "tcp", 0);

	this->inputFormatCtx->interrupt_callback.callback = interrupt_cb;
	//this->inputFormatCtx->interrupt_callback.opaque = formatContext;

	if (avformat_open_input(&this->inputFormatCtx, fileName, NULL, &options1) != 0)
	{
		AVDictionary* options1 = nullptr;
		av_dict_set(&options1, "rtsp_transport", "udp", 0);

		if (avformat_open_input(&this->inputFormatCtx, fileName, NULL, &options1) != 0)
		{
			AVDictionary* options1 = nullptr;
			if (avformat_open_input(&this->inputFormatCtx, fileName, NULL, &options1) != 0) {
				return false;
			}
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