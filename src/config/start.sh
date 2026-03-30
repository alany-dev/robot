LOG_ROOT="${ROBOT_LOG_DIR:-/home/orangepi/robot/logs}"
PROCESS_LOG_DIR="${ROBOT_PROCESS_LOG_DIR:-${LOG_ROOT}/processes}"
ROS_HOME_DIR="${ROBOT_ROS_HOME:-${LOG_ROOT}/ros_home}"
ROS_LOG_DIR="${ROBOT_ROS_LOG_DIR:-${LOG_ROOT}/ros}"

mkdir -p "$LOG_ROOT" "$PROCESS_LOG_DIR" "$ROS_HOME_DIR" "$ROS_LOG_DIR"
export ROS_HOME="$ROS_HOME_DIR"
export ROS_LOG_DIR="$ROS_LOG_DIR"

# 检查是否保存有WIFI连接
wifi_ssid=$(nmcli connection show | grep wifi | awk '{print $1}')
if [ -z "$wifi_ssid" ]; then
    echo "系统没有存储任何WIFI密码"
    #host_ip=$(hostname -I | awk '{print $1}')
    host_ip=$(hostname -I | awk '{print $1}' | grep -E '^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+') #只采用IPv4地址
else
    echo "系统中保存有WIFI密码"
    # 尝试获取IP地址15秒
    for cnt in $(seq 1 15); do
        #host_ip=$(hostname -I | awk '{print $1}')
        host_ip=$(hostname -I | awk '{print $1}' | grep -E '^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+') #只采用IPv4地址
        if [ -z "$host_ip" ]; then
            echo "尝试第$cnt次获取IP地址失败，等待1秒后重试"  
            sleep 1
        else
            echo "IP地址已成功获取: $host_ip"
            break
        fi
    done
fi

pactl set-default-sink "alsa_output.platform-rk809-sound.stereo-fallback"

# 如果IP地址为空，则开启AP模式
if [ -z "$host_ip" ]; then
    echo "开启AP模式..."
    echo "orangepi" | sudo -S create_ap --no-virt -m nat wlan0 eth0 orangepi orangepi &
    host_ip="192.168.12.1"  #AP模式的默认IP地址是192.168.12.1  
    echo "AP模式已开启，IP地址: $host_ip"
    
    # 在AP模式下启动WiFi配置Web服务
    echo "启动WiFi配置Web服务..."
    cd /home/orangepi/robot && nohup bash -c "source /opt/ros/noetic/setup.sh && source /home/orangepi/robot/devel/setup.sh && sleep 2  && python3 web_controller.py --ap-mode" > "${PROCESS_LOG_DIR}/web_controller.log" 2>&1 &
    sleep 3  # 等待web服务启动
    echo "WiFi配置服务已启动，访问地址: http://192.168.12.1:5000/wifi_config"
    ffplay -nodisp -autoexit  /home/orangepi/robot/src/config/sound/ap.mp3 &
    
    # 等待用户配置WiFi，不继续执行ROS启动
    echo "等待用户配置WiFi网络..."
    echo "请在浏览器中访问: http://192.168.12.1:5000/wifi_config"
    echo "配置完成后，系统将自动切换到WiFi"
    exit 0
fi


export ROS_IP=$host_ip
export ROS_HOSTNAME=$host_ip
export ROS_MASTER_URI=http://$host_ip:11311

source /opt/ros/noetic/setup.sh
source /home/orangepi/robot/devel/setup.sh
sleep 2 #等待3秒，防止网络不稳定引起的ROS启动错误



#设置默认的麦克风设备为USB麦克风
pactl set-default-source "alsa_input.usb-C-Media_Electronics_Inc._USB_PnP_Sound_Device-00.mono-fallback"
#麦克风接收增益调到100%，范围0~16
amixer -c 2 sset Mic 16

roscore > "${PROCESS_LOG_DIR}/roscore.log" 2>&1 &
sleep 3
#启动all.launch
cd /home/orangepi/robot && nohup bash -c "source /opt/ros/noetic/setup.sh && source /home/orangepi/robot/devel/setup.sh && sleep 3  && python3 web_controller.py" > "${PROCESS_LOG_DIR}/web_controller.log" 2>&1 &
sleep 1  # 等待web服务启动
echo "Web控制服务已启动，访问地址: http://$host_ip:5000"

# 启动agent进程，设置为普通用户最高优先级
echo "启动agent进程，设置为普通用户最高优先级..."
nice -n 0  bash -iec "source /opt/ros/noetic/setup.sh && source /home/orangepi/robot/devel/setup.sh && sleep 3 && /home/orangepi/robot/src/agent/start_agent.sh" \
        > "${PROCESS_LOG_DIR}/agent.log" 2>&1 &


sleep 1

cd /home/orangepi/robot
  taskset -c 1,2 nice -n 15   bash -iec "source /opt/ros/noetic/setup.sh && source /home/orangepi/robot/devel/setup.sh && sleep 3 && roslaunch base_control base_control.launch" > "${PROCESS_LOG_DIR}/base_control.log" 2>&1 &

# ROS launch已移至Web界面控制，不再自动启动
# 可通过Web界面手动启动：/api/start_ros_launch
# 可通过Web界面手动停止：/api/stop_ros_launch






