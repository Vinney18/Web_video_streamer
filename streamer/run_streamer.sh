#!/bin/sh
echo "starting streamer executable"
cd /opt/i2v-analytic-manager/streamer/
LD_LIBRARY_PATH=/usr/local/lib/:/opt/i2v-analytic-manager/streamer/libs ./streamer
