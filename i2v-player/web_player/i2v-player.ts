declare var JMuxer: any;

class I2vSdk {
    playerIp: string = "localhost";
    useSecureConnection: boolean = false;
    player: I2vPlayer;
    playerServerIp:string;
    
    constructor(_playerip, _playerserverip, useSecureConnection?: boolean) {
        this.playerServerIp = _playerserverip;
        this.playerIp = _playerip;
        if (useSecureConnection) {
            this.useSecureConnection = useSecureConnection;
        }     
    }


    GetLivePlayer(elId, cameraId, streamtype, advanceDecoding, connectionmode) {
        this.player = new I2vPlayer(elId, cameraId, "Live", streamtype, 0, this.useSecureConnection, advanceDecoding, connectionmode, "1");
        this.player.playerIp = this.playerIp;
        this.player.playerServerIp = this.playerServerIp;
        return this.player;
    }
   
    GetPlaybackPlayer(elId, cameraId, startTime, _playbackviaapache) {
        this.player = new I2vPlayer(elId, cameraId, "PlayBack", "0", startTime, this.useSecureConnection, "0", "tcp", _playbackviaapache);
        this.player.playerIp = this.playerIp;
        this.player.playerServerIp = this.playerServerIp;
        return this.player;
    }
}
class I2vPlayer {
    elId: any;
    cameraId: any;
    streamtype: any;
    mode: string;
    useSecureConnection: boolean = false;
    startTime: any;
    urlCreator: { new(url: string, base?: string | URL): URL; prototype: URL; createObjectURL(object: any): string; revokeObjectURL(url: string): void; };
    errorCallback: any;
    w: WebSocket; //websocket client
    m: MediaSource; //MediaSource object
    isSourceReady: boolean;
    b: SourceBuffer; // buffer
    isPlayerSet: boolean;
    v: HTMLVideoElement; // video element
    i: HTMLImageElement; //image element
    isJpeg: boolean;
    intS: Uint8Array; //initSegment
    retryingCallback: any;
    lastSegment: Uint8Array;
    playerIp: string;
    playerServerIp: string;
    doesStopRequested: boolean = false;
    isErrorMessageVisible: boolean = false;
    playrecursivetimeout: any;
    jmuxer: any;
    useJmuxer: boolean = false;
    connectionmode: string = "tcp";
    playbackviaapache: boolean = true;
    constructor(elId, cameraId, mode, streamtype, startTime, useSecureConnection, advanceDecoding, _connectionmode, _playbackviaapache) {
        this.elId = elId;
        this.cameraId = cameraId;
        this.mode = mode;
        this.streamtype = streamtype;
        this.urlCreator = window.URL || window.webkitURL;
        this.startTime = startTime;
        this.useSecureConnection = useSecureConnection;
        this.connectionmode = _connectionmode;
        this.playbackviaapache = _playbackviaapache;
        //TODO Check playback also working or not
        if (!this.connectionmode) {
            this.connectionmode = "tcp";
        }
        else if (this.connectionmode != "tcp" && this.connectionmode != "udp")
        {
            this.connectionmode = "tcp";
        }


        if (mode == "Live")
        {
            if (!advanceDecoding)
            {
                this.useJmuxer = true;
            }
           else if (advanceDecoding == "0")
            {
                this.useJmuxer = true;
            }
            else
            {
                this.useJmuxer = false;
            }
        }
        else
        {
            this.useJmuxer = true;
        }
    }

    setErrorCallback(errorCallback) {
        this.errorCallback = errorCallback;       
    }

    setRetryingCallback(retryingCallback) {
        this.retryingCallback = retryingCallback;
    }

    stop() {
        try {
            this.removeErrorMessage();
            this.doesStopRequested = true;
            if (this.playrecursivetimeout) {
                clearTimeout(this.playrecursivetimeout);
            }
            try {
                if (this.w) {
                    this.w.close();
                }
            } catch (ex) {

            }

            delete this.m;
            delete this.v;
            if (this.isJpeg) {
                var i = document.getElementById(`${this.elId}_img`) as HTMLImageElement;
                if (i) {
                    i.src = "";
                    i.parentNode.removeChild(i);
                }

            } else {
                var v = document.getElementById(`${this.elId}_video`) as HTMLVideoElement;
                if (v) {
                    v.src = "";
                    v.parentNode.removeChild(v);
                }

            }
        } catch (ex) {

        }

    }

