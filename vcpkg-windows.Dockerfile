# Use the official Microsoft Windows Server Core image
FROM mcr.microsoft.com/windows/servercore:ltsc2022

# Set the working directory
WORKDIR C:\\app

# RUN powershell.exe -command Set-ExecutionPolicy Bypass -Scope Process -Force; iex ((New-Object System.Net.WebClient).DownloadString('https://community.chocolatey.org/install.ps1'))
RUN @"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -InputFormat None -ExecutionPolicy Bypass -Command "iex ((New-Object System.Net.WebClient).DownloadString('https://chocolatey.org/install.ps1'))" && SET "PATH=%PATH%;%ALLUSERSPROFILE%\chocolatey\bin"
RUN choco -v
RUN choco install cmake visualstudio2022community visualstudio2022-workload-nativedesktop visualstudio2022buildtools -y
RUN choco install git.install -y --no-progress
RUN refreshenv
# set git and cmake path
ENV PATH="C:\Windows\System32\WindowsPowerShell\v1.0;%ALLUSERSPROFILE%\chocolatey\bin;C:\Program Files\CMake\bin\;C:\Program Files\Git\bin\;%PATH%"
RUN git --version
RUN cmake --version
# Install vcpkg from a specific commit
RUN git clone https://github.com/microsoft/vcpkg.git
RUN cd vcpkg && dir && bootstrap-vcpkg.bat \
    && vcpkg integrate install \
    && vcpkg install boost-filesystem boost-regex boost-system boost-thread boost-dll poco websocketpp cpr jsoncpp cryptopp fmt spdlog zlib openssl
