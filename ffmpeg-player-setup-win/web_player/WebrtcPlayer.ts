export interface ServerConfig {
  wPlayerIp: string;
  wPlayerPort: string;
  wServerIp: string;
  wServerPort?: any;
  useSecureConnection?: boolean;
}

export interface StreamConfig {
  cameraId: number;
  mode: string;
  streamType: number;
  startTime: number;
  endTime: number;
  analyticType: string;
  connectionMode: string;
  playbackSpeed: number;
  vaServerId?: string;
  vaServerPipeId?: string;
}

export class I2vWebRtcSdk {
  clVersion: string = "7.3";
  wPlayerIp: string = "localhost";
  wServerIp: string;
  wServerPort: any = 8890;
  player: I2vWebRtcPlayer;
  vaServerId: string;
  vaServerPipeId: string;

  constructor(
    _wPlayerIp: string,
    _wServerIp: string,
    _wServerPort?: any,
    useSecureConnection?: boolean,
  ) {
    this.wPlayerIp = _wPlayerIp;
    this.wServerIp = _wServerIp;
    this.wServerPort = _wServerPort || this.wServerPort;
  }

  GetLivePlayer(
    elId: any,
    cameraId: number,
    streamtype: number,
    analyticType: string,
    connectionmode: string,
    vaServerId?: string,
    vaServerPipeId?: string,
  ) {
    this.player = new I2vWebRtcPlayer(
      elId,
      cameraId,
      "Live",
      streamtype,
      0,
      0,
      analyticType,
      connectionmode,
      this.clVersion,
      1,
      vaServerId,
      vaServerPipeId,
    );
    this.player.wPlayerIp = this.wPlayerIp;
    this.player.wServerIp = this.wServerIp;
    this.player.wServerPort = this.wServerPort;

    return this.player;
  }

  GetPlaybackPlayer(
    elId: any,
    cameraId: number,
    startTime: number,
    endTime: number,
    _playbackviaapache: string,
    playbackSpeed: number = 1,
    connectionMode: string = "tcp",
  ) {
    this.player = new I2vWebRtcPlayer(
      elId,
      cameraId,
      "PlayBack",
      0,
      startTime,
      endTime,
      "",
      connectionMode,
      this.clVersion,
      playbackSpeed,
    );
    this.player.wPlayerIp = this.wPlayerIp;
    this.player.wServerIp = this.wServerIp;
    this.player.wServerPort = this.wServerPort;

    return this.player;
  }

  SeekVideo(startTime: any) {
    if (this.player && this.player.mode != "Live") {
      this.player.SeekVideo(startTime);
    }
  }

  Pause() {
    if (this.player && this.player.mode != "Live") {
      this.player.Pause();
    }
  }

  FastForward(factor: number) {
    if (this.player && this.player.mode != "Live") {
      this.player.FastForward(factor);
    }
  }
}

export class I2vWebRtcPlayer {
  wPlayerIp: string;
  elId: any;
  cameraId: number;
  mode: string;
  streamType: number;
  startTime: number;
  endTime: number;
  analyticType: string;
  connectionMode: string = "tcp";
  wServerIp: string;
  wServerPort: any = 8890;
  clVersion: string;

  useSecureConnection: boolean = false;

  w: WebSocket;
  v: HTMLVideoElement;
  pc: RTCPeerConnection;
  dc: RTCDataChannel;
  clientId: string;

  remoteDescSet: boolean = false;
  pendingCandidates: RTCIceCandidate[] = [];

  errorCallback: any;
  retryingCallback: any;

  isVisible: boolean;
  isPlayerSet: boolean;
  doesStopRequested: boolean = false;
  isErrorMessageVisible: boolean = false;

  playrecursivetimeout: any;
  status: any;
  svVersion: any;

  playbackSpeed: number = 1;
  vaServerId: string;
  vaServerPipeId: string;

  private static readonly rtcConfig: RTCConfiguration = {
    iceServers: [
      { urls: "stun:stun.l.google.com:19302" },
      { urls: "stun:stun1.l.google.com:19302" },
    ],
  };

