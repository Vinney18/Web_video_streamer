/**
 * WebRTC Video Streamer Client
 * Connects to C++ WebRTC server using WebSocket signaling with Trickle ICE
 */

// Global state
let peerConnection = null;
let dataChannel = null;
let signalingWs = null;  // WebSocket for signaling
let serverUrl = '';
let clientId = '';
let isConnected = false;
let messagesSent = 0;
let messagesReceived = 0;
let pendingCandidates = [];  // Buffer ICE candidates until remote description is set
let remoteDescriptionSet = false;

// Codec / MJPEG state
let currentCodec = null;
let mjpegMode = false;
let mjpegImgEl = null;
let mjpegLastObjectUrl = null;
let mjpegFrames = 0;
let mjpegBytes = 0;
let mjpegLastFrameMs = 0;
let statsInterval = null;

// Frame metadata delivered over data channel, keyed by RTP timestamp
const metadataByRtpTs = new Map();

// WS connect retry
let retryCount = 0;
let retryScheduled = false;
let connectTimeoutId = null;
const MAX_RETRIES = 5;
const CONNECT_TIMEOUT_MS = 10000;
const RETRY_DELAY_MS = 2000;

/**
 * Detect whether this browser's WebRTC stack will negotiate H.265/HEVC.
 * The receiver capabilities are authoritative for the RTP video path;
 * if H265 isn't listed, SDP will never carry it.
 */
function detectH265WebRTC() {
    try {
        if (window.RTCRtpReceiver && RTCRtpReceiver.getCapabilities) {
            const caps = RTCRtpReceiver.getCapabilities('video');
            return !!(caps && caps.codecs &&
                caps.codecs.some(c => /h265|hevc|hev1|hvc1/i.test(c.mimeType)));
        }
    } catch (_) {}
    return false;
}

const H265_SUPPORTED = detectH265WebRTC();

// Default WebRTC configuration - overridden if server supplies iceServers in the offer
const rtcConfig = {
    iceServers: [
        { urls: 'stun:stun.l.google.com:19302' },
        { urls: 'stun:stun1.l.google.com:19302' }
    ],
    iceCandidatePoolSize: 10
};

/**
 * Generate unique client ID
 */
function generateClientId() {
    return 'client_' + Date.now() + '_' + Math.random().toString(36).substr(2, 9);
}

/**
 * Log message to console
 */
function log(message, type = 'info') {
    const logConsole = document.getElementById('logConsole');
    const timestamp = new Date().toLocaleTimeString();
    const entry = document.createElement('div');
    entry.className = `log-entry ${type}`;
    entry.textContent = `[${timestamp}] ${message}`;
    logConsole.appendChild(entry);
    logConsole.scrollTop = logConsole.scrollHeight;
    console.log(`[${type.toUpperCase()}] ${message}`);
}

/**
 * Update connection status
 */
function updateStatus(status, state) {
    const statusText = document.getElementById('connectionStatus');
    const statusIndicator = document.getElementById('statusIndicator');

    statusText.textContent = status;
    statusIndicator.className = 'status-indicator ' + state;

    isConnected = (state === 'connected');
    updateUIState();
}

/**
 * Update UI state based on connection
 */
function updateUIState() {
    const connectBtn = document.getElementById('connectBtn');
    const disconnectBtnGroup = document.getElementById('disconnectBtnGroup');
    const videoOverlay = document.getElementById('videoOverlay');
    const controlButtons = document.querySelectorAll('.control-button');
    const seekBtn = document.getElementById('seekBtn');
    const pauseBtn = document.getElementById('pauseBtn');
    const resumeBtn = document.getElementById('resumeBtn');

    connectBtn.disabled = isConnected;
    disconnectBtnGroup.style.display = isConnected ? 'block' : 'none';

    if (isConnected) {
        videoOverlay.classList.add('hidden');
        controlButtons.forEach(btn => btn.disabled = false);
        seekBtn.disabled = false;
        pauseBtn.disabled = false;
        resumeBtn.disabled = false;
    } else {
        videoOverlay.classList.remove('hidden');
        controlButtons.forEach(btn => btn.disabled = true);
        seekBtn.disabled = true;
        pauseBtn.disabled = true;
        resumeBtn.disabled = true;
    }
}

/**
 * Update statistics
 */
function updateStats(key, value) {
    const statElement = document.getElementById('stat' + key);
    if (statElement) {
        statElement.textContent = value;
    }
}

/**
 * Build query object from form inputs (server expects JSON object)
 */
