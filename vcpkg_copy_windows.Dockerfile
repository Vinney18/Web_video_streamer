# Use the minimal Microsoft Nano Server image
FROM mcr.microsoft.com/windows/nanoserver:ltsc2019

# Set the working directory
WORKDIR C:\\app

# Install CMake
SHELL ["CMD", "-Command", "$ErrorActionPreference = 'Stop'; $ProgressPreference = 'SilentlyContinue';"]
RUN echo "Hello World"
# https://github.com/Kitware/CMake/releases/download/v3.28.1/cmake-3.28.1-windows-arm64.zip
RUN Invoke-WebRequest -Uri https://github.com/Kitware/CMake/releases/download/v3.28.1/cmake-3.28.1-windows-arm64.zip -OutFile cmake.zip; \
    dir \
    Expand-Archive -Path .\cmake.zip -DestinationPath C:\cmake -Force; \
    Remove-Item -Force cmake.zip

# Add CMake to the PATH
RUN setx /M PATH $('C:\app\cmake\bin\;' + $env:PATH)
RUN cmake --version
# Copy vcpkg into the image
COPY vcpkg C:/app/vcpkg

# Set vcpkg environment variable
ENV VCPKG_ROOT C:/app/vcpkg

