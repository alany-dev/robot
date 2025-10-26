#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
机器人跟随控制器
用于控制机器人的跟随功能，通过ROS话题发布控制命令
"""

import rospy
from std_msgs.msg import Bool
import time
import threading
from typing import Dict, Any, Optional

class RobotFollowingController:
    """机器人跟随控制器类"""
    
    def __init__(self):
        """
        初始化机器人跟随控制器
        """
        self.tracking_enabled = False
        self.lidar_enabled = False
        self.is_initialized = False
        
        # ROS话题发布器
        self.tracking_pub = None
        self.lidar_pub = None
        
        # 初始化ROS节点和发布器
        self._initialize_ros()
    
    def _initialize_ros(self):
        """
        初始化ROS节点和发布器
        """
        try:
            # 初始化ROS节点（如果还没有初始化）
            if not rospy.get_node_uri():
                rospy.init_node('robot_following_controller', anonymous=True)
            
            # 创建发布器
            self.tracking_pub = rospy.Publisher('/enable_tracking', Bool, queue_size=1)
            self.lidar_pub = rospy.Publisher('/enable_lidar', Bool, queue_size=1)
            
            # 等待发布器连接
            time.sleep(0.5)
            
            self.is_initialized = True
            print("机器人跟随控制器ROS初始化成功")
            
        except Exception as e:
            print(f"ROS初始化失败: {e}")
            self.is_initialized = False
    
    def _publish_tracking_command(self, enable: bool):
        """
        发布跟踪控制命令
        
        Args:
            enable: 是否启用跟踪
        """
        if not self.is_initialized or not self.tracking_pub:
            print("ROS未初始化或发布器不可用")
            return False
        
        try:
            msg = Bool()
            msg.data = enable
            self.tracking_pub.publish(msg)
            self.tracking_enabled = enable
            print(f"已发布跟踪控制命令: {'启用' if enable else '禁用'}")
            return True
        except Exception as e:
            print(f"发布跟踪命令失败: {e}")
            return False
    
    def _publish_lidar_command(self, enable: bool):
        """
        发布激光雷达控制命令
        
        Args:
            enable: 是否启用激光雷达
        """
        if not self.is_initialized or not self.lidar_pub:
            print("ROS未初始化或发布器不可用")
            return False
        
        try:
            msg = Bool()
            msg.data = enable
            self.lidar_pub.publish(msg)
            self.lidar_enabled = enable
            print(f"已发布激光雷达控制命令: {'启用' if enable else '禁用'}")
            return True
        except Exception as e:
            print(f"发布激光雷达命令失败: {e}")
            return False
    
    def start_following(self):
        """
        启动机器人跟随功能
        
        Returns:
            Dict: 执行结果
        """
        print("启动机器人跟随功能...")
        
        if not self.is_initialized:
            return {
                "success": False,
                "error": "ROS未初始化",
                "action": "start_following"
            }
        
        try:
            # 启用激光雷达
            lidar_success = self._publish_lidar_command(True)
            time.sleep(0.1)  # 短暂延迟
            
            # 启用跟踪
            tracking_success = self._publish_tracking_command(True)
            
            if lidar_success and tracking_success:
                print("机器人跟随功能已启动")
                return {
                    "success": True,
                    "message": "机器人跟随功能已启动",
                    "tracking_enabled": True,
                    "lidar_enabled": True,
                    "action": "start_following"
                }
            else:
                return {
                    "success": False,
                    "error": "部分命令发布失败",
                    "tracking_enabled": self.tracking_enabled,
                    "lidar_enabled": self.lidar_enabled,
                    "action": "start_following"
                }
                
        except Exception as e:
            print(f"启动跟随功能时出错: {e}")
            return {
                "success": False,
                "error": str(e),
                "action": "start_following"
            }
    
    def stop_following(self):
        """
        停止机器人跟随功能
        
        Returns:
            Dict: 执行结果
        """
        print("停止机器人跟随功能...")
        
        if not self.is_initialized:
            return {
                "success": False,
                "error": "ROS未初始化",
                "action": "stop_following"
            }
        
        try:
            # 禁用跟踪
            tracking_success = self._publish_tracking_command(False)
            time.sleep(0.1)  # 短暂延迟
            
            # 禁用激光雷达
            lidar_success = self._publish_lidar_command(False)
            
            if tracking_success and lidar_success:
                print("机器人跟随功能已停止")
                return {
                    "success": True,
                    "message": "机器人跟随功能已停止",
                    "tracking_enabled": False,
                    "lidar_enabled": False,
                    "action": "stop_following"
                }
            else:
                return {
                    "success": False,
                    "error": "部分命令发布失败",
                    "tracking_enabled": self.tracking_enabled,
                    "lidar_enabled": self.lidar_enabled,
                    "action": "stop_following"
                }
                
        except Exception as e:
            print(f"停止跟随功能时出错: {e}")
            return {
                "success": False,
                "error": str(e),
                "action": "stop_following"
            }
    
    def toggle_following(self):
        """
        切换机器人跟随功能状态
        
        Returns:
            Dict: 执行结果
        """
        if self.tracking_enabled and self.lidar_enabled:
            return self.stop_following()
        else:
            return self.start_following()
    
    def get_status(self):
        """
        获取当前跟随状态
        
        Returns:
            Dict: 状态信息
        """
        return {
            "is_initialized": self.is_initialized,
            "tracking_enabled": self.tracking_enabled,
            "lidar_enabled": self.lidar_enabled,
            "following_active": self.tracking_enabled and self.lidar_enabled
        }
    
    def enable_tracking_only(self):
        """
        仅启用跟踪功能（不启用激光雷达）
        
        Returns:
            Dict: 执行结果
        """
        print("启用跟踪功能...")
        
        if not self.is_initialized:
            return {
                "success": False,
                "error": "ROS未初始化",
                "action": "enable_tracking_only"
            }
        
        try:
            success = self._publish_tracking_command(True)
            if success:
                print("跟踪功能已启用")
                return {
                    "success": True,
                    "message": "跟踪功能已启用",
                    "tracking_enabled": True,
                    "action": "enable_tracking_only"
                }
            else:
                return {
                    "success": False,
                    "error": "跟踪命令发布失败",
                    "action": "enable_tracking_only"
                }
        except Exception as e:
            print(f"启用跟踪功能时出错: {e}")
            return {
                "success": False,
                "error": str(e),
                "action": "enable_tracking_only"
            }
    
    def enable_lidar_only(self):
        """
        仅启用激光雷达功能（不启用跟踪）
        
        Returns:
            Dict: 执行结果
        """
        print("启用激光雷达功能...")
        
        if not self.is_initialized:
            return {
                "success": False,
                "error": "ROS未初始化",
                "action": "enable_lidar_only"
            }
        
        try:
            success = self._publish_lidar_command(True)
            if success:
                print("激光雷达功能已启用")
                return {
                    "success": True,
                    "message": "激光雷达功能已启用",
                    "lidar_enabled": True,
                    "action": "enable_lidar_only"
                }
            else:
                return {
                    "success": False,
                    "error": "激光雷达命令发布失败",
                    "action": "enable_lidar_only"
                }
        except Exception as e:
            print(f"启用激光雷达功能时出错: {e}")
            return {
                "success": False,
                "error": str(e),
                "action": "enable_lidar_only"
            }
    
    def disable_tracking_only(self):
        """
        仅禁用跟踪功能
        
        Returns:
            Dict: 执行结果
        """
        print("禁用跟踪功能...")
        
        if not self.is_initialized:
            return {
                "success": False,
                "error": "ROS未初始化",
                "action": "disable_tracking_only"
            }
        
        try:
            success = self._publish_tracking_command(False)
            if success:
                print("跟踪功能已禁用")
                return {
                    "success": True,
                    "message": "跟踪功能已禁用",
                    "tracking_enabled": False,
                    "action": "disable_tracking_only"
                }
            else:
                return {
                    "success": False,
                    "error": "跟踪命令发布失败",
                    "action": "disable_tracking_only"
                }
        except Exception as e:
            print(f"禁用跟踪功能时出错: {e}")
            return {
                "success": False,
                "error": str(e),
                "action": "disable_tracking_only"
            }
    
    def disable_lidar_only(self):
        """
        仅禁用激光雷达功能
        
        Returns:
            Dict: 执行结果
        """
        print("禁用激光雷达功能...")
        
        if not self.is_initialized:
            return {
                "success": False,
                "error": "ROS未初始化",
                "action": "disable_lidar_only"
            }
        
        try:
            success = self._publish_lidar_command(False)
            if success:
                print("激光雷达功能已禁用")
                return {
                    "success": True,
                    "message": "激光雷达功能已禁用",
                    "lidar_enabled": False,
                    "action": "disable_lidar_only"
                }
            else:
                return {
                    "success": False,
                    "error": "激光雷达命令发布失败",
                    "action": "disable_lidar_only"
                }
        except Exception as e:
            print(f"禁用激光雷达功能时出错: {e}")
            return {
                "success": False,
                "error": str(e),
                "action": "disable_lidar_only"
            }
    
    def cleanup(self):
        """
        清理资源
        """
        try:
            if self.is_initialized:
                # 停止跟随功能
                self.stop_following()
                print("机器人跟随控制器资源已清理")
        except Exception as e:
            print(f"清理资源时出错: {e}")
    
    def __del__(self):
        """
        析构函数，确保资源被清理
        """
        self.cleanup()


# 全局机器人跟随控制器实例
_global_following_controller = None

def get_global_following_controller() -> RobotFollowingController:
    """
    获取全局机器人跟随控制器实例
    
    Returns:
        RobotFollowingController: 全局控制器实例
    """
    global _global_following_controller
    if _global_following_controller is None:
        _global_following_controller = RobotFollowingController()
    return _global_following_controller

def set_global_following_controller(controller: RobotFollowingController):
    """
    设置全局机器人跟随控制器实例
    
    Args:
        controller: 控制器实例
    """
    global _global_following_controller
    _global_following_controller = controller


# 使用示例
if __name__ == "__main__":
    print("=" * 60)
    print("机器人跟随控制器测试")
    print("=" * 60)
    
    # 创建控制器实例
    controller = RobotFollowingController()
    
    # 检查初始化状态
    print(f"初始化状态: {controller.get_status()}")
    
    if controller.is_initialized:
        print("\n测试跟随功能...")
        
        # 测试启动跟随
        result = controller.start_following()
        print(f"启动跟随结果: {result}")
        
        # 等待一段时间
        time.sleep(2)
        
        # 测试获取状态
        status = controller.get_status()
        print(f"当前状态: {status}")
        
        # 测试停止跟随
        result = controller.stop_following()
        print(f"停止跟随结果: {result}")
        
        # 测试单独控制
        print("\n测试单独控制...")
        controller.enable_tracking_only()
        time.sleep(1)
        controller.enable_lidar_only()
        time.sleep(1)
        controller.disable_tracking_only()
        time.sleep(1)
        controller.disable_lidar_only()
        
        # 测试切换功能
        print("\n测试切换功能...")
        controller.toggle_following()
        time.sleep(1)
        controller.toggle_following()
        
    else:
        print("控制器初始化失败，跳过测试")
    
    # 清理资源
    print("\n清理资源...")
    controller.cleanup()
    
    print("\n测试完成")




