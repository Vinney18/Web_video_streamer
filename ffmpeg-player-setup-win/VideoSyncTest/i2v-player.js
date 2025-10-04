var I2vSdk = /** @class */ (function () {
    function I2vSdk(_wPlayerIp, _wServerIp, _wServerPort, useSecureConnection) {
        this.clVersion = "7.0.0";
        this.wPlayerIp = "localhost";
        this.wServerPort = 8890;
        this.useSecureConnection = false;
        this.wPlayerIp = _wPlayerIp;
        this.wServerIp = _wServerIp;
        this.wServerPort = _wServerPort;
        if (useSecureConnection) {
            this.useSecureConnection = useSecureConnection;
        }
    }
    I2vSdk.prototype.GetLivePlayer = function (elId, cameraId, streamtype, analyticType, connectionmode) {
        this.player = new I2vPlayer(elId, cameraId, "Live", streamtype, 0, analyticType, connectionmode, this.clVersion, this.useSecureConnection);
        this.player.wPlayerIp = this.wPlayerIp;
        this.player.wServerIp = this.wServerIp;
        this.player.wServerPort = this.wServerPort;
        return this.player;
    };
    I2vSdk.prototype.GetPlaybackPlayer = function (elId, cameraId, startTime, _playbackviaapache) {
        this.player = new I2vPlayer(elId, cameraId, "PlayBack", 0, startTime, "", "tcp", this.clVersion, this.useSecureConnection);
        this.player.wPlayerIp = this.wPlayerIp;
        this.player.wServerIp = this.wServerIp;
        this.player.wServerPort = this.wServerPort;
        return this.player;
    };
    I2vSdk.prototype.SeekVideo = function (startTime) {
        if (this.player && this.player.mode != "Live") {
            this.player.SeekVideo(startTime);
        }
    };
    I2vSdk.prototype.Pause = function () {
        if (this.player && this.player.mode != "Live") {
            this.player.Pause();
        }
    };
    return I2vSdk;
}());
var I2vPlayer = /** @class */ (function () {
    function I2vPlayer(elId, cameraId, mode, streamtype, startTime, _analyticType, _connectionmode, _clVersion, useSecureConnection) {
        var _this = this;
        this.connectionMode = "tcp";
        this.wServerPort = 8890;
        this.useSecureConnection = false;
        this.IsEmptyUrl = false;
        this.doesStopRequested = false;
        this.isErrorMessageVisible = false;
        this.IsPlayerServerConnected = false;
        this.URL_Server_Not_Connected = false;
        this.OnVideoVisiblityChange = function (event) {
            if (document.visibilityState == 'hidden') {
                _this.isVisible = false;
                if (_this.jmuxer)
                    _this.jmuxer = null;
            }
            else {
                _this.isVisible = true;
                if (!_this.isRgb)
                    _this.Createjmuxerobject();
            }
        };
        this.elId = elId;
        this.cameraId = cameraId;
        this.mode = mode;
        this.streamType = streamtype;
        this.startTime = startTime;
        this.analyticType = _analyticType;
        this.connectionMode = _connectionmode;
        this.clVersion = _clVersion;
        this.useSecureConnection = useSecureConnection;
        if (!this.analyticType)
            this.analyticType = "";
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
    I2vPlayer.prototype.setErrorCallback = function (errorCallback) {
        this.errorCallback = errorCallback;
    };
    I2vPlayer.prototype.setRetryingCallback = function (retryingCallback) {
        this.retryingCallback = retryingCallback;
    };
    I2vPlayer.prototype.stop = function () {
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
            }
            catch (ex) {
                console.error("wClient: Unable to close Websocket");
            }
            delete this.v;
            delete this.c;
            if (this.isRgb) {
                var c = document.getElementById("".concat(this.elId, "_canvas"));
                if (c) {
                    var c_context = c.getContext('2d');
                    c_context.clearRect(0, 0, this.width, this.height);
                    c.parentNode.removeChild(c);
                }
            }
            else {
                var v = document.getElementById("".concat(this.elId, "_video"));
                if (v) {
                    v.src = "";
                    v.parentNode.removeChild(v);
                }
            }
        }
        catch (ex) {
            console.error("wClient: Error in Stop Function");
        }
    };
    I2vPlayer.prototype.play = function () {
        var _this = this;
        var protocolType = "ws";
        var port = 8181;
        if (this.useSecureConnection) {
            protocolType = "wss";
            port = 8182;
        }
        this.removeErrorMessage();
        this.IsEmptyUrl = false;
        if (!this.IsEmptyUrl)
            this.showErrorMessage("Trying to Connect...");
        this.IsPlayerServerConnected = false;
        this.URL_Server_Not_Connected = false;
        this.w = new WebSocket("".concat(protocolType, "://").concat(this.wPlayerIp, ":").concat(port, "?cameraId~~").concat(this.cameraId, "&&mode~~").concat(this.mode, "&&streamType~~").concat(this.streamType, "&&startTime~~").concat(this.startTime, "&&analyticType~~").concat(this.analyticType, "&&connectionMode~~").concat(this.connectionMode, "&&wServerIp~~").concat(this.wServerIp, "&&wServerPort~~").concat(this.wServerPort, "&&clVersion~~").concat(this.clVersion));
        this.w.binaryType = 'arraybuffer';
        this.w.addEventListener('open', function (event) {
            _this.doesStopRequested = false;
            _this.w.send('Hello Server!');
        });
        this.w.addEventListener('close', function (event) {
            if (_this.doesStopRequested) {
                console.log('socket closed');
                _this.removeErrorMessage();
            }
            else {
                console.log('socket closed and retrying...');
                if (_this.IsPlayerServerConnected) {
                    var errMsg = "Player Server Not Connected ";
                    _this.showErrorMessage(errMsg);
                }
                else if (_this.URL_Server_Not_Connected) {
                    var errMsg = "URL Server Not Connected";
                    _this.showErrorMessage(errMsg);
                }
                else if (_this.IsEmptyUrl) {
                    var errMsg = _this.mode == "Live" ? "Stream not Found" : "Recording not Found";
                    _this.showErrorMessage(errMsg);
                }
                else {
                    _this.showErrorMessage("Player Not Connected...");
                }
                delete _this.w;
                if (_this.jmuxer) {
                    _this.disposejmuxer();
                }
                delete _this.c;
                delete _this.v;
                _this.isPlayerSet = false;
                _this.isVisible = false;
                if (_this.isRgb) {
                    var c = document.getElementById("".concat(_this.elId, "_canvas"));
                    if (c) {
                        var c_context = c.getContext('2d');
                        c_context.clearRect(0, 0, _this.width, _this.height);
                        c.parentNode.removeChild(c);
                    }
                }
                else {
                    var v = document.getElementById("".concat(_this.elId, "_video"));
                    if (v) {
                        v.src = "";
                        v.parentNode.removeChild(v);
                    }
                }
                _this.playrecursivetimeout = setTimeout(function () {
                    if (!_this.doesStopRequested) {
                        _this.play();
                    }
                }, 3000);
            }
        });
        this.w.addEventListener('message', function (e) {
            _this.IsEmptyUrl = false;
            _this.IsPlayerServerConnected = false;
            _this.URL_Server_Not_Connected = false;
            if (e.data.toString().startsWith("--version")) {
                _this.svVersion = e.data.substring(10);
                console.log("Client Version: " + _this.clVersion);
                console.log("Server Version: " + _this.svVersion);
                return;
            }
            else if (e.data.toString().startsWith("--servStatus")) {
                console.log(e.data.substring(13));
                return;
            }
            switch (e.data) {
                case "Server_ip_not_provided":
                    var errMsg = "Please Provide Valid Server Ip";
                    if (_this.errorCallback) {
                        _this.errorCallback(errMsg);
                    }
                    _this.showErrorMessage(errMsg);
                    return;
                case "Playback_Finished":
                    var errMsg = "Playback_Finished";
                    console.log(errMsg);
                    if (_this.errorCallback) {
                        _this.errorCallback(errMsg);
                    }
                    _this.stop();
                    return;
                case "Video_Started":
                    var errMsg = "Video_Started";
                    console.log(errMsg);
                    // if (_this.errorCallback) {
                    //     _this.errorCallback(errMsg);
                    // }
                    return;
                case "unable_to_play":
                    var errMsg = "unable_to_play";
                    console.log(errMsg);
                    if (_this.errorCallback) {
                        _this.errorCallback(errMsg);
                    }
                    _this.stop();
                    return;
                case "EmptyUrl":
                    _this.IsEmptyUrl = true;
                    var errMsg = _this.mode == "Live" ? "Stream not Found" : "Recording not Found";
                    if (_this.errorCallback) {
                        _this.errorCallback(errMsg);
                    }
                    _this.showErrorMessage(errMsg);
                    return;
                case "Player_Server_Not_Connected":
                    _this.IsPlayerServerConnected = true;
                    var errMsg = "Player Server Not Connected ";
                    if (_this.errorCallback) {
                        _this.errorCallback(errMsg);
                    }
                    _this.showErrorMessage(errMsg);
                    return;
                case "URL_Server_Not_Connected":
                    _this.URL_Server_Not_Connected = true;
                    var errMsg = "URL Server Not Connected";
                    if (_this.errorCallback) {
                        _this.errorCallback(errMsg);
                    }
                    _this.showErrorMessage(errMsg);
                    return;
                case "retrying":
                    if (_this.retryingCallback) {
                        _this.retryingCallback();
                    }
                    _this.showErrorMessage("Trying to Connect...");
                    return;
                case "License Expired":
                    var errMsg = "License Expired/Invalid";
                    if (_this.errorCallback) {
                        _this.errorCallback(errMsg);
                    }
                    _this.showErrorMessage(errMsg);
                    return;
                case "Some problem occured":
                    var errMsg = "Some Problem Occured";
                    if (_this.errorCallback) {
                        _this.errorCallback(errMsg);
                    }
                    _this.showErrorMessage(errMsg);
                    return;
                default:
                    _this.removeErrorMessage();
            }
            if (!_this.isPlayerSet) {
                if (e.data instanceof ArrayBuffer) {
                    return;
                }
                else {
                    if (e.data === "mp4") {
                        _this.removeErrorMessage();
                        _this.v = document.createElement("video");
                        var div = document.getElementById(_this.elId);
                        div.style.background = "black";
                        div.appendChild(_this.v);
                        _this.v.id = "".concat(_this.elId, "_video");
                        _this.v.style.height = "100%";
                        _this.v.style.width = "100%";
                        _this.v.style.display = "inline";
                        _this.isRgb = false;
                        _this.v.autoplay = true;
                        _this.v.muted = true;
                        _this.isVisible = true;
                        if (document.addEventListener) {
                            document.addEventListener("visibilitychange", _this.OnVideoVisiblityChange);
                        }
                        _this.Createjmuxerobject();
                        _this.isPlayerSet = true;
                    }
                    else if (e.data === "rgba") {
                        _this.removeErrorMessage();
                        _this.c = document.createElement("canvas");
                        var div = document.getElementById(_this.elId);
                        div.style.background = "black";
                        div.appendChild(_this.c);
                        _this.c.id = "".concat(_this.elId, "_canvas");
                        // _this.c.style.height = "100%";
                        // _this.c.style.width = "100%";
                        _this.c.style.display = "inline";
                        _this.isRgb = true;
                        _this.isVisible = true;
                        if (document.addEventListener) {
                            document.addEventListener("visibilitychange", _this.OnVideoVisiblityChange);
                        }
                        _this.isPlayerSet = false;
                    }
                    else if (e.data.startsWith("rgba")) {
                        var metadata = e.data.substring(5);
                        var dimensions = metadata.split('x');
                        _this.width = dimensions[0];
                        _this.height = dimensions[1];
                        _this.c.width = dimensions[0];
                        _this.c.height = dimensions[1];
                        _this.isPlayerSet = true;
                    }
                    else {
                        console.log("Unknown Format");
                    }
                    return;
                }
            }
            if (_this.isVisible) {
                if (!_this.isRgb) {
                    var mp4Data;
                    if (_this.mode !== "Live") {
                        var incomingData = e.data;
                        var timestamp8byte = new Uint8Array(incomingData.slice(0, 8));
                        var timestamp = 0;
                        for (var i = timestamp8byte.length - 1; i >= 0; i--) {
                            timestamp = timestamp * 256 + timestamp8byte[i];
                        }
                        _this.status = timestamp;
                        mp4Data = new Uint8Array(incomingData.slice(8));
                    }
                    else {
                        mp4Data = new Uint8Array(e.data);
                    }
                    if (_this.jmuxer && _this.jmuxer.mseReady) {
                        _this.jmuxer.feed({
                            video: mp4Data
                        });
                    }
                }
                else {
                    //h265 video
                    //can be both liveview and playback
                    var rgbaData;
                    if (_this.mode !== "Live") {
                        var incomingData = e.data;
                        var timestamp8byte = new Uint8Array(incomingData.slice(0, 8));
                        var timestamp = 0;
                        for (var i = timestamp8byte.length - 1; i >= 0; i--) {
                            timestamp = timestamp * 256 + timestamp8byte[i];
                        }
                        _this.status = timestamp;
                        rgbaData = new Uint8ClampedArray(incomingData.slice(8));
                    }
                    else {
                        rgbaData = new Uint8ClampedArray(e.data);
                    }
                    var canvas = document.getElementById("".concat(_this.elId, "_canvas"));
                    if (canvas) {
                        var ctxaaa = canvas.getContext('2d');
                        ctxaaa.clearRect(0, 0, _this.width, _this.height);
                    }
                    var ctx1 = _this.c.getContext('2d');
                    var imgdata = new ImageData(rgbaData, _this.width, _this.height);
                    ctx1.putImageData(imgdata, 0, 0);
                }
            }
        }, false);
    };
    I2vPlayer.prototype.disposejmuxer = function () {
        this.jmuxer = null;
    };
    I2vPlayer.prototype.Createjmuxerobject = function () {
        this.jmuxer = null;
        if (this.v) {
            this.jmuxer = new JMuxer({
                node: this.v.id,
                debug: false,
                mode: 'video',
                flushingTime: 0,
                fps: 25,
                maxDelay:300
            });
        }
    };
    I2vPlayer.prototype.showErrorMessage = function (message) {
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
        }
        else {
            spanElement.innerHTML = message + "...";
        }
    };
    I2vPlayer.prototype.removeErrorMessage = function () {
        try {
            this.isErrorMessageVisible = false;
            var spanElement = document.getElementById("errorMessage" + this.elId);
            if (spanElement) {
                var element = document.getElementById(this.elId);
                element.removeChild(spanElement);
            }
        }
        catch (ex) {
        }
    };
    I2vPlayer.prototype.getBase64SnapshotUrl = function () {
        var dataURI = "";
        try {
            var canvas = document.createElement('canvas');
            if (this.isRgb) {
                canvas.width = this.c.width;
                canvas.height = this.c.height;
                var ctx = canvas.getContext('2d');
                ctx.drawImage(this.c, 0, 0, canvas.width, canvas.height);
            }
            else {
                canvas.width = this.v.videoWidth;
                canvas.height = this.v.videoHeight;
                var ctx = canvas.getContext('2d');
                ctx.drawImage(this.v, 0, 0, canvas.width, canvas.height);
            }
            dataURI = canvas.toDataURL('image/png');
        }
        catch (ex) {
            console.log(ex);
        }
        return dataURI;
    };
    I2vPlayer.prototype.Version = function () {
        if (this.svVersion) {
            console.log("Client Version: " + this.clVersion);
            console.log("Server Version: " + this.svVersion);
        }
        else if (this.w) {
            this.w.send("Version");
        }
        else {
            console.log("Please Connect to server via Play Live or Playback");
        }
    };
    I2vPlayer.prototype.servStatus = function () {
        if (this.w) {
            this.w.send("Server Status");
        }
    };
    I2vPlayer.prototype.Close = function () {
        this.doesStopRequested = true;
        if (this.w) {
            this.w.close();
        }
    };
    I2vPlayer.prototype.Pause = function () {
        if (this.w) {
            this.w.send("Pause");
        }
    };
    I2vPlayer.prototype.SeekVideo = function (starttime) {
        if (this.w) {
            this.w.send("seek_Time" + starttime);
        }
    };
    return I2vPlayer;
}());
