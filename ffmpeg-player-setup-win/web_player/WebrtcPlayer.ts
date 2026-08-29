export class ServerConfig {
  streamerIp: string;
  streamerPort: number;
  playerServerIp: string;
  playerServerPort: number;
  useSecureConnection?: boolean;
}

class StreamConfig {
  cameraId: number;
  mode: string;
  streamType: number;
  connectionMode: string;

  constructor(
    cameraId: number,
    mode: string,
    streamType: number,
    connectionMode: string
  ) {
    this.cameraId = cameraId;
    this.mode = mode;
    this.streamType = streamType;
    this.connectionMode = connectionMode;
  }
}

export class LiveStreamConfig extends StreamConfig {
  constructor(cameraId: number, streamType: number, connectionMode: string) {
    super(cameraId, 'Live', streamType, connectionMode);
  }
}

class StreamPacketInfo {
  lastDecoded: number;
  lastReceived: number;
  zeroDecodeCount: number;

  constructor() {
    this.lastDecoded = 0;
    this.lastReceived = 0;
    this.zeroDecodeCount = 0;
  }
}

export class AnalyticStreamConfig extends LiveStreamConfig {
  analyticType: string;
  vaServerId: string;
  vaServerPipeId: string;

  constructor(
    cameraId: number,
    streamType: number,
    connectionMode: string,
    analyticType: string,
    vaServerId: string = '',
    vaServerPipeId: string = ''
  ) {
    super(cameraId, streamType, connectionMode);
    this.analyticType = analyticType;
    this.vaServerId = vaServerId;
    this.vaServerPipeId = vaServerPipeId;
  }
}

export class PlaybackStreamConfig extends StreamConfig {
  startTime: number;
  endTime: number;
  playbackSpeed: number;
  syncGroup: string;

  constructor(
    cameraId: number,
    streamType: number,
    connectionMode: string,
    startTime: number,
    endTime: number,
    playbackSpeed: number = 1,
    syncGroup?: string
  ) {
    super(cameraId, 'PlayBack', streamType, connectionMode);
    this.startTime = startTime;
    this.endTime = endTime;
    this.playbackSpeed = playbackSpeed;
    this.syncGroup = syncGroup || crypto.randomUUID();
  }
}

export class I2vWebRtcPlayer {
  clVersion: string = '7.3';
  elId: any;
  serverConfig: ServerConfig;
  streamConfig: LiveStreamConfig | PlaybackStreamConfig | AnalyticStreamConfig;

  // WebRTC state
  w: WebSocket;
  v: HTMLVideoElement;
  pc: RTCPeerConnection;
  dc: RTCDataChannel;

  remoteDescSet: boolean = false;
  pendingCandidates: RTCIceCandidate[] = [];

  doesStopRequested: boolean = false;

  consoleDetailedLog: boolean = false;

  playrecursivetimeout: any;
  statsInterval: any;
  svVersion: any;

  streamPacketInfo: StreamPacketInfo;

  // MJPEG state
  codec: string;
  imgEl: HTMLImageElement;
  lastObjectUrl: string = null;
  mjpegStats: { framesReceived: number; bytesReceived: number; lastFrameMs: number };
  mjpegStaleInterval: any;

  private static readonly rtcConfig: RTCConfiguration = {
    iceServers: [
      { urls: 'stun:stun.l.google.com:19302' },
      { urls: 'stun:stun1.l.google.com:19302' },
    ],
  };

  // Detect whether this browser's WebRTC stack will negotiate H.265/HEVC.
  // The receiver capabilities are authoritative for the RTP video path this
  // player uses; if H265 isn't listed, SDP will never carry it and the server
  // should transcode to MJPEG instead.
  private static detectH265WebRTC(): boolean {
    try {
      if (window.RTCRtpReceiver && RTCRtpReceiver.getCapabilities) {
        const caps = RTCRtpReceiver.getCapabilities('video');
        return !!(
          caps &&
          caps.codecs &&
          caps.codecs.some(c => /h265|hevc|hev1|hvc1/i.test(c.mimeType))
        );
      }
    } catch (_) {}
    return false;
  }

  private static readonly H265_SUPPORTED: boolean =
    I2vWebRtcPlayer.detectH265WebRTC();

