#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
外部函数执行器"""

import json
import threading
from typing import Dict, Any, Optional, Callable
import time
import queue
from collections import deque

# 导入ROS相关模块
try:
    import rospy
    from std_msgs.msg import String
    ROS_AVAILABLE = True
    print(" ROS模块导入成功")
except ImportError as e:
    ROS_AVAILABLE = False
    print(f" ROS模块导入失败: {e}")
    print("ROS功能将不可用")

# 导入头部舵机控制器
try:
    from head_servo_controller import HeadServoController
    HEAD_CONTROLLER_AVAILABLE = True
    print(" 头部舵机控制器模块导入成功")
except ImportError as e:
    HEAD_CONTROLLER_AVAILABLE = False
    print(f" 头部舵机控制器模块导入失败: {e}")
    print("头部动作功能将不可用")

# 导入云音乐模块
try:
    from cloud_music import (
        play_cloud_music, stop_music, next_song, 
        add_to_queue, get_music_status, play_music_with_control,
        get_recommended_songs
    )
    CLOUD_MUSIC_AVAILABLE = True
    print(" 云音乐模块导入成功")
except ImportError as e:
    CLOUD_MUSIC_AVAILABLE = False
    print(f" 云音乐模块导入失败: {e}")
    print("云音乐功能将不可用")

# 导入机器人跟随控制器
try:
    from robot_following_controller import RobotFollowingController, get_global_following_controller
    ROBOT_FOLLOWING_AVAILABLE = True
    print(" 机器人跟随控制器模块导入成功")
except ImportError as e:
    ROBOT_FOLLOWING_AVAILABLE = False
    print(f" 机器人跟随控制器模块导入失败: {e}")
    print("机器人跟随功能将不可用")

# 导入导航控制器
try:
    from navigation_controller import NavigationController, get_global_navigation_controller
    NAVIGATION_AVAILABLE = True
    print(" 导航控制器模块导入成功")
except ImportError as e:
    NAVIGATION_AVAILABLE = False
    print(f" 导航控制器模块导入失败: {e}")
    print("导航功能将不可用")


class FunctionExecutor:
    """函数执行器类，用于执行外部函数调用"""
    
    def __init__(self, emoji_obj=None, asr_instance=None):
        """
        初始化函数执行器
        
        Args:
            emoji_obj: 表情控制对象
            asr_instance: ASR实例，用于控制音频监听状态
        """
        self.emoji_obj = emoji_obj
        self.asr_instance = asr_instance  # 保存ASR实例引用
        self.function_registry = {}
        self.execution_log = []
        
        # 头部动作队列和锁
        self.head_action_queue = queue.Queue()
        self.head_action_lock = threading.Lock()
        self.head_action_thread = None
        self.head_action_running = False
        
        # 初始化ROS话题订阅者
        self.head_control_sub = None
        if ROS_AVAILABLE:
            try:
                # 订阅来自web界面的头部控制命令
                self.head_control_sub = rospy.Subscriber('/head_control', String, self.head_control_callback)
                print(" ROS头部控制话题订阅者初始化成功")
            except Exception as e:
                print(f" ROS头部控制话题订阅者初始化失败: {e}")
                self.head_control_sub = None
        
        # 初始化头部舵机控制器
        self.head_controller = None
        if HEAD_CONTROLLER_AVAILABLE:
            try:
                self.head_controller = HeadServoController()
                if self.head_controller.is_ready():
                    print(" 头部舵机控制器初始化成功")
                else:
                    print(" 头部舵机控制器初始化失败")
                    self.head_controller = None
            except Exception as e:
                print(f" 头部舵机控制器初始化出错: {e}")
                self.head_controller = None
        
        # 初始化机器人跟随控制器
        self.following_controller = None
        if ROBOT_FOLLOWING_AVAILABLE:
            try:
                self.following_controller = get_global_following_controller()
                if self.following_controller.is_initialized:
                    print(" 机器人跟随控制器初始化成功")
                else:
                    print(" 机器人跟随控制器初始化失败")
                    self.following_controller = None
            except Exception as e:
                print(f" 机器人跟随控制器初始化出错: {e}")
                self.following_controller = None
        
        # 初始化导航控制器
        self.navigation_controller = None
        if NAVIGATION_AVAILABLE:
            try:
                self.navigation_controller = get_global_navigation_controller()
                if self.navigation_controller.is_initialized:
                    print(" 导航控制器初始化成功")
                else:
                    print(" 导航控制器初始化失败")
                    self.navigation_controller = None
            except Exception as e:
                print(f" 导航控制器初始化出错: {e}")
                self.navigation_controller = None
        
        # 启动头部动作队列处理线程
        self._start_head_action_queue_thread()
        
        # 注册默认函数
        self._register_default_functions()
    
    def _start_head_action_queue_thread(self):
        """启动头部动作队列处理线程"""
        if self.head_controller and not self.head_action_running:
            self.head_action_running = True
            self.head_action_thread = threading.Thread(
                target=self._head_action_queue_worker, 
                daemon=True,
                name="HeadActionQueue"
            )
            self.head_action_thread.start()
            print(" 头部动作队列处理线程已启动")
    
    def _stop_head_action_queue_thread(self):
        """停止头部动作队列处理线程"""
        if self.head_action_running:
            self.head_action_running = False
            # 添加一个停止信号到队列
            try:
                self.head_action_queue.put(None, timeout=1)
            except queue.Full:
                pass
            
            if self.head_action_thread and self.head_action_thread.is_alive():
                self.head_action_thread.join(timeout=3)
                print(" 头部动作队列处理线程已停止")
    
    def _head_action_queue_worker(self):
        """头部动作队列处理工作线程"""
        print(" 头部动作队列处理线程启动")
        
        while self.head_action_running:
            try:
                # 从队列中获取动作，设置超时以便能够响应停止信号
                action = self.head_action_queue.get(timeout=1)
                
                # 检查是否是停止信号
                if action is None:
                    break
                
                # 执行头部动作
                self._execute_head_action(action)
                
                # 标记任务完成
                self.head_action_queue.task_done()
                
            except queue.Empty:
                # 超时，继续循环检查运行状态
                continue
            except Exception as e:
                print(f" 头部动作队列处理出错: {e}")
                continue
        
        print(" 头部动作队列处理线程结束")
    
    def _execute_head_action(self, action):
        """执行头部动作"""
        try:
            action_type = action.get('type')
            action_name = action.get('name', 'unknown')
            args = action.get('args', [])
            kwargs = action.get('kwargs', {})
                        
            if action_type == 'basic':
                # 基本动作
                if hasattr(self.head_controller, action_name):
                    method = getattr(self.head_controller, action_name)
                    result = method(*args, **kwargs)
                    print(f" 基本动作 {action_name} 完成: {result}")
                else:
                    print(f" 未找到基本动作方法: {action_name}")
            
            elif action_type == 'function':
                # 通过函数执行器执行
                function_name = action_name
                if function_name in self.function_registry:
                    func = self.function_registry[function_name]
                    result = func(*args, **kwargs)
                    print(f" 函数动作 {function_name} 完成: {result}")
                else:
                    print(f" 未找到函数: {function_name}")
            
            else:
                print(f" 未知的动作类型: {action_type}")
                
        except Exception as e:
            print(f" 执行头部动作失败: {e}")
    
    def _queue_head_action(self, action_type, action_name, *args, **kwargs):
        """将头部动作添加到队列"""
        if not self.head_controller:
            print(" 头部控制器不可用，跳过动作")
            return False
        
        if not self.head_action_running:
            print(" 头部动作队列未运行，跳过动作")
            return False
        
        try:
            action = {
                'type': action_type,
                'name': action_name,
                'args': args,
                'kwargs': kwargs,
                'timestamp': time.time()
            }
            
            self.head_action_queue.put(action, timeout=2)
            return True
            
        except queue.Full:
            print(f" 头部动作队列已满，跳过动作: {action_name}")
            return False
        except Exception as e:
            print(f" 添加头部动作到队列失败: {e}")
            return False
    
    def _is_music_playing(self) -> bool:
        """
        检查是否正在播放音乐
        
        Returns:
            bool: 是否正在播放音乐
        """
        if not CLOUD_MUSIC_AVAILABLE:
            return False
        
        try:
            status = get_music_status()
            return status.get("is_playing", False)
        except Exception as e:
            print(f"检查音乐播放状态时出错: {e}")
            return False
    
    def _register_default_functions(self):
        """注册默认的函数映射"""
        # 表情相关函数
        self.function_registry.update({
            "head_smile": self._head_smile,
            "head_nod": self._head_nod,
            "head_shake": self._head_shake,
            "head_dance": self._head_dance,  # 添加缺失的跳舞函数
            "excited": self._excited,
            "normal": self._normal,
            "sleep": self._sleep,
            "wake_up": self._wake_up,
        })
        
        # 音量控制函数
        self.function_registry.update({
            "volume_up": self._volume_up,
            "volume_down": self._volume_down,
            "volume_set": self._volume_set,
            "volume_mute": self._volume_mute,
            "volume_unmute": self._volume_unmute,
            "volume_status": self._volume_status,
        })
        
        # 音乐播放函数
        if CLOUD_MUSIC_AVAILABLE:
            self.function_registry.update({
                "play_music": self._play_music,
                "play_song": self._play_song,
                "stop_music": self._stop_music,
                "next_song": self._next_song,
                "add_to_queue": self._add_to_queue,
                "music_status": self._music_status,
                "recommend_songs": self._recommend_songs,
            })
        
        # 机器人跟随控制函数
        if self.following_controller:
            self.function_registry.update({
                "start_following": self._start_following,
                "stop_following": self._stop_following,
                "toggle_following": self._toggle_following,
                "following_status": self._following_status,
                "enable_tracking": self._enable_tracking,
                "disable_tracking": self._disable_tracking,
                "enable_lidar": self._enable_lidar,
                "disable_lidar": self._disable_lidar,
            })
        
        # 导航控制函数
        if self.navigation_controller:
            self.function_registry.update({
                "navigate_to": self._navigate_to,
                "go_to": self._navigate_to,  # 别名
                "cancel_navigation": self._cancel_navigation,
                "navigation_status": self._navigation_status,
                "list_locations": self._list_locations,
                "add_location": self._add_location,
            })
        
        # 基本头部动作函数（总是注册，即使头部控制器不可用）
        self.function_registry.update({
            # 基本头部控制
            "head_forward": self._head_forward,
            "head_down": self._head_down,
            "head_up": self._head_up,
            "head_left": self._head_left,
            "head_right": self._head_right,
            "head_reset": self._head_reset,
            "head_position": self._head_position,
        })
        
        # WiFi显示功能
        self.function_registry.update({
            "show_wifi_info": self._show_wifi_info,
            "get_wifi_ip": self._get_wifi_ip,
            "wifi_status": self._wifi_status,
        })
        
        # 拟人化动作（需要头部控制器）
        if self.head_controller:
            self.function_registry.update({
                "head_think": self._head_think,
                "head_confused": self._head_confused,
                "head_surprised": self._head_surprised,
                "head_look_around": self._head_look_around,
                "head_greet": self._head_greet,
                "head_goodbye": self._head_goodbye,
                "head_refuse": self._head_refuse,
                "head_breathing": self._head_breathing,
                "head_attention": self._head_attention,
                "head_emotional": self._head_emotional,
                
                # 新增拟人化动作
                "head_listen": self._head_listen,
                "head_interest": self._head_interest,
                "head_concern": self._head_concern,
                "head_agree": self._head_agree,
                "head_disagree": self._head_disagree,
            })
        
        print(f" 已注册函数: {list(self.function_registry.keys())}")
    
    def head_control_callback(self, msg):
        """处理来自web界面的头部控制命令"""
        try:
            direction = msg.data.strip()
            print(f" 收到头部控制命令: {direction}")
            
            # 根据方向调用相应的函数，使用队列机制
            function_map = {
                'center': 'look_forward',
                'up': 'look_up',
                'down': 'look_down',
                'left': 'look_left',
                'right': 'look_right'
            }
            
            action_name = function_map.get(direction)
            if action_name:
                print(f" 将头部控制动作加入队列: {action_name}")
                success = self._queue_head_action('basic', action_name)
                if success:
                    print(f" 头部控制动作已加入队列: {action_name}")
                else:
                    print(f" 头部控制动作加入队列失败: {action_name}")
            else:
                print(f" 未知的头部控制方向: {direction}")
                
        except Exception as e:
            print(f" 处理头部控制命令时出错: {e}")
    
    def register_function(self, name: str, func: Callable):
        """
        注册自定义函数
        
        Args:
            name: 函数名
            func: 函数对象
        """
        self.function_registry[name] = func
        print(f" 已注册函数: {name}")
    
    def execute_function(self, function_name: str, *args, **kwargs) -> Dict[str, Any]:
        """
        执行指定函数
        
        Args:
            function_name: 函数名
            *args: 位置参数
            **kwargs: 关键字参数
            
        Returns:
            Dict: 执行结果
        """
        if function_name not in self.function_registry:
            error_msg = f"函数 '{function_name}' 未注册"
            print(f" {error_msg}")
            return {
                "success": False,
                "error": error_msg,
                "function_name": function_name
            }
        
        try:
            result = self.function_registry[function_name](*args, **kwargs)
            
            execution_info = {
                "success": True,
                "function_name": function_name,
                "result": result,
                "timestamp": time.time()
            }
            
            self.execution_log.append(execution_info)
            print(f" 函数执行成功: {function_name}, 结果: {result}")
            return execution_info
            
        except Exception as e:
            error_info = {
                "success": False,
                "function_name": function_name,
                "error": str(e),
                "timestamp": time.time()
            }
            
            self.execution_log.append(error_info)
            print(f" 函数执行失败: {function_name}, 错误: {e}")
            return error_info
    
    def execute_functions_async(self, function_names: list, *args, **kwargs):
        """
        异步执行多个函数
        
        Args:
            function_names: 函数名列表
            *args: 位置参数
            **kwargs: 关键字参数
        """
        def _execute_async():
            for func_name in function_names:
                self.execute_function(func_name, *args, **kwargs)
                time.sleep(0.1)  # 函数间间隔
        
        thread = threading.Thread(target=_execute_async, daemon=True)
        thread.start()
        return thread
    
    # 默认函数实现
    def _head_smile(self):
        """微笑动作"""
        print("执行微笑动作")
        if self.emoji_obj:
            self.emoji_obj.set_display_cmd("兴奋")
        print("执行微笑动作完成")
        return "执行微笑动作"
    
    def _head_nod(self):
        """点头动作"""
        if self.emoji_obj:
            self.emoji_obj.set_display_cmd("兴奋")  # 使用兴奋代替点头
        return "执行点头动作"
    
    def _head_shake(self):
        """摇头动作"""
        if self.emoji_obj:
            self.emoji_obj.set_display_cmd("正常")
        return "执行摇头动作"
    
    def _head_dance(self):
        """跳舞动作"""
        print("执行跳舞动作")
        if self.emoji_obj:
            self.emoji_obj.set_display_cmd("兴奋")  # 使用兴奋代替跳舞
        print("执行跳舞动作完成")
        return "执行跳舞动作"
    
    def _excited(self):
        """兴奋表情"""
        if self.emoji_obj:
            self.emoji_obj.set_display_cmd("兴奋")
        return "执行兴奋表情"
    
    def _normal(self):
        """正常表情"""
        if self.emoji_obj:
            self.emoji_obj.set_display_cmd("正常")
        return "执行正常表情"
    
    def _sleep(self):
        """睡觉表情"""
        if self.emoji_obj:
            self.emoji_obj.set_display_cmd("good_night")
        return "执行睡觉表情"
    
    def _wake_up(self):
        """苏醒表情"""
        if self.emoji_obj:
            self.emoji_obj.set_display_cmd("苏醒")
        return "执行苏醒表情"
    
    # 基本头部动作函数（使用队列机制）
    def _head_forward(self):
        """看向正前方"""
        success = self._queue_head_action('basic', 'look_forward')
        return f"头部看向正前方: {'已加入队列' if success else '加入队列失败'}"
    
    def _head_down(self):
        """低头"""
        success = self._queue_head_action('basic', 'look_down')
        return f"低头动作: {'已加入队列' if success else '加入队列失败'}"
    
    def _head_up(self):
        """仰头"""
        success = self._queue_head_action('basic', 'look_up')
        return f"仰头动作: {'已加入队列' if success else '加入队列失败'}"
    
    def _head_left(self):
        """向左看"""
        success = self._queue_head_action('basic', 'look_left')
        return f"向左看: {'已加入队列' if success else '加入队列失败'}"
    
    def _head_right(self):
        """向右看"""
        success = self._queue_head_action('basic', 'look_right')
        return f"向右看: {'已加入队列' if success else '加入队列失败'}"
    
    def _head_reset(self):
        """重置头部到中心位置"""
        success = self._queue_head_action('basic', 'reset_to_center')
        return f"头部重置: {'已加入队列' if success else '加入队列失败'}"
    
    def _head_position(self, pitch=None, yaw=None):
        """设置头部位置"""
        if pitch is not None and yaw is not None:
            success = self._queue_head_action('basic', 'set_head_position', pitch, yaw)
        elif pitch is not None:
            success = self._queue_head_action('basic', 'set_pitch', pitch)
        elif yaw is not None:
            success = self._queue_head_action('basic', 'set_yaw', yaw)
        else:
            return "需要提供pitch或yaw参数"
    
    # WiFi显示功能
    def _show_wifi_info(self):
        """显示WiFi信息"""
        try:
            # 使用emoji对象中的LCD显示WiFi信息
            if self.emoji_obj and hasattr(self.emoji_obj, 'show_wifi_info'):
                self.emoji_obj.show_wifi_info()
                return "WiFi信息已显示在LCD上"
            else:
                return "emoji对象不可用或没有show_wifi_info方法"
        except Exception as e:
            return f"显示WiFi信息失败: {e}"
    
    def _get_wifi_ip(self):
        """获取WiFi IP地址"""
        try:
            import subprocess
            result = subprocess.run(['hostname', '-I'], capture_output=True, text=True, timeout=5)
            if result.returncode == 0:
                ip_addresses = result.stdout.strip().split()
                for ip in ip_addresses:
                    if '.' in ip and not ip.startswith('127.'):
                        return f"当前WiFi IP地址: {ip}"
            return "未检测到WiFi连接"
        except Exception as e:
            return f"获取WiFi IP失败: {e}"
    
    def _wifi_status(self):
        """检查WiFi连接状态"""
        try:
            import subprocess
            result = subprocess.run(['hostname', '-I'], capture_output=True, text=True, timeout=5)
            if result.returncode == 0:
                ip_addresses = result.stdout.strip().split()
                for ip in ip_addresses:
                    if '.' in ip and not ip.startswith('127.'):
                        return f"WiFi已连接，IP地址: {ip}"
            return "WiFi未连接"
        except Exception as e:
            return f"检查WiFi状态失败: {e}"
    
    # 拟人化动作函数（使用队列机制）
    def _head_think(self, duration=2.0):
        """思考动作"""
        success = self._queue_head_action('basic', 'think', duration)
        return f"思考动作({duration}秒): {'已加入队列' if success else '加入队列失败'}"
    
    def _head_confused(self, duration=1.5):
        """困惑动作"""
        success = self._queue_head_action('basic', 'confused', duration)
        return f"困惑动作({duration}秒): {'已加入队列' if success else '加入队列失败'}"
    
    def _head_surprised(self, duration=1.0):
        """惊讶动作"""
        success = self._queue_head_action('basic', 'surprised', duration)
        return f"惊讶动作({duration}秒): {'已加入队列' if success else '加入队列失败'}"
    
    def _head_look_around(self, duration=3.0):
        """环视动作"""
        success = self._queue_head_action('basic', 'look_around', duration)
        return f"环视动作({duration}秒): {'已加入队列' if success else '加入队列失败'}"
    
    def _head_greet(self, duration=2.0):
        """打招呼动作"""
        success = self._queue_head_action('basic', 'greet', duration)
        return f"打招呼动作({duration}秒): {'已加入队列' if success else '加入队列失败'}"
    
    def _head_goodbye(self, duration=1.5):
        """告别动作"""
        success = self._queue_head_action('basic', 'goodbye', duration)
        return f"告别动作({duration}秒): {'已加入队列' if success else '加入队列失败'}"
    
    def _head_refuse(self, duration=1.0):
        """拒绝动作"""
        success = self._queue_head_action('basic', 'refuse', duration)
        return f"拒绝动作({duration}秒): {'已加入队列' if success else '加入队列失败'}"
    
    def _head_breathing(self, cycles=5, duration=0.5):
        """呼吸动作"""
        success = self._queue_head_action('basic', 'breathing', cycles, duration)
        return f"呼吸动作({cycles}周期, {duration}秒): {'已加入队列' if success else '加入队列失败'}"
    
    def _head_attention(self, direction="center", duration=1.0):
        """注意力动作"""
        success = self._queue_head_action('basic', 'attention', direction, duration)
        return f"注意力动作({direction}, {duration}秒): {'已加入队列' if success else '加入队列失败'}"
    
    def _head_emotional(self, emotion="happy", duration=3.0):
        """情感表达动作"""
        success = self._queue_head_action('basic', 'emotional_sequence', emotion, duration)
        return f"情感表达({emotion}, {duration}秒): {'已加入队列' if success else '加入队列失败'}"
    
    # 新增拟人化动作函数（使用队列机制）
    def _head_listen(self, duration=2.0):
        """专注倾听动作"""
        success = self._queue_head_action('basic', 'listen_attentively', duration)
        return f"专注倾听动作({duration}秒): {'已加入队列' if success else '加入队列失败'}"
    
    def _head_interest(self, duration=1.5):
        """表示兴趣动作"""
        success = self._queue_head_action('basic', 'show_interest', duration)
        return f"表示兴趣动作({duration}秒): {'已加入队列' if success else '加入队列失败'}"
    
    def _head_concern(self, duration=2.0):
        """表示关心动作"""
        success = self._queue_head_action('basic', 'show_concern', duration)
        return f"表示关心动作({duration}秒): {'已加入队列' if success else '加入队列失败'}"
    
    def _head_agree(self, duration=1.0):
        """表示同意动作"""
        success = self._queue_head_action('basic', 'show_agreement', duration)
        return f"表示同意动作({duration}秒): {'已加入队列' if success else '加入队列失败'}"
    
    def _head_disagree(self, duration=1.0):
        """表示不同意动作"""
        success = self._queue_head_action('basic', 'show_disagreement', duration)
        return f"表示不同意动作({duration}秒): {'已加入队列' if success else '加入队列失败'}"
    
    # 音量控制函数实现
    def _volume_up(self, step=5):
        """增加音量"""
        try:
            import subprocess
            result = subprocess.run(['amixer', '-D', 'pulse', 'sset', 'Master', f'{step}%+'], 
                                  capture_output=True, text=True, timeout=5)
            if result.returncode == 0:
                return f"音量增加 {step}%"
            else:
                return f"音量增加失败: {result.stderr}"
        except subprocess.TimeoutExpired:
            return "音量控制超时"
        except Exception as e:
            return f"音量控制出错: {e}"
    
    def _volume_down(self, step=5):
        """减少音量"""
        try:
            import subprocess
            result = subprocess.run(['amixer', '-D', 'pulse', 'sset', 'Master', f'{step}%-'], 
                                  capture_output=True, text=True, timeout=5)
            if result.returncode == 0:
                return f"音量减少 {step}%"
            else:
                return f"音量减少失败: {result.stderr}"
        except subprocess.TimeoutExpired:
            return "音量控制超时"
        except Exception as e:
            return f"音量控制出错: {e}"
    
    def _volume_set(self, level=50):
        """设置音量到指定级别"""
        try:
            import subprocess
            # 确保音量在0-100范围内
            level = max(0, min(100, int(level)))
            result = subprocess.run(['amixer', '-D', 'pulse', 'sset', 'Master', f'{level}%'], 
                                  capture_output=True, text=True, timeout=5)
            if result.returncode == 0:
                return f"音量设置为 {level}%"
            else:
                return f"音量设置失败: {result.stderr}"
        except subprocess.TimeoutExpired:
            return "音量控制超时"
        except Exception as e:
            return f"音量控制出错: {e}"
    
    def _volume_mute(self):
        """静音"""
        try:
            import subprocess
            result = subprocess.run(['amixer', '-D', 'pulse', 'sset', 'Master', 'mute'], 
                                  capture_output=True, text=True, timeout=5)
            if result.returncode == 0:
                return "已静音"
            else:
                return f"静音失败: {result.stderr}"
        except subprocess.TimeoutExpired:
            return "音量控制超时"
        except Exception as e:
            return f"音量控制出错: {e}"
    
    def _volume_unmute(self):
        """取消静音"""
        try:
            import subprocess
            result = subprocess.run(['amixer', '-D', 'pulse', 'sset', 'Master', 'unmute'], 
                                  capture_output=True, text=True, timeout=5)
            if result.returncode == 0:
                return "已取消静音"
            else:
                return f"取消静音失败: {result.stderr}"
        except subprocess.TimeoutExpired:
            return "音量控制超时"
        except Exception as e:
            return f"音量控制出错: {e}"
    
    def _volume_status(self):
        """获取当前音量状态"""
        try:
            import subprocess
            result = subprocess.run(['amixer', '-D', 'pulse', 'get', 'Master'], 
                                  capture_output=True, text=True, timeout=5)
            if result.returncode == 0:
                # 解析音量信息
                output = result.stdout
                lines = output.split('\n')
                for line in lines:
                    if '[' in line and '%' in line:
                        # 提取音量百分比
                        import re
                        match = re.search(r'\[(\d+)%\]', line)
                        if match:
                            volume = match.group(1)
                            # 检查是否静音
                            is_muted = '[off]' in line or '[muted]' in line
                            status = "静音" if is_muted else "正常"
                            return f"当前音量: {volume}% ({status})"
                return "无法解析音量信息"
            else:
                return f"获取音量状态失败: {result.stderr}"
        except subprocess.TimeoutExpired:
            return "音量控制超时"
        except Exception as e:
            return f"音量控制出错: {e}"
    
    def _play_music(self, song_name):
        """播放音乐"""
        if not CLOUD_MUSIC_AVAILABLE:
            return "云音乐功能不可用"
        
        try:
            if self.emoji_obj:
                self.emoji_obj.set_display_cmd("兴奋")  # 播放音乐时显示兴奋表情
            
            result = play_cloud_music(song_name)
            if result == 0:
                # 通知ASR音乐开始播放，启用过滤模式
                if self.asr_instance:
                    self.asr_instance.set_music_playing(True)
                return f" 正在播放: {song_name}"
            else:
                return f" 播放失败: {song_name}"
        except Exception as e:
            return f" 音乐播放出错: {e}"
    
    def _play_song(self, song_name):
        """播放歌曲（play_music的别名）"""
        return self._play_music(song_name)
    
    def _stop_music(self):
        """停止音乐播放"""
        if not CLOUD_MUSIC_AVAILABLE:
            return "云音乐功能不可用"
        
        try:
            print(" 停止音乐播放...")
            if self.emoji_obj:
                self.emoji_obj.set_display_cmd("正常")  # 停止音乐时显示正常表情
            
            result = stop_music()
            if result:
                # 通知ASR音乐停止播放，关闭过滤模式
                if self.asr_instance:
                    self.asr_instance.set_music_playing(False)
                return " 音乐已停止"
            else:
                return " 停止音乐失败"
        except Exception as e:
            return f" 停止音乐出错: {e}"
    
    def _next_song(self):
        """切到下一首歌"""
        if not CLOUD_MUSIC_AVAILABLE:
            return "云音乐功能不可用"
        
        try:
            print(" 切到下一首歌...")
            if self.emoji_obj:
                self.emoji_obj.set_display_cmd("兴奋")  # 切歌时显示兴奋表情
            
            result = next_song()
            if result:
                return " 已切换到下一首歌"
            else:
                return " 没有下一首歌或切换失败"
        except Exception as e:
            return f" 切歌出错: {e}"
    
    def _add_to_queue(self, song_name):
        """添加歌曲到播放队列"""
        if not CLOUD_MUSIC_AVAILABLE:
            return "云音乐功能不可用"
        
        try:
            print(f" 添加歌曲到队列: {song_name}")
            add_to_queue(song_name)
            return f" 已添加 {song_name} 到播放队列"
        except Exception as e:
            return f" 添加歌曲到队列出错: {e}"
    
    def _music_status(self):
        """获取音乐播放状态"""
        if not CLOUD_MUSIC_AVAILABLE:
            return "云音乐功能不可用"
        
        try:
            status = get_music_status()
            if status["is_playing"]:
                current = status["current_song"]
                queue_count = len(status["queue"])
                return f" 正在播放: {current}，队列中有 {queue_count} 首歌"
            else:
                queue_count = len(status["queue"])
                return f" 当前没有播放音乐，队列中有 {queue_count} 首歌"
        except Exception as e:
            return f" 获取音乐状态出错: {e}"
    
    def _recommend_songs(self, song_name=None):
        """获取推荐歌曲"""
        if not CLOUD_MUSIC_AVAILABLE:
            return "云音乐功能不可用"
        
        try:
            if song_name:
                recommended = get_recommended_songs(song_name)
                return f" 基于 '{song_name}' 的推荐歌曲: {', '.join(recommended)}"
            else:
                recommended = get_recommended_songs()
                return f" 推荐歌曲: {', '.join(recommended)}"
        except Exception as e:
            return f" 获取推荐歌曲出错: {e}"
    
    # 机器人跟随控制函数实现
    def _start_following(self):
        """启动机器人跟随功能"""
        if not self.following_controller:
            return "机器人跟随控制器不可用"
        
        try:
            result = self.following_controller.start_following()
            if result["success"]:
                return f" {result['message']}"
            else:
                return f" 启动跟随失败: {result.get('error', '未知错误')}"
        except Exception as e:
            return f" 启动跟随功能出错: {e}"
    
    def _stop_following(self):
        """停止机器人跟随功能"""
        if not self.following_controller:
            return "机器人跟随控制器不可用"
        
        try:
            result = self.following_controller.stop_following()
            if result["success"]:
                return f" {result['message']}"
            else:
                return f" 停止跟随失败: {result.get('error', '未知错误')}"
        except Exception as e:
            return f" 停止跟随功能出错: {e}"
    
    def _toggle_following(self):
        """切换机器人跟随功能状态"""
        if not self.following_controller:
            return "机器人跟随控制器不可用"
        
        try:
            result = self.following_controller.toggle_following()
            if result["success"]:
                return f" {result['message']}"
            else:
                return f" 切换跟随状态失败: {result.get('error', '未知错误')}"
        except Exception as e:
            return f" 切换跟随功能出错: {e}"
    
    def _following_status(self):
        """获取机器人跟随状态"""
        if not self.following_controller:
            return "机器人跟随控制器不可用"
        
        try:
            status = self.following_controller.get_status()
            if status["following_active"]:
                return f" 机器人跟随功能: 已启用 (跟踪: {'是' if status['tracking_enabled'] else '否'}, 激光雷达: {'是' if status['lidar_enabled'] else '否'})"
            else:
                return f" 机器人跟随功能: 已禁用 (跟踪: {'是' if status['tracking_enabled'] else '否'}, 激光雷达: {'是' if status['lidar_enabled'] else '否'})"
        except Exception as e:
            return f" 获取跟随状态出错: {e}"
    
    def _enable_tracking(self):
        """启用跟踪功能"""
        if not self.following_controller:
            return "机器人跟随控制器不可用"
        
        try:
            result = self.following_controller.enable_tracking_only()
            if result["success"]:
                return f" {result['message']}"
            else:
                return f" 启用跟踪失败: {result.get('error', '未知错误')}"
        except Exception as e:
            return f" 启用跟踪功能出错: {e}"
    
    def _disable_tracking(self):
        """禁用跟踪功能"""
        if not self.following_controller:
            return "机器人跟随控制器不可用"
        
        try:
            result = self.following_controller.disable_tracking_only()
            if result["success"]:
                return f" {result['message']}"
            else:
                return f" 禁用跟踪失败: {result.get('error', '未知错误')}"
        except Exception as e:
            return f" 禁用跟踪功能出错: {e}"
    
    def _enable_lidar(self):
        """启用激光雷达功能"""
        if not self.following_controller:
            return "机器人跟随控制器不可用"
        
        try:
            result = self.following_controller.enable_lidar_only()
            if result["success"]:
                return f" {result['message']}"
            else:
                return f" 启用激光雷达失败: {result.get('error', '未知错误')}"
        except Exception as e:
            return f" 启用激光雷达功能出错: {e}"
    
    def _disable_lidar(self):
        """禁用激光雷达功能"""
        if not self.following_controller:
            return "机器人跟随控制器不可用"
        
        try:
            result = self.following_controller.disable_lidar_only()
            if result["success"]:
                return f" {result['message']}"
            else:
                return f" 禁用激光雷达失败: {result.get('error', '未知错误')}"
        except Exception as e:
            return f" 禁用激光雷达功能出错: {e}"
    
    # 导航控制函数实现
    def _navigate_to(self, location):
        """
        导航到指定位置
        
        Args:
            location: 位置名称（如"厨房"、"客厅"）
        """
        if not self.navigation_controller:
            return "导航控制器不可用"
        
        try:
            result = self.navigation_controller.navigate_to_location(location)
            if result["success"]:
                return f" {result['message']}"
            else:
                return f" 导航失败: {result.get('error', '未知错误')}"
        except Exception as e:
            return f" 导航功能出错: {e}"
    
    def _cancel_navigation(self):
        """取消当前导航任务"""
        if not self.navigation_controller:
            return "导航控制器不可用"
        
        try:
            result = self.navigation_controller.cancel_navigation()
            if result["success"]:
                return f" {result['message']}"
            else:
                return f" {result.get('message', '取消导航失败')}"
        except Exception as e:
            return f" 取消导航功能出错: {e}"
    
    def _navigation_status(self):
        """获取导航状态"""
        if not self.navigation_controller:
            return "导航控制器不可用"
        
        try:
            status = self.navigation_controller.get_navigation_status()
            if status["initialized"]:
                if status["navigating"]:
                    state_text = status.get("state_text", "进行中")
                    return f" 导航状态: {state_text}"
                else:
                    locations = ", ".join(status["available_locations"])
                    return f" 导航空闲。可用位置: {locations}"
            else:
                return " 导航控制器未初始化"
        except Exception as e:
            return f" 获取导航状态出错: {e}"
    
    def _list_locations(self):
        """列出所有可用位置"""
        if not self.navigation_controller:
            return "导航控制器不可用"
        
        try:
            locations = self.navigation_controller.list_locations()
            if locations:
                locations_str = "、".join(locations)
                return f" 可用位置: {locations_str}"
            else:
                return " 暂无可用位置"
        except Exception as e:
            return f" 获取位置列表出错: {e}"
    
    def _add_location(self, name, x, y, z=0.0, qx=0.0, qy=0.0, qz=0.0, qw=1.0):
        """
        添加新位置
        
        Args:
            name: 位置名称
            x, y, z: 位置坐标
            qx, qy, qz, qw: 姿态四元数
        """
        if not self.navigation_controller:
            return "导航控制器不可用"
        
        try:
            self.navigation_controller.add_location(name, x, y, z, qx, qy, qz, qw)
            return f" 已添加位置: {name}"
        except Exception as e:
            return f" 添加位置出错: {e}"
    
    def get_execution_log(self) -> list:
        """获取执行日志"""
        return self.execution_log.copy()
    
    def clear_execution_log(self):
        """清空执行日志"""
        self.execution_log.clear()
    
    def get_registered_functions(self) -> list:
        """获取已注册的函数列表"""
        return list(self.function_registry.keys())
    
    def cleanup(self):
        """清理资源"""
        # 停止头部动作队列线程
        self._stop_head_action_queue_thread()
        
        if self.head_controller:
            try:
                self.head_controller.cleanup()
                print(" 头部控制器资源已清理")
            except Exception as e:
                print(f" 清理头部控制器资源时出错: {e}")
        
        if self.following_controller:
            try:
                self.following_controller.cleanup()
                print(" 机器人跟随控制器资源已清理")
            except Exception as e:
                print(f" 清理机器人跟随控制器资源时出错: {e}")
        
        if self.navigation_controller:
            try:
                self.navigation_controller.cleanup()
                print(" 导航控制器资源已清理")
            except Exception as e:
                print(f" 清理导航控制器资源时出错: {e}")
    
    def __del__(self):
        """析构函数，确保资源被清理"""
        self.cleanup()


# 全局函数执行器实例
_global_executor = None

def get_global_executor() -> FunctionExecutor:
    """获取全局函数执行器实例"""
    global _global_executor
    if _global_executor is None:
        _global_executor = FunctionExecutor()
    return _global_executor

def set_global_executor(executor: FunctionExecutor):
    """设置全局函数执行器实例"""
    global _global_executor
    _global_executor = executor

def execute_function(function_name: str, *args, **kwargs) -> Dict[str, Any]:
    """便捷函数：执行指定函数"""
    return get_global_executor().execute_function(function_name, *args, **kwargs)

def execute_functions_async(function_names: list, *args, **kwargs):
    """便捷函数：异步执行多个函数"""
    return get_global_executor().execute_functions_async(function_names, *args, **kwargs)


# 使用示例
if __name__ == "__main__":
    # 创建函数执行器
    executor = FunctionExecutor()
    
    print("=" * 50)
    print(" 开始测试函数执行器")
    print("=" * 50)
    
    # 显示已注册的函数
    print(f" 已注册的函数: {executor.get_registered_functions()}")
    
    # 测试单个函数执行
    print("\n 测试单个函数执行:")
    result = executor.execute_function("head_nod")
    print(f"执行结果: {result}")
    
    # 测试头部动作函数
    print("\n 测试头部动作函数:")
    if executor.head_controller:
        # 测试基本头部动作
        result = executor.execute_function("head_forward")
        print(f"head_forward: {result}")
        
        result = executor.execute_function("head_think", 1.0)
        print(f"head_think: {result}")
        
        result = executor.execute_function("head_greet", 1.0)
        print(f"head_greet: {result}")
        
        result = executor.execute_function("head_emotional", "happy", 2.0)
        print(f"head_emotional: {result}")
        
        # 测试新增拟人化动作
        result = executor.execute_function("head_listen", 1.0)
        print(f"head_listen: {result}")
        
        result = executor.execute_function("head_interest", 1.0)
        print(f"head_interest: {result}")
        
        result = executor.execute_function("head_concern", 1.0)
        print(f"head_concern: {result}")
        
        result = executor.execute_function("head_agree")
        print(f"head_agree: {result}")
        
        result = executor.execute_function("head_disagree")
        print(f"head_disagree: {result}")
    else:
        print("头部控制器不可用，跳过头部动作测试")
    
    # 测试不存在的函数
    print("\n 测试不存在的函数:")
    result = executor.execute_function("nonexistent_function")
    print(f"执行结果: {result}")
    
    # 测试异步执行
    print("\n 测试异步执行:")
    if executor.head_controller:
        executor.execute_functions_async(["head_smile", "head_think", "excited"])
    else:
        executor.execute_functions_async(["head_smile", "excited"])
    
    # 等待一下让异步执行完成
    import time
    time.sleep(2)
    
    # 显示执行日志
    print("\n 执行日志:")
    for log in executor.get_execution_log():
        print(f"  - {log}")
    
    # 清理资源
    print("\n 清理资源:")
    executor.cleanup()
    
    print("\n 测试完成")
