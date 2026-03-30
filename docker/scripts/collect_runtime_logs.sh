#!/usr/bin/env bash
set -euo pipefail

CONTAINER_NAME="${1:-${ROBOT_CONTAINER_NAME:-robot}}"

docker exec "${CONTAINER_NAME}" \
  python3 /home/orangepi/robot/docker/scripts/collect_runtime_logs.py --inside-container "${CONTAINER_NAME}"