  constructor(
    elId: any,
    serverConfig: ServerConfig,
    streamConfig: AnalyticStreamConfig,
    consoleDetailedLog?: boolean
  );
  constructor(
    elId: any,
    serverConfig: ServerConfig,
    streamConfig: LiveStreamConfig,
    consoleDetailedLog?: boolean
  );
  constructor(
    elId: any,
    serverConfig: ServerConfig,
    streamConfig: PlaybackStreamConfig,
    consoleDetailedLog?: boolean
  );
  constructor(
    elId: any,
    serverConfig: ServerConfig,
    streamConfig:
      | LiveStreamConfig
      | PlaybackStreamConfig
      | AnalyticStreamConfig,
    consoleDetailedLog: boolean = false
  ) {
    this.elId = elId;
    this.serverConfig = serverConfig;
    this.streamConfig = streamConfig;
    this.consoleDetailedLog = consoleDetailedLog;

    // Validate connectionMode
    if (this.streamConfig.connectionMode) {
      this.streamConfig.connectionMode =
        this.streamConfig.connectionMode.toLowerCase();
      if (
        this.streamConfig.connectionMode != 'tcp' &&
        this.streamConfig.connectionMode != 'udp'
      ) {
        this.streamConfig.connectionMode = '';
      }
    } else {
      this.streamConfig.connectionMode = '';
    }
  }

  private buildQuery(): object {
    return {
      ...this.streamConfig,
      playerServerIp: this.serverConfig.playerServerIp,
      playerServerPort: this.serverConfig.playerServerPort,
      clVersion: this.clVersion,
      h265Supported: I2vWebRtcPlayer.H265_SUPPORTED,
    };
  }

  stop() {
    try {
      this.removeErrorMessage();
      this.doesStopRequested = true;
      this.stopStatsLogging();
      if (this.playrecursivetimeout) {
        clearTimeout(this.playrecursivetimeout);
        this.playrecursivetimeout = null;
      }
      if (this.dc) {
        this.dc.close();
        this.dc = null;
      }
      if (this.pc) {
        this.pc.close();
        this.pc = null;
      }
      if (this.w) {
        this.w.close();
        this.w = null;
      }
      this.remoteDescSet = false;
      this.pendingCandidates = [];

      let v = document.getElementById(`${this.elId}_video`) as HTMLVideoElement;
      if (v) {
        v.srcObject = null;
        v.parentNode.removeChild(v);
      }
      delete this.v;
      this.cleanupMjpeg();
    } catch (ex) {
      console.error('wClient: Error in Stop Function');
    }
  }

  private cleanup() {
    this.stopStatsLogging();
    if (this.dc) {
      this.dc.close();
      this.dc = null;
    }
    if (this.pc) {
      this.pc.close();
      this.pc = null;
    }
    if (this.w) {
      this.w.close();
      this.w = null;
    }
    this.remoteDescSet = false;
    this.pendingCandidates = [];

    delete this.v;
    let v = document.getElementById(`${this.elId}_video`) as HTMLVideoElement;
    if (v) {
      v.srcObject = null;
      v.parentNode.removeChild(v);
    }
    this.cleanupMjpeg();
  }

  private retryConnection() {
    if (this.doesStopRequested) return;
    this.cleanup();
    this.playrecursivetimeout = setTimeout(() => {
      if (!this.doesStopRequested) {
        this.play();
      }
    }, 3000);
  }

  play() {
    var protocolType: string = 'ws';

    if (this.serverConfig.useSecureConnection) {
      protocolType = 'wss';
    }

    this.removeErrorMessage();
    this.DisplayStatus('Trying to Connect...');
    this.doesStopRequested = false;

    this.w = new WebSocket(
      `${protocolType}://${this.serverConfig.streamerIp}:${this.serverConfig.streamerPort}`
    );
    this.w.binaryType = 'blob';

    this.w.addEventListener('open', () => {
      if (this.consoleDetailedLog)
        console.log('WebSocket connected, sending request');
      this.w.send(
        JSON.stringify({
          type: 'request',
          query: this.buildQuery(),
        })
      );
    });

    this.w.addEventListener('close', () => {
      // if (this.doesStopRequested) {
      //     console.log('socket closed');
      //     this.removeErrorMessage();
      // } else {
      //     console.log('socket closed unexpectedly, retrying...');
      //     this.showErrorMessage("Player Not Connected...");
      //     this.retryConnection();
      // }
    });

    this.w.addEventListener('error', () => {
      if (this.consoleDetailedLog) console.error('WebSocket error');
    });

    this.w.addEventListener('message', e => {
      if (e.data instanceof Blob) {
        this.handleMjpegFrame(e.data);
        return;
      }
      try {
        const msg = JSON.parse(e.data);
        this.handleSignaling(msg);
      } catch (err) {
        console.error('Signaling parse error:', err);
      }
    });
  }
  


  
  private handleSignaling(msg: any) {
    switch (msg.type) {
      case 'codec':
        this.handleCodec(msg.codec);
        break;
      case 'offer':
        this.handleOffer(msg);
        break;
      case 'candidate':
        this.handleCandidate(msg);
        break;
      case 'mjpegStream':
        this.removeErrorMessage();
        break;
      case 'mjpegInfo':
        if (this.consoleDetailedLog) console.log('mjpeg-info:', msg.message);
        break;
      case 'error':
        console.error('Server error:', msg.message);
        this.DisplayStatus(msg.message);
        setTimeout(() => {
          this.w.send(
            JSON.stringify({
              type: 'request',
              query: this.buildQuery(),
            })
          );
        }, 4000);
        break;
      case 'status':
        this.DisplayStatus(msg.message);
        break;
    }
  }

