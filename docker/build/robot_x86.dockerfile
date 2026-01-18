FROM ubuntu:20.04
ENV DEBIAN_FRONTEND=noninteractive
COPY apt/sources.list /etc/apt/

RUN apt-get clean && \
    apt-get autoclean
RUN apt update && \ 
    apt install  -y \
    curl \
    lsb-release \
    gnupg gnupg1 gnupg2 \
    gdb \
    vim \
    htop \
    apt-utils \
    curl \
    cmake \
    net-tools \
    python3 python3-pip   
# 添加ROS源
RUN sh -c 'echo "deb http://mirrors.ustc.edu.cn/ros/ubuntu $(lsb_release -sc) main" > /etc/apt/sources.list.d/ros-latest.list' #中科大源
RUN apt-key adv --keyserver 'hkp://keyserver.ubuntu.com:80' --recv-key C1CF6E31E6BADE8868B172B4F42ED6FBAB17C654

RUN apt update && \
    apt install -y  ros-noetic-desktop-full \
     python3-rosdep python3-rosinstall python3-rosinstall-generator python3-wstool build-essential \
     ros-noetic-teleop-twist-keyboard ros-noetic-move-base-msgs ros-noetic-move-base ros-noetic-map-server ros-noetic-base-local-planner ros-noetic-dwa-local-planner ros-noetic-teb-local-planner ros-noetic-global-planner ros-noetic-gmapping ros-noetic-amcl libudev-dev

RUN echo "source /opt/ros/noetic/setup.bash" >> ~/.bashrc


    

# RUN pip config set global.index-url https://pypi.mirrors.ustc.edu.cn/simple
# RUN pip install --upgrade pip
# RUN pip install \
#     opencv-python  \ 
#     pulsectl \
#     pyttsx3 \
#     pyzbar \
#     gpio \
#     python-periphery \
#     sounddevice \
#     httpx \
#     pycryptodome  \
#     pytz \
#     dashscope openai pyaudio flask \          
#     websocket-client==1.8.0 websockets \     


COPY install/ydlidar-sdk /tmp/install/ydlidar-sdk
RUN /tmp/install/ydlidar-sdk/install_ydlidar_sdk.sh

RUN apt install -y libportaudio2 libportaudiocpp0 portaudio19-dev

COPY install/offline_agent /tmp/

RUN echo "source /robot/devel/setup.bash" >> ~/.bashrc

WORKDIR /robot
 
