# Quick Start Guide - WebRTC Video Streamer

## Installation (5 minutes)

### 1. Install libdatachannel
```bash
cd /vcpkg
./vcpkg install libdatachannel
```

### 2. Build
```bash
cd /home/vineet/vineet/i2v_projects/web-video-streamer/streamer/build
cmake .. -DCMAKE_TOOLCHAIN_FILE=/vcpkg/scripts/buildsystems/vcpkg.cmake
make -j$(nproc)
```

### 3. Run Server
```bash
./videorelay
```

### 4. Open Client
```bash
# In new terminal
cd /home/vineet/vineet/i2v_projects/web-video-streamer/client
python3 -m http.server 8080

# Open browser to: http://localhost:8080/webrtc-player.html
```

### 5. Connect
- Server URL: `http://localhost:8181`
- Camera ID: `cam1` (or your camera ID)
- Mode: `Live` or `PlayBack`
- Click **Connect**

## Common Commands

### Build
```bash
cd streamer/build
cmake .. -DCMAKE_TOOLCHAIN_FILE=/vcpkg/scripts/buildsystems/vcpkg.cmake && make -j$(nproc)
```

### Run
```bash
./videorelay
```

### Test
```bash
# Serve client
python3 -m http.server 8080 -d client

# Open: http://localhost:8080/webrtc-player.html
```

### Logs
```bash
tail -f ~/.i2v_streamer/logs/log_*.log
```

## Key Files

| File | Purpose |
|------|---------|
| `streamer/include/WebRTCWrapper.h` | WebRTC header |
| `streamer/src/WebRTCWrapper.cpp` | WebRTC implementation |
| `streamer/include/FFmpegWrapper.h` | Video processing (typedef changed) |
| `streamer/src/main.cpp` | Entry point (uses WebRTCWrapper) |
| `client/webrtc-player.html` | Browser client UI |
| `client/webrtc-client.js` | WebRTC client logic |

## Ports

- **8181** (TCP): HTTP signaling server
- **10000-10100** (UDP): WebRTC media

## Troubleshooting

### Build fails
```bash
# Reinstall libdatachannel
cd /vcpkg
./vcpkg remove libdatachannel
./vcpkg install libdatachannel
```

### Connection fails
```bash
# Check server is running
ps aux | grep videorelay

# Check port is listening
netstat -tlnp | grep 8181
```

### Video not playing
- Check browser console (F12)
- Check server logs
- Verify camera ID is correct

## What Changed

### Before (WebSocket)
```cpp
// Old typedef
typedef websocketpp::connection_hdl webConnHdl;

// Old main.cpp
WebSocketWrapper ws_wrapper(...);
ws_wrapper.run();
```

### After (WebRTC)
```cpp
// New typedef
typedef std::shared_ptr<rtc::PeerConnection> webConnHdl;

// New main.cpp
WebRTCWrapper rtc_wrapper(...);
rtc_wrapper.run();
```

## API Endpoints

### POST /offer
Send SDP offer, get SDP answer

### POST /ice
Send ICE candidate

## Control Messages

Send via data channel:
- `seek_Time120` - Seek to 120s
- `Pause` - Pause
- `FastForward2.0` - 2x speed

## Performance

- **Latency**: <200ms (Live)
- **CPU**: ~5-10% per stream
- **Memory**: ~50-100MB per instance

## Next Steps

1. Test with your cameras
2. Configure for production (HTTPS, auth)
3. Add TURN server for NAT traversal
4. Monitor performance

## Support

See [WEBRTC_MIGRATION.md](WEBRTC_MIGRATION.md) for detailed documentation.
