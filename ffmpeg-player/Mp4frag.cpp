#include "Mp4frag.h"

using namespace std::placeholders;

Mp4frag::Mp4frag(string path, SendSegmentCallback callback) 
{
	Id = path;
	sendSegment = callback;
	_parseChunk = std::bind(&Mp4frag::_findFtyp, this, _1); // &this->_findFtyp;
}

void Mp4frag::_findFtyp(vector<uint8_t> chunk)
{
	int chunkLength = chunk.size();
	if (chunkLength < 8 || chunk[4] != 102 || chunk[5] != 116 || chunk[6] != 121 || chunk[7] != 112)
	{
		return;  // FTYP not found
	}

	_ftypLength = read_32s(chunk, 0, true);

	if (_ftypLength < chunkLength)
	{
		vector<uint8_t>::const_iterator start = chunk.begin();
		vector<uint8_t>::const_iterator end = chunk.end();

		vector<uint8_t> _ftyp(start, start + _ftypLength);
		this->_ftyp = _ftyp;
		_parseChunk = std::bind(&Mp4frag::_findMoov, this, _1);// this->_findMoov;

		vector<uint8_t> tempChunk(start + _ftypLength, end);
		_parseChunk(tempChunk);
	}
	else if (_ftypLength == chunkLength)
	{
		this->_ftyp = chunk;
		_parseChunk = std::bind(&Mp4frag::_findMoov, this, _1); //this->_findMoov;
	}
	else
	{
		//should not be possible to get here because ftyp is approximately 24 bytes
		//will have to buffer this chunk and wait for rest of it on next pass
		return;
	}
}

void Mp4frag::_findMoov(vector<uint8_t> chunk)
{
	int chunkLength = chunk.size();
	if (chunkLength < 8 || chunk[4] != 109 || chunk[5] != 111 || chunk[6] != 111 || chunk[7] != 118)
	{
		return; //MOOV not found
	}

	int32_t moovLength = read_32s(chunk, 0, true);
	if (moovLength < chunkLength)
	{
		vector<uint8_t> tempBuf(_ftypLength + moovLength);
		copy_n(_ftyp.begin(), _ftypLength, tempBuf.begin());
		copy_n(chunk.begin(), moovLength, tempBuf.begin() + _ftypLength);
		_parseMoov(tempBuf);
		_ftyp.clear();
		_ftypLength = 0;
		_parseChunk = std::bind(&Mp4frag::_findMoof, this, _1); //this->_findMoof;

		vector<uint8_t> tempChunk(chunk.begin() + moovLength, chunk.end());
		_parseChunk(tempChunk);
	}
	else if (moovLength == chunkLength)
	{
		vector<uint8_t> tempBuf(_ftypLength + moovLength);
		copy_n(_ftyp.begin(), _ftypLength, tempBuf.begin());
		copy_n(chunk.begin(), moovLength, tempBuf.begin() + _ftypLength);
		_parseMoov(tempBuf);
		_ftyp.clear();
		_ftypLength = 0;
		_parseChunk = std::bind(&Mp4frag::_findMoof, this, _1); // this->_findMoof;
	}
	else
	{
		//probably should not arrive here here because moov is typically < 800 bytes
		//will have to store chunk until size is big enough to have entire moov piece
		//ffmpeg may have crashed before it could output moov and got us here
		return;
	}
}

void Mp4frag::_parseMoov(vector<uint8_t> value)
{
	initialization = value;
	string audioString = "";
	if (indexOf(initialization, _MP4A) != -1)
	{
		audioString = ", mp4a.40.2";
	}
	auto index = indexOf(initialization, _AVCC);
	if (index == -1)
	{
		//codec info not found.
		return;
	}
	index += 5;
	vector<uint8_t> tempBuf(3);
	copy_n(initialization.begin() + index, 3, tempBuf.begin());
	std::ostringstream mimeStream;
	std::locale loc;
	//mimeStream << "video / mp4; codecs = \"avc1." << std::toupper(uint8_vector_to_hex_string(tempBuf), loc) << audioString;
	mime = mimeStream.str();
	timestamp = time(0);

	//raise initialized event
	//this.emit('initialized', { mime: this._mime, initialization: this._initialization, m3u8: this._m3u8 || null});
	//onInit ? .Invoke(initialization, Id);
	sendSegment(initialization, Id);
}

