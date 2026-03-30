#!/usr/bin/env bash

#当脚本中指令报错后，会直接退出
set -e

#BASH_SOURCE[0] ，对脚本执行的地方，所在路径，/tmp/install/ydlidar-sdk/install_ydlidar_sdk.sh
# dirname，/tmp/install/ydlidar-sdk/
# $(）/tmp/install/ydlidar-sdk/

#cd /tmp/install/ydlidar-sdk/
cd "$(dirname "${BASH_SOURCE[0]}")"

#编译核数
THREAD_NUM=$(nproc)

#版本号
VERSION="1.2.7"

#版本-源码压缩包
PKG_NAME="YDLidar-SDK-${VERSION}.tar.gz"
SRC_DIR="YDLidar-SDK"
EXTRACTED_DIR="YDLidar-SDK-${VERSION}"

if [[ -d "${SRC_DIR}" ]]; then
    WORK_DIR="${SRC_DIR}"
elif [[ -f "${PKG_NAME}" ]]; then
    tar xzf "${PKG_NAME}"
    WORK_DIR="${EXTRACTED_DIR}"
else
    echo "missing YDLidar SDK source: ${SRC_DIR} or ${PKG_NAME}" >&2
    exit 1
fi

#进入代码目录
pushd "${WORK_DIR}"

    #编译代码
    cmake -S . -B build
    cmake --build build -j"${THREAD_NUM}"

    #最终成果安装，头文件和动态库复制到系统目录
    cmake --install build
popd

#刷新动态链接库缓存，让linux系统知道有这个动态库
ldconfig

# Clean up
if [[ "${WORK_DIR}" == "${EXTRACTED_DIR}" ]]; then
    rm -rf "${PKG_NAME}" "${EXTRACTED_DIR}"
fi
