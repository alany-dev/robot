#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
小沫Web控制程序
使用Flask创建Web界面，通过ROS控制小沫移动
"""

import rospy
import json
import subprocess
import os
import signal
import sys
import atexit
from flask import Flask, render_template, request, jsonify
from geometry_msgs.msg import Twist
from std_msgs.msg import String, Bool
import threading
import time


app = Flask(__name__)

class RobotController:
    def __init__(self, ap_mode=False):
        """初始化ROS节点和发布者"""
        self.ap_mode = ap_mode
        self.ros_initialized = False
        
        if not ap_mode:
            try:
                rospy.init_node('web_robot_controller', anonymous=True)
                
                # 创建发布者，发布速度命令
                self.cmd_vel_pub = rospy.Publisher('/cmd_vel', Twist, queue_size=1)
                
                # 创建状态发布者
                self.status_pub = rospy.Publisher('/robot_status', String, queue_size=1)
                
                # 创建头部控制发布者
                self.head_pub = rospy.Publisher('/head_control', String, queue_size=1)
                
                # 创建系统控制发布者
                self.enable_tracking_pub = rospy.Publisher('/enable_tracking', Bool, queue_size=1)
                self.enable_lidar_pub = rospy.Publisher('/enable_lidar', Bool, queue_size=1)
                
                # 创建头部控制订阅者，用于接收来自agent模块的头部控制命令
                self.head_sub = rospy.Subscriber('/web_head_control', String, self.head_control_callback)
                
                self.ros_initialized = True
                print("ROS节点初始化完成")
            except Exception as e:
                print(f"ROS初始化失败: {e}")
                print("在AP模式下运行，跳过ROS初始化")
                self.ros_initialized = False
        
        # 小车状态
        self.is_moving = False
        self.current_speed = 0.0
        self.current_angular = 0.0
        
        # 头部状态
        self.head_position = "center"  # center, left, right, up, down
        
        # 速度参数
        self.linear_speed = 0.3  # 线速度 m/s
        self.angular_speed = 0.5  # 角速度 rad/s
        
        # 创建Twist消息
        self.twist = Twist()
        
        if ap_mode:
            print("AP模式机器人控制器初始化完成（无ROS）")
        else:
            print("机器人控制器初始化完成")
    
    def stop_robot(self):
        """停止机器人"""
        self.twist.linear.x = 0.0
        self.twist.angular.z = 0.0
        if self.ros_initialized:
            self.cmd_vel_pub.publish(self.twist)
        self.is_moving = False
        self.current_speed = 0.0
        self.current_angular = 0.0
        self.publish_status("停止")
        print("机器人已停止")
    
    def move_forward(self):
        """前进"""
        self.twist.linear.x = self.linear_speed
        self.twist.angular.z = 0.0
        if self.ros_initialized:
            self.cmd_vel_pub.publish(self.twist)
        self.is_moving = True
        self.current_speed = self.linear_speed
        self.current_angular = 0.0
        self.publish_status("前进")
        print("机器人前进")
    
    def move_backward(self):
        """后退"""
        self.twist.linear.x = -self.linear_speed
        self.twist.angular.z = 0.0
        if self.ros_initialized:
            self.cmd_vel_pub.publish(self.twist)
        self.is_moving = True
        self.current_speed = -self.linear_speed
        self.current_angular = 0.0
        self.publish_status("后退")
        print("机器人后退")
    
    def turn_left(self):
        """左转"""
        self.twist.linear.x = 0.0
        self.twist.angular.z = self.angular_speed
        if self.ros_initialized:
            self.cmd_vel_pub.publish(self.twist)
        self.is_moving = True
        self.current_speed = 0.0
        self.current_angular = self.angular_speed
        self.publish_status("左转")
        print("机器人左转")
    
    def turn_right(self):
        """右转"""
        self.twist.linear.x = 0.0
        self.twist.angular.z = -self.angular_speed
        if self.ros_initialized:
            self.cmd_vel_pub.publish(self.twist)
        self.is_moving = True
        self.current_speed = 0.0
        self.current_angular = -self.angular_speed
        self.publish_status("右转")
        print("机器人右转")
    
    def publish_status(self, status):
        """发布状态信息"""
        if self.ros_initialized:
            status_msg = String()
            status_msg.data = status
            self.status_pub.publish(status_msg)
    
    def head_control_callback(self, msg):
        """处理来自agent模块的头部控制命令"""
        try:
            command = msg.data.strip()
            print(f"收到头部控制命令: {command}")
            
            # 解析命令格式: "direction:action" 或简单的 "direction"
            if ':' in command:
                direction, action = command.split(':', 1)
                direction = direction.strip()
                action = action.strip()
            else:
                direction = command
                action = "move"
            
            # 更新头部位置状态
            self.head_position = direction
            
            # 发布状态信息
            self.publish_status(f"头部动作: {direction}")
            print(f"执行头部动作: {direction}")
            
        except Exception as e:
            print(f"处理头部控制命令时出错: {e}")
    
    def control_head(self, direction):
        """控制头部动作"""
        self.head_position = direction
        
        # 通过ROS话题发布头部控制命令
        if self.ros_initialized:
            head_msg = String()
            head_msg.data = direction
            self.head_pub.publish(head_msg)
        
        self.publish_status(f"头部动作: {direction}")
        print(f"头部动作: {direction}")
    
    def start_tracking_mode(self):
        """启动跟随模式（启用跟踪和激光雷达）"""
        try:
            if self.ros_initialized:
                # 发布启用跟踪和激光雷达的消息
                tracking_msg = Bool()
                tracking_msg.data = True
                self.enable_tracking_pub.publish(tracking_msg)
                
                lidar_msg = Bool()
                lidar_msg.data = True
                self.enable_lidar_pub.publish(lidar_msg)
                
                self.publish_status("跟随模式已启动")
                print("✅ 跟随模式已启动 - 跟踪和激光雷达已启用")
                return True
            else:
                print("❌ ROS未初始化，无法启动跟随模式")
                return False
        except Exception as e:
            print(f"❌ 启动跟随模式失败: {e}")
            return False
    
    def stop_tracking_mode(self):
        """停止跟随模式（禁用跟踪和激光雷达）"""
        try:
            if self.ros_initialized:
                # 发布禁用跟踪和激光雷达的消息
                tracking_msg = Bool()
                tracking_msg.data = False
                self.enable_tracking_pub.publish(tracking_msg)
                
                lidar_msg = Bool()
                lidar_msg.data = False
                self.enable_lidar_pub.publish(lidar_msg)
                
                self.publish_status("跟随模式已停止")
                print("✅ 跟随模式已停止 - 跟踪和激光雷达已禁用")
                return True
            else:
                print("❌ ROS未初始化，无法停止跟随模式")
                return False
        except Exception as e:
            print(f"❌ 停止跟随模式失败: {e}")
            return False

    def restart_agent(self):
        """重启agent模块"""
        try:
            print("🔄 开始重启Agent模块...")
            
            # 停止现有的agent进程
            print("🛑 停止现有Agent进程...")
            subprocess.run(['pkill', '-f', 'main.py'], check=False)
            subprocess.run(['pkill', '-f', 'agent'], check=False)
            time.sleep(3)  # 等待进程完全停止
            
            # 检查是否还有agent进程在运行
            try:
                result = subprocess.run(['pgrep', '-f', 'main.py'], capture_output=True, text=True)
                if result.stdout.strip():
                    print("⚠️ 仍有agent进程在运行，强制终止...")
                    subprocess.run(['pkill', '-9', '-f', 'main.py'], check=False)
                    time.sleep(1)
            except:
                pass
            
            # 启动新的agent进程
            agent_script = '/home/orangepi/robot/src/agent/start_agent.sh'
            
            if os.path.exists(agent_script):
                print("🚀 启动新的Agent进程...")
                # 使用nice -n 0 bash在后台运行，并重定向输出
                process = subprocess.Popen(
                    ['nice', '-n', '0', 'bash', agent_script],
                    stdout=open('/tmp/agent_output.log', 'w'),
                    stderr=open('/tmp/agent_error.log', 'w'),
                    preexec_fn=os.setsid  # 创建新的进程组
                )
                
                # 等待一下确保进程启动
                time.sleep(2)
                
                # 检查进程是否成功启动
                try:
                    result = subprocess.run(['pgrep', '-f', 'main.py'], capture_output=True, text=True)
                    if result.stdout.strip():
                        self.publish_status("Agent模块重启成功")
                        print("✅ Agent模块重启成功")
                        return True
                    else:
                        print("❌ Agent模块启动失败")
                        return False
                except:
                    print("❌ 无法检查Agent进程状态")
                    return False
            else:
                print(f"❌ Agent启动脚本不存在: {agent_script}")
                return False
                
        except Exception as e:
            print(f"❌ 重启Agent模块失败: {e}")
            return False
    
    def start_ros_launch(self):
        """启动ROS launch"""
        try:
            print("🚀 开始启动ROS launch...")
            
            # 检查是否已经在运行
            result = subprocess.run(['pgrep', '-f', 'all.launch'], capture_output=True, text=True)
            if result.stdout.strip():
                print("⚠️ ROS launch已在运行中")
                self.publish_status("ROS launch已在运行")
                return True
            
            # 启动ROS launch
            launch_cmd = "taskset -c 1,2 nice -n 15 bash -lc 'source /opt/ros/noetic/setup.sh && source /home/orangepi/robot/devel/setup.sh && sleep 3 && roslaunch pkg_launch all.launch'"
            
            print(f"执行启动命令: {launch_cmd}")
            
            # 在后台运行ROS launch
            process = subprocess.Popen(
                launch_cmd,
                shell=True,
                stdout=open('/tmp/ros_launch.log', 'w'),
                stderr=open('/tmp/ros_launch_error.log', 'w'),
                preexec_fn=os.setsid  # 创建新的进程组
            )
            
            # 等待一下确保进程启动
            time.sleep(3)
            
            # 检查进程是否成功启动
            try:
                result = subprocess.run(['pgrep', '-f', 'all.launch'], capture_output=True, text=True)
                if result.stdout.strip():
                    self.publish_status("ROS launch启动成功")
                    print("✅ ROS launch启动成功")
                    return True
                else:
                    print("❌ ROS launch启动失败")
                    return False
            except:
                print("❌ 无法检查ROS launch进程状态")
                return False
                
        except Exception as e:
            print(f"❌ 启动ROS launch失败: {e}")
            return False
    
    def stop_ros_launch(self):
        """停止ROS launch"""
        try:
            print("🛑 开始停止ROS launch...")
            
            # 根据all.launch文件内容，杀死所有启动的节点进程
            # all.launch启动的节点包括：
            # - urdf相关进程  
            # - usb_camera相关进程
            # - img_decode相关进程
            # - rknn_yolov6相关进程
            # - img_encode相关进程
            # - ydlidar相关进程
            # - object_track相关进程
            
            node_patterns = [
                'urdf',
                'usb_camera', 
                'img_decode',
                'rknn_yolov6',
                'img_encode',
                'ydlidar',
                'object_track'
            ]
            
            print("终止ROS节点进程...")
            for pattern in node_patterns:
                subprocess.run(['pkill', '-f', pattern], check=False)
                print(f"  - 终止 {pattern} 相关进程")
            
            # 只杀死all.launch相关的roslaunch进程
            print("终止all.launch进程...")
            subprocess.run(['pkill', '-f', 'all.launch'], check=False)
            
            # 等待进程完全停止
            time.sleep(3)
            
            # 检查是否还有相关进程在运行
            try:
                remaining_processes = []
                for pattern in node_patterns:
                    result = subprocess.run(['pgrep', '-f', pattern], capture_output=True, text=True)
                    if result.stdout.strip():
                        remaining_processes.append(pattern)
                
                if remaining_processes:
                    print(f"⚠️ 仍有以下进程在运行: {remaining_processes}，强制终止...")
                    for pattern in remaining_processes:
                        subprocess.run(['pkill', '-9', '-f', pattern], check=False)
                        print(f"  - 强制终止 {pattern} 进程")
                    time.sleep(1)
                    
                    # 再次检查
                    still_running = []
                    for pattern in node_patterns:
                        result = subprocess.run(['pgrep', '-f', pattern], capture_output=True, text=True)
                        if result.stdout.strip():
                            still_running.append(pattern)
                    
                    if still_running:
                        print(f"❌ 无法完全停止以下进程: {still_running}")
                        return False
            except:
                pass
            
            self.publish_status("ROS launch已停止")
            print("✅ ROS launch已停止")
            return True
                
        except Exception as e:
            print(f"❌ 停止ROS launch失败: {e}")
            return False
    
    def start_navigation(self):
        """启动导航系统"""
        try:
            print("🚀 开始启动导航系统...")
            
            # 检查是否已经在运行
            result = subprocess.run(['pgrep', '-f', 'robot_navigation.launch'], capture_output=True, text=True)
            if result.stdout.strip():
                print("⚠️ 导航系统已在运行中")
                self.publish_status("导航系统已在运行")
                return True
            
            # 启动导航launch文件
            launch_cmd = "bash -lc 'source /opt/ros/noetic/setup.sh && source /home/orangepi/robot/devel/setup.sh && sleep 2  && roslaunch robot_navigation robot_navigation.launch'"
            
            print(f"执行启动命令: {launch_cmd}")
            
            # 在后台运行导航launch
            process = subprocess.Popen(
                launch_cmd,
                shell=True,
                stdout=open('/tmp/navigation_launch.log', 'w'),
                stderr=open('/tmp/navigation_launch_error.log', 'w'),
                preexec_fn=os.setsid  # 创建新的进程组
            )
            
            # 等待一下确保进程启动
            time.sleep(5)
            
            # 检查进程是否成功启动
            try:
                result = subprocess.run(['pgrep', '-f', 'robot_navigation.launch'], capture_output=True, text=True)
                if result.stdout.strip():
                    self.publish_status("导航系统启动成功")
                    print("✅ 导航系统启动成功")
                    return True
                else:
                    print("❌ 导航系统启动失败")
                    return False
            except:
                print("❌ 无法检查导航系统进程状态")
                return False
                
        except Exception as e:
            print(f"❌ 启动导航系统失败: {e}")
            return False
    
    def stop_navigation(self):
        """停止导航系统"""
        try:
            print("🛑 开始停止导航系统...")
            
            # robot_navigation.launch启动的节点包括：
            # - map_server
            # - amcl
            # - move_base
            # - 可能还有urdf和ydlidar（如果launch文件中包含）
            
            node_patterns = [
                'map_server',
                'amcl',
                'move_base',
                'urdf',
                'ydlidar'
            ]
            
            print("终止导航节点进程...")
            for pattern in node_patterns:
                subprocess.run(['pkill', '-f', pattern], check=False)
                print(f"  - 终止 {pattern} 相关进程")
            
            # 杀死robot_navigation.launch进程
            print("终止robot_navigation.launch进程...")
            subprocess.run(['pkill', '-f', 'robot_navigation.launch'], check=False)
            
            # 等待进程完全停止
            time.sleep(3)
            
            # 检查是否还有相关进程在运行
            try:
                remaining_processes = []
                for pattern in node_patterns:
                    result = subprocess.run(['pgrep', '-f', pattern], capture_output=True, text=True)
                    if result.stdout.strip():
                        remaining_processes.append(pattern)
                
                if remaining_processes:
                    print(f"⚠️ 仍有以下进程在运行: {remaining_processes}，强制终止...")
                    for pattern in remaining_processes:
                        subprocess.run(['pkill', '-9', '-f', pattern], check=False)
                        print(f"  - 强制终止 {pattern} 进程")
                    time.sleep(1)
                    
                    # 再次检查
                    still_running = []
                    for pattern in node_patterns:
                        result = subprocess.run(['pgrep', '-f', pattern], capture_output=True, text=True)
                        if result.stdout.strip():
                            still_running.append(pattern)
                    
                    if still_running:
                        print(f"❌ 无法完全停止以下进程: {still_running}")
                        return False
            except:
                pass
            
            self.publish_status("导航系统已停止")
            print("✅ 导航系统已停止")
            return True
                
        except Exception as e:
            print(f"❌ 停止导航系统失败: {e}")
            return False
    
    
    def get_status(self):
        """获取当前状态"""
        return {
            'is_moving': self.is_moving,
            'current_speed': self.current_speed,
            'current_angular': self.current_angular,
            'linear_speed': self.linear_speed,
            'angular_speed': self.angular_speed,
            'head_position': self.head_position
        }

# 创建全局机器人控制器实例
robot_controller = None
ros_thread = None
is_shutting_down = False

def init_robot_controller(ap_mode=False):
    """初始化机器人控制器"""
    global robot_controller
    try:
        robot_controller = RobotController(ap_mode=ap_mode)
        return True
    except Exception as e:
        print(f"初始化机器人控制器失败: {e}")
        return False

@app.route('/')
def index():
    """主页"""
    return render_template('index.html')

@app.route('/wifi_config')
def wifi_config():
    """WiFi配置页面"""
    return render_template('wifi_config.html')

@app.route('/api/control', methods=['POST'])
def control_robot():
    """控制机器人API"""
    if not robot_controller:
        return jsonify({'success': False, 'message': '机器人控制器未初始化'})
    
    try:
        data = request.get_json()
        action = data.get('action')
        
        if action == 'forward':
            robot_controller.move_forward()
        elif action == 'backward':
            robot_controller.move_backward()
        elif action == 'left':
            robot_controller.turn_left()
        elif action == 'right':
            robot_controller.turn_right()
        elif action == 'stop':
            robot_controller.stop_robot()
        else:
            return jsonify({'success': False, 'message': '无效的动作'})
        
        return jsonify({'success': True, 'message': f'执行动作: {action}'})
    
    except Exception as e:
        return jsonify({'success': False, 'message': f'控制失败: {str(e)}'})

@app.route('/api/status')
def get_robot_status():
    """获取机器人状态API"""
    if not robot_controller:
        return jsonify({'success': False, 'message': '机器人控制器未初始化'})
    
    try:
        status = robot_controller.get_status()
        return jsonify({'success': True, 'data': status})
    except Exception as e:
        return jsonify({'success': False, 'message': f'获取状态失败: {str(e)}'})

@app.route('/api/speed', methods=['POST'])
def set_speed():
    """设置速度API"""
    if not robot_controller:
        return jsonify({'success': False, 'message': '机器人控制器未初始化'})
    
    try:
        data = request.get_json()
        linear_speed = data.get('linear_speed')
        angular_speed = data.get('angular_speed')
        
        if linear_speed is not None:
            robot_controller.linear_speed = float(linear_speed)
        if angular_speed is not None:
            robot_controller.angular_speed = float(angular_speed)
        
        return jsonify({'success': True, 'message': '速度设置成功'})
    except Exception as e:
        return jsonify({'success': False, 'message': f'设置速度失败: {str(e)}'})

@app.route('/api/head', methods=['POST'])
def control_head():
    """控制头部动作API"""
    if not robot_controller:
        return jsonify({'success': False, 'message': '机器人控制器未初始化'})
    
    try:
        data = request.get_json()
        direction = data.get('direction')
        
        if direction not in ['center', 'left', 'right', 'up', 'down']:
            return jsonify({'success': False, 'message': '无效的头部动作方向'})
        
        robot_controller.control_head(direction)
        return jsonify({'success': True, 'message': f'头部动作: {direction}'})
    
    except Exception as e:
        return jsonify({'success': False, 'message': f'控制头部失败: {str(e)}'})

@app.route('/api/start_tracking_mode', methods=['POST'])
def start_tracking_mode():
    """启动跟随模式API"""
    if not robot_controller:
        return jsonify({'success': False, 'message': '机器人控制器未初始化'})
    
    try:
        success = robot_controller.start_tracking_mode()
        if success:
            return jsonify({'success': True, 'message': '跟随模式启动成功'})
        else:
            return jsonify({'success': False, 'message': '跟随模式启动失败'})
    
    except Exception as e:
        return jsonify({'success': False, 'message': f'启动跟随模式失败: {str(e)}'})

@app.route('/api/stop_tracking_mode', methods=['POST'])
def stop_tracking_mode():
    """停止跟随模式API"""
    if not robot_controller:
        return jsonify({'success': False, 'message': '机器人控制器未初始化'})
    
    try:
        success = robot_controller.stop_tracking_mode()
        if success:
            return jsonify({'success': True, 'message': '跟随模式停止成功'})
        else:
            return jsonify({'success': False, 'message': '跟随模式停止失败'})
    
    except Exception as e:
        return jsonify({'success': False, 'message': f'停止跟随模式失败: {str(e)}'})

@app.route('/api/restart_agent', methods=['POST'])
def restart_agent():
    """重启agent模块API"""
    if not robot_controller:
        return jsonify({'success': False, 'message': '机器人控制器未初始化'})
    
    try:
        success = robot_controller.restart_agent()
        if success:
            return jsonify({'success': True, 'message': 'Agent模块重启成功'})
        else:
            return jsonify({'success': False, 'message': 'Agent模块重启失败'})
    
    except Exception as e:
        return jsonify({'success': False, 'message': f'重启Agent失败: {str(e)}'})


@app.route('/api/start_ros_launch', methods=['POST'])
def start_ros_launch():
    """启动ROS launch API"""
    if not robot_controller:
        return jsonify({'success': False, 'message': '机器人控制器未初始化'})
    
    try:
        success = robot_controller.start_ros_launch()
        if success:
            return jsonify({'success': True, 'message': 'ROS launch启动成功'})
        else:
            return jsonify({'success': False, 'message': 'ROS launch启动失败'})
    
    except Exception as e:
        return jsonify({'success': False, 'message': f'启动ROS launch失败: {str(e)}'})

@app.route('/api/stop_ros_launch', methods=['POST'])
def stop_ros_launch():
    """停止ROS launch API"""
    if not robot_controller:
        return jsonify({'success': False, 'message': '机器人控制器未初始化'})
    
    try:
        success = robot_controller.stop_ros_launch()
        if success:
            return jsonify({'success': True, 'message': 'ROS launch停止成功'})
        else:
            return jsonify({'success': False, 'message': 'ROS launch停止失败'})
    
    except Exception as e:
        return jsonify({'success': False, 'message': f'停止ROS launch失败: {str(e)}'})

@app.route('/api/start_navigation', methods=['POST'])
def start_navigation():
    """启动导航系统API"""
    if not robot_controller:
        return jsonify({'success': False, 'message': '机器人控制器未初始化'})
    
    try:
        success = robot_controller.start_navigation()
        if success:
            return jsonify({'success': True, 'message': '导航系统启动成功'})
        else:
            return jsonify({'success': False, 'message': '导航系统启动失败'})
    
    except Exception as e:
        return jsonify({'success': False, 'message': f'启动导航系统失败: {str(e)}'})

@app.route('/api/stop_navigation', methods=['POST'])
def stop_navigation():
    """停止导航系统API"""
    if not robot_controller:
        return jsonify({'success': False, 'message': '机器人控制器未初始化'})
    
    try:
        success = robot_controller.stop_navigation()
        if success:
            return jsonify({'success': True, 'message': '导航系统停止成功'})
        else:
            return jsonify({'success': False, 'message': '导航系统停止失败'})
    
    except Exception as e:
        return jsonify({'success': False, 'message': f'停止导航系统失败: {str(e)}'})


@app.route('/api/scan_wifi', methods=['POST'])
def scan_wifi():
    """扫描WiFi网络API"""
    try:
        print("开始扫描WiFi网络...")
        
        # 使用nmcli扫描WiFi网络
        result = subprocess.run(['nmcli', 'dev', 'wifi', 'list'], 
                              capture_output=True, text=True, timeout=30)
        
        if result.returncode != 0:
            return jsonify({'success': False, 'message': f'扫描失败: {result.stderr}'})
        
        networks = []
        lines = result.stdout.strip().split('\n')[1:]  # 跳过标题行
        
        for line in lines:
            if line.strip():
                parts = line.split()
                if len(parts) >= 4:
                    # 处理nmcli输出格式
                    if parts[0] == '*':
                        # 有*标记的行：* BSSID SSID MODE CHAN RATE Mbit/s SIGNAL BARS SECURITY
                        # 索引: 0=*, 1=BSSID, 2=SSID, 3=MODE, 4=CHAN, 5=RATE, 6=Mbit/s, 7=SIGNAL
                        ssid = parts[2] if parts[2] != '--' else '隐藏网络'
                        signal_idx = 7  # SIGNAL在第7个位置
                    else:
                        # 没有*标记的行：BSSID SSID MODE CHAN RATE Mbit/s SIGNAL BARS SECURITY
                        # 索引: 0=BSSID, 1=SSID, 2=MODE, 3=CHAN, 4=RATE, 5=Mbit/s, 6=SIGNAL
                        ssid = parts[1] if parts[1] != '--' else '隐藏网络'
                        signal_idx = 6  # SIGNAL在第6个位置
                    
                    # 获取信号强度
                    signal = '0'
                    try:
                        if signal_idx < len(parts):
                            signal_str = parts[signal_idx]
                            # 直接尝试转换为整数
                            signal = str(int(signal_str))
                    except (ValueError, IndexError):
                        signal = '0'
                    
                    # 过滤掉空SSID和信号强度为0的网络
                    if ssid and ssid != '--' and signal != '0':
                        networks.append({
                            'ssid': ssid,
                            'signal': signal
                        })
        
        # 去重并排序
        unique_networks = []
        seen_ssids = set()
        for network in networks:
            if network['ssid'] not in seen_ssids:
                unique_networks.append(network)
                seen_ssids.add(network['ssid'])
        
        # 按信号强度排序
        unique_networks.sort(key=lambda x: int(x['signal']), reverse=True)
        
        print(f"扫描完成，发现 {len(unique_networks)} 个网络")
        return jsonify({'success': True, 'data': unique_networks})
        
    except subprocess.TimeoutExpired:
        return jsonify({'success': False, 'message': '扫描超时，请重试'})
    except Exception as e:
        print(f"扫描WiFi网络失败: {e}")
        return jsonify({'success': False, 'message': f'扫描失败: {str(e)}'})

@app.route('/api/configure_wifi', methods=['POST'])
def configure_wifi():
    """配置WiFi网络API"""
    try:
        data = request.get_json()
        ssid = data.get('ssid')
        password = data.get('password')
        
        if not ssid or not password:
            return jsonify({'success': False, 'message': 'SSID和密码不能为空'})
        
        print(f"开始配置WiFi: {ssid}")
        
        # 参考network.py的做法，将关闭AP和配置WiFi放在一个命令中执行
        # 避免中间状态导致界面异常
        cmd = f"echo orangepi | sudo -S create_ap --fix-unmanaged && sleep 8 && nmcli device wifi connect '{ssid}' password '{password}' && sync && sleep 5 && reboot"
        
        print(f"执行WiFi配置命令: {cmd}")
        
        # 在后台执行命令，避免阻塞
        subprocess.Popen(cmd, shell=True, 
                        stdout=open('/tmp/wifi_config.log', 'w'),
                        stderr=open('/tmp/wifi_config_error.log', 'w'))
        
        print(f"WiFi配置命令已启动: {ssid}")
        return jsonify({'success': True, 'message': 'WiFi配置已启动，系统将重启并连接到新网络'})
        
    except Exception as e:
        print(f"配置WiFi失败: {e}")
        return jsonify({'success': False, 'message': f'配置失败: {str(e)}'})

@app.route('/settings')
def settings_page():
    """设置页面"""
    return render_template('settings.html')

@app.route('/api/get_config', methods=['GET'])
def get_config():
    """获取配置API"""
    try:
        # 导入配置管理器
        sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'src', 'agent'))
        from config_manager import get_global_config_manager
        
        config_mgr = get_global_config_manager()
        config = config_mgr.get_all_config()
        
        return jsonify({'success': True, 'config': config})
    except Exception as e:
        print(f"获取配置失败: {e}")
        return jsonify({'success': False, 'message': f'获取配置失败: {str(e)}'})

@app.route('/api/save_config', methods=['POST'])
def save_config():
    """保存配置API"""
    try:
        data = request.get_json()
        
        # 导入配置管理器
        sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'src', 'agent'))
        from config_manager import get_global_config_manager
        
        config_mgr = get_global_config_manager()
        
        # 更新配置
        updates = {}
        if 'api_key' in data:
            updates['api_key'] = data['api_key']
        if 'model' in data:
            updates['model'] = data['model']
        if 'tts_model' in data:
            updates['tts_model'] = data['tts_model']
        if 'tts_voice' in data:
            updates['tts_voice'] = data['tts_voice']
        if 'enable_tts' in data:
            updates['enable_tts'] = data['enable_tts']
        
        success = config_mgr.update(updates)
        
        if success:
            return jsonify({'success': True, 'message': '配置保存成功，重启 Agent 后生效'})
        else:
            return jsonify({'success': False, 'message': '配置保存失败'})
            
    except Exception as e:
        print(f"保存配置失败: {e}")
        return jsonify({'success': False, 'message': f'保存失败: {str(e)}'})

def cleanup_resources():
    """清理所有资源"""
    global robot_controller, ros_thread, is_shutting_down
    
    if is_shutting_down:
        return
        
    is_shutting_down = True
    print("\n🧹 开始清理资源...")
    
    try:
        # 停止机器人控制器
        if robot_controller:
            print("🛑 停止机器人控制器...")
            robot_controller.stop_robot()
            robot_controller = None
        
        # 关闭ROS节点
        if not rospy.is_shutdown():
            print("🛑 关闭ROS节点...")
            rospy.signal_shutdown("程序退出")
        
        # 等待ROS线程结束
        if ros_thread and ros_thread.is_alive():
            print("⏳ 等待ROS线程结束...")
            ros_thread.join(timeout=3)
            if ros_thread.is_alive():
                print("⚠️ ROS线程未能在3秒内结束，强制继续")
        
        print("✅ 资源清理完成")
        
    except Exception as e:
        print(f"❌ 清理资源时出错: {e}")

def signal_handler(signum, frame):
    """信号处理器"""
    print(f"\n🛑 收到信号 {signum}，开始优雅关闭...")
    cleanup_resources()
    sys.exit(0)

def ros_spin_thread():
    """ROS主循环线程"""
    try:
        rospy.spin()
    except rospy.ROSInterruptException:
        print("ROS节点被中断")
    except Exception as e:
        print(f"ROS线程出错: {e}")

if __name__ == '__main__':
    import sys
    
    # 检查是否为AP模式
    ap_mode = '--ap-mode' in sys.argv or '--ap' in sys.argv
    
    if ap_mode:
        print("正在启动AP模式Web控制程序...")
    else:
        print("正在启动小车Web控制程序...")
    
    # 注册信号处理器
    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)
    
    # 注册退出时的清理函数
    atexit.register(cleanup_resources)
    
    # 初始化机器人控制器
    if not init_robot_controller(ap_mode=ap_mode):
        print("机器人控制器初始化失败，程序退出")
        sys.exit(1)
    
    # 只在非AP模式下启动ROS主循环线程
    if not ap_mode:
        ros_thread = threading.Thread(target=ros_spin_thread)
        ros_thread.daemon = True
        ros_thread.start()
    
    if ap_mode:
        print("AP模式Web服务器启动中...")
        print("请在浏览器中访问: http://192.168.12.1:5000/wifi_config")
    else:
        print("Web服务器启动中...")
        print("请在浏览器中访问: http://localhost:5000")
    print("按 Ctrl+C 停止程序")
    
    try:
        # 启动Flask应用
        app.run(host='0.0.0.0', port=5000, debug=False, threaded=True)
    except KeyboardInterrupt:
        print("\n🛑 收到键盘中断信号...")
        cleanup_resources()
    except Exception as e:
        print(f"\n❌ 程序运行出错: {e}")
        cleanup_resources()
        sys.exit(1)
    finally:
        print("✅ Web控制器已停止")