function buildQuery() {
    const cameraId = document.getElementById('cameraId').value;
    const mode = document.getElementById('streamMode').value;
    const streamType = document.getElementById('streamType').value;
    const startTime = document.getElementById('startTime').value;
    const syncGroup = document.getElementById('syncGroup').value.trim();

    const query = {
        cameraId: cameraId,
        mode: mode,
        streamType: Number(streamType),
        h265Supported: H265_SUPPORTED
    };

    if (mode === 'PlayBack' && startTime) {
        query.startTime = Number(startTime);
    }

    if (syncGroup) {
        query.syncGroup = syncGroup;
    }

    return query;
}

/**
 * Send data via data channel
 */
function sendDataChannelMessage(message) {
    if (dataChannel && dataChannel.readyState === 'open') {
        dataChannel.send(message);
        messagesSent++;
        updateStats('MessagesSent', messagesSent);
        log(`Sent: ${message}`, 'info');
        return true;
    } else {
        log('Data channel not open', 'error');
        return false;
    }
}

/**
 * Send signaling message via WebSocket
 */
function sendSignalingMessage(message) {
    if (signalingWs && signalingWs.readyState === WebSocket.OPEN) {
        signalingWs.send(JSON.stringify(message));
        log(`Signaling sent: ${message.type}`, 'info');
    } else {
        log('Signaling WebSocket not open', 'error');
    }
}

/**
 * Handle signaling messages from server
 */
function handleSignalingMessage(message) {
    log(`Signaling received: ${message.type}`, 'info');

    switch (message.type) {
        case 'codec':
            handleCodec(message.codec);
            break;
        case 'offer':
            handleServerOffer(message);
            break;
        case 'candidate':
            handleRemoteCandidate(message);
            break;
        case 'mjpegStream':
            log('MJPEG stream ready', 'success');
            updateStatus('Connected', 'connected');
            break;
        case 'mjpegInfo':
            log(`mjpeg-info: ${message.message}`, 'info');
            break;
        case 'status':
            log(`status: ${message.message}`, 'info');
            break;
        case 'error':
            log(`Server error: ${message.message}`, 'error');
            if (message.message && message.message.toLowerCase().includes('unsupported codec')) {
                log('Unsupported codec, retrying request in 2s...', 'info');
                setTimeout(() => {
                    sendSignalingMessage({
                        type: 'request',
                        clientId: clientId,
                        query: buildQuery()
                    });
                }, 2000);
            }
            break;
        default:
            log(`Unknown signaling message type: ${message.type}`, 'error');
    }
}

/**
 * Handle codec announcement from server. Swaps between <video> (WebRTC) and
 * <img> (MJPEG-over-WebSocket).
 */
function handleCodec(codec) {
    currentCodec = codec;
    updateStats('Codec', codec);
    log(`Codec: ${codec}`, 'info');

    const video = document.getElementById('videoPlayer');
    const img = document.getElementById('mjpegImage');

    if (codec === 'mjpeg') {
        mjpegMode = true;
        video.style.display = 'none';
        img.style.display = 'block';
        mjpegImgEl = img;
        mjpegFrames = 0;
        mjpegBytes = 0;
        mjpegLastFrameMs = Date.now();

        if (statsInterval) clearInterval(statsInterval);
        statsInterval = setInterval(() => {
            updateStats('Frames', `${mjpegFrames} / ${mjpegBytes}B`);
            if (Date.now() - mjpegLastFrameMs > 6000) {
                log('MJPEG stale (>6s), reconnecting...', 'error');
                clearInterval(statsInterval);
                statsInterval = null;
                try { if (signalingWs) signalingWs.close(); } catch (_) {}
            }
        }, 2000);
    } else {
        mjpegMode = false;
        img.style.display = 'none';
        video.style.display = 'block';
    }
}

/**
 * Handle incoming MJPEG frame (Blob over WebSocket).
 */
function handleMjpegFrame(blob) {
    if (!mjpegImgEl) return;
    const url = URL.createObjectURL(blob);
    const prev = mjpegLastObjectUrl;
    mjpegImgEl.onload = () => { if (prev) URL.revokeObjectURL(prev); };
    mjpegImgEl.src = url;
    mjpegLastObjectUrl = url;
    mjpegFrames++;
    mjpegBytes += blob.size;
    mjpegLastFrameMs = Date.now();
}

/**
 * Handle SDP offer from server (server-as-offerer pattern)
 */