    initializeMediaSource() {
        this.m = new MediaSource();
        var mime = 'video/mp4; codecs="avc1.4D0020"';

        if (!MediaSource.isTypeSupported(mime)) {
            return;
        }
        //this.m.addEventListener('sourceended', (e) => { console.log('sourceended: ' + this.m.readyState); });
        //this.m.addEventListener('sourceclose', (e) => { console.log('sourceclose: ' + this.m.readyState); });
        this.m.addEventListener('error', (e) => { console.log('error: ' + this.m.readyState); });
        this.m.addEventListener('sourceopen', (e) => {
            console.log('sourceopen: ' + this.m.readyState);
            try {
                this.v.play();
            } catch (ex) {
                e = ex;
            }

            this.b = this.m.addSourceBuffer(mime);
            this.b.mode = 'sequence';
            this.b.addEventListener('updateend', (e) => {
                if (this.b.updating) {
                    return;
                }
                if (this.lastSegment) {
                    this.b.appendBuffer(this.lastSegment);
                    this.lastSegment = null;
                }
                //check if buffered media exists
                if (!this.b.buffered.length) {
                    return;
                }
                const currentTime = this.v.currentTime;
                const start = this.b.buffered.start(0);
                const end = this.b.buffered.end(0);
                const past = currentTime - start;
                // if (end - currentTime > 1) {
                //     this.v.currentTime = end - 1;
                // }
                //todo play with numbers and make dynamic or user configurable
            
                if (past > 20 && currentTime < end && !this.b.updating) {
                    this.b.remove(start, currentTime - 4);
                }
            });
            if (!this.b.updating && this.m.readyState === 'open' && this.intS) {
                this.b.appendBuffer(this.intS);
            }
            this.isSourceReady = true;

        }, false);
        //this.v.src = null;
        //this.v.src = window.URL.createObjectURL(this.m);
    }

    _arrayBufferToBase64(b) {
        var binary = '';
        var bytes = new Uint8Array(b);
        var len = bytes.byteLength;
        for (var i = 0; i < len; i++) {
            binary += String.fromCharCode(bytes[i]);
        }
        return window.btoa(binary);
    }

    play() {
        var protocolType: string = "ws";
        var port: number = 8181;
        if (this.useSecureConnection) {
            protocolType = "wss";
            port = 8182;
        }
        if (!this.useJmuxer)
        {
            this.initializeMediaSource();
        }
        this.removeErrorMessage();
        this.showErrorMessage("Trying to Connect...");
        this.w = new WebSocket(`${protocolType}://${this.playerIp}:${port}?cameraId~~${this.cameraId}&&id~~${this.elId}&&startTime~~${this.startTime}&&mode~~${this.mode}&&streamtype~~${this.streamtype}&&useJmuxer~~${this.useJmuxer}&&connectionmode~~${this.connectionmode}&&playbackviaapache~~${this.playbackviaapache}&&serverIp~~${this.playerServerIp}`);
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
                console.log('socket closed and retrying...');
                if (!this.isErrorMessageVisible) {
                    this.showErrorMessage("Trying to Connect...");
                }
                delete this.w;
                if (this.b) {
                    this.b.abort();
                }
                if (this.jmuxer) {
                    this.disposejmuxer();
                }
                this.b = null;
                this.m = null;
                delete this.v;
                this.isPlayerSet = false;
                if (this.isJpeg) {
                    var i = document.getElementById(`${this.elId}_img`) as HTMLImageElement;
                    if (i) {
                        i.src = "";
                        i.parentNode.removeChild(i);
                    }

                } else {
                    var v = document.getElementById(`${this.elId}_video`) as HTMLVideoElement;
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
                case "EmptyUrl":
                    var errMsg = this.mode == "Live" ? "Url not configured" : "Recording not Found";
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

                    try {

                        if (this.jmuxer)
                        {
                            this.disposejmuxer();
                        }

                        if (this.b && this.b.buffered && this.b.buffered.length > 0)
                        {
                            const start = this.b.buffered.start(0);
                            const end = this.b.buffered.end(0);
                            this.b.remove(start, end);
                        }                    
                    } catch (ex)
                    {
                    }
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
                    if (e.data === "mp4")
                    {
                        this.removeErrorMessage();
                        this.v = document.createElement("video");
                        var div = document.getElementById(this.elId);
                        div.style.background = "black";
                        div.appendChild(this.v);
                        this.v.id = `${this.elId}_video`;
                        this.v.style.height = "100%";
                        this.v.style.width = "100%";
                        this.v.style.display = "inline";                      
                        this.isJpeg = false;
                        this.intS = null;
                        if (this.useJmuxer) {
                            this.v.autoplay = true;
                            if (document.addEventListener) {
                                document.addEventListener("visibilitychange", this.OnVideoVisiblityChange)
                            }
                            this.Createjmuxerobject();
                        }
                        else
                        {
                            this.v.src = window.URL.createObjectURL(this.m);

                        }
                    }
                    else if (e.data === "mjpeg")
                    {
                        this.useJmuxer = false;
                        this.removeErrorMessage()
                        this.i = document.createElement("img");
                        var div = document.getElementById(this.elId);
                        div.style.background = "black";
                        div.appendChild(this.i);
                        this.i.id = `${this.elId}_img`;
                        this.i.style.height = "100%";
                        this.i.style.width = "100%";
                        this.i.style.display = "inline";
                        this.isJpeg = true;
                        this.i.setAttribute(
                            'src', `http://${this.playerIp}:4554/${this.elId}`
                        );
                    }
                    this.isPlayerSet = true;
                    return;
                }
            }


