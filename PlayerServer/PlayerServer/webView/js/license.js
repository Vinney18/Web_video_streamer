
licStr = "";
function GenerateKey() {
    var loader = document.getElementById("loader");
    if (loader) {
        loader.style.display = "block";
    }
    var genratebutton = document.getElementById("generate");
    var activatebutton = document.getElementById("activate");
    genratebutton.disabled = true;
    activatebutton.disabled = true;
    var xhttp = new XMLHttpRequest();
    xhttp.onreadystatechange = function () {
        if (this.readyState === 4) {
            if (loader) {
                loader.style.display = "none";
            }
            genratebutton.disabled = false;
            activatebutton.disabled = false;
            if (this.status === 200) {
                downloadString(xhttp.responseText, "text", "device-key.txt");
            }
            else if (this.status === 0) {
                toastr.error("Server not responding");
            }
            else {
                toastr.error(this.responseText);
            }
        }
    };
    xhttp.open("GET", "/api/GenerateDeviceKey", true);
    xhttp.send();
}

function readLicFile(evt) {
    var fileReader = new FileReader();
    fileReader.onload = function (event) {
        licStr = event.target.result;
        document.getElementById("licStr").value = licStr;
        console.log(licStr);
    };
    fileReader.readAsText(event.target.files[0]);
    document.getElementById("dragLabel").style.display = "none";
    document.getElementById("fileName").innerHTML = event.target.files[0].name;
}

function downloadString(text, fileType, fileName) {
    var blob = new Blob([text], { type: fileType });

    var a = document.createElement('a');
    a.download = fileName;
    a.href = URL.createObjectURL(blob);
    a.dataset.downloadurl = [fileType, a.download, a.href].join(':');
    a.style.display = "none";
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    setTimeout(function () { URL.revokeObjectURL(a.href); }, 1500);
}

function dropHandler(ev) {

    // Prevent default behavior (Prevent file from being opened)
    ev.preventDefault();

    var fileReader = new FileReader();
    fileReader.onload = function (event) {
        licStr = event.target.result;
        document.getElementById("licStr").value = licStr;
        console.log(licStr);
    };
    fileReader.readAsText(event.dataTransfer.files[0]);
    document.getElementById("dragLabel").style.display = "none";
    document.getElementById("fileName").innerHTML = event.dataTransfer.files[0].name;

}

function dragOverHandler(ev) {
    console.log('File(s) in drop zone');

    // Prevent default behavior (Prevent file from being opened)
    ev.preventDefault();
}

function ActivateLicense() {
    if (!licStr || licStr === "") {
        toastr.error("Please upload a license file first !!");
        return;
    }
    var loader = document.getElementById("loader");
    if (loader) {
        loader.style.display = "block";
    }
    var genratebutton = document.getElementById("generate");
    var activatebutton = document.getElementById("activate");
    genratebutton.disabled = true;
    activatebutton.disabled = true;

    var xhttp = new XMLHttpRequest();
    xhttp.open("POST", "/api/License", true);
    xhttp.setRequestHeader("Content-Type", "application/json");
    xhttp.onreadystatechange = function () {
        if (this.readyState === 4) {
            if (loader) {
                loader.style.display = "none";
            }
            genratebutton.disabled = false;
            activatebutton.disabled = false;
            if (this.status === 200) {

                toastr.success("License applied successfully.");
                window.open(""+ window.location.protocol + "//" + window.location.host + "/index.html", "_self");

            }
            else if (this.status === 0) {
                toastr.error("Server not responding");
            }
            else {
                toastr.error(this.responseText);
            }
        }
    };
    var obj = { license_data: licStr };
    xhttp.send(JSON.stringify(obj));
}

function licenseKeyChange(event) {
    licStr = document.getElementById("licStr").value;
}

