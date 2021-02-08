#!/bin/bash
echo "creating release docker images ..."

# check if base image for compiling streamer exists 
if [[ "$(docker images -q streamer_build_base:latest 2> /dev/null)" == "" ]]; then
    echo ">>>>> streamer compile base image does not exist, building it"
    docker build -t streamer_compile_base -f ./streamer/Dockerfile.base .  
else
    echo ">>>>> stream compile base image already exists"
fi

echo ">>>>> building streamer release ..."
docker rmi i2v_player_streamer
docker build -t i2v_player_streamer -f ./Dockerfile.streamer.release .

# TODO: build player server release image
# echo "building release image ..."
# docker build -t pawanyadavi2v/i2v_analytic_server_gpu -f ./Dockerfile.release .

echo ">>>>> images built"