void Mp4frag::_findMoof(vector<uint8_t> chunk)
{
	if (!_moofBuffer.empty())
	{
		_moofBuffer.push_back(chunk);
		auto chunkLength = chunk.size();
		_moofBufferSize += chunkLength;
		if (_moofLength == _moofBufferSize)
		{
			//todo verify this works
			_moof.assign(_moofLength, 0);
			int writeIdx = 0;
			for (auto& byteArr : _moofBuffer)
			{
				if (writeIdx < _moofLength)
				{
					auto len = _moofLength > (byteArr.size() + writeIdx) ? byteArr.size() : (_moofLength - writeIdx);
					copy_n(byteArr.begin(), len, _moof.begin() + writeIdx);
					writeIdx += byteArr.size();
				}
				else
					break;
			}

			_moofBuffer.clear();
			_moofBufferSize = 0;
			this->_parseChunk = std::bind(&Mp4frag::_findMdat, this, _1); // &this->_findMdat;
		}
		else if (_moofLength < _moofBufferSize)
		{
			_moof.assign(_moofLength, 0);
			int writeIdx = 0;
			for (auto& byteArr : _moofBuffer)
			{
				if (writeIdx < _moofLength)
				{
					auto len = _moofLength > (byteArr.size() + writeIdx) ? byteArr.size() : (_moofLength - writeIdx);
					copy_n(byteArr.begin(), len, _moof.begin() + writeIdx);
					writeIdx += byteArr.size();
				}
				else
					break;
			}

			auto sliceIndex = chunkLength - (_moofBufferSize - _moofLength);
			_moofBuffer.clear();
			_moofBufferSize = 0;
			_parseChunk = std::bind(&Mp4frag::_findMdat, this, _1); // &this->_findMdat;
			vector<uint8_t> tempChunk(chunk.begin() + sliceIndex, chunk.end());
			_parseChunk(tempChunk);
		}
	}
	else
	{
		auto chunkLength = chunk.size();
		if (chunkLength < 8 || chunk[4] != 109 || chunk[5] != 111 || chunk[6] != 111 || chunk[7] != 102)
		{
			//ffmpeg occasionally pipes corrupt data, lets try to get back to normal if we can find next MOOF box before attempts run out
			auto mfraIndex = indexOf(chunk, _MFRA);
			if (mfraIndex != -1)
			{
				//console.log(`MFRA was found at ${mfraIndex}. This is expected at the end of stream.`);
				return;
			}
			//console.warn('Failed to find MOOF. Starting MOOF hunt. Ignore this if your file stream input has ended.');
			_moofHunts = 0;
			_moofHuntsLimit = 40;
			_parseChunk = std::bind(&Mp4frag::_moofHunt, this, _1); // &this->_moofHunt;
			_parseChunk(chunk);
			return;
		}
		_moofLength = read_32s(chunk, 0, true);
		if (_moofLength == 0)
		{
			//this.emit('error', new Error(`Bad data from input stream reports ${ _MOOF.toString() } length of 0.`));
			return;
		}
		if (_moofLength < chunkLength)
		{
			_moof.assign(_moofLength, 0);
			copy_n(chunk.begin(), _moofLength, _moof.begin());
			_parseChunk = std::bind(&Mp4frag::_findMdat, this, _1); //  &this->_findMdat;
			vector<uint8_t> tempChunk(chunk.begin() + _moofLength, chunk.end());
			_parseChunk(tempChunk);
		}
		else if (_moofLength == chunkLength)
		{
			//todo verify this works
			_moof = chunk;
			_parseChunk = std::bind(&Mp4frag::_findMdat, this, _1); //  &this->_findMdat;
		}
		else
		{
			_moofBuffer.assign(1, vector<uint8_t> {chunk});
			_moofBufferSize = chunkLength;
		}
	}
}

void Mp4frag::_moofHunt(vector<uint8_t> chunk)
{
	if (_moofHunts < _moofHuntsLimit)
	{
		this->_moofHunts++;
		//console.warn(`MOOF hunt attempt number ${this._moofHunts}.`);
		auto index = indexOf(chunk, _MOOF);
		if (index > 3 && chunk.size() > index + 3)
		{
			_moofHunts = 0;
			_moofHuntsLimit = 0;
			_parseChunk = std::bind(&Mp4frag::_findMoof, this, _1); //   &this->_findMoof;
			vector<uint8_t> tempChunk(chunk.begin() + (index - 4), chunk.end());
			_parseChunk(tempChunk);
		}
	}
	else
	{
		//hunt failed after ${ this._moofHunts}
		return;
	}
}