async function handleServerOffer(message) {
    try {
        log('Received offer from server, creating peer connection...', 'info');
        console.log('=== SDP OFFER FROM SERVER ===\n' + message.sdp);

        // If the server sent iceServers (e.g. TURN credentials), use them instead
        // of the default STUN-only config. Required for NAT-to-NAT connections.
        const cfg = (message.iceServers && message.iceServers.length)
            ? { ...rtcConfig, iceServers: message.iceServers }
            : rtcConfig;
        log(`Using ${cfg.iceServers.length} ICE server(s)${message.iceServers ? ' (from server)' : ' (default STUN)'}`, 'info');

        // Always create a fresh peer connection for each offer. Reusing an
        // existing PC across offers forces DTLS renegotiation with a new
        // server fingerprint — SRTP unwrap then silently fails and no
        // frames decode (bytesReceived climbs, framesDecoded stays 0).
        if (peerConnection) {
            try { peerConnection.close(); } catch (_) {}
            peerConnection = null;
            dataChannel = null;
        }
        remoteDescriptionSet = false;
        pendingCandidates = [];
        if (!await createPeerConnection(cfg)) {
            updateStatus('Failed', 'disconnected');
            return;
        }

        // Set remote description (server's offer)
        log('Setting remote description (offer)...', 'info');
        await peerConnection.setRemoteDescription({
            type: 'offer',
            sdp: message.sdp
        });
        remoteDescriptionSet = true;
        log('Remote description set successfully', 'success');

        // Flush any ICE candidates that arrived before the offer
        for (const candidate of pendingCandidates) {
            await peerConnection.addIceCandidate(candidate);
            log('Added buffered ICE candidate', 'info');
        }
        pendingCandidates = [];

        // Create answer
        log('Creating answer...', 'info');
        const answer = await peerConnection.createAnswer();
        await peerConnection.setLocalDescription(answer);

        // Send answer to server
        log('Sending answer to server...', 'info');
        sendSignalingMessage({
            type: 'answer',
            clientId: clientId,
            sdp: answer.sdp,
            sdpType: answer.type
        });
    } catch (error) {
        log(`Error handling server offer: ${error.message}`, 'error');
    }
}

/**
 * Handle ICE candidate from server (Trickle ICE)
 */
async function handleRemoteCandidate(message) {
    try {
        if (peerConnection && message.candidate) {
            const candidate = new RTCIceCandidate({
                candidate: message.candidate,
                sdpMid: message.sdpMid || '0',
                sdpMLineIndex: message.sdpMLineIndex || 0
            });

            if (remoteDescriptionSet) {
                await peerConnection.addIceCandidate(candidate);
                log('Added remote ICE candidate', 'info');
            } else {
                pendingCandidates.push(candidate);
                log('Buffered ICE candidate (waiting for remote description)', 'info');
            }
        }
    } catch (error) {
        log(`Error adding remote ICE candidate: ${error.message}`, 'error');
    }
}

/**
 * Create peer connection
 */
