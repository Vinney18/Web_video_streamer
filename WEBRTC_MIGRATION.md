# WebRTC Migration Guide

This guide explains how to build and use the new WebRTC-based video streaming system that replaces the WebSocket implementation.

## Overview

The system has been migrated from WebSocket to WebRTC using libdatachannel:
- **Video Streaming**: H.264 via RTP media tracks (low latency)
- **Control Messages**: Data channels for seek, pause, speed control
- **Signaling**: HTTP REST API (POST /offer, POST /ice)
- **Client**: Browser-based WebRTC player

## Prerequisites

- Docker with dev container support (already configured)
- vcpkg installed at `/vcpkg` in the container
- Modern C++ compiler (GCC 8+)
- CMake 3.18+

## Installation Steps

### Step 1: Install libdatachannel via vcpkg

Inside the dev container:

```bash
cd /vcpkg
./vcpkg install libdatachannel
```

This will install:
- libdatachannel
- libsrtp (for SRTP/SRTCP)
- usrsctp (for data channels)
- libjuice (for ICE)
- Dependencies: OpenSSL, etc.

Installation may take 10-15 minutes.

### Step 2: Configure and Build

```bash
cd /home/vineet/vineet/i2v_projects/web-video-streamer/streamer

# Create build directory
mkdir -p build
cd build

# Configure with vcpkg toolchain
cmake .. -DCMAKE_TOOLCHAIN_FILE=/vcpkg/scripts/buildsystems/vcpkg.cmake

# Build
make -j$(nproc)
```

Expected output:
```
-- Found LibDataChannel
-- Configuring done
-- Generating done
-- Build files written to: /home/vineet/vineet/i2v_projects/web-video-streamer/streamer/build
[ 50%] Building CXX object CMakeFiles/videorelay.dir/src/WebRTCWrapper.cpp.o
[100%] Linking CXX executable videorelay
```

### Step 3: Verify Build

```bash
./videorelay --help
```

You should see the help output with version information.

## Running the Server

### Start the Streamer

```bash
cd /home/vineet/vineet/i2v_projects/web-video-streamer/streamer/build
./videorelay
```

Expected logs:
```
[info] Logger created Successfully
[info] WebRTCWrapper initialized on port: 8181
[info] Starting WebRTC signaling server on port: 8181
[info] Rest server IP is: 127.0.0.1 and port is: 2908
[info] WebRTC signaling server started successfully
```

The server is now listening on:
- **Port 8181 (TCP)**: HTTP signaling server
- **Ports 10000-10100 (UDP)**: WebRTC media

## Using the Browser Client

### Step 1: Serve the Client

In a new terminal:

```bash
cd /home/vineet/vineet/i2v_projects/web-video-streamer/client
python3 -m http.server 8080
```

### Step 2: Open in Browser

Navigate to:
```
http://localhost:8080/testingclient.html
```

### Step 3: Configure Connection

1. **Server URL**: `http://localhost:8181`
2. **Camera ID**: Enter your camera ID (e.g., `cam1`)
3. **Stream Mode**:
   - `Live` for real-time streaming
   - `PlayBack` for recorded video
4. **Stream Type**:
   - `0` for main stream (high quality)
   - `1` for sub stream (lower quality)
5. For PlayBack mode: Enter **Start Time** (Unix timestamp)

### Step 4: Connect

Click **Connect** button.

You should see:
1. Connection status changes to "Connecting..."
2. ICE candidates being exchanged (in logs)
3. Connection status changes to "Connected"
4. Video starts playing

## Control Features

Once connected, you can:

### Playback Controls
- **Pause**: Pause video playback
- **Resume**: Resume playback (if implemented)
- **Speed**: 0.5x, 1.0x, 2.0x, 4.0x speed

### Seek Control
- Enter time in seconds
- Click **Seek** to jump to that position

### Statistics
Monitor real-time stats:
- Connection State
- ICE Connection State
- Data Channel State
- Video Format
- Messages Sent/Received

## Testing

### Test Live Streaming

```bash
# Configure with a live camera
# In browser client:
# - Mode: Live
# - Camera ID: cam1
# - Stream Type: 0
# Click Connect
```

### Test Playback

```bash
# Configure with playback
# In browser client:
# - Mode: PlayBack
# - Camera ID: cam1
# - Start Time: 1706745600 (example timestamp)
# Click Connect
```

### Test Control Messages

1. Connect to stream
2. Try seeking: Enter `120` and click Seek
3. Try pause: Click Pause button
4. Try speed: Click 2.0x button

Check server logs for confirmation:
```
[info] Client client_123 sent message: seek_Time120
[info] Client client_123 sent message: Pause
[info] Client client_123 sent message: FastForward2.0
```

## Troubleshooting

### Build Errors

**Error: `libdatachannel not found`**
```bash
cd /vcpkg
./vcpkg install libdatachannel
# Then rebuild
```

**Error: `undefined reference to rtc::PeerConnection`**
```bash
# Check CMakeLists.txt has:
find_package(LibDataChannel CONFIG REQUIRED)
target_link_libraries(... LibDataChannel::LibDataChannel)
```

### Connection Errors

**Browser shows "Failed to connect"**
- Check server is running: `ps aux | grep videorelay`
- Check port 8181 is listening: `netstat -tlnp | grep 8181`
- Check firewall allows port 8181

