declare var JMuxer: any;

class I2vSdk {
    clVersion: string = "7.2.1";
    wPlayerIp: string = "localhost";
    wServerIp: string;
    wServerPort: any = 8890;
    useSecureConnection: boolean = false;
    player: I2vPlayer;
    vaServerId: string;
    vaServerPipeId: string;

    constructor(_wPlayerIp: string, _wServerIp: string, _wServerPort?: any, useSecureConnection?: boolean) {
        this.wPlayerIp = _wPlayerIp;
        this.wServerIp = _wServerIp;
        this.wServerPort = _wServerPort || this.wServerPort;
        this.useSecureConnection = useSecureConnection || this.useSecureConnection;
    }

    GetLivePlayer(elId: any, cameraId: number, streamtype: number, analyticType: string, connectionmode: string, vaServerId?: string, vaServerPipeId?: string) {
        this.player = new I2vPlayer(elId, cameraId, "Live", streamtype, 0, 0, analyticType, connectionmode, this.clVersion, this.useSecureConnection, 1, vaServerId, vaServerPipeId);
        this.player.wPlayerIp = this.wPlayerIp;
        this.player.wServerIp = this.wServerIp;
        this.player.wServerPort = this.wServerPort;

        return this.player;
    }

    GetPlaybackPlayer(elId: any, cameraId: number, startTime: number, endTime: number, _playbackviaapache: string, playbackSpeed: number = 1) {
        this.player = new I2vPlayer(elId, cameraId, "PlayBack", 0, startTime, endTime, "", "tcp", this.clVersion, this.useSecureConnection, playbackSpeed);
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


class I2vPlayer {
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

    w: WebSocket; //websocket client
    v: HTMLVideoElement; // Video element
    c: HTMLCanvasElement; // Canvas element

    jmuxer: any;
    width: number;
    height: number;

    errorCallback: any;
    retryingCallback: any;

    isRgb: boolean;
    isVisible: boolean;
    isPlayerSet: boolean;
    IsEmptyUrl: boolean = false;
    doesStopRequested: boolean = false;
    isErrorMessageVisible: boolean = false;
    IsPlayerServerConnected: boolean = false;
    URL_Server_Not_Connected: boolean = false;

    playrecursivetimeout: any;
    status: any;
    svVersion: any;

    playbackSpeed: number = 1;
    vaServerId: string;
    vaServerPipeId: string;

    constructor(elId: any, cameraId: number, mode: string, streamtype: number, startTime: number, endTime: number, _analyticType: string, _connectionmode: string, _clVersion: string, useSecureConnection: boolean, playbackSpeed: number = 1, vaServerId: string = "", vaServerPipeId: string = "") {
        this.elId = elId;
        this.cameraId = cameraId;
        this.mode = mode;
        this.streamType = streamtype;
        this.startTime = startTime;
        this.endTime = endTime;
        this.analyticType = _analyticType;
        this.connectionMode = _connectionmode;
        this.clVersion = _clVersion;
        this.useSecureConnection = useSecureConnection;
        // only speed allowed is in range 0.5 to 5, with 0.5 step, 1 is default,
        // validation for playback speed, round off to closest value
        if (playbackSpeed < 0.5) playbackSpeed = 0.5;
        if (playbackSpeed > 5) playbackSpeed = 5;
        playbackSpeed = Math.round(playbackSpeed * 2) / 2;
        console.log("playbackSpeed Allowed: 0.5 to 5, with 0.5 step, 1 is default")
        console.log("playbackSpeed: " + playbackSpeed)
        this.playbackSpeed = playbackSpeed;
        this.vaServerId = vaServerId;
        this.vaServerPipeId = vaServerPipeId;


        if (!this.analyticType) this.analyticType = "";

        if (!this.connectionMode) {
            this.connectionMode = "";
        }
        else {
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

    stop() {
        try {
            this.removeErrorMessage();
            this.doesStopRequested = true;
            if (this.playrecursivetimeout) {
                clearTimeout(this.playrecursivetimeout);
                this.playrecursivetimeout = null;
            }
            if (this.w) {
                this.w.close();
                this.w = null;
            }

            delete this.v;
            delete this.c;
            if (this.isRgb) {
                let c = document.getElementById(`${this.elId}_canvas`) as HTMLCanvasElement;
                if (c) {
                    let c_context = c.getContext('2d');
                    c_context.clearRect(0, 0, this.width, this.height);
                    c.parentNode.removeChild(c);
                }
            } else {
                let v = document.getElementById(`${this.elId}_video`) as HTMLVideoElement;
                if (v) {
                    v.src = "";
                    v.parentNode.removeChild(v);
                }
            }
        } catch (ex) {
            console.error("wClient: Error in Stop Function");
        }

    }

    play() {
        var protocolType: string = "ws";
        var port: number = 8181;

        if (this.useSecureConnection) {
            protocolType = "wss";
            port = 8182;
        }

        this.removeErrorMessage();

        this.IsEmptyUrl = false;
        if (!this.IsEmptyUrl) this.showErrorMessage("Trying to Connect...");
        this.IsPlayerServerConnected = false;
        this.URL_Server_Not_Connected = false;
        this.w = new WebSocket(`${protocolType}://${this.wPlayerIp}:${port}?cameraId~~${this.cameraId}&&mode~~${this.mode}&&streamType~~${this.streamType}&&startTime~~${this.startTime}&&endTime~~${this.endTime}&&analyticType~~${this.analyticType}&&connectionMode~~${this.connectionMode}&&wServerIp~~${this.wServerIp}&&wServerPort~~${this.wServerPort}&&clVersion~~${this.clVersion}&&playbackSpeed~~${this.playbackSpeed}&&vaServerId~~${this.vaServerId}&&vaServerPipeId~~${this.vaServerPipeId}`);

        this.w.binaryType = 'arraybuffer';

        this.w.addEventListener('open', (event) => {
            this.doesStopRequested = false;
            this.w.send('Hello Server!');
        });

        this.w.addEventListener('close', (event) => {
            if (this.doesStopRequested) {
                console.log('socket closed');
                this.removeErrorMessage();
            } else {
                let errMsg = "";
                console.log('socket closed and retrying...');
                if (this.IsPlayerServerConnected) {
                    errMsg = "Player Server Not Connected ";
                }
                else if (this.URL_Server_Not_Connected) {
                    errMsg = "URL Server Not Connected";
                }
                else if (this.IsEmptyUrl) {
                    errMsg = this.mode == "Live" ? "Stream not Found" : "Recording not Found";
                }
                else {
                    errMsg = "Player Not Connected...";
                }
                this.showErrorMessage(errMsg);

                delete this.w;
                if (this.jmuxer) {
                    this.disposejmuxer();
                }
                delete this.c;
                delete this.v;
                this.isPlayerSet = false;
                this.isVisible = false;
                if (this.isRgb) {
                    let c = document.getElementById(`${this.elId}_canvas`) as HTMLCanvasElement;
                    if (c) {
                        let c_context = c.getContext('2d');
                        c_context.clearRect(0, 0, this.width, this.height);
                        c.parentNode.removeChild(c);
                    }
                } else {
                    let v = document.getElementById(`${this.elId}_video`) as HTMLVideoElement;
                    if (v) {
                        v.src = "";
                        v.parentNode.removeChild(v);
                    }
                }
                this.playrecursivetimeout = setTimeout(() => {
                    if (!this.doesStopRequested) {
                        this.play();
                    }
                }, 3000);
            }

        });

        this.w.addEventListener('message', (e) => {
            this.IsEmptyUrl = false;
            this.IsPlayerServerConnected = false;
            this.URL_Server_Not_Connected = false;
            if (e.data.toString().startsWith("--version")) {
                this.svVersion = e.data.substring(10);
                console.log("Client Version: " + this.clVersion);
                console.log("Server Version: " + this.svVersion);
                return;
            } else if (e.data.toString().startsWith("--servStatus")) {
                console.log(e.data.substring(13));
                return;
            }
            switch (e.data) {
                case "Server_ip_not_provided":
                    var errMsg = "Please Provide Valid Server Ip";
                    if (this.errorCallback) {
                        this.errorCallback(errMsg);
                    }
                    this.showErrorMessage(errMsg);
                    return;
                case "Playback_Finished":
                    var errMsg = "Playback_Finished";
                    console.log(errMsg);
                    if (this.errorCallback) {
                        this.errorCallback(errMsg);
                    }
                    this.stop();
                    return;
                case "Video_Started":
                    var errMsg = "Video_Started";
                    console.log(errMsg);
                    if (this.errorCallback) {
                        this.errorCallback(errMsg);
                    }
                    return;
                case "unable_to_play":
                    var errMsg = "unable_to_play";
                    console.log(errMsg);
                    if (this.errorCallback) {
                        this.errorCallback(errMsg);
                    }
                    this.stop();
                    return;
                case "EmptyUrl":
                    this.IsEmptyUrl = true;
                    var errMsg = this.mode == "Live" ? "Stream not Found" : "Recording not Found";
                    if (this.errorCallback) {
                        this.errorCallback(errMsg);
                    }
                    this.showErrorMessage(errMsg);
                    return;
                case "Player_Server_Not_Connected":
                    this.IsPlayerServerConnected = true;

                    var errMsg = "Player Server Not Connected ";
                    if (this.errorCallback) {
                        this.errorCallback(errMsg);
                    }
                    this.showErrorMessage(errMsg);
                    return;
                case "URL_Server_Not_Connected":
                    this.URL_Server_Not_Connected = true;

                    var errMsg = "URL Server Not Connected";
                    if (this.errorCallback) {
                        this.errorCallback(errMsg);
                    }
                    this.showErrorMessage(errMsg);
                    return;
                case "retrying":
                    if (this.retryingCallback) {
                        this.retryingCallback();
                    }
                    this.showErrorMessage("Trying to Connect...");
                    return;
                case "License Expired":
                    var errMsg = "License Expired/Invalid";
                    if (this.errorCallback) {
                        this.errorCallback(errMsg);
                    }
                    this.showErrorMessage(errMsg);
                    return;
                case "Some problem occured":
                    var errMsg = "Some Problem Occured";
                    if (this.errorCallback) {
                        this.errorCallback(errMsg);
                    }
                    this.showErrorMessage(errMsg);
                    return;
                default:
                    this.removeErrorMessage();
            }
            if (!this.isPlayerSet) {
                if (e.data instanceof ArrayBuffer) {
                    return;
                } else {
                    if (e.data === "mp4") {
                        this.removeErrorMessage();
                        this.v = document.createElement("video");
                        var div = document.getElementById(this.elId);
                        div.style.background = "black";
                        div.appendChild(this.v);
                        this.v.id = `${this.elId}_video`;
                        this.v.style.height = "100%";
                        this.v.style.width = "100%";
                        this.v.style.display = "inline";
                        this.isRgb = false;
                        this.v.autoplay = true;
                        this.v.muted = true;
                        this.isVisible = true;
                        // The visibilitychange event is fired at the document when the contents of its tab have become visible or have been hidden.
                        // if (document.addEventListener) {
                        //     document.addEventListener("visibilitychange", this.OnVideoVisiblityChange)
                        // }
                        this.isPlayerSet = false;
                    }
                    else if (e.data.startsWith("mp4")) {
                        let metadata: any = e.data.substring(5);
                        let frameRate = metadata.split("x")[2]
                        this.Createjmuxerobject(parseInt(frameRate));
                        this.isPlayerSet = true;
                    }
                    else if (e.data === "rgba") {
                        this.removeErrorMessage();
                        this.c = document.createElement("canvas");
                        let div = document.getElementById(this.elId);
                        div.style.background = "black";
                        div.appendChild(this.c);
                        this.c.id = `${this.elId}_canvas`;
                        this.c.style.height = "100%";
                        this.c.style.width = "100%";
                        this.c.style.display = "inline";
                        this.isRgb = true;
                        this.isVisible = true;
                        // if (document.addEventListener) {
                        //     document.addEventListener("visibilitychange", this.OnVideoVisiblityChange)
                        // }
                        this.isPlayerSet = false;
                    }
                    else if (e.data.startsWith("rgba")) {
                        let metadata: any = e.data.substring(5);
                        let dimensions = metadata.split('x');
                        this.width = dimensions[0];
                        this.height = dimensions[1];
                        this.c.width = dimensions[0];
                        this.c.height = dimensions[1];
                        this.isPlayerSet = true;
                    }
                    else {
                        console.log("Unknown Format");
                    }

                    return;
                }
            }
            if (this.isVisible) {
                if (!this.isRgb) {
                    let mp4Data: Uint8Array;
                    if (this.mode !== "Live") {
                        let incomingData: ArrayBuffer = (e.data as ArrayBuffer);
                        // optimized, but not being used
                        // let dataView = new DataView(incomingData.slice(0, 8));
                        // let timestamp = dataView.getUint32(0) * 0x100000000 + dataView.getUint32(4);
                        // this.status = timestamp;
                        mp4Data = new Uint8Array(incomingData.slice(8));
                    }
                    else {
                        mp4Data = new Uint8Array(e.data);
                    }
                    if (this.jmuxer && this.jmuxer.mseReady) {
                        this.jmuxer.feed({
                            video: mp4Data,
                        });
                    }
                }
                else {
                    //h265 video
                    //can be both liveview and playback
                    let rgbaData: Uint8ClampedArray;

                    if (this.mode !== "Live") {
                        const incomingData: ArrayBuffer = e.data;
                        // optimized, but not being used
                        // const dataView = new DataView(incomingData.slice(0, 8));
                        // const timestamp = dataView.getUint32(0) * 0x100000000 + dataView.getUint32(4);
                        // this.status = timestamp;
                        rgbaData = new Uint8ClampedArray(incomingData.slice(8));
                    } else {
                        rgbaData = new Uint8ClampedArray(e.data);
                    }
                    const canvas = document.getElementById(`${this.elId}_canvas`);

                    if (canvas) {
                        const ctxaaa = (canvas as HTMLCanvasElement).getContext('2d');
                        ctxaaa.clearRect(0, 0, this.width, this.height);
                    }
                    var ctx1 = this.c.getContext('2d');
                    var imgdata = new ImageData(rgbaData, this.width, this.height);
                    ctx1.putImageData(imgdata, 0, 0);
                }
            }

        }, true);
    }

    OnVideoVisiblityChange = (event) => {
        if (document.visibilityState == 'hidden') {
            this.isVisible = false;
            if (this.jmuxer) this.jmuxer = null;
        }
        else {
            this.isVisible = true;
            if (!this.isRgb) this.Createjmuxerobject();
        }
    }

    disposejmuxer() {
        this.jmuxer = null;
    }

    Createjmuxerobject(FPS = 30) {
        this.jmuxer = null;
        if (this.v) {
            this.jmuxer = new JMuxer({
                node: this.v.id,
                debug: false,
                mode: 'video',
                flushingTime: 0,
                clearBuffer: true,
                fps: FPS * this.playbackSpeed  // will be 1 at init
            });
        }
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
            console.error("Error removing error message: ", ex)
        }
    }

    getBase64SnapshotUrl() {
        var dataURI = "";
        try {
            var canvas = document.createElement('canvas');

            if (this.isRgb) {
                canvas.width = this.c.width;
                canvas.height = this.c.height;
                var ctx = canvas.getContext('2d');
                ctx.drawImage(this.c, 0, 0, canvas.width, canvas.height);
            } else {
                canvas.width = this.v.videoWidth;
                canvas.height = this.v.videoHeight;
                var ctx = canvas.getContext('2d');
                ctx.drawImage(this.v, 0, 0, canvas.width, canvas.height);
            }
            dataURI = canvas.toDataURL('image/png');
        } catch (ex) {
            console.log(ex);
        }
        return dataURI;
    }

    Version() {
        if (this.svVersion) {
            console.log("Client Version: " + this.clVersion);
            console.log("Server Version: " + this.svVersion);
        } else if (this.w) {
            this.w.send("Version");
        } else {
            console.log("Please Connect to server via Play Live or Playback");
        }
    }

    servStatus() {
        if (this.w) {
            this.w.send("Server Status");
        }
    }

    Close() {
        this.doesStopRequested = true;
        if (this.w) {
            this.w.close();
        }
    }

    Pause() {
        if (this.w) {
            this.w.send("Pause");
        }
    }

    SeekVideo(starttime: string) {
        if (this.w) {
            this.w.send("seek_Time" + starttime);
        }
    }

    FastForward(factor: number) {
        this.playbackSpeed = factor;
        if (this.w) {
            this.w.send("FastForward" + factor);
        }
    }
}