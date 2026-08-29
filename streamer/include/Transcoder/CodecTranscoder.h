#ifndef CODECTRANSCODER_H
#define CODECTRANSCODER_H
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
}

// Strategy interface for server-side codec transcoding. A transcoder decodes
// compressed input packets and re-encodes them to a different codec before the
// bytes are handed to the transport callback. Implementations are stateful
// (they own decoder/encoder/scaler contexts) and are used from a single thread
// (the FFmpeg read thread), so they need no internal locking.
class CodecTranscoder {
public:
	virtual ~CodecTranscoder() = default;

	// Set up decode + encode + scale from the input stream's parameters.
	// inputPar carries the extradata (VPS/SPS/PPS), dimensions and pixel format
	// the decoder needs. Returns false if the pipeline could not be built.
	virtual bool init(const AVCodecParameters* inputPar) = 0;

	// Feed one compressed input packet. Because decoders buffer and reorder
	// frames, a single packet may yield zero, one, or several output frames;
	// sink is invoked once per produced output frame with its encoded bytes.
	virtual void transcode(const AVPacket& packet,
						   const std::function<void(std::vector<uint8_t>&)>& sink) = 0;

	// Factory — mirrors CodecHandler::create. Returns nullptr for an
	// unsupported (src -> dst) combination.
	static std::unique_ptr<CodecTranscoder> create(AVCodecID src, AVCodecID dst);
};

#endif
