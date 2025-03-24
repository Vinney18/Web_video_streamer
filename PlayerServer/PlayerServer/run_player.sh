#!/bin/sh
echo "starting webcacapp executable"
cd /opt/i2v-analytic-manager/player/
LD_LIBRARY_PATH=/usr/local/lib/:/opt/i2v-analytic-manager/player/ ./PlayerServer