            if (this.isJpeg) {
                //if (this.imageUrl) {
                //    this.urlCreator.revokeObjectURL(this.imageUrl);
                //}
                //this.imageUrl = this.urlCreator.createObjectURL(e.data);
                ////this.i.setAttribute(
                ////    'src', `data:image/png;base64,${this._arrayBufferToBase64(e.data)}`
                ////);
                //this.i.setAttribute('src', this.imageUrl);
            } else {

                var data = new Uint8Array(e.data);
                if (this.useJmuxer) {
                    if (!this.jmuxer) {
                        this.Createjmuxerobject();
                    }
                    if (this.jmuxer && this.jmuxer.mseReady) {
                        this.jmuxer.feed({
                            video: data
                        });
                    }
                }
                else
                {
                    if (!this.intS) {
                        this.intS = data;
                    }

                    if (!this.isSourceReady)
                        return;

                    if (this.b && this.b.buffered.length) {
                        const lag = this.b.buffered.end(0) - this.v.currentTime;
                        if (lag > 0.5) {
                            this.v.currentTime = this.b.buffered.end(0) - 0.5;
                        }
                    }
                    this.lastSegment = data;
                    if (this.b && (!this.b.updating && this.m.readyState === 'open')) {
                        try {
                            this.b.appendBuffer(this.lastSegment);
                        } catch (ex) {
                            this.isSourceReady = false;
                            this.initializeMediaSource();
                            this.v.src = null;
                            this.v.src = window.URL.createObjectURL(this.m);
                        }
                        this.lastSegment = null;
                    }
                }              
            }
        }, false);
    }
    OnVideoVisiblityChange = (event) =>
    {
        if (this.useJmuxer)
        {
            if (document.visibilityState == 'hidden') {
                if (this.jmuxer) {
                    this.jmuxer = null;
                }
            }
            else {
                this.Createjmuxerobject();
            }
        }  
    }

    disposejmuxer()
    {
        this.jmuxer = null;
    }
    Createjmuxerobject()
    {
        this.jmuxer = null;
        this.jmuxer = new JMuxer({
            node: this.v.id,
            debug: false,
            mode: 'video',
            flushingTime: 0,
            fps: 30
        });
    }
    showErrorMessage(message) {
        this.isErrorMessageVisible = true;
        var spanElement = document.getElementById("errorMessage" + this.elId);
        if (!spanElement) {
            var span = document.createElement("span");
            span.innerHTML = message + "...";
            span.style.color = "red";
            span.style.position = "absolute";
            span.style.fontSize = "25px";
            span.style.top = "5px";
            span.style.left = "10px";
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
            var spanElement = document.getElementById("errorMessage" + this.elId);
            if (spanElement) {
                var element = document.getElementById(this.elId);
                element.removeChild(spanElement);
            }
        } catch (ex) {

        }
    }
}