  constructor(
    elId: any,
    cameraId: number,
    mode: string,
    streamtype: number,
    startTime: number,
    endTime: number,
    _analyticType: string,
    _connectionmode: string,
    _clVersion: string,
    playbackSpeed: number = 1,
    vaServerId: string = "",
    vaServerPipeId: string = "",
  ) {
    this.elId = elId;
    this.cameraId = cameraId;
    this.mode = mode;
    this.streamType = streamtype;
    this.startTime = startTime;
    this.endTime = endTime;
    this.analyticType = _analyticType;
    this.clVersion = _clVersion;

    if (playbackSpeed < 0.5) playbackSpeed = 0.5;
    if (playbackSpeed > 5) playbackSpeed = 5;
    playbackSpeed = Math.round(playbackSpeed * 2) / 2;
    console.log("playbackSpeed Allowed: 0.5 to 5, with 0.5 step, 1 is default");
    console.log("playbackSpeed: " + playbackSpeed);
    this.playbackSpeed = playbackSpeed;
    this.vaServerId = vaServerId;
    this.vaServerPipeId = vaServerPipeId;

    if (!this.analyticType) this.analyticType = "";

    if (!this.connectionMode) {
      this.connectionMode = "";
    } else {
      this.connectionMode = this.connectionMode.toLowerCase();
      if (this.connectionMode != "tcp" && this.connectionMode != "udp") {
        this.connectionMode = "";
      }
    }
  }

  setErrorCallback(errorCallback: any) {
    this.errorCallback = errorCallback;
  }

  setRetryingCallback(retryingCallback: any) {
    this.retryingCallback = retryingCallback;
  }

  private generateClientId(): string {
    return (
      "webrtc_" + Date.now() + "_" + Math.random().toString(36).substr(2, 9)
    );
  }

  private buildQuery(): object {
    return {
      cameraId: this.cameraId,
      mode: this.mode,
      streamType: this.streamType,
      startTime: this.startTime,
      endTime: this.endTime,
      analyticType: this.analyticType,
      connectionMode: this.connectionMode,
      wServerIp: this.wServerIp,
      wServerPort: this.wServerPort,
      clVersion: this.clVersion,
      playbackSpeed: this.playbackSpeed,
      vaServerId: this.vaServerId,
      vaServerPipeId: this.vaServerPipeId,
    };
  }

  stop() {
    try {
      this.removeErrorMessage();
      this.doesStopRequested = true;
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
    } catch (ex) {
      console.error("wClient: Error in Stop Function");
    }
  }

  private cleanup() {
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
    this.isPlayerSet = false;
    this.isVisible = false;
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
    var protocolType: string = "ws";
    var port: number = 8181;

    if (this.useSecureConnection) {
      protocolType = "wss";
      port = 8182;
    }

    this.removeErrorMessage();
    this.showErrorMessage("Trying to Connect...");
    this.doesStopRequested = false;
    this.clientId = this.generateClientId();

    this.w = new WebSocket(`${protocolType}://${this.wPlayerIp}:${port}`);

    this.w.addEventListener("open", () => {
      console.log("WebSocket connected, sending request");
      this.w.send(
        JSON.stringify({
          type: "request",
          clientId: this.clientId,
          query: this.buildQuery(),
        }),
      );
    });

    this.w.addEventListener("close", () => {
      // if (this.doesStopRequested) {
      //     console.log('socket closed');
      //     this.removeErrorMessage();
      // } else {
      //     console.log('socket closed unexpectedly, retrying...');
      //     this.showErrorMessage("Player Not Connected...");
      //     this.retryConnection();
      // }
    });

    this.w.addEventListener("error", () => {
      console.error("WebSocket error");
    });

    this.w.addEventListener("message", (e) => {
      try {
        const msg = JSON.parse(e.data);
        this.handleSignaling(msg);
      } catch (err) {
        console.error("Signaling parse error:", err);
      }
    });
  }

  private handleSignaling(msg: any) {
    switch (msg.type) {
      case "offer":
        this.handleOffer(msg);
        break;
      case "candidate":
        this.handleCandidate(msg);
        break;
      case "error":
        console.error("Server error:", msg.message);
        if (this.errorCallback) {
          this.errorCallback(msg.message);
        }
        this.showErrorMessage(msg.message || "Server Error");
        break;
    }
  }

