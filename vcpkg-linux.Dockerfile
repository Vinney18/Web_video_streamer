FROM ubuntu:20.04 as base

# Install necessary dependencies
RUN apt-get update && \
    apt-get install -y \
    build-essential \
    cmake \
    git \
    libavformat-dev libavcodec-dev libswresample-dev libswscale-dev libavutil-dev libavdevice-dev libavfilter-dev libpostproc-dev \
    --fix-missing

# Clone and bootstrap vcpkg
RUN git clone https://github.com/microsoft/vcpkg.git && \
    cd vcpkg && \
    ./bootstrap-vcpkg.sh

# Install vcpkg packages
RUN cd vcpkg && \
    ./vcpkg install boost-filesystem boost-regex boost-system boost-thread boost-dll poco websocketpp cpr jsoncpp cryptopp fmt spdlog zlib openssl