void Mp4frag::_findMdat(vector<uint8_t> chunk)
{
	if (!_mdatBuffer.empty())
	{
		_mdatBuffer.push_back(chunk);
		auto chunkLength = chunk.size();
		_mdatBufferSize += chunkLength;
		if (_mdatLength == _mdatBufferSize)
		{
			vector<uint8_t> tempChunk(_moofLength + _mdatLength);
			copy_n(_moof.begin(), _moofLength, tempChunk.begin());
			int writeIdx = (int)_moofLength;
			for (auto& byteArr : _mdatBuffer)
			{
				if (writeIdx < _moofLength + _mdatLength)
				{
					auto len = _moofLength + _mdatLength > (byteArr.size() + writeIdx) ? byteArr.size() : (_moofLength + _mdatLength - writeIdx);
					copy_n(byteArr.begin(), len, tempChunk.begin() + writeIdx);
					writeIdx += byteArr.size();
				}
				else
					break;
			}
			_setSegment(tempChunk);
			_moof.clear();
			_mdatBuffer.clear();
			_mdatBufferSize = 0;
			_mdatLength = 0;
			_moofLength = 0;
			_parseChunk = std::bind(&Mp4frag::_findMoof, this, _1); //  &this->_findMoof;
		}
		else if (_mdatLength < _mdatBufferSize)
		{
			vector<uint8_t> tempChunk(_moof.begin(), _moof.begin() + _moofLength);
			int writeIdx = (int)_moofLength;
			for (auto& byteArr : _mdatBuffer)
			{
				if (writeIdx < _moofLength + _mdatLength)
				{
					auto len = _moofLength + _mdatLength > (byteArr.size() + writeIdx) ? byteArr.size() : (_moofLength + _mdatLength - writeIdx);
					copy_n(byteArr.begin(), len, tempChunk.begin() + writeIdx);
					writeIdx += byteArr.size();
				}
				else
					break;
			}
			_setSegment(tempChunk);
			auto sliceIndex = chunkLength - (_mdatBufferSize - _mdatLength);
			_moof.clear();
			_mdatBuffer.clear();
			_mdatBufferSize = 0;
			_mdatLength = 0;
			_moofLength = 0;
			_parseChunk = std::bind(&Mp4frag::_findMoof, this, _1); //  &this->_findMoof;
			vector<uint8_t> temp(chunk.begin() + sliceIndex, chunk.end());
			_parseChunk(temp);
		}
	}
	else
	{
		auto chunkLength = chunk.size();
		if (chunkLength < 8 || chunk[4] != 109 || chunk[5] != 100 || chunk[6] != 97 || chunk[7] != 116)
		{
			//this.emit('error', new Error(`${ _MDAT.toString() } not found.`));
			return;
		}
		_mdatLength = read_32s(chunk, 0, true);
		if (_mdatLength > chunkLength)
		{
			_mdatBuffer.assign(1, vector<uint8_t> {chunk});
			_mdatBufferSize = chunkLength;
		}
		else if (_mdatLength == chunkLength)
		{
			vector<uint8_t> tempChunk(_moofLength + chunkLength);
			copy_n(_moof.begin(), _moofLength, tempChunk.begin());
			copy_n(chunk.begin(), chunkLength, tempChunk.begin() + _moofLength);
			_setSegment(tempChunk);
			_moof.clear();
			_moofLength = 0;
			_mdatLength = 0;
			_parseChunk = std::bind(&Mp4frag::_findMoof, this, _1); //  &this->_findMoof;
		}
		else
		{
			vector<uint8_t> tempChunk(_moofLength + _mdatLength);
			copy_n(_moof.begin(), _moofLength, tempChunk.begin());
			copy_n(chunk.begin(), _mdatLength, tempChunk.begin() + _moofLength);
			_setSegment(tempChunk);
			auto sliceIndex = _mdatLength;
			_moof.clear();
			_moofLength = 0;
			_mdatLength = 0;
			_parseChunk = std::bind(&Mp4frag::_findMoof, this, _1); // &this->_findMoof;
			vector<uint8_t> tempParseChunk(chunk.begin() + sliceIndex, chunk.end());
			_parseChunk(tempParseChunk);
		}
	}
}

void Mp4frag::_setSegment(vector<uint8_t> chunk)
{
	segment = chunk;
	auto currentTime = time(0);
	duration = max((currentTime - timestamp) / 1000, (long long)1);
	timestamp = currentTime;

	if (!bufferList.empty())
	{
		bufferList.push_back(segment);
		while (bufferList.size() > _bufferListSize)
		{
			bufferList.erase(bufferList.begin());
		}
	}
	sendSegment(segment, Id);
}



Mp4frag::~Mp4frag()
{

}
