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

#解压   
tar xzf "${PKG_NAME}"

#进入代码目录
pushd YDLidar-SDK-${VERSION}

    #编译代码
    mkdir build && cd build
    cmake .. 
    make -j$(nproc)

    #最终成果安装，头文件和动态库复制到系统目录
    make install
popd

#刷新动态链接库缓存，让linux系统知道有这个动态库
ldconfig

# Clean up
rm -rf "${PKG_NAME}" YDLidar-SDK-${VERSION}