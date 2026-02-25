#!/bin/bash
echo "creating release docker images ..."

# check if base image for compiling streamer exists 
if [[ "$(docker images -q streamer_build_base:latest 2> /dev/null)" == "" ]]; then
    echo ">>>>> streamer compile base image does not exist, building it"
    docker build -t streamer_build_base -f ./Dockerfile.streamer.base .  
else
    echo ">>>>> streamer compile base image already exists"
fi

echo ">>>>> building streamer release ..."
docker build -t patwalvineet/player_streamer:2.0 -f ./Dockerfile.streamer.release .

# TODO: build player server release image
# echo ">>>>> building player server image ..."
# docker build -t patwalvineet/player_server:2.0 -f ./Dockerfile.playerServer.release .

echo ">>>>> images built"