var I2vSdk = (function () {
    function I2vSdk(_playerip, _playerserverip, useSecureConnection) {
        this.playerIp = "localhost";
        this.useSecureConnection = false;
        this.playerServerIp = _playerserverip;
        this.playerIp = _playerip;
        if (useSecureConnection) {
            this.useSecureConnection = useSecureConnection;
        }
    }
    I2vSdk.prototype.GetLivePlayer = function (elId, cameraId, streamtype, analyticType, advanceDecoding, connectionmode) {
        this.player = new I2vPlayer(elId, cameraId, "Live", streamtype, 0, this.useSecureConnection, advanceDecoding, connectionmode, "1", analyticType);
        this.player.playerIp = this.playerIp;
        this.player.playerServerIp = this.playerServerIp;
        return this.player;
    };
    I2vSdk.prototype.GetPlaybackPlayer = function (elId, cameraId, startTime, _playbackviaapache) {
        this.player = new I2vPlayer(elId, cameraId, "PlayBack", "0", startTime, this.useSecureConnection, "0", "tcp", _playbackviaapache, "");
        this.player.playerIp = this.playerIp;
        this.player.playerServerIp = this.playerServerIp;
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
    I2vSdk.prototype.Resume = function () {
        if (this.player && this.player.mode != "Live") {
            this.player.Resume();
        }
    };
    return I2vSdk;
}());
var I2vPlayer = (function () {
    function I2vPlayer(elId, cameraId, mode, streamtype, startTime, useSecureConnection, advanceDecoding, _connectionmode, _playbackviaapache, _analyticType) {
        var _this = this;
        this.useSecureConnection = false;
        this.doesStopRequested = false;
        this.isErrorMessageVisible = false;
        this.useJmuxer = false;
        this.connectionmode = "tcp";
        this.playbackviaapache = true;
        this.mjpeg_overhttpurl = "";
        this.OnVideoVisiblityChange = function (event) {
            if (_this.useJmuxer) {
                if (document.visibilityState == 'hidden') {
                    if (_this.jmuxer) {
                        _this.jmuxer = null;
                    }
                }
                else {
                    _this.Createjmuxerobject();
                }
            }
        };
        this.elId = elId;
        this.cameraId = cameraId;
        this.mode = mode;
        this.streamtype = streamtype;
        this.urlCreator = window.URL || window.webkitURL;
        this.startTime = startTime;
        this.useSecureConnection = useSecureConnection;
        this.connectionmode = _connectionmode;
        this.playbackviaapache = _playbackviaapache;
        this.analyticType = _analyticType;
        if (!this.connectionmode) {
            this.connectionmode = "";
        }
        else {
            this.connectionmode = this.connectionmode.toLowerCase();
        }
        if (this.connectionmode != "tcp" && this.connectionmode != "udp") {
            this.connectionmode = "";
        }
        if (mode == "Live") {
            if (!advanceDecoding) {
                this.useJmuxer = true;
            }
            else if (advanceDecoding == "0") {
                this.useJmuxer = true;
            }
            else {
                this.useJmuxer = false;
            }
        }
        else {
            this.useJmuxer = true;
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
            if (this.mjpeg_over_httptimeout) {
                clearTimeout(this.mjpeg_over_httptimeout);
            }
            try {
                if (this.w) {
                    this.w.close();
                }
            }
            catch (ex) {
            }
            delete this.m;
            delete this.v;
            if (this.isJpeg) {
                var i = document.getElementById(this.elId + "_img");
                if (i) {
                    i.src = "";
                    i.parentNode.removeChild(i);
                }
            }
            else {
                var v = document.getElementById(this.elId + "_video");
                if (v) {
                    v.src = "";
                    v.parentNode.removeChild(v);
                }
            }
        }
        catch (ex) {
        }
    };
    I2vPlayer.prototype.initializeMediaSource = function () {
        var _this = this;
        this.m = new MediaSource();
        var mime = 'video/mp4; codecs="avc1.4D0020"';
        if (!MediaSource.isTypeSupported(mime)) {
            return;
        }
        this.m.addEventListener('error', function (e) { console.log('error: ' + _this.m.readyState); });
        this.m.addEventListener('sourceopen', function (e) {
            console.log('sourceopen: ' + _this.m.readyState);
            try {
                _this.v.play();
            }
            catch (ex) {
                e = ex;
            }
            _this.b = _this.m.addSourceBuffer(mime);
            _this.b.mode = 'sequence';
            _this.b.addEventListener('updateend', function (e) {
                if (_this.b.updating) {
                    return;
                }
                if (_this.lastSegment) {
                    _this.b.appendBuffer(_this.lastSegment);
                    _this.lastSegment = null;
                }
                if (!_this.b.buffered.length) {
                    return;
                }
                var currentTime = _this.v.currentTime;
                var start = _this.b.buffered.start(0);
                var end = _this.b.buffered.end(0);
                var past = currentTime - start;
                if (past > 20 && currentTime < end && !_this.b.updating) {
                    _this.b.remove(start, currentTime - 4);
                }
            });
            if (!_this.b.updating && _this.m.readyState === 'open' && _this.intS) {
                _this.b.appendBuffer(_this.intS);
            }
            _this.isSourceReady = true;
        }, false);
    };
    I2vPlayer.prototype._arrayBufferToBase64 = function (b) {
        var binary = '';
        var bytes = new Uint8Array(b);
        var len = bytes.byteLength;
        for (var i = 0; i < len; i++) {
            binary += String.fromCharCode(bytes[i]);
        }
        return window.btoa(binary);
    };
    I2vPlayer.prototype.play = function () {
        var _this = this;
        var protocolType = "ws";
        var port = 8181;
        if (this.useSecureConnection) {
            protocolType = "wss";
            port = 8182;
        }
        if (!this.useJmuxer) {
            this.initializeMediaSource();
        }
        this.removeErrorMessage();
        this.showErrorMessage("Trying to Connect...");
        this.w = new WebSocket(protocolType + "://" + this.playerIp + ":" + port + "?cameraId~~" + this.cameraId + "&&id~~" + this.elId + "&&startTime~~" + this.startTime + "&&mode~~" + this.mode + "&&streamtype~~" + this.streamtype + "&&useJmuxer~~" + this.useJmuxer + "&&connectionmode~~" + this.connectionmode + "&&playbackviaapache~~" + this.playbackviaapache + "&&serverIp~~" + this.playerServerIp + "&&analyticType~~" + this.analyticType);
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
                if (!_this.isErrorMessageVisible) {
                    _this.showErrorMessage("Trying to Connect...");
                }
                delete _this.w;
                if (_this.b) {
                    _this.b.abort();
                }
                if (_this.jmuxer) {
                    _this.disposejmuxer();
                }
                _this.b = null;
                _this.m = null;
                delete _this.v;
                _this.isPlayerSet = false;
                if (_this.isJpeg) {
                    var i = document.getElementById(_this.elId + "_img");
                    if (i) {
                        i.src = "";
                        i.parentNode.removeChild(i);
                    }
                }
                else {
                    var v = document.getElementById(_this.elId + "_video");
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
        this.mjpeg_over_httptimeout = setInterval(function () {
            try {
                if (_this.mjpeg_overhttpurl != "") {
                    _this.i.setAttribute('src', _this.mjpeg_overhttpurl);
                }
            }
            catch (ex) {
            }
        }, 3000);
        this.w.addEventListener('message', function (e) {
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
                    if (_this.errorCallback) {
                        _this.errorCallback(errMsg);
                    }
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
                    var errMsg = _this.mode == "Live" ? "Url not configured" : "Recording not Found";
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
                    try {
                        if (_this.jmuxer) {
                            _this.disposejmuxer();
                        }
                        if (_this.b && _this.b.buffered && _this.b.buffered.length > 0) {
                            var start = _this.b.buffered.start(0);
                            var end = _this.b.buffered.end(0);
                            _this.b.remove(start, end);
                        }
                    }
                    catch (ex) {
                    }
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
                        _this.v.id = _this.elId + "_video";
                        _this.v.style.height = "100%";
                        _this.v.style.width = "100%";
                        _this.v.style.display = "inline";
                        _this.isJpeg = false;
                        _this.intS = null;
                        if (_this.useJmuxer) {
                            _this.v.autoplay = true;
                            if (document.addEventListener) {
                                document.addEventListener("visibilitychange", _this.OnVideoVisiblityChange);
                            }
                            _this.Createjmuxerobject();
                        }
                        else {
                            _this.v.src = window.URL.createObjectURL(_this.m);
                        }
                    }
                    else if (e.data === "mjpeg") {
                        _this.useJmuxer = false;
                        _this.removeErrorMessage();
                        _this.i = document.createElement("img");
                        var div = document.getElementById(_this.elId);
                        div.style.background = "black";
                        div.appendChild(_this.i);
                        _this.i.id = _this.elId + "_img";
                        _this.i.style.height = "100%";
                        _this.i.style.width = "100%";
                        _this.i.style.display = "inline";
                        _this.isJpeg = true;
                        _this.i.setAttribute('src', "http://" + _this.playerIp + ":4554/" + _this.elId);
                    }
                    else if (e.data.indexOf("mjpeg_overhttp") !== -1) {
                        _this.mjpeg_overhttpurl = e.data.substring(14);
                        _this.useJmuxer = false;
                        _this.removeErrorMessage();
                        _this.i = document.createElement("img");
                        var div = document.getElementById(_this.elId);
                        div.style.background = "black";
                        div.appendChild(_this.i);
                        _this.i.id = _this.elId + "_img";
                        _this.i.style.height = "100%";
                        _this.i.style.width = "100%";
                        _this.i.style.display = "inline";
                        _this.isJpeg = true;
                        _this.i.setAttribute('src', _this.mjpeg_overhttpurl);
                        _this.doesStopRequested = true;
                        try {
                            if (_this.w) {
                                _this.w.close();
                            }
                        }
                        catch (ex) {
                        }
                    }
                    _this.isPlayerSet = true;
                    return;
                }
            }
            if (_this.isJpeg) {
            }
            else {
                var data = new Uint8Array(e.data);
                if (_this.useJmuxer) {
                    if (!_this.jmuxer) {
                        _this.Createjmuxerobject();
                    }
                    if (_this.jmuxer && _this.jmuxer.mseReady) {
                        _this.jmuxer.feed({
                            video: data
                        });
                    }
                }
                else {
                    if (!_this.intS) {
                        _this.intS = data;
                    }
                    if (!_this.isSourceReady)
                        return;
                    if (_this.b && _this.b.buffered.length) {
                        var lag = _this.b.buffered.end(0) - _this.v.currentTime;
                        if (lag > 0.5) {
                            _this.v.currentTime = _this.b.buffered.end(0) - 0.5;
                        }
                    }
                    _this.lastSegment = data;
                    if (_this.b && (!_this.b.updating && _this.m.readyState === 'open')) {
                        try {
                            _this.b.appendBuffer(_this.lastSegment);
                        }
                        catch (ex) {
                            _this.isSourceReady = false;
                            _this.initializeMediaSource();
                            _this.v.src = null;
                            _this.v.src = window.URL.createObjectURL(_this.m);
                        }
                        _this.lastSegment = null;
                    }
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
                fps: 30
            });
        }
    };
    I2vPlayer.prototype.showErrorMessage = function (message) {
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
    I2vPlayer.prototype.SeekVideo = function (starttime) {
        if (this.w) {
            this.w.send("seek_Time" + starttime);
        }
    };
    I2vPlayer.prototype.Pause = function () {
        if (this.w) {
            this.w.send("Pause");
        }
    };
    I2vPlayer.prototype.Resume = function () {
        if (this.w) {
            this.w.send("Resume");
        }
    };
    return I2vPlayer;
}());
