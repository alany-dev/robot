#!/usr/bin/env bash

MONITOR_HOME_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )/../.." && pwd )"

display=""
if [ -z ${DISPLAY} ];then
    display=":1"
else
    display="${DISPLAY}"
fi

local_host="$(hostname)"
user="${USER}"
uid="$(id -u)"
group="$(id -g -n)"
gid="$(id -g)"

ros_ip=$(hostname -I | awk '{print $1}')
if [ -z "$ros_ip" ]; then
    ros_ip="127.0.0.1"
fi
ros_master_uri="http://localhost:11311"

echo "stop and rm docker" 
docker stop robot > /dev/null
docker rm -v -f robot > /dev/null

echo "start docker"
echo "ROS_MASTER_URI=${ros_master_uri}"
echo "ROS_IP=${ros_ip}"

docker run -it -d \
--privileged=true \
--name robot \
-e DISPLAY=$display \
-e DOCKER_USER="${user}" \
-e USER="${user}" \
-e DOCKER_USER_ID="${uid}" \
-e DOCKER_GRP="${group}" \
-e DOCKER_GRP_ID="${gid}" \
-e XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR \
-e ROS_MASTER_URI="${ros_master_uri}" \
-e ROS_IP="${ros_ip}" \
-v ${MONITOR_HOME_DIR}:/robot \
-v ${XDG_RUNTIME_DIR}:${XDG_RUNTIME_DIR} \
--network host \
robot:v2.0