  private async handleOffer(msg: any) {
    if (this.codec === 'mjpeg') return;
    try {
      const cfg: RTCConfiguration =
        msg.iceServers && Array.isArray(msg.iceServers) && msg.iceServers.length > 0
          ? { ...I2vWebRtcPlayer.rtcConfig, iceServers: msg.iceServers }
          : I2vWebRtcPlayer.rtcConfig;
      this.pc = new RTCPeerConnection(cfg);

      this.pc.onconnectionstatechange = () => {
        const state = this.pc.connectionState;
        if (this.consoleDetailedLog) console.log('Connection state:', state);
        if (state === 'connected') {
          // this.removeErrorMessage();
          
          this.startStatsLogging();
        } else if (state === 'failed' || state === 'closed') {
          this.stopStatsLogging();
          if (!this.doesStopRequested) {
            this.DisplayStatus('Connection Lost...');
            this.retryConnection();
          }
        }
      };

      this.pc.oniceconnectionstatechange = () => {
        if (this.consoleDetailedLog)
          console.log('ICE state:', this.pc.iceConnectionState);
      };

      this.pc.onicegatheringstatechange = () => {
        if (this.consoleDetailedLog)
          console.log('ICE gathering state:', this.pc.iceGatheringState);
      };

      this.pc.onsignalingstatechange = () => {
        if (this.consoleDetailedLog)
          console.log('Signaling state:', this.pc.signalingState);
      };

      this.pc.onicecandidate = event => {
        if (this.consoleDetailedLog)
          console.log(
            'ICE candidate:',
            event.candidate ? event.candidate.candidate : 'gathering complete'
          );
        if (event.candidate && this.w && this.w.readyState === WebSocket.OPEN) {
          this.w.send(
            JSON.stringify({
              type: 'candidate',
              candidate: event.candidate.candidate,
              sdpMid: event.candidate.sdpMid,
              sdpMLineIndex: event.candidate.sdpMLineIndex,
            })
          );
        }
      };

      this.pc.ontrack = event => {
        if (this.consoleDetailedLog)
          console.log(
            'Got track:',
            event.track.kind,
            'readyState:',
            event.track.readyState
          );
        if (event.track.kind === 'video') {
          this.setupVideoElement();
          if (event.streams && event.streams[0]) {
            this.v.srcObject = event.streams[0];
          } else {
            this.v.srcObject = new MediaStream([event.track]);
          }
          this.v.addEventListener(
            'playing',
            () => {
              this.removeErrorMessage();
            },
            { once: true }
          ); // 'once' auto-removes listener after first fire
        }
      };

      this.pc.ondatachannel = event => {
        this.dc = event.channel;
        if (this.consoleDetailedLog)
          console.log('Data channel received:', this.dc.label);
        this.dc.onopen = () => {
          if (this.consoleDetailedLog) console.log('Data channel open');
        };
        this.dc.onclose = () => {
          if (this.consoleDetailedLog) console.log('Data channel closed');
        };
        this.dc.onmessage = e => {
          if (this.consoleDetailedLog)
            console.log('Data channel message:', e.data);
          this.handleDataChannelMessage(e.data);
        };
      };

      await this.pc.setRemoteDescription({ type: 'offer', sdp: msg.sdp });
      this.remoteDescSet = true;

      for (const c of this.pendingCandidates) {
        await this.pc.addIceCandidate(c);
      }
      this.pendingCandidates = [];

      const answer = await this.pc.createAnswer();
      await this.pc.setLocalDescription(answer);

      if (this.w && this.w.readyState === WebSocket.OPEN) {
        this.w.send(
          JSON.stringify({
            type: 'answer',
            sdp: answer.sdp,
            sdpType: answer.type,
          })
        );
      }
    } catch (e) {
      console.error('Offer handling error:', e);
      this.DisplayStatus('Connection Failed...');
      this.retryConnection();
    }
  }

