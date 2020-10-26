var I2vSdk = (function () {
    function I2vSdk() {
        this.playerIp = "localhost";
        this.useSecureConnection = false;
    }
    I2vSdk.prototype.InitPlayer = function (serverIP, serverType, successCallback, errorCallback, _playerIp, useSecureConnection) {
        if (_playerIp) {
            this.playerIp = _playerIp;
        }
        if (useSecureConnection) {
            this.useSecureConnection = useSecureConnection;
        }
        var protocolType = "ws";
        var port = 8181;
        if (this.useSecureConnection) {
            protocolType = "wss";
            port = 8182;
        }
        var wc = new WebSocket(protocolType + "://" + this.playerIp + ":" + port + "?serverIp~~" + serverIP);
        wc.onmessage = function (e) {
            if (e.data == "Ok" || e.data == "Init") {
                successCallback();
            }
            else {
                errorCallback(e.data);
                console.error(e.data);
            }
            wc.close();
        };
        wc.onerror = function (e) {
            var errMsg = "Not able to connect to player.";
            console.error(errMsg);
            errorCallback(errMsg);
            wc.close();
        };
    };
    I2vSdk.prototype.GetPlayer = function (elId, cameraId, mode, streamtype, useTranscoding, ctrlInputRate, startTime) {
        this.player = new I2vPlayer(elId, cameraId, mode, streamtype, useTranscoding, ctrlInputRate, startTime, this.useSecureConnection);
        this.player.playerIp = this.playerIp;
        return this.player;
    };
    return I2vSdk;
}());
var I2vPlayer = (function () {
    function I2vPlayer(elId, cameraId, mode, streamtype, useTranscoding, ctrlInputRate, startTime, useSecureConnection) {
        this.useSecureConnection = false;
        this.doesStopRequested = false;
        this.elId = elId;
        this.cameraId = cameraId;
        this.mode = mode;
        this.streamtype = streamtype;
        this.useT = useTranscoding;
        this.urlCreator = window.URL || window.webkitURL;
        this.startTime = startTime;
        this.ctrlInputRate = ctrlInputRate;
        this.useSecureConnection = useSecureConnection;
    }
    I2vPlayer.prototype.setErrorCallback = function (errorCallback) {
        this.errorCallback = errorCallback;
    };
    I2vPlayer.prototype.setRetryingCallback = function (retryingCallback) {
        this.retryingCallback = retryingCallback;
    };
    I2vPlayer.prototype.stop = function () {
        this.doesStopRequested = true;
        this.w.close();
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
        this.initializeMediaSource();
        this.w = new WebSocket(protocolType + "://" + this.playerIp + ":" + port + "?cameraId~~" + this.cameraId + "&&id~~" + this.elId + "&&useTranscoding~~" + this.useT + "&&startTime~~" + this.startTime + "&&mode~~" + this.mode + "&&streamtype~~" + this.streamtype + "&&ctrlInputRate~~" + this.ctrlInputRate);
        this.w.binaryType = 'arraybuffer';
        this.w.addEventListener('open', function (event) {
            _this.doesStopRequested = false;
            _this.w.send('Hello Server!');
        });
        this.w.addEventListener('close', function (event) {
            if (_this.doesStopRequested) {
                console.log('socket closed');
            }
            else {
                console.log('socket closed and retrying...');
                if (_this.isPlayerSet) {
                    _this.showErrorMessage("trying to reconnect...");
                }
                delete _this.w;
                if (_this.b) {
                    _this.b.abort();
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
                setTimeout(function () {
                    _this.play();
                }, 1000);
            }
        });
        this.w.addEventListener('message', function (e) {
            switch (e.data) {
                case "Init":
                    var errMsg = "Player is not initialized. Please call InitPlayer() first!!";
                    if (_this.errorCallback) {
                        _this.errorCallback(errMsg);
                    }
                    _this.showErrorMessage(errMsg);
                    return;
                case "EmptyUrl":
                    var errMsg = _this.mode == "Live" ? "Url not configured" : "Recording not found";
                    if (_this.errorCallback) {
                        _this.errorCallback(errMsg);
                    }
                    _this.showErrorMessage(errMsg);
                    return;
                case "retrying":
                    if (_this.retryingCallback) {
                        _this.retryingCallback();
                    }
                    _this.showErrorMessage("trying to reconnect...");
                    return;
                case "License Expired":
                    var errMsg = "License Expired/Invalid";
                    if (_this.errorCallback) {
                        _this.errorCallback(errMsg);
                    }
                    _this.showErrorMessage(errMsg);
                    return;
                case "Some problem occured":
                    var errMsg = "Some problem occured";
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
                        _this.v.src = window.URL.createObjectURL(_this.m);
                        _this.v.style.height = "100%";
                        _this.v.style.width = "100%";
                        _this.v.style.display = "inline";
                        _this.isJpeg = false;
                        _this.intS = null;
                    }
                    else if (e.data === "mjpeg") {
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
                    _this.isPlayerSet = true;
                    return;
                }
            }
            if (_this.isJpeg) {
            }
            else {
                var data = new Uint8Array(e.data);
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
        }, false);
    };
    I2vPlayer.prototype.showErrorMessage = function (message) {
        var spanElement = document.getElementById("errorMessage" + this.elId);
        if (!spanElement) {
            var span = document.createElement("span");
            span.innerHTML = message + "...";
            span.style.color = "red";
            span.style.position = "absolute";
            span.style.fontSize = "25px";
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
        var spanElement = document.getElementById("errorMessage" + this.elId);
        if (spanElement) {
            var element = document.getElementById(this.elId);
            element.removeChild(spanElement);
        }
    };
    return I2vPlayer;
}());
