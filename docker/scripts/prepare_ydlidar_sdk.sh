#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SRC_DIR="${YDLIDAR_SDK_SOURCE_DIR:-${ROOT_DIR}/src/ydlidar/YDLidar-SDK}"
TARGET_ROOT="${ROOT_DIR}/docker/build/install/ydlidar-sdk"
TARGET_DIR="${TARGET_ROOT}/YDLidar-SDK"

if [[ ! -d "${SRC_DIR}" ]]; then
  cat >&2 <<EOF
缺少 YDLidar SDK 源码目录，prepare 失败。

期望存在:
  - ${SRC_DIR}

当前 Docker 构建不会再使用 docker/build/install/ydlidar-sdk/YDLidar-SDK-1.2.7.tar.gz，
因为那个文件只是 Git LFS 指针，不是真实源码包。

如果你的仓库里没有 src/ydlidar/YDLidar-SDK，请先把这部分源码补齐。
EOF
  exit 1
fi

rm -rf "${TARGET_DIR}"
mkdir -p "${TARGET_ROOT}"
cp -a "${SRC_DIR}" "${TARGET_DIR}"

# 清掉明显的历史构建产物，避免污染容器内编译。
rm -rf "${TARGET_DIR}/build" \
       "${TARGET_DIR}/catkin_generated" \
       "${TARGET_DIR}/CMakeFiles" \
       "${TARGET_DIR}/atomic_configure"
rm -f "${TARGET_DIR}/CMakeCache.txt" \
      "${TARGET_DIR}/Makefile" \
      "${TARGET_DIR}/cmake_install.cmake" \
      "${TARGET_DIR}/CTestConfiguration.ini" \
      "${TARGET_DIR}/CTestCustom.cmake" \
      "${TARGET_DIR}/CTestTestfile.cmake"

echo "prepared YDLidar SDK payload in ${TARGET_DIR}"