  private async handleCandidate(msg: any) {
    try {
      if (!this.pc || !msg.candidate) return;
      const candidate = new RTCIceCandidate({
        candidate: msg.candidate,
        sdpMid: msg.sdpMid || '0',
        sdpMLineIndex: msg.sdpMLineIndex || 0,
      });
      if (this.remoteDescSet) {
        await this.pc.addIceCandidate(candidate);
      } else {
        this.pendingCandidates.push(candidate);
      }
    } catch (e) {
      console.error('ICE candidate error:', e);
    }
  }

  private setupVideoElement() {
    let existing = document.getElementById(
      `${this.elId}_video`
    ) as HTMLVideoElement;
    if (existing) {
      this.v = existing;
      return;
    }

    this.v = document.createElement('video');
    var div = document.getElementById(this.elId);
    div.style.background = 'black';
    div.appendChild(this.v);
    this.v.id = `${this.elId}_video`;
    this.v.style.height = '100%';
    this.v.style.width = '100%';
    this.v.style.display = 'inline';
    this.v.autoplay = true;
    this.v.muted = true;
    (this.v as any).playsInline = true;
  }

  private setupImageElement() {
    let existing = document.getElementById(
      `${this.elId}_img`
    ) as HTMLImageElement;
    if (existing) {
      this.imgEl = existing;
      return;
    }

    this.imgEl = document.createElement('img');
    var div = document.getElementById(this.elId);
    div.style.background = 'black';
    div.appendChild(this.imgEl);
    this.imgEl.id = `${this.elId}_img`;
    this.imgEl.style.height = '100%';
    this.imgEl.style.width = '100%';
    this.imgEl.style.display = 'inline';
    (this.imgEl.style as any).objectFit = 'contain';
  }

  private handleCodec(codec: string) {
    if (this.consoleDetailedLog) console.log('Codec:', codec);
    this.codec = codec;
    if (codec !== 'mjpeg') return;

    let v = document.getElementById(`${this.elId}_video`) as HTMLVideoElement;
    if (v) {
      v.srcObject = null;
      v.parentNode.removeChild(v);
    }
    delete this.v;

    this.setupImageElement();
    this.mjpegStats = {
      framesReceived: 0,
      bytesReceived: 0,
      lastFrameMs: Date.now(),
    };
    if (this.mjpegStaleInterval) clearInterval(this.mjpegStaleInterval);
    this.mjpegStaleInterval = setInterval(() => {
      if (Date.now() - this.mjpegStats.lastFrameMs > 6000) {
        if (this.consoleDetailedLog)
          console.log('MJPEG stale (>6s), reconnecting...');
        this.DisplayStatus('Connection Lost...');
        this.retryConnection();
      }
    }, 2000);
  }

  private handleMjpegFrame(blob: Blob) {
    if (!this.imgEl) return;
    const url = URL.createObjectURL(blob);
    const prev = this.lastObjectUrl;
    this.imgEl.onload = () => {
      if (prev) URL.revokeObjectURL(prev);
    };
    this.imgEl.src = url;
    this.lastObjectUrl = url;
    this.mjpegStats.framesReceived++;
    this.mjpegStats.bytesReceived += blob.size;
    this.mjpegStats.lastFrameMs = Date.now();
    this.removeErrorMessage();
  }

  private cleanupMjpeg() {
    if (this.mjpegStaleInterval) {
      clearInterval(this.mjpegStaleInterval);
      this.mjpegStaleInterval = null;
    }
    if (this.lastObjectUrl) {
      URL.revokeObjectURL(this.lastObjectUrl);
      this.lastObjectUrl = null;
    }
    let img = document.getElementById(
      `${this.elId}_img`
    ) as HTMLImageElement;
    if (img) {
      img.src = '';
      img.parentNode.removeChild(img);
    }
    this.imgEl = null;
    this.codec = null;
  }