async function createPeerConnection(cfg) {
    try {
        log('Creating peer connection...', 'info');

        peerConnection = new RTCPeerConnection(cfg || rtcConfig);

        // Connection state changes
        peerConnection.onconnectionstatechange = () => {
            const state = peerConnection.connectionState;
            log(`Connection state: ${state}`, 'info');
            updateStats('ConnectionState', state);

            if (state === 'connected') {
                updateStatus('Connected', 'connected');
                log('WebRTC connection established!', 'success');

                // Log which ICE candidate pair was selected
                peerConnection.getStats().then(stats => {
                    stats.forEach(report => {
                        if (report.type === 'candidate-pair' && report.state === 'succeeded') {
                            console.log('Active candidate pair:', report);
                            stats.forEach(r => {
                                if (r.id === report.localCandidateId) {
                                    console.log('Local candidate:', r);
                                    log(`Local ICE: ${r.candidateType} ${r.address}:${r.port} ${r.protocol}`, 'info');
                                }
                                if (r.id === report.remoteCandidateId) {
                                    console.log('Remote candidate:', r);
                                    log(`Remote ICE: ${r.candidateType} ${r.address}:${r.port} ${r.protocol}`, 'info');
                                }
                            });
                        }
                    });
                });
            } else if (state === 'disconnected' || state === 'failed' || state === 'closed') {
                updateStatus('Disconnected', 'disconnected');
                log('WebRTC connection lost', 'error');
            }
        };

        // ICE connection state
        peerConnection.oniceconnectionstatechange = () => {
            const state = peerConnection.iceConnectionState;
            log(`ICE connection state: ${state}`, 'info');
            updateStats('IceState', state);
        };

        // ICE candidate gathering
        peerConnection.onicegatheringstatechange = () => {
            log(`ICE gathering state: ${peerConnection.iceGatheringState}`, 'info');
        };

        // Handle ICE candidates - send immediately via WebSocket (Trickle ICE)
        peerConnection.onicecandidate = (event) => {
            if (event.candidate) {
                log(`Sending ICE candidate to server`, 'info');
                sendSignalingMessage({
                    type: 'candidate',
                    clientId: clientId,
                    candidate: event.candidate.candidate,
                    sdpMid: event.candidate.sdpMid,
                    sdpMLineIndex: event.candidate.sdpMLineIndex
                });
            } else {
                log('All local ICE candidates gathered', 'success');
            }
        };

        // Handle incoming tracks (video stream)
        peerConnection.ontrack = (event) => {
            log(`Received ${event.track.kind} track, streams: ${event.streams.length}`, 'success');

            if (event.track.kind === 'video') {
                const videoPlayer = document.getElementById('videoPlayer');
                if (event.streams && event.streams[0]) {
                    videoPlayer.srcObject = event.streams[0];
                } else {
                    const stream = new MediaStream([event.track]);
                    videoPlayer.srcObject = stream;
                }
                log('Video stream connected to player', 'success');

                // Log RTP timestamp per presented frame and match against metadata
                // delivered via the data channel (keyed by RTP timestamp).
                if ('requestVideoFrameCallback' in videoPlayer) {
                    const onFrame = (now, metadata) => {
                        const meta = metadataByRtpTs.get(metadata.rtpTimestamp);
                        if (meta) {
                            console.log(
                                `frame#${metadata.presentedFrames} rtpTs=${metadata.rtpTimestamp} matched meta:`,
                                meta
                            );
                            metadataByRtpTs.delete(metadata.rtpTimestamp);
                        } else {
                            console.log(
                                `frame#${metadata.presentedFrames} rtpTs=${metadata.rtpTimestamp} (no meta)`
                            );
                        }
                        videoPlayer.requestVideoFrameCallback(onFrame);
                    };
                    videoPlayer.requestVideoFrameCallback(onFrame);
                }

                // Poll WebRTC stats to check if bytes are arriving
                if (statsInterval) clearInterval(statsInterval);
                statsInterval = setInterval(async () => {
                    if (!peerConnection) { clearInterval(statsInterval); statsInterval = null; return; }
                    const stats = await peerConnection.getStats();
                    stats.forEach(report => {
                        if (report.type === 'inbound-rtp' && report.kind === 'video') {
                            console.log(`[VIDEO STATS] bytesReceived=${report.bytesReceived} packetsReceived=${report.packetsReceived} framesDecoded=${report.framesDecoded} framesDropped=${report.framesDropped}`);
                            updateStats('Frames', `${report.framesDecoded || 0} / ${report.bytesReceived || 0}B`);
                        }
                    });
                }, 2000);
            }
        };

        // Handle data channel from server (server creates it as offerer)
        peerConnection.ondatachannel = (event) => {
            log('Received data channel from server: ' + event.channel.label, 'info');
            dataChannel = event.channel;

            dataChannel.onopen = () => {
                log('Data channel opened', 'success');
                updateStats('DataChannelState', 'open');
            };

            dataChannel.onclose = () => {
                log('Data channel closed', 'error');
                updateStats('DataChannelState', 'closed');
            };

            dataChannel.onerror = (error) => {
                log(`Data channel error: ${error}`, 'error');
            };

            dataChannel.onmessage = (event) => {
                messagesReceived++;
                updateStats('MessagesReceived', messagesReceived);

                // Intercept per-frame metadata JSON keyed by RTP timestamp;
                // it is matched against video frames in requestVideoFrameCallback.
                try {
                    const parsed = JSON.parse(event.data);
                    if (parsed && parsed.type === 'frameMeta') {
                        metadataByRtpTs.set(parsed.rtpTs, parsed);
                        if (metadataByRtpTs.size > 100) {
                            const oldestKey = metadataByRtpTs.keys().next().value;
                            metadataByRtpTs.delete(oldestKey);
                        }
                        return;
                    }
                } catch (_) { /* not JSON, fall through to text handler */ }

                log(`Received: ${event.data}`, 'success');
                handleServerMessage(event.data);
            };
        };

        return true;
    } catch (error) {
        log(`Error creating peer connection: ${error.message}`, 'error');
        return false;
    }
}

