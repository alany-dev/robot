#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
IMAGE_TAG="${ROBOT_IMAGE_TAG:-robot:board}"
BASE_IMAGE="${ROBOT_BASE_IMAGE:-ubuntu:20.04}"

echo "Preparing RKNN vendor payload..."
bash "${ROOT_DIR}/docker/scripts/prepare_rk_vendor.sh"

echo "Preparing YDLidar SDK payload..."
bash "${ROOT_DIR}/docker/scripts/prepare_ydlidar_sdk.sh"

echo "Building image ${IMAGE_TAG}"
DOCKER_BUILDKIT="${DOCKER_BUILDKIT:-1}" docker build \
  --build-arg BASE_IMAGE="${BASE_IMAGE}" \
  -t "${IMAGE_TAG}" \
  -f "${ROOT_DIR}/docker/build/robot_board.dockerfile" \
  "${ROOT_DIR}/docker/build"
