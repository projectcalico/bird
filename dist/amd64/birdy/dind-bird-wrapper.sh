#!/bin/sh

nohup /usr/local/bin/dockerd-entrypoint.sh &

/bin/sh /opt/bird-wrapper.sh
