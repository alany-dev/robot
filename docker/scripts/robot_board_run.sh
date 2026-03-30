#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

export ROBOT_BASE_IMAGE="${ROBOT_BASE_IMAGE:-ubuntu:20.04}"
export ROBOT_IMAGE_TAG="${ROBOT_IMAGE_TAG:-robot:board}"
export ROBOT_CONTAINER_NAME="${ROBOT_CONTAINER_NAME:-robot}"
export DISPLAY="${DISPLAY:-:0}"
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"

HOST_IP="$(hostname -I | tr ' ' '\n' | awk '/^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$/ {print; exit}')"
if [[ -z "${HOST_IP}" ]]; then
  HOST_IP="127.0.0.1"
fi

export ROS_IP="${ROS_IP:-${HOST_IP}}"
export ROS_HOSTNAME="${ROS_HOSTNAME:-${ROS_IP}}"
export ROS_MASTER_URI="${ROS_MASTER_URI:-http://${ROS_IP}:11311}"

echo "Preparing RKNN vendor payload..."
bash "${ROOT_DIR}/docker/scripts/prepare_rk_vendor.sh"

echo "Preparing YDLidar SDK payload..."
bash "${ROOT_DIR}/docker/scripts/prepare_ydlidar_sdk.sh"

echo "ROS_MASTER_URI=${ROS_MASTER_URI}"
echo "ROS_IP=${ROS_IP}"

docker compose -f "${ROOT_DIR}/docker/docker-compose.orangepi.yml" down --remove-orphans >/dev/null 2>&1 || true
docker compose -f "${ROOT_DIR}/docker/docker-compose.orangepi.yml" up -d --build --force-recreate
