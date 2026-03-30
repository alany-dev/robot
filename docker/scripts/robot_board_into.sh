#!/usr/bin/env bash
set -euo pipefail

CONTAINER_NAME="${ROBOT_CONTAINER_NAME:-robot}"

docker exec -it "${CONTAINER_NAME}" /bin/bash
