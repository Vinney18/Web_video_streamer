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

// WebRTC configuration
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
 * Build query string from form inputs
 */
function buildQueryString() {
    const cameraId = document.getElementById('cameraId').value;
    const mode = document.getElementById('streamMode').value;
    const streamType = document.getElementById('streamType').value;
    const startTime = document.getElementById('startTime').value;

    let query = `cameraId~~${cameraId}&&mode~~${mode}&&streamType~~${streamType}`;

    if (mode === 'PlayBack' && startTime) {
        query += `&&startTime~~${startTime}`;
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
        case 'offer':
            handleServerOffer(message);
            break;
        case 'candidate':
            handleRemoteCandidate(message);
            break;
        case 'error':
            log(`Server error: ${message.message}`, 'error');
            break;
        default:
            log(`Unknown signaling message type: ${message.type}`, 'error');
    }
}

/**
 * Handle SDP offer from server (server-as-offerer pattern)
 */
async function handleServerOffer(message) {
    try {
        log('Received offer from server, creating peer connection...', 'info');
        console.log('=== SDP OFFER FROM SERVER ===\n' + message.sdp);

        // Create peer connection if not already created
        if (!peerConnection) {
            if (!await createPeerConnection()) {
                updateStatus('Failed', 'disconnected');
                return;
            }
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
async function createPeerConnection() {
    try {
        log('Creating peer connection...', 'info');

        peerConnection = new RTCPeerConnection(rtcConfig);

        // Connection state changes
        peerConnection.onconnectionstatechange = () => {
            const state = peerConnection.connectionState;
            log(`Connection state: ${state}`, 'info');
            updateStats('ConnectionState', state);

            if (state === 'connected') {
                updateStatus('Connected', 'connected');
                log('WebRTC connection established!', 'success');
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

                // Poll WebRTC stats to check if bytes are arriving
                const statsInterval = setInterval(async () => {
                    if (!peerConnection) { clearInterval(statsInterval); return; }
                    const stats = await peerConnection.getStats();
                    stats.forEach(report => {
                        if (report.type === 'inbound-rtp' && report.kind === 'video') {
                            console.log(`[VIDEO STATS] bytesReceived=${report.bytesReceived} packetsReceived=${report.packetsReceived} framesDecoded=${report.framesDecoded} framesDropped=${report.framesDropped}`);
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
    try {
        serverUrl = document.getElementById('serverUrl').value.trim();
        clientId = generateClientId();

        // Convert HTTP URL to WebSocket URL
        let wsUrl = serverUrl.replace(/^http/, 'ws');
        if (!wsUrl.startsWith('ws://') && !wsUrl.startsWith('wss://')) {
            wsUrl = 'ws://' + wsUrl;
        }

        log(`Connecting to ${wsUrl}...`, 'info');
        log(`Client ID: ${clientId}`, 'info');

        updateStatus('Connecting...', 'connecting');

        // Create WebSocket connection for signaling
        signalingWs = new WebSocket(wsUrl);

        signalingWs.onopen = () => {
            log('WebSocket signaling connected', 'success');

            // Build query string
            const queryString = buildQueryString();
            log(`Query: ${queryString}`, 'info');

            // Send request to server (server will create offer)
            log('Sending request to server...', 'info');
            sendSignalingMessage({
                type: 'request',
                clientId: clientId,
                query: queryString
            });

            // Server will respond with an SDP offer, handled by handleServerOffer()
        };

        signalingWs.onmessage = (event) => {
            try {
                const message = JSON.parse(event.data);
                handleSignalingMessage(message);
            } catch (error) {
                log(`Error parsing signaling message: ${error.message}`, 'error');
            }
        };

        signalingWs.onerror = (error) => {
            log(`WebSocket error: ${error}`, 'error');
            updateStatus('Failed', 'disconnected');
        };

        signalingWs.onclose = () => {
            log('WebSocket signaling closed', 'info');
            if (isConnected) {
                updateStatus('Disconnected', 'disconnected');
            }
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
    if (dataChannel) {
        dataChannel.close();
        dataChannel = null;
    }

    if (peerConnection) {
        peerConnection.close();
        peerConnection = null;
    }

    if (signalingWs) {
        signalingWs.close();
        signalingWs = null;
    }

    const videoPlayer = document.getElementById('videoPlayer');
    videoPlayer.srcObject = null;

    isConnected = false;
    remoteDescriptionSet = false;
    pendingCandidates = [];
    updateUIState();

    // Reset stats
    updateStats('ConnectionState', 'closed');
    updateStats('IceState', 'closed');
    updateStats('DataChannelState', '-');
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
    log('Configure connection settings and click Connect', 'info');
    updateUIState();
});

/**
 * Cleanup on page unload
 */
window.addEventListener('beforeunload', () => {
    cleanup();
});