  private async handleOffer(msg: any) {
    try {
      this.pc = new RTCPeerConnection(I2vWebRtcPlayer.rtcConfig);

      this.pc.onconnectionstatechange = () => {
        const state = this.pc.connectionState;
        console.log("Connection state:", state);
        if (state === "connected") {
          this.removeErrorMessage();
        } else if (state === "failed" || state === "closed") {
          if (!this.doesStopRequested) {
            this.showErrorMessage("Connection Lost...");
            this.retryConnection();
          }
        }
      };

      this.pc.oniceconnectionstatechange = () => {
        console.log("ICE state:", this.pc.iceConnectionState);
      };

      this.pc.onicecandidate = (event) => {
        if (event.candidate && this.w && this.w.readyState === WebSocket.OPEN) {
          this.w.send(
            JSON.stringify({
              type: "candidate",
              clientId: this.clientId,
              candidate: event.candidate.candidate,
              sdpMid: event.candidate.sdpMid,
              sdpMLineIndex: event.candidate.sdpMLineIndex,
            }),
          );
        }
      };

      this.pc.ontrack = (event) => {
        console.log("Got track:", event.track.kind);
        if (event.track.kind === "video") {
          this.setupVideoElement();
          if (event.streams && event.streams[0]) {
            this.v.srcObject = event.streams[0];
          } else {
            this.v.srcObject = new MediaStream([event.track]);
          }
          this.isVisible = true;
          this.isPlayerSet = true;
          this.removeErrorMessage();
        }
      };

      this.pc.ondatachannel = (event) => {
        this.dc = event.channel;
        this.dc.onopen = () => {
          console.log("Data channel open");
        };
        this.dc.onclose = () => {
          console.log("Data channel closed");
        };
        this.dc.onmessage = (e) => {
          this.handleDataChannelMessage(e.data);
        };
      };

      await this.pc.setRemoteDescription({ type: "offer", sdp: msg.sdp });
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
            type: "answer",
            clientId: this.clientId,
            sdp: answer.sdp,
            sdpType: answer.type,
          }),
        );
      }
    } catch (e) {
      console.error("Offer handling error:", e);
      if (this.errorCallback) {
        this.errorCallback("Connection failed");
      }
      this.showErrorMessage("Connection Failed...");
      this.retryConnection();
    }
  }

  private async handleCandidate(msg: any) {
    try {
      if (!this.pc || !msg.candidate) return;
      const candidate = new RTCIceCandidate({
        candidate: msg.candidate,
        sdpMid: msg.sdpMid || "0",
        sdpMLineIndex: msg.sdpMLineIndex || 0,
      });
      if (this.remoteDescSet) {
        await this.pc.addIceCandidate(candidate);
      } else {
        this.pendingCandidates.push(candidate);
      }
    } catch (e) {
      console.error("ICE candidate error:", e);
    }
  }

  private setupVideoElement() {
    let existing = document.getElementById(
      `${this.elId}_video`,
    ) as HTMLVideoElement;
    if (existing) {
      this.v = existing;
      return;
    }

    this.v = document.createElement("video");
    var div = document.getElementById(this.elId);
    div.style.background = "black";
    div.appendChild(this.v);
    this.v.id = `${this.elId}_video`;
    this.v.style.height = "100%";
    this.v.style.width = "100%";
    this.v.style.display = "inline";
    this.v.autoplay = true;
    this.v.muted = true;
    (this.v as any).playsInline = true;
  }

  private handleDataChannelMessage(data: string) {
    if (data.startsWith("--version")) {
      this.svVersion = data.substring(10);
      console.log("Client Version: " + this.clVersion);
      console.log("Server Version: " + this.svVersion);
      return;
    } else if (data.startsWith("--servStatus")) {
      console.log(data.substring(13));
      return;
    }

    switch (data) {
      case "Playback_Finished":
        console.log("Playback_Finished");
        if (this.errorCallback) {
          this.errorCallback("Playback_Finished");
        }
        this.stop();
        return;
      case "Video_Started":
        console.log("Video_Started");
        this.removeErrorMessage();
        if (this.errorCallback) {
          this.errorCallback("Video_Started");
        }
        return;
      case "unable_to_play":
        console.log("unable_to_play");
        if (this.errorCallback) {
          this.errorCallback("unable_to_play");
        }
        this.stop();
        return;
      case "EmptyUrl":
        var errMsg =
          this.mode == "Live" ? "Stream not Found" : "Recording not Found";
        if (this.errorCallback) {
          this.errorCallback(errMsg);
        }
        this.showErrorMessage(errMsg);
        return;
      case "Player_Server_Not_Connected":
        if (this.errorCallback) {
          this.errorCallback("Player Server Not Connected");
        }
        this.showErrorMessage("Player Server Not Connected");
        return;
      case "URL_Server_Not_Connected":
        if (this.errorCallback) {
          this.errorCallback("URL Server Not Connected");
        }
        this.showErrorMessage("URL Server Not Connected");
        return;
      case "retrying":
        if (this.retryingCallback) {
          this.retryingCallback();
        }
        this.showErrorMessage("Trying to Connect...");
        return;
      case "License Expired":
        if (this.errorCallback) {
          this.errorCallback("License Expired/Invalid");
        }
        this.showErrorMessage("License Expired/Invalid");
        return;
      case "Some problem occured":
        if (this.errorCallback) {
          this.errorCallback("Some Problem Occured");
        }
        this.showErrorMessage("Some Problem Occured");
        return;
      case "Stopped":
        console.log("Stream stopped");
        if (!this.doesStopRequested) {
          this.retryConnection();
        }
        return;
      default:
        // Handle Playback_Finished JSON format
        if (data.startsWith("{")) {
          try {
            const json = JSON.parse(data);
            if (json.event === "Playback_Finished") {
              console.log("Playback_Finished, nextTime:", json.nextTime);
              if (this.errorCallback) {
                this.errorCallback("Playback_Finished");
              }
              this.stop();
            }
          } catch (e) {
            console.log("DC message:", data);
          }
        } else {
          console.log("DC message:", data);
        }
    }
  }

  Pause() {
    if (this.dc && this.dc.readyState === "open") {
      this.dc.send("Pause");
    }
  }

  SeekVideo(starttime: string) {
    if (this.dc && this.dc.readyState === "open") {
      this.dc.send("seek_Time" + starttime);
    }
  }

  FastForward(factor: number) {
    this.playbackSpeed = factor;
    if (this.dc && this.dc.readyState === "open") {
      this.dc.send("FastForward" + factor);
    }
  }

  Version() {
    if (this.svVersion) {
      console.log("Client Version: " + this.clVersion);
      console.log("Server Version: " + this.svVersion);
    } else if (this.dc && this.dc.readyState === "open") {
      this.dc.send("Version");
    } else {
      console.log("Please Connect to server via Play Live or Playback");
    }
  }

  servStatus() {
    if (this.dc && this.dc.readyState === "open") {
      this.dc.send("Server Status");
    }
  }

  Close() {
    this.stop();
  }

  getBase64SnapshotUrl(): string {
    var dataURI = "";
    try {
      if (this.v && this.v.videoWidth > 0) {
        var canvas = document.createElement("canvas");
        canvas.width = this.v.videoWidth;
        canvas.height = this.v.videoHeight;
        var ctx = canvas.getContext("2d");
        ctx.drawImage(this.v, 0, 0, canvas.width, canvas.height);
        dataURI = canvas.toDataURL("image/png");
      }
    } catch (ex) {
      console.log(ex);
    }
    return dataURI;
  }

  showErrorMessage(message: string) {
    this.isErrorMessageVisible = true;
    var spanElement = document.getElementById("errorMessage" + this.elId);
    if (!spanElement) {
      var span = document.createElement("span");
      span.innerHTML = message + "...";
      span.classList.add("errorMessage");
      span.style.color = "red";
      span.style.position = "absolute";
      span.style.fontSize = "25px";
      span.style.top = "50%";
      span.style.height = "30px";
      span.style.marginTop = "-15px";
      span.style.width = "100%";
      span.style.textAlign = "center";
      span.style.fontWeight = "bold";
      span.id = "errorMessage" + this.elId;
      var element = document.getElementById(this.elId);
      if (element) {
        element.style.background = "black";
        element.style.position = "relative";
        element.appendChild(span);
      }
    } else {
      spanElement.innerHTML = message + "...";
    }
  }

  removeErrorMessage() {
    try {
      this.isErrorMessageVisible = false;
      let spanElement = document.getElementById("errorMessage" + this.elId);
      if (spanElement) {
        let element = document.getElementById(this.elId);
        if (element) {
          element.removeChild(spanElement);
        }
      }
    } catch (ex) {
      console.error("Error removing error message: ", ex);
    }
  }
}