  private handleDataChannelMessage(data: string) {
    const msg = JSON.parse(data);
    if (msg.type == 'Codec_Changed') {
      this.stop();
      this.play();
      return;
    }
    if (msg.type == 'Playback_Finished') {
      this.stop();
      if (this.streamConfig instanceof PlaybackStreamConfig)
        this.streamConfig.startTime = Number(msg.time);
      console.log('Playback finished new time:', msg.time);
      console.log('cam id', this.streamConfig.cameraId);

      this.play();
      return;
    }

    if (data.startsWith('--version')) {
      this.svVersion = data.substring(10);
      console.log('Client Version: ' + this.clVersion);
      console.log('Server Version: ' + this.svVersion);
      return;
    } else if (data.startsWith('--servStatus')) {
      console.log(data.substring(13));
      return;
    }

    switch (data) {
      case 'Playback_Finished':
        console.log('Playback_Finished');
        this.stop();
        return;
      case 'Video_Started':
        console.log('Video_Started');
        this.removeErrorMessage();
        return;
      case 'unable_to_play':
        console.log('unable_to_play');
        this.stop();
        return;
      case 'EmptyUrl':
        var errMsg =
          this.streamConfig.mode == 'Live'
            ? 'Stream not Found'
            : 'Recording not Found';
        this.DisplayStatus(errMsg);
        return;

      case 'License Expired':
        this.DisplayStatus('License Expired/Invalid');
        return;
      case 'Some problem occured':
        this.DisplayStatus('Some Problem Occured');
        return;
      case 'Stopped':
        console.log('Stream stopped');
        if (!this.doesStopRequested) {
          this.retryConnection();
        }
        return;
      default:
        // Handle Playback_Finished JSON format
        if (data.startsWith('{')) {
          try {
            const json = JSON.parse(data);
            if (json.event === 'Playback_Finished') {
              console.log('Playback_Finished, nextTime:', json.nextTime);
              this.stop();
            }
          } catch (e) {
            console.log('DC message:', data);
          }
        } else {
          console.log('DC message:', data);
        }
    }
  }

  private startStatsLogging() {
    this.stopStatsLogging();
    this.streamPacketInfo = new StreamPacketInfo();
    this.statsInterval = setInterval(() => {
      this.logDetailedStats();
    }, 3000);
  }

  private stopStatsLogging() {
    this.streamPacketInfo = null;
    if (this.statsInterval) {
      clearInterval(this.statsInterval);
      this.statsInterval = null;
    }
  }

  private logDetailedStats() {
    if (!this.pc) return;
    this.pc.getStats().then(stats => {
      stats.forEach(report => {
        if (report.type === 'inbound-rtp' && report.kind === 'video') {
          if (this.logDetailedStats) {
            console.log(
              `[Stats] packetsReceived: ${report.packetsReceived}, ` +
                `packetsLost: ${report.packetsLost}, ` +
                `framesDecoded: ${report.framesDecoded}, ` +
                `framesDropped: ${report.framesDropped}, ` +
                `framesReceived: ${report.framesReceived}, ` +
                `bytesReceived: ${report.bytesReceived}`
            );
          }

          // const newDecoded =
          //   report.framesDecoded - this.streamPacketInfo.lastDecoded;
          // const newReceived =
          //   report.framesReceived - this.streamPacketInfo.lastReceived;
          // this.streamPacketInfo.lastDecoded = report.framesDecoded;
          // this.streamPacketInfo.lastReceived = report.framesReceived;

          // // const needsReconnect =
          // //   (newReceived > 0 && newDecoded === 0) || // codec change: packets arrive but can't decode
          // //   (newReceived === 0 && newDecoded === 0);
            
          //   const needsReconnect =newReceived === 0;// no packets at all: connection dead

          // if (needsReconnect) {
          //   this.streamPacketInfo.zeroDecodeCount++;
            
          //   if (this.streamPacketInfo.zeroDecodeCount >= 3) {
              
          //     this.streamPacketInfo.zeroDecodeCount = 0;
          //     this.stop();
          //     this.play();
          //   }
          // } else {
          //   this.streamPacketInfo.zeroDecodeCount = 0;
          // }
        }
      });
    });
  }

  Pause() {
    if (this.dc && this.dc.readyState === 'open') {
      this.dc.send(JSON.stringify({ Pause: true }));
    }
  }

