ARG BASE_IMAGE=ubuntu:20.04
FROM ${BASE_IMAGE}

ENV DEBIAN_FRONTEND=noninteractive \
    LANG=C.UTF-8 \
    LC_ALL=C.UTF-8 \
    PYTHONUNBUFFERED=1

SHELL ["/bin/bash", "-lc"]

COPY apt/sources.list /etc/apt/sources.list

RUN apt-get clean && \
    apt-get autoclean || true

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
      apt-utils \
      alsa-utils \
      binutils \
      bash-completion \
      build-essential \
      ca-certificates \
      cmake \
      curl \
      dbus \
      espeak \
      ffmpeg \
      gdb \
      git \
      gnupg \
      gnupg1 \
      gnupg2 \
      htop \
      iproute2 \
      iputils-ping \
      iw \
      libasound2-dev \
      libatlas-base-dev \
      libdrm2 \
      libdrm-dev \
      libopencv-dev \
      libportaudio2 \
      libportaudiocpp0 \
      libsox-fmt-mp3 \
      libudev-dev \
      libzbar0 \
      libzmq3-dev \
      lsb-release \
      net-tools \
      network-manager \
      ocl-icd-libopencl1 \
      pciutils \
      portaudio19-dev \
      pulseaudio \
      pulseaudio-utils \
      pkg-config \
      python3 \
      python3-dev \
      python3-genmsg \
      python3-pip \
      sox \
      sudo \
      udev \
      usbutils \
      vim \
      wireless-tools \
      && rm -rf /var/lib/apt/lists/*

RUN sh -c 'echo "deb http://mirrors.ustc.edu.cn/ros/ubuntu $(lsb_release -sc) main" > /etc/apt/sources.list.d/ros-latest.list'
RUN apt-key adv --keyserver 'hkp://keyserver.ubuntu.com:80' --recv-key C1CF6E31E6BADE8868B172B4F42ED6FBAB17C654

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
      python3-opencv \
      python3-pyaudio \
      python3-rosdep \
      python3-rosinstall \
      python3-rosinstall-generator \
      python3-websocket \
      python3-wstool \
      ros-noetic-amcl \
      ros-noetic-base-local-planner \
      ros-noetic-cv-bridge \
      ros-noetic-desktop-full \
      ros-noetic-dwa-local-planner \
      ros-noetic-global-planner \
      ros-noetic-gmapping \
      ros-noetic-image-transport \
      ros-noetic-map-server \
      ros-noetic-move-base \
      ros-noetic-move-base-msgs \
      ros-noetic-teb-local-planner \
      ros-noetic-teleop-twist-keyboard \
      && rm -rf /var/lib/apt/lists/*

COPY install/agent_requirements.txt /tmp/install/agent_requirements.txt
RUN python3 -m pip install --no-cache-dir --upgrade pip && \
    python3 -m pip install --no-cache-dir -r /tmp/install/agent_requirements.txt

COPY install/ydlidar-sdk /tmp/install/ydlidar-sdk
RUN /tmp/install/ydlidar-sdk/install_ydlidar_sdk.sh

COPY vendor/rk/lib/ /usr/local/lib/
RUN ldconfig

COPY runtime/robot_board_entrypoint.sh /opt/robot/robot_board_entrypoint.sh
RUN chmod +x /opt/robot/robot_board_entrypoint.sh && \
    mkdir -p /home/orangepi/robot /home/orangepi/.ros /opt/robot /var/log/robot

RUN echo "source /opt/ros/noetic/setup.bash" >> /root/.bashrc
RUN echo "source /home/orangepi/robot/devel/setup.bash" >> /root/.bashrc

WORKDIR /home/orangepi/robot
ENTRYPOINT ["/opt/robot/robot_board_entrypoint.sh"]