/**
 * Handle messages from server
 */
function handleServerMessage(message) {
    // Handle metadata and events from server
    if (message.startsWith('mp4')) {
        // Format: "mp4 WIDTHxHEIGHTxFPS"
        updateStats('VideoFormat', message);
    } else if (message === 'Video_Started') {
        log('Video playback started', 'success');
    } else if (message === 'Stopped') {
        log('Video playback stopped', 'info');
    } else if (message.includes('Playback_Finished')) {
        try {
            const data = JSON.parse(message);
            log(`Playback finished. Next time: ${data.nextTime}`, 'info');
        } catch (e) {
            log('Playback finished', 'info');
        }
    } else if (message === 'Player_Server_Not_Connected') {
        log('Player server not connected', 'error');
        disconnect();
    } else if (message === 'URL_Server_Not_Connected') {
        log('URL server not connected', 'error');
        disconnect();
    } else if (message.startsWith('--version')) {
        log(message, 'info');
    } else {
        // Generic message
        log(`Server message: ${message}`, 'info');
    }
}

/**
 * Connect to server using WebSocket signaling
 */
async function connect() {
    // First user-initiated connect: reset retry state.
    retryCount = 0;
    serverUrl = document.getElementById('serverUrl').value.trim();
    clientId = generateClientId();
    log(`Client ID: ${clientId}`, 'info');
    openSignalingSocket();
}

/**
 * Open (or re-open) the signaling WebSocket. Extracted so retry logic can
 * reuse it without going through the full connect() setup each attempt.
 */
function openSignalingSocket() {
    try {
        // Convert HTTP URL to WebSocket URL
        let wsUrl = serverUrl.replace(/^http/, 'ws');
        if (!wsUrl.startsWith('ws://') && !wsUrl.startsWith('wss://')) {
            wsUrl = 'ws://' + wsUrl;
        }

        // Tear down any previous WS before opening a new one. Null the handlers
        // first so a late-firing event on the stale socket doesn't trigger
        // another retry path.
        if (signalingWs) {
            try {
                signalingWs.onopen = signalingWs.onmessage = signalingWs.onerror = signalingWs.onclose = null;
                signalingWs.close();
            } catch (_) {}
            signalingWs = null;
        }

        if (retryCount > MAX_RETRIES) {
            log(`Giving up after ${retryCount} retries`, 'error');
            updateStatus('Failed', 'disconnected');
            return;
        }

        log(`Connecting to ${wsUrl} (attempt ${retryCount + 1})`, 'info');
        updateStatus('Connecting...', 'connecting');

        const ws = new WebSocket(wsUrl);
        ws.binaryType = 'blob';  // required for MJPEG frames
        signalingWs = ws;

        // Single-shot retry guard: error and timeout share this path
        // so we don't fire multiple reconnects for the same failure.
        // onclose does NOT trigger retry — the server closes the signaling
        // WS intentionally after "starting video", and retrying on that
        // close causes DTLS/SRTP renegotiation confusion.
        retryScheduled = false;
        let hasOpened = false;
        const scheduleRetry = (reason) => {
            if (retryScheduled) return;
            retryScheduled = true;
            if (connectTimeoutId) { clearTimeout(connectTimeoutId); connectTimeoutId = null; }
            retryCount++;
            log(`${reason}, retrying in ${RETRY_DELAY_MS}ms...`, 'error');
            try { ws.onopen = ws.onmessage = ws.onerror = ws.onclose = null; } catch (_) {}
            try { ws.close(); } catch (_) {}
            setTimeout(openSignalingSocket, RETRY_DELAY_MS);
        };

        connectTimeoutId = setTimeout(() => {
            if (ws.readyState !== WebSocket.OPEN) {
                scheduleRetry(`WebSocket connection timeout (${CONNECT_TIMEOUT_MS}ms)`);
            }
        }, CONNECT_TIMEOUT_MS);

        ws.onopen = () => {
            if (connectTimeoutId) { clearTimeout(connectTimeoutId); connectTimeoutId = null; }
            hasOpened = true;
            retryCount = 0;  // reset on successful open
            log('WebSocket signaling connected', 'success');

            const query = buildQuery();
            log(`Query: ${JSON.stringify(query)}`, 'info');
            log('Sending request to server...', 'info');
            sendSignalingMessage({
                type: 'request',
                clientId: clientId,
                query: query
            });
        };

        ws.onmessage = (event) => {
            // Binary frames = MJPEG payload; JSON = signaling.
            if (event.data instanceof Blob) {
                handleMjpegFrame(event.data);
                return;
            }
            try {
                const message = JSON.parse(event.data);
                handleSignalingMessage(message);
            } catch (error) {
                log(`Error parsing signaling message: ${error.message}`, 'error');
            }
        };

        ws.onerror = () => scheduleRetry('WebSocket error');

        ws.onclose = () => {
            if (connectTimeoutId) { clearTimeout(connectTimeoutId); connectTimeoutId = null; }
            // Close before open means the connection failed — retry.
            // Close after open is expected: server closes the signaling WS
            // once "starting video" is sent. Just log; do not retry.
            if (!hasOpened && !retryScheduled) {
                scheduleRetry('WebSocket closed before open');
                return;
            }
            // Ignore closes for a socket that has been replaced (intentional stop).
            if (signalingWs !== ws) return;
            log('WebSocket signaling closed', 'info');
        };
    } catch (error) {
        log(`Connection failed: ${error.message}`, 'error');
        updateStatus('Failed', 'disconnected');
        cleanup();
    }
}

