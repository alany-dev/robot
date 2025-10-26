#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
机器人导航控制器
用于语音控制机器人导航到指定位置
"""

import threading
import time
from typing import Dict, Any, Optional, Tuple

# 导入ROS相关模块
try:
    import rospy
    import actionlib
    from actionlib_msgs.msg import GoalStatus
    from geometry_msgs.msg import Pose, Point, Quaternion
    from move_base_msgs.msg import MoveBaseAction, MoveBaseGoal
    ROS_AVAILABLE = True
    print("ROS导航模块导入成功")
except ImportError as e:
    ROS_AVAILABLE = False
    print(f"ROS导航模块导入失败: {e}")
    print("导航功能将不可用")


class NavigationController:
    """机器人导航控制器类"""
    
    # 预定义位置字典 (x, y, z, qx, qy, qz, qw)
    LOCATIONS = {
        "厨房": (2.491, -0.331, 0.000, 0.000, 0.000, -0.389, 0.921)
    }
    
    def __init__(self):
        """初始化导航控制器"""
        self.is_initialized = False
        self.move_base_client = None
        self.current_goal = None
        self.navigation_thread = None
        self.is_navigating = False
        
        if not ROS_AVAILABLE:
            print("ROS不可用，导航控制器初始化失败")
            return
        
        try:
            self.move_base_client = actionlib.SimpleActionClient("move_base", MoveBaseAction)
            
            self.is_initialized = True
            print("导航控制器初始化成功（move_base client已创建）")
            print("导航时会自动连接move_base服务器")
                
        except Exception as e:
            print(f"导航控制器初始化失败: {e}")
            self.is_initialized = False
    
    def add_location(self, name: str, x: float, y: float, z: float = 0.0,
                     qx: float = 0.0, qy: float = 0.0, qz: float = 0.0, qw: float = 1.0):
        """
        添加新位置到位置字典
        
        Args:
            name: 位置名称
            x, y, z: 位置坐标
            qx, qy, qz, qw: 四元数姿态
        """
        self.LOCATIONS[name] = (x, y, z, qx, qy, qz, qw)
        print(f"已添加位置: {name} -> ({x}, {y}, {z})")
    
    def get_location(self, name: str) -> Optional[Tuple]:
        """
        获取指定位置的坐标
        
        Args:
            name: 位置名称
            
        Returns:
            位置坐标元组或None
        """
        return self.LOCATIONS.get(name)
    
    def list_locations(self) -> list:
        """
        列出所有可用位置
        
        Returns:
            位置名称列表
        """
        return list(self.LOCATIONS.keys())
    
    def navigate_to_location(self, location_name: str, timeout: float = 300.0) -> Dict[str, Any]:
        """
        导航到指定位置（异步）
        
        Args:
            location_name: 位置名称
            timeout: 导航超时时间（秒）
            
        Returns:
            执行结果字典
        """
        if not self.is_initialized:
            return {
                "success": False,
                "error": "导航控制器未初始化"
            }
        
        # 等待move_base服务器连接（10秒超时）
        print("正在连接move_base服务器...")
        server_ready = self.move_base_client.wait_for_server(rospy.Duration(10.0))
        
        if not server_ready:
            return {
                "success": False,
                "error": "无法连接到move_base服务器，请确保导航系统已启动 (roslaunch robot_navigation robot_navigation.launch)"
            }
        
        # 检查位置是否存在
        if location_name not in self.LOCATIONS:
            available = ", ".join(self.LOCATIONS.keys())
            return {
                "success": False,
                "error": f"未知位置: {location_name}。可用位置: {available}"
            }
        
        # 如果正在导航，取消当前导航
        if self.is_navigating:
            print("已有导航任务在执行，取消当前任务")
            self.cancel_navigation()
            time.sleep(0.5)
        
        # 获取位置坐标
        x, y, z, qx, qy, qz, qw = self.LOCATIONS[location_name]
        
        # 在后台线程中执行导航
        self.navigation_thread = threading.Thread(
            target=self._navigate_async,
            args=(location_name, x, y, z, qx, qy, qz, qw, timeout),
            daemon=True
        )
        self.navigation_thread.start()
        
        return {
            "success": True,
            "message": f"开始导航到{location_name}",
            "location": location_name,
            "coordinates": {"x": x, "y": y, "z": z}
        }
    
    def navigate_to_coordinates(self, x: float, y: float, z: float = 0.0,
                                qx: float = 0.0, qy: float = 0.0, 
                                qz: float = 0.0, qw: float = 1.0,
                                timeout: float = 300.0) -> Dict[str, Any]:
        """
        导航到指定坐标（异步）
        
        Args:
            x, y, z: 目标位置坐标
            qx, qy, qz, qw: 目标姿态（四元数）
            timeout: 导航超时时间（秒）
            
        Returns:
            执行结果字典
        """
        if not self.is_initialized:
            return {
                "success": False,
                "error": "导航控制器未初始化"
            }
        
        # 等待move_base服务器连接（10秒超时）
        print("正在连接move_base服务器...")
        server_ready = self.move_base_client.wait_for_server(rospy.Duration(10.0))
        
        if not server_ready:
            return {
                "success": False,
                "error": "无法连接到move_base服务器，请确保导航系统已启动"
            }
        
        # 如果正在导航，取消当前导航
        if self.is_navigating:
            print("已有导航任务在执行，取消当前任务")
            self.cancel_navigation()
            time.sleep(0.5)
        
        # 在后台线程中执行导航
        self.navigation_thread = threading.Thread(
            target=self._navigate_async,
            args=("自定义位置", x, y, z, qx, qy, qz, qw, timeout),
            daemon=True
        )
        self.navigation_thread.start()
        
        return {
            "success": True,
            "message": f"开始导航到坐标 ({x:.2f}, {y:.2f})",
            "coordinates": {"x": x, "y": y, "z": z}
        }
    
    def _navigate_async(self, location_name: str, x: float, y: float, z: float,
                       qx: float, qy: float, qz: float, qw: float, timeout: float):
        """
        异步导航执行函数
        
        Args:
            location_name: 位置名称（用于日志）
            x, y, z: 目标位置
            qx, qy, qz, qw: 目标姿态
            timeout: 超时时间
        """
        self.is_navigating = True
        
        try:
            # 创建目标
            goal = MoveBaseGoal()
            goal.target_pose.header.frame_id = 'map'
            goal.target_pose.header.stamp = rospy.Time.now()
            
            # 设置目标位置和姿态
            goal.target_pose.pose = Pose(
                Point(x, y, z),
                Quaternion(qx, qy, qz, qw)
            )
            
            self.current_goal = goal
            
            print(f"目标设定: {location_name}")
            print(f"   位置: ({x:.3f}, {y:.3f}, {z:.3f})")
            print(f"   姿态: ({qx:.3f}, {qy:.3f}, {qz:.3f}, {qw:.3f})")
            
            # 发送目标
            self.move_base_client.send_goal(goal)
            print(f"导航目标已发送，等待结果...")
            
            # 等待结果
            finished = self.move_base_client.wait_for_result(rospy.Duration(timeout))
            
            if finished:
                state = self.move_base_client.get_state()
                if state == GoalStatus.SUCCEEDED:
                    print(f"成功到达 {location_name}!")
                elif state == GoalStatus.PREEMPTED:
                    print(f"导航被取消")
                elif state == GoalStatus.ABORTED:
                    print(f"导航失败: 无法到达目标")
                else:
                    print(f"导航结束，状态码: {state}")
            else:
                print(f"导航超时 ({timeout}秒)")
                self.move_base_client.cancel_goal()
                
        except Exception as e:
            print(f"导航过程中出错: {e}")
        finally:
            self.is_navigating = False
            self.current_goal = None
    
    def cancel_navigation(self) -> Dict[str, Any]:
        """
        取消当前导航任务
        
        Returns:
            执行结果字典
        """
        if not self.is_initialized:
            return {
                "success": False,
                "error": "导航控制器未初始化"
            }
        
        if not self.is_navigating:
            return {
                "success": False,
                "message": "当前没有正在执行的导航任务"
            }
        
        try:
            self.move_base_client.cancel_goal()
            print("导航任务已取消")
            return {
                "success": True,
                "message": "导航任务已取消"
            }
        except Exception as e:
            return {
                "success": False,
                "error": f"取消导航失败: {e}"
            }
    
    def get_navigation_status(self) -> Dict[str, Any]:
        """
        获取导航状态
        
        Returns:
            状态信息字典
        """
        if not self.is_initialized:
            return {
                "initialized": False,
                "navigating": False,
                "message": "导航控制器未初始化"
            }
        
        status = {
            "initialized": True,
            "navigating": self.is_navigating,
            "available_locations": self.list_locations()
        }
        
        if self.is_navigating and self.move_base_client:
            try:
                state = self.move_base_client.get_state()
                status["state"] = state
                status["state_text"] = self._get_state_text(state)
            except:
                pass
        
        return status
    
    def _get_state_text(self, state: int) -> str:
        """获取状态文本描述"""
        state_texts = {
            GoalStatus.PENDING: "等待中",
            GoalStatus.ACTIVE: "执行中",
            GoalStatus.PREEMPTED: "已取消",
            GoalStatus.SUCCEEDED: "成功",
            GoalStatus.ABORTED: "失败",
            GoalStatus.REJECTED: "被拒绝",
            GoalStatus.PREEMPTING: "取消中",
            GoalStatus.RECALLING: "召回中",
            GoalStatus.RECALLED: "已召回",
            GoalStatus.LOST: "丢失"
        }
        return state_texts.get(state, f"未知状态({state})")
    
    def cleanup(self):
        """清理资源"""
        if self.is_navigating:
            try:
                self.cancel_navigation()
                print("导航任务已清理")
            except Exception as e:
                print(f"清理导航任务时出错: {e}")


# 全局导航控制器实例
_global_navigation_controller = None

def get_global_navigation_controller() -> NavigationController:
    """获取全局导航控制器实例"""
    global _global_navigation_controller
    if _global_navigation_controller is None:
        _global_navigation_controller = NavigationController()
    return _global_navigation_controller


# 测试代码
if __name__ == "__main__":
    # 初始化ROS节点
    if ROS_AVAILABLE:
        rospy.init_node('navigation_controller_test', anonymous=True)
    
    # 创建导航控制器
    nav = NavigationController()
    
    if nav.is_initialized:
        print("=" * 50)
        print("导航控制器测试")
        print("=" * 50)
        
        # 显示可用位置
        print(f"\n可用位置: {nav.list_locations()}")
        
        # 测试导航到厨房
        print("\n测试: 导航到厨房")
        result = nav.navigate_to_location("厨房", timeout=60.0)
        print(f"结果: {result}")
        
        # 等待一段时间
        time.sleep(5)
        
        # 获取状态
        status = nav.get_navigation_status()
        print(f"\n当前状态: {status}")
        
        # 等待导航完成或用户中断
        try:
            while nav.is_navigating:
                time.sleep(1)
        except KeyboardInterrupt:
            print("\n用户中断")
            nav.cancel_navigation()
        
        print("\n测试完成")
    else:
        print("导航控制器初始化失败，无法进行测试")

