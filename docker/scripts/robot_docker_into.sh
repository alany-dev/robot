#!/usr/bin/env bash

xhost +local:root 1>/dev/null 2>&1
docker exec \
    -u root \
    -it robot \
    /bin/bash
xhost -local:root 1>/dev/null 2>&1