/**
 * Disconnect from server
 */
function disconnect() {
    log('Disconnecting...', 'info');
    cleanup();
    updateStatus('Disconnected', 'disconnected');
}

/**
 * Cleanup resources
 */
function cleanup() {
    if (statsInterval) { clearInterval(statsInterval); statsInterval = null; }
    if (connectTimeoutId) { clearTimeout(connectTimeoutId); connectTimeoutId = null; }

    if (dataChannel) {
        dataChannel.close();
        dataChannel = null;
    }

    if (peerConnection) {
        peerConnection.close();
        peerConnection = null;
    }

    if (signalingWs) {
        // Null handlers first so intentional close doesn't trigger a retry.
        try { signalingWs.onopen = signalingWs.onmessage = signalingWs.onerror = signalingWs.onclose = null; } catch (_) {}
        signalingWs.close();
        signalingWs = null;
    }

    // Reset video / MJPEG display
    const videoPlayer = document.getElementById('videoPlayer');
    videoPlayer.srcObject = null;
    const img = document.getElementById('mjpegImage');
    if (img) {
        img.src = '';
        img.style.display = 'none';
    }
    videoPlayer.style.display = 'block';
    if (mjpegLastObjectUrl) { URL.revokeObjectURL(mjpegLastObjectUrl); mjpegLastObjectUrl = null; }
    mjpegImgEl = null;
    mjpegMode = false;
    currentCodec = null;
    mjpegFrames = 0;
    mjpegBytes = 0;
    metadataByRtpTs.clear();

    // Reset retry state so a subsequent user connect starts fresh
    retryCount = 0;
    retryScheduled = false;

    isConnected = false;
    remoteDescriptionSet = false;
    pendingCandidates = [];
    updateUIState();

    // Reset stats
    updateStats('ConnectionState', 'closed');
    updateStats('IceState', 'closed');
    updateStats('DataChannelState', '-');
    updateStats('Codec', '-');
    updateStats('Frames', '-');
}

/**
 * Pause video
 */
function pauseVideo() {
    sendDataChannelMessage('Pause');
}

/**
 * Resume video
 */
function resumeVideo() {
    sendDataChannelMessage('Resume');
}

/**
 * Seek video
 */
function seekVideo() {
    const seekTime = document.getElementById('seekTime').value;
    if (seekTime >= 0) {
        sendDataChannelMessage(`seek_Time${seekTime}`);
    }
}

/**
 * Set playback speed
 */
function setSpeed(speed) {
    sendDataChannelMessage(`FastForward${speed}`);
}

/**
 * Handle stream mode change
 */
document.getElementById('streamMode').addEventListener('change', (e) => {
    const playbackTimeGroup = document.getElementById('playbackTimeGroup');
    if (e.target.value === 'PlayBack') {
        playbackTimeGroup.style.display = 'block';
    } else {
        playbackTimeGroup.style.display = 'none';
    }
});

/**
 * Initialize on page load
 */
window.addEventListener('load', () => {
    log('WebRTC Video Streamer Client initialized (Server-as-Offerer + Trickle ICE)', 'success');
    log(`H.265 WebRTC support: ${H265_SUPPORTED}`, 'info');
    updateStats('H265', H265_SUPPORTED ? 'yes' : 'no');
    log('Configure connection settings and click Connect', 'info');
    updateUIState();
});

/**
 * Cleanup on page unload
 */
window.addEventListener('beforeunload', () => {
    cleanup();
});