#include <string>
#include <vector>
#include<ctime>
#include<iomanip>
#include <sstream>
#include <locale> 
#include <algorithm>
#include <functional>

using namespace std;
typedef function<void(vector<uint8_t>, string)> SendSegmentCallback;

#pragma once
class Mp4frag
{
public:
	SendSegmentCallback	sendSegment;
	//void(*sendSegment)(vector<uint8_t> chunk, string id);
	//void(*_parseChunk)(vector<uint8_t> chunk);

	typedef std::function<void(vector<uint8_t>)> ParseChunkCallback;
	ParseChunkCallback _parseChunk;

	string Id;

	vector<uint8_t> _FTYP{ 102, 116, 121, 112 };// ftyp
	vector<uint8_t> _MOOV{ 109, 111, 111, 118 };// moov
	vector<uint8_t> _MOOF{ 109, 111, 111, 102 };// moof
	vector<uint8_t> _MFRA{ 109, 102, 114, 97 };// mfra
	vector<uint8_t> _MDAT{ 109, 100, 97, 116 };// mdat
	vector<uint8_t> _MP4A{ 109, 112, 52, 97 };// mp4a
	vector<uint8_t> _AVCC{ 97, 118, 99, 67 };// avcC

	string mime;
	vector<uint8_t> initialization;
	vector<uint8_t> segment;
	long timestamp;
	long duration;

	Mp4frag(string path, SendSegmentCallback sendSegment);
	void _findFtyp(vector<uint8_t> chunk);
	void _findMoov(vector<uint8_t> chunk);
	void _parseMoov(vector<uint8_t> value);
	void _findMoof(vector<uint8_t> chunk);
	void _moofHunt(vector<uint8_t> chunk);
	void _findMdat(vector<uint8_t> chunk);
	void _setSegment(vector<uint8_t> chunk);

	int32_t read_32s(const std::vector<uint8_t> &buf, const unsigned offset,
		const bool bswap = false) {
		// Check for out of bounds
		if (offset > buf.size() - sizeof(int32_t)) {
			// error handling
		}
		// Swap bytes if necessary
		if (bswap) {
			return (buf[offset] << 24) | (buf[offset + 1] << 16) |
				(buf[offset + 2] << 8) | buf[offset + 3];
		}
		return (buf[offset + 3] << 24) | (buf[offset + 2] << 16) |
			(buf[offset + 1] << 8) | buf[offset];
	}


	static int indexOf(vector<uint8_t> data, vector<uint8_t> sequence)
	{
		int currOffset = 0;
		int position = 0;
		int lengthOfSeq = sequence.size();
		for (auto it = data.begin(); it != data.end(); ++it, ++position)
		{
			auto b = *it;
			if (b == sequence.at(currOffset))
			{
				if (currOffset == lengthOfSeq - 1)
				{
					auto pos = position - lengthOfSeq + 1;
					return pos;
				};
				currOffset++;
				continue;
			}

			// Fixup the offset to the byte after the beginning of the abortive sequence
			if (currOffset == 0) continue;
			position -= currOffset;
			it -= currOffset;
			currOffset = 0;
		}

		return -1;
	}

	string uint8_vector_to_hex_string(const vector<uint8_t>& v) {
		stringstream ss;
		ss << std::hex << std::setfill('0');
		vector<uint8_t>::const_iterator it;

		for (it = v.begin(); it != v.end(); it++) {
			ss << "\\x" << std::setw(2) << static_cast<unsigned>(*it);
		}

		return ss.str();
	}

	~Mp4frag();

private:
	int32_t _ftypLength;
	int32_t _moofLength;
	int32_t _mdatLength;

	vector<uint8_t> _ftyp;
	vector<uint8_t> _moof;
	int _moofHunts;
	int _moofHuntsLimit;
	vector<vector<uint8_t>> _moofBuffer;
	vector<vector<uint8_t>> _mdatBuffer;
	vector<vector<uint8_t>> bufferList;

	long _moofBufferSize;
	long _mdatBufferSize;
	long _bufferListSize;
};