**ICE connection fails**
- STUN servers are configured (stun.l.google.com:19302)
- For restrictive networks, you may need a TURN server
- Check UDP ports 10000-10100 are open

**Video not playing**
- Check FFmpegWrapper logs for codec errors
- Verify camera ID is correct
- Check RestService (restServer.ip:restServer.port) is accessible

### No Video/Audio

**Black screen but connected**
- Check browser console (F12) for errors
- Verify H.264 codec support: `MediaRecorder.isTypeSupported('video/mp4; codecs="avc1.42E01E"')`
- Check server logs for "Video_Started" message

**Data channel not opening**
- Check browser console for data channel state
- Verify WebRTC connection is established
- Check server logs for "Data channel opened"

## Architecture Details

### Data Flow

```
Browser (testingclient.html)
    ↓ HTTP POST /offer (SDP)
WebRTCWrapper (C++ - Poco HTTP Server)
    ↓ Creates rtc::PeerConnection
    ↓ RTP Video Track + Data Channel
FFmpegWrapper (H.264 encoder)
    ↓ Callbacks: SendData, SendStringData
WebRTC Media Stream → Browser
```

### Signaling Protocol

**POST /offer**
```json
{
  "clientId": "client_1234567890_abc",
  "sdp": "v=0\r\no=- ...",
  "type": "offer",
  "query": "cameraId~~cam1&&mode~~Live&&streamType~~0"
}
```

**Response**
```json
{
  "sdp": "v=0\r\no=- ...",
  "type": "answer"
}
```

**POST /ice**
```json
{
  "clientId": "client_1234567890_abc",
  "candidate": "candidate:...",
  "sdpMid": "0",
  "sdpMLineIndex": 0
}
```

### Control Messages (Data Channel)

**Client → Server:**
- `seek_Time120` - Seek to 120 seconds
- `Pause` - Pause playback
- `Resume` - Resume playback
- `FastForward2.0` - Set speed to 2x
- `Version` - Request server version
- `Server Status` - Request server status

**Server → Client:**
- `mp4` - Video format
- `mp4 1920x1080x30` - Format with resolution/FPS
- `Video_Started` - Playback started
- `Stopped` - Stream stopped
- `{"event":"Playback_Finished","cameraId":"...","nextTime":...}` - Segment finished

## Performance

### Expected Latency

- **Live Streaming**: <200ms (WebRTC RTP)
- **Playback**: 1-3 seconds (buffering)

### Resource Usage

- **CPU**: ~5-10% per stream (H.264 encoding)
- **Memory**: ~50-100MB per FFmpegWrapper instance
- **Network**: Depends on video resolution/bitrate

### Scaling

- Supports multiple simultaneous clients
- Each client gets its own PeerConnection
- Live streams are shared (one FFmpegWrapper, multiple connections)
- Playback streams are per-client (one FFmpegWrapper per connection)

## Differences from WebSocket

| Feature | WebSocket | WebRTC |
|---------|-----------|--------|
| **Latency** | 1-3 seconds | <200ms |
| **Transport** | TCP | UDP (RTP) |
| **Signaling** | WebSocket | HTTP REST |
| **Video** | Binary frames | RTP packets |
| **Control** | Text messages | Data channel |
| **NAT Traversal** | N/A | STUN/TURN |
| **Browser Support** | Excellent | Excellent |

## Security Considerations

### Current Implementation

- HTTP signaling (not HTTPS)
- No authentication on endpoints
- STUN servers (Google public)
- No TURN server (no traversal for restrictive NATs)

### Production Recommendations

1. **HTTPS Signaling**: Use Poco SSL for HTTPS
2. **Authentication**: Add API keys or JWT tokens
3. **TURN Server**: For NAT traversal in production
4. **CORS**: Currently allows all origins (`*`)
5. **Rate Limiting**: Add to prevent abuse

## Next Steps

1. **Test thoroughly** with your camera setup
2. **Monitor logs** for any errors
3. **Add HTTPS** for production
4. **Configure TURN** if needed for NAT traversal
5. **Implement authentication** if required

## Support

Check logs for detailed error messages:
```bash
tail -f ~/.i2v_streamer/logs/log_*.log
```

For issues:
- Check server logs in `~/.i2v_streamer/logs/`
- Check browser console (F12)
- Verify ffmpeg dependencies
- Verify RestService is running

## Files Modified

### Created
- `streamer/include/WebRTCWrapper.h`
- `streamer/src/WebRTCWrapper.cpp`
- `streamer/vcpkg.json`
- `client/testingclient.html`
- `client/testingclient.js`
- `client/README.md`

### Modified
- `streamer/CMakeLists.txt` - Added libdatachannel
- `streamer/include/FFmpegWrapper.h` - Changed typedef
- `streamer/src/FFmpegWrapper.cpp` - Changed connection comparisons
- `streamer/src/main.cpp` - Use WebRTCWrapper instead of WebSocketWrapper
- `streamer/.devcontainer/docker-compose.yml` - Added UDP ports

## Version

- **Streamer Version**: 7.2.1
- **libdatachannel**: Latest from vcpkg
- **WebRTC Standard**: Compliant with browser implementations
