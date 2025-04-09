FROM ubuntu:20.04 as base


ENV TZ=Asia/Kolkata
ARG DEBIAN_FRONTEND=noninteractive
ENV TZ=Asia/Kolkata
RUN apt-get update && \
    apt-get install -y \
    build-essential \
    wget \
    git \
    curl \
    libavformat-dev libavcodec-dev libswresample-dev libswscale-dev libavutil-dev libavdevice-dev libavfilter-dev libpostproc-dev libwebsocketpp-dev \
    --fix-missing

RUN wget https://github.com/Kitware/CMake/releases/download/v3.19.0-rc1/cmake-3.19.0-rc1-Linux-x86_64.sh \
    && chmod +x cmake-3.19.0-rc1-Linux-x86_64.sh \
    && ./cmake-3.19.0-rc1-Linux-x86_64.sh --prefix=/usr/local --skip-license \
    && rm cmake-3.19.0-rc1-Linux-x86_64.sh

RUN apt-get install -y zip
RUN apt-get install -y linux-libc-dev pkg-config
# Clone and bootstrap vcpkg
RUN git clone https://github.com/microsoft/vcpkg.git && \
    cd vcpkg && \
    ./bootstrap-vcpkg.sh

# Install vcpkg packages
RUN cd vcpkg && \
    ./vcpkg install boost-filesystem boost-regex boost-system boost-thread boost-dll poco websocketpp cpr jsoncpp cryptopp fmt spdlog zlib openssl
