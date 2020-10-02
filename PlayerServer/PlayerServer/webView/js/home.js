function saveConfiguration() {
    var ip = document.getElementById("ip").value;
    var port = parseInt(document.getElementById("port").value);
    var token = document.getElementById("token").value;
    if ((!ip || ip === "") || (!port)) {
        toastr.error("Please fill the details properly");
        return;
    }
    var obj = {
        serverIp: ip,
        port: port,
        token: token
    };
    var xhttp = new XMLHttpRequest();
    xhttp.onreadystatechange = function () {
        if (this.readyState === 4) {
            if (this.status === 200) {
                toastr.success("Configuration Saved Successfully");
            }
            else if (this.status === 0) {
                toastr.error("Server not responding");
            }
            else {
                toastr.error(this.responseText);
            }
        }
    };
    xhttp.open("POST", "/api/Configuration", true);
    xhttp.setRequestHeader("Content-Type", "application/json");
    xhttp.send(JSON.stringify(obj));
}


function saveNetworkSetting() {
    var isVPN = document.getElementById("isVPN").checked;
    var returnIp = document.getElementById("returnIp").value;
    if (!returnIp || returnIp === "") {
        toastr.error("Please fill the details properly");
        return;
    }
    var obj = {
        isVPN: isVPN,
        returnIp: returnIp
    };
    var xhttp = new XMLHttpRequest();
    xhttp.onreadystatechange = function () {
        if (this.readyState === 4) {
            if (this.status === 200) {
                toastr.success("Network Setting Saved Successfully");
            }
            else if (this.status === 0) {
                toastr.error("Server not responding");
            }
            else {
                toastr.error(this.responseText);
            }
        }
    };
    xhttp.open("POST", "/api/NetworkSetting", true);
    xhttp.setRequestHeader("Content-Type", "application/json");
    xhttp.send(JSON.stringify(obj));
}