  SeekVideo(starttime: string) {
    if (this.dc && this.dc.readyState === 'open') {
      this.dc.send(JSON.stringify({ seek_Time: parseInt(starttime) }));
    }
  }

  FastForward(factor: number) {
    if (this.streamConfig instanceof PlaybackStreamConfig) {
      this.streamConfig.playbackSpeed = factor;
    }
    if (this.dc && this.dc.readyState === 'open') {
      this.dc.send(JSON.stringify({ FastForward: factor }));
    }
  }

  Version() {
    if (this.svVersion) {
      console.log('Client Version: ' + this.clVersion);
      console.log('Server Version: ' + this.svVersion);
    } else if (this.dc && this.dc.readyState === 'open') {
      this.dc.send('Version');
    } else {
      console.log('Please Connect to server via Play Live or Playback');
    }
  }

  servStatus() {
    if (this.dc && this.dc.readyState === 'open') {
      this.dc.send('Server Status');
    }
  }

  Close() {
    this.stop();
  }

  getBase64SnapshotUrl(): string {
    var dataURI = '';
    try {
      if (this.codec === 'mjpeg' && this.imgEl && this.imgEl.naturalWidth > 0) {
        var canvas = document.createElement('canvas');
        canvas.width = this.imgEl.naturalWidth;
        canvas.height = this.imgEl.naturalHeight;
        var ctx = canvas.getContext('2d');
        ctx.drawImage(this.imgEl, 0, 0, canvas.width, canvas.height);
        dataURI = canvas.toDataURL('image/png');
      } else if (this.v && this.v.videoWidth > 0) {
        var canvas = document.createElement('canvas');
        canvas.width = this.v.videoWidth;
        canvas.height = this.v.videoHeight;
        var ctx = canvas.getContext('2d');
        ctx.drawImage(this.v, 0, 0, canvas.width, canvas.height);
        dataURI = canvas.toDataURL('image/png');
      }
    } catch (ex) {
      console.log(ex);
    }
    return dataURI;
  }

  // showErrorMessage(message: string) {
  //   this.isErrorMessageVisible = true;
  //   var spanElement = document.getElementById("errorMessage" + this.elId);
  //   if (!spanElement) {
  //     var span = document.createElement("span");
  //     span.innerHTML = message + "...";
  //     span.classList.add("errorMessage");
  //     span.style.color = "red";
  //     span.style.position = "absolute";
  //     span.style.fontSize = "25px";
  //     span.style.top = "50%";
  //     span.style.height = "30px";
  //     span.style.marginTop = "-15px";
  //     span.style.width = "100%";
  //     span.style.textAlign = "center";
  //     span.style.fontWeight = "bold";
  //     span.id = "errorMessage" + this.elId;
  //     var element = document.getElementById(this.elId);
  //     if (element) {
  //       element.style.background = "black";
  //       element.style.position = "relative";
  //       element.appendChild(span);
  //     }
  //   } else {
  //     spanElement.innerHTML = message + "...";
  //   }
  // }

  DisplayStatus(message: string) {
    const spanId = 'errorMessage' + this.elId;
    let spanElement = document.getElementById(spanId);

    const element = document.getElementById(this.elId);
    if (!element) return;

    element.style.background = 'black';
    element.style.position = 'relative'; // anchor for absolute child

    if (!spanElement) {
      const span = document.createElement('span');
      span.innerHTML = message + '...';
      span.classList.add('errorMessage');
      span.id = spanId;

      // Absolute overlay — works even when video element is present
      span.style.position = 'absolute';
      span.style.top = '50%';
      span.style.left = '50%';
      span.style.transform = 'translate(-50%, -50%)';
      span.style.color = 'red';
      span.style.fontSize = '25px';
      span.style.fontWeight = 'bold';
      span.style.textAlign = 'center';
      span.style.width = '80%';
      span.style.zIndex = '10'; // ensure it overlays the video
      span.style.pointerEvents = 'none';

      element.appendChild(span);
    } else {
      spanElement.innerHTML = message + '...';
    }
  }

  removeErrorMessage() {
    try {
      let spanElement = document.getElementById('errorMessage' + this.elId);
      if (spanElement) {
        let element = document.getElementById(this.elId);
        if (element) {
          element.removeChild(spanElement);
        }
      }
    } catch (ex) {
      console.error('Error removing error message: ', ex);
    }
  }
}
