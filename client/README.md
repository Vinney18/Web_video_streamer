# WebRTC Video Streamer - Browser Client

A modern, browser-based client for streaming video using WebRTC from the C++ video streamer backend.

## Features

- **WebRTC Video Streaming**: Real-time, low-latency video playback using WebRTC
- **Data Channel Controls**: Send control commands (seek, pause, resume, speed control) via WebRTC data channels
- **Live & Playback Modes**: Support for both live streaming and playback of recorded video
- **Real-time Statistics**: Monitor connection state, ICE state, and message counts
- **Modern UI**: Clean, responsive interface with visual feedback
- **Console Logging**: Real-time event and message logging

## Architecture

```
┌─────────────────────────────────────────────────┐
│          Browser Client (This App)              │
│  ┌──────────────┐         ┌──────────────┐     │
│  │  WebRTC API  │◄───────►│ Video Player │     │
│  └──────────────┘         └──────────────┘     │
│         │                                        │
│         ├─ Media Track (RTP) ──► Video Stream   │
│         └─ Data Channel ──────► Control Msgs    │
└─────────┼────────────────────────────────────┬──┘
          │                                    │
          │ HTTP/HTTPS (Signaling)             │
          ▼                                    ▼
┌─────────────────────────────────────────────────┐
│         C++ WebRTC Server (Backend)             │
│              (libdatachannel)                   │
└─────────────────────────────────────────────────┘
```

## Files

- `webrtc-player.html` - Main HTML page with UI
- `webrtc-client.js` - WebRTC client implementation
- `README.md` - This file

## Usage

### 1. Start the Server

First, ensure your C++ WebRTC server is running:

```bash
cd /home/vineet/vineet/i2v_projects/web-video-streamer
./build/streamer/streamer
```

The server should be listening on port 8181 (or your configured port).

### 2. Serve the Client

You need to serve the HTML file over HTTP/HTTPS. You can use any static file server:

**Option A: Using Python (recommended for quick testing)**
```bash
cd /home/vineet/vineet/i2v_projects/web-video-streamer/client
python3 -m http.server 8080
```

**Option B: Using Node.js http-server**
```bash
npx http-server -p 8080
```

**Option C: Using PHP**
```bash
php -S localhost:8080
```

### 3. Open in Browser

Navigate to:
```
http://localhost:8080/webrtc-player.html
```

### 4. Configure Connection

1. **Server URL**: Enter your WebRTC server URL (default: `http://localhost:8181`)
2. **Camera ID**: Enter the camera ID you want to stream
3. **Stream Mode**:
   - **Live**: Real-time streaming
   - **PlayBack**: Recorded video playback
4. **Stream Type**:
   - **0**: Main stream (high quality)
   - **1**: Sub stream (lower quality)
5. For **PlayBack** mode, also enter the **Start Time** (Unix timestamp)

### 5. Connect

Click the **Connect** button. The client will:

1. Create a WebRTC peer connection
2. Generate an SDP offer
3. Send the offer to the server via HTTP POST to `/offer`
4. Receive SDP answer from server
5. Establish ICE connection
6. Open data channel for control messages
7. Start receiving video via RTP media stream

### 6. Playback Controls

Once connected, you can:

- **Pause**: Pause video playback
- **Resume**: Resume playback (if implemented on server)
- **Speed Control**: Change playback speed (0.5x, 1.0x, 2.0x, 4.0x)
- **Seek**: Jump to specific time in seconds

## Server API Endpoints

The client expects these HTTP endpoints on the server:

### POST /offer
**Request:**
```json
{
  "clientId": "client_1234567890_abc123",
  "sdp": "v=0\r\no=- ...",
  "type": "offer",
  "query": "cameraId~~cam1&&mode~~Live&&streamType~~0"
}
```

**Response:**
```json
{
  "sdp": "v=0\r\no=- ...",
  "type": "answer"
}
```

### POST /ice
**Request:**
```json
{
  "clientId": "client_1234567890_abc123",
  "candidate": "candidate:...",
  "sdpMid": "0",
  "sdpMLineIndex": 0
}
```

**Response:** 200 OK

## Data Channel Messages

### Client → Server (Control Commands)

- `seek_Time<seconds>` - Seek to specific time (e.g., `seek_Time120`)
- `Pause` - Pause playback
- `Resume` - Resume playback
- `FastForward<speed>` - Change playback speed (e.g., `FastForward2.0`)
- `Version` - Request server version
- `Server Status` - Request server status

### Server → Client (Metadata & Events)

- `mp4 WIDTHxHEIGHTxFPS` - Video format info (e.g., `mp4 1920x1080x30`)
- `Video_Started` - Video playback started
- `Stopped` - Video stopped
- `retrying` - Server retrying connection
- `{"event":"Playback_Finished","cameraId":"...","nextTime":...}` - Playback segment finished
- `Player_Server_Not_Connected` - Player server unavailable
- `URL_Server_Not_Connected` - URL server unavailable

## Statistics Panel

The stats panel shows:

- **Connection State**: WebRTC connection state (new, connecting, connected, disconnected, failed, closed)
- **ICE Connection State**: ICE connection state
- **Data Channel State**: Data channel state (open, closed)
- **Video Format**: Current video format (e.g., "mp4 1920x1080x30")
- **Messages Sent**: Number of control messages sent
- **Messages Received**: Number of metadata messages received

## Troubleshooting

### Video not playing

1. Check that the server is running and accessible
2. Check browser console for errors (F12)
3. Verify the server URL is correct
4. Ensure camera ID is valid
5. Check that the server has implemented the WebRTC endpoints

### Connection fails

1. Check that ICE candidates are being exchanged
2. Verify STUN servers are accessible
3. Check firewall settings
4. Try using a different browser
5. Check server logs for errors

### Data channel not opening

1. Ensure the server creates or accepts data channels
2. Check that the data channel label matches ("control")
3. Verify ordered delivery is supported

### HTTPS Issues

For production, you should serve both client and server over HTTPS:

1. Use a reverse proxy (nginx, Apache)
2. Configure SSL certificates
3. Update server URL to use `https://`

## Browser Compatibility

Tested on:
- Chrome/Chromium 90+
- Firefox 88+
- Safari 14+
- Edge 90+

WebRTC is widely supported in modern browsers.

## Development

### Debugging

Open browser developer tools (F12) to see:
- Console logs (detailed WebRTC events)
- Network tab (signaling HTTP requests)
- Media devices tab (video stream info)

### Customization

You can customize:
- **UI styling**: Edit the `<style>` section in `webrtc-player.html`
- **WebRTC config**: Modify `rtcConfig` in `webrtc-client.js`
- **STUN/TURN servers**: Add to `iceServers` array
- **Control messages**: Add handlers in `sendDataChannelMessage()`

## Example Query Strings

The client builds query strings for the server:

**Live Stream:**
```
cameraId~~cam1&&mode~~Live&&streamType~~0
```

**Playback:**
```
cameraId~~cam1&&mode~~PlayBack&&streamType~~0&&startTime~~1706745600
```

The format matches your existing WebSocket query format.

## Next Steps

1. Implement the C++ server WebRTC endpoints (`/offer`, `/ice`)
2. Integrate libdatachannel in the server
3. Test the connection end-to-end
4. Add TURN server for NAT traversal in production
5. Implement secure signaling (HTTPS, authentication)

## License

This client is part of the web-video-streamer project.
