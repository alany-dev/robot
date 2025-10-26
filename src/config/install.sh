#!/bin/bash
#1.安装和挂载NFS
sudo apt update
sudo apt install -y nfs-common avahi-daemon

#2.配置avahi-daemon服务
#注意事项：一个局域网不能同时有多个orangepi.local设备
sudo sed -i 's/#host-name=foo/host-name=orangepi/' /etc/avahi/avahi-daemon.conf
sudo service avahi-daemon restart

#3.安装和配置ROS
#sudo sh -c 'echo "deb http://packages.ros.org/ros/ubuntu $(lsb_release -sc) main" > /etc/apt/sources.list.d/ros-latest.list' #官方源网速较慢
sudo sh -c 'echo "deb http://mirrors.ustc.edu.cn/ros/ubuntu $(lsb_release -sc) main" > /etc/apt/sources.list.d/ros-latest.list' #中科大源
sudo apt-key adv --keyserver 'hkp://keyserver.ubuntu.com:80' --recv-key C1CF6E31E6BADE8868B172B4F42ED6FBAB17C654
sudo apt update
#安装ros完整版，等待数分钟完成
sudo apt install -y ros-noetic-desktop-full

if ! grep -q "source /opt/ros/noetic/setup.bash" ~/.bashrc; then
	echo "source /opt/ros/noetic/setup.bash" >> ~/.bashrc
fi
if ! grep -q "HOST_IP=\$(hostname -I | awk '{print \$1}')" ~/.bashrc; then
	echo "HOST_IP=\$(hostname -I | awk '{print \$1}')" >> ~/.bashrc
fi
if ! grep -q "export ROS_IP=\$HOST_IP" ~/.bashrc; then
	echo "export ROS_IP=\$HOST_IP" >> ~/.bashrc
fi
if ! grep -q "export ROS_HOSTNAME=\$HOST_IP" ~/.bashrc; then
	echo "export ROS_HOSTNAME=\$HOST_IP" >> ~/.bashrc
fi
if ! grep -q "export ROS_MASTER_URI=http://\$HOST_IP:11311" ~/.bashrc; then
	echo "export ROS_MASTER_URI=http://\$HOST_IP:11311" >> ~/.bashrc
fi

if ! grep -q "export LIDAR_TYPE=LD14" ~/.bashrc; then
	echo "export LIDAR_TYPE=LD14" >> ~/.bashrc
fi


source ~/.bashrc

#4.安装常用的ROS包
sudo apt install -y ros-noetic-teleop-twist-keyboard ros-noetic-move-base-msgs ros-noetic-move-base ros-noetic-map-server ros-noetic-base-local-planner ros-noetic-dwa-local-planner ros-noetic-teb-local-planner ros-noetic-global-planner ros-noetic-gmapping ros-noetic-amcl libudev-dev

#5.安装 YDLidar库
# cd ~/robot/src/lidar_sensors/ydlidar/YDLidar-SDK
# mkdir build 
# cd build
# #cmake ..
# #make -j #注意：如果已经编译过了不用再编译，编译报错需要删除build文件夹和CMakeCache.txt
# sudo make install #编译过了只需安装，如果报错需要重新编译


#6.安装常用的python包
export PATH=$PATH:/home/orangepi/.local/bin #防止安装python包时候的WARNING: The script read_zbar is installed in '/home/orangepi/.local/bin' which is not on PATH.
sudo apt install -y python3-pip python3-websocket python3-pyaudio libsox-fmt-mp3 libatlas-base-dev espeak sox #后面几个是音频相关的依赖包
pip config set global.index-url https://pypi.mirrors.ustc.edu.cn/simple
pip install -U pip

pip install opencv-python 

pip install pulsectl
pip install pyttsx3
pip install pyzbar

pip install gpio
pip install python-periphery

pip install sounddevice
pip install httpx
pip install pycryptodome 
pip install pytz

pip3 install dashscope openai pyaudio flask
pip install websocket-client==1.8.0 websockets

#7.配置音频设备和音量
#这两个命令查看默认设备，前面有星号的代表默认：
pacmd list-sinks | grep -e 'index:' -e 'name:'
pacmd list-sources | grep -e 'index:' -e 'name:'
#这个命令配置在重启后可能会变化，加入启动脚本
sudo apt remove -y pulseaudio
sudo apt install -y pulseaudio
sudo apt remove -y pulseaudio
sudo apt install -y pulseaudio
pacmd list-sinks | grep -e 'index:' -e 'name:'
pacmd list-sources | grep -e 'index:' -e 'name:'
#这个命令配置在重启后可能会变化，加入启动脚本
pactl set-default-sink "alsa_output.platform-rk809-sound.stereo-fallback"
pactl set-default-source "alsa_input.usb-C-Media_Electronics_Inc._USB_PnP_Sound_Device-00.mono-fallback"
pacmd list-sinks | grep -e 'index:' -e 'name:'
pacmd list-sources | grep -e 'index:' -e 'name:'

#9.配置SPI3和UART2,9 pwm11,15使能，注意：UART2使能之后，串口调试功能会失效，只能通过网络或屏幕连接
if ! grep -q "overlays=spi3-m0-cs0-spidev uart2-m0 uart9-m2 pwm11-m1 pwm15-m1" /boot/orangepiEnv.txt; then
    sudo sh -c 'echo "overlays=spi3-m0-cs0-spidev uart2-m0 uart9-m2 pwm11-m1 pwm15-m1" >> /boot/orangepiEnv.txt'
fi
#配置完SPI和UART之后要重启生效
#不要直接拔电，可能造成文件拷贝不完整，要用命令行重启
reboot

