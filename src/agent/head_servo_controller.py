#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
头部舵机控制器
控制机器人头部的俯仰角和左右转动
调用servo_controller_pwm.py中的接口
"""

import time
import sys
from typing import Optional, Tuple

# 导入PWM舵机控制器
try:
    from servo_controller_pwm import ServoControllerPWM
    print("使用PWM舵机控制器")
except ImportError:
    print("错误: 未找到servo_controller_pwm模块")
    print("请确保servo_controller_pwm.py文件在同一目录下")
    sys.exit(1)


class HeadServoController:
    """头部舵机控制器类"""
    
    def __init__(self, pitch_chip: int = 1, yaw_chip: int = 2, frequency: float = 50.0):
        """
        初始化头部舵机控制器
        
        Args:
            pitch_chip: 俯仰角PWM芯片编号 (默认1)
            yaw_chip: 左右转动PWM芯片编号 (默认2)
            frequency: PWM频率，默认50Hz
        """
        self.pitch_chip = pitch_chip
        self.yaw_chip = yaw_chip
        self.frequency = frequency
        
        # 俯仰角舵机参数 (chip=1)
        self.pitch_servo = None
        self.pitch_min_angle = 65   # 低头角度
        self.pitch_max_angle = 120  # 仰头角度
        self.pitch_center_angle = 90  # 正前方角度
        
        # 左右转动舵机参数 (chip=2)
        self.yaw_servo = None
        self.yaw_min_angle = 50    # 右转角度
        self.yaw_max_angle = 130   # 左转角度
        self.yaw_center_angle = 90  # 正中间角度
        
        # 当前角度状态
        self.current_pitch = None
        self.current_yaw = None
        
        # 初始化舵机
        self._init_servos()
    
    def _init_servos(self) -> bool:
        """
        初始化两个舵机
        
        Returns:
            bool: 初始化是否成功
        """
        try:
            # 初始化俯仰角舵机 (chip=1, channel=0)
            self.pitch_servo = ServoControllerPWM(self.pitch_chip, 0, self.frequency)
            
            # 初始化左右转动舵机 (chip=2, channel=0)
            self.yaw_servo = ServoControllerPWM(self.yaw_chip, 0, self.frequency)
            
            # 检查初始化状态
            if not self.pitch_servo.is_ready() or not self.yaw_servo.is_ready():
                print("舵机初始化失败")
                return False
            
            print(f"头部舵机控制器初始化成功")
            print(f"俯仰角舵机: 芯片{self.pitch_chip}, 角度范围{self.pitch_min_angle}-{self.pitch_max_angle}°")
            print(f"左右转动舵机: 芯片{self.yaw_chip}, 角度范围{self.yaw_min_angle}-{self.yaw_max_angle}°")
            
            # 初始化到中心位置
            self.reset_to_center()
            
            return True
            
        except Exception as e:
            print(f"头部舵机控制器初始化失败: {e}")
            return False
    
    def _validate_pitch_angle(self, angle: float) -> bool:
        """验证俯仰角角度范围"""
        return self.pitch_min_angle <= angle <= self.pitch_max_angle
    
    def _validate_yaw_angle(self, angle: float) -> bool:
        """验证左右转动角度范围"""
        return self.yaw_min_angle <= angle <= self.yaw_max_angle
    
    def set_pitch(self, angle: float, duration: float = 0.1) -> bool:
        """
        设置俯仰角（使用平滑转动）
        
        Args:
            angle: 俯仰角 (60-110度, 90度正前方)
            duration: 保持时间(秒)
            
        Returns:
            bool: 设置是否成功
        """
        if not self._validate_pitch_angle(angle):
            print(f"错误: 俯仰角必须在{self.pitch_min_angle}-{self.pitch_max_angle}度之间，当前输入: {angle}")
            return False
        
        if not self.pitch_servo or not self.pitch_servo.is_ready():
            print("俯仰角舵机未就绪")
            return False
        
        try:
            success = self.pitch_servo.set_angle_smooth(angle, step_angle=2.0, step_delay=0.05)
            if success:
                self.current_pitch = angle
            return success
            
        except Exception as e:
            print(f"设置俯仰角时出错: {e}")
            return False
    
    def set_yaw(self, angle: float, duration: float = 0.1) -> bool:
        """
        设置左右转动角度（使用平滑转动）
        
        Args:
            angle: 左右角度 (60-120度, 90度正中间, 60度右转, 120度左转)
            duration: 保持时间(秒)
            
        Returns:
            bool: 设置是否成功
        """
        if not self._validate_yaw_angle(angle):
            print(f"错误: 左右角度必须在{self.yaw_min_angle}-{self.yaw_max_angle}度之间，当前输入: {angle}")
            return False
        
        if not self.yaw_servo or not self.yaw_servo.is_ready():
            print("左右转动舵机未就绪")
            return False
        
        try:
            success = self.yaw_servo.set_angle_smooth(angle, step_angle=2.0, step_delay=0.05)
            if success:
                self.current_yaw = angle
                print(f" 左右角度已平滑设置到 {angle}°")
            return success
            
        except Exception as e:
            print(f"设置左右角度时出错: {e}")
            return False
    
    def set_head_position(self, pitch: float, yaw: float, duration: float = 0.1) -> bool:
        """
        同时设置俯仰角和左右角度（使用平滑转动）
        
        Args:
            pitch: 俯仰角 (60-110度)
            yaw: 左右角度 (60-120度)
            duration: 保持时间(秒)
            
        Returns:
            bool: 设置是否成功
        """
        
        # 验证角度范围
        if not self._validate_pitch_angle(pitch):
            print(f"错误: 俯仰角超出范围 {self.pitch_min_angle}-{self.pitch_max_angle}°")
            return False
        
        if not self._validate_yaw_angle(yaw):
            print(f"错误: 左右角度超出范围 {self.yaw_min_angle}-{self.yaw_max_angle}°")
            return False
        
        try:
            # 同时设置两个舵机
            pitch_success = self.set_pitch(pitch, duration)
            yaw_success = self.set_yaw(yaw, duration)
            
            return pitch_success and yaw_success
            
        except Exception as e:
            print(f"设置头部位置出错: {e}")
            return False
    
    def reset_to_center(self, duration: float = 0.1) -> bool:
        """
        重置到中心位置
        
        Args:
            duration: 保持时间(秒)
            
        Returns:
            bool: 重置是否成功
        """
        print("重置头部到中心位置...")
        return self.set_head_position(self.pitch_center_angle, self.yaw_center_angle, duration)
    
    def look_forward(self, duration: float = 0.1) -> bool:
        """
        看向正前方
        
        Args:
            duration: 保持时间(秒)
            
        Returns:
            bool: 设置是否成功
        """
        print("看向正前方...")
        return self.set_head_position(self.pitch_center_angle, self.yaw_center_angle, duration)
    
    def look_down(self, duration: float = 0.1) -> bool:
        """
        低头
        
        Args:
            duration: 保持时间(秒)
            
        Returns:
            bool: 设置是否成功
        """
        print("低头...")
        return self.set_pitch(self.pitch_min_angle, duration)
    
    def look_up(self, duration: float = 0.1) -> bool:
        """
        仰头
        
        Args:
            duration: 保持时间(秒)
            
        Returns:
            bool: 设置是否成功
        """
        print("仰头...")
        return self.set_pitch(self.pitch_max_angle, duration)
    
    def look_left(self, duration: float = 0.1) -> bool:
        """
        向左看
        
        Args:
            duration: 保持时间(秒)
            
        Returns:
            bool: 设置是否成功
        """
        print("向左看...")
        return self.set_yaw(self.yaw_max_angle, duration)
    
    def look_right(self, duration: float = 0.1) -> bool:
        """
        向右看
        
        Args:
            duration: 保持时间(秒)
            
        Returns:
            bool: 设置是否成功
        """
        print("向右看...")
        return self.set_yaw(self.yaw_min_angle, duration)
    
    def nod(self, count: int = 3, duration: float = 0.2) -> bool:
        """
        点头动作
        
        Args:
            count: 点头次数
            duration: 每次动作时间(秒)
            
        Returns:
            bool: 动作是否成功
        """
        print(f"开始点头 {count} 次...")
        
        try:
            for i in range(count):
                print(f"点头 {i+1}/{count}")
                self.look_down(duration/2)
                time.sleep(duration/2)
                self.look_forward(duration/2)
                time.sleep(duration/2)
            
            print("点头完成")
            return True
            
        except Exception as e:
            print(f"点头动作出错: {e}")
            return False
    
    def shake_head(self, count: int = 3, duration: float = 0.2) -> bool:
        """
        摇头动作
        
        Args:
            count: 摇头次数
            duration: 每次动作时间(秒)
            
        Returns:
            bool: 动作是否成功
        """
        print(f"开始摇头 {count} 次...")
        
        try:
            for i in range(count):
                print(f"摇头 {i+1}/{count}")
                self.look_left(duration/2)
                time.sleep(duration/2)
                self.look_right(duration/2)
                time.sleep(duration/2)
            
            # 回到中心位置
            self.look_forward(duration/2)
            print("摇头完成")
            return True
            
        except Exception as e:
            print(f"摇头动作出错: {e}")
            return False
    
    def think(self, duration: float = 2.0) -> bool:
        """
        思考动作 - 模拟人类思考时的自然头部动作
        
        Args:
            duration: 思考总时间(秒)
            
        Returns:
            bool: 动作是否成功
        """
        print("开始思考...")
        
        try:
            start_time = time.time()
            cycle_count = 0
            
            while time.time() - start_time < duration:
                cycle_count += 1
                
                # 思考阶段1: 明显低头思考
                self.set_head_position(80, 90, 0.15)
                time.sleep(0.4)
                
                # 思考阶段2: 明显左摆，模拟思考转向
                self.set_head_position(82, 75, 0.12)
                time.sleep(0.3)
                
                # 思考阶段3: 回到中心，短暂停顿
                self.set_head_position(90, 90, 0.1)
                time.sleep(0.2)
                
                # 思考阶段4: 明显右摆，继续思考
                self.set_head_position(85, 105, 0.12)
                time.sleep(0.35)
                
                # 思考阶段5: 明显仰头，表示理解或顿悟
                if cycle_count % 2 == 0:  # 每隔一个周期
                    self.set_head_position(100, 90, 0.1)
                    time.sleep(0.25)
                
                # 思考阶段6: 回到中心，准备下一轮思考
                self.set_head_position(90, 90, 0.1)
                time.sleep(0.3)
            
            # 思考结束，回到正常位置
            self.look_forward(0.2)
            print("思考完成")
            return True
            
        except Exception as e:
            print(f"思考动作出错: {e}")
            return False
    
    def confused(self, duration: float = 1.5) -> bool:
        """
        困惑动作 - 快速左右摆动，表示困惑
        
        Args:
            duration: 困惑动作时间(秒)
            
        Returns:
            bool: 动作是否成功
        """
        print("表示困惑...")
        
        try:
            start_time = time.time()
            while time.time() - start_time < duration:
                # 快速左摆
                self.set_yaw(80, 0.05)
                time.sleep(0.1)
                # 快速右摆
                self.set_yaw(100, 0.05)
                time.sleep(0.1)
            
            # 回到中心
            self.set_yaw(90, 0.1)
            print("困惑动作完成")
            return True
            
        except Exception as e:
            print(f"困惑动作出错: {e}")
            return False
    
    def surprised(self, duration: float = 1.0) -> bool:
        """
        惊讶动作 - 快速仰头然后回到正常位置
        
        Args:
            duration: 惊讶动作时间(秒)
            
        Returns:
            bool: 动作是否成功
        """
        print("表示惊讶...")
        
        try:
            # 快速仰头
            self.set_pitch(105, 0.1)
            time.sleep(duration * 0.3)
            # 稍微低头
            self.set_pitch(85, 0.1)
            time.sleep(duration * 0.2)
            # 回到正常位置
            self.set_pitch(90, 0.1)
            time.sleep(duration * 0.5)
            
            print("惊讶动作完成")
            return True
            
        except Exception as e:
            print(f"惊讶动作出错: {e}")
            return False
    
    def look_around(self, duration: float = 3.0) -> bool:
        """
        环视动作 - 模拟人类观察周围环境的自然动作
        
        Args:
            duration: 环视总时间(秒)
            
        Returns:
            bool: 动作是否成功
        """
        print("开始环视...")
        
        try:
            # 阶段1: 从中心开始，明显抬头观察
            self.set_head_position(100, 90, 0.2)
            time.sleep(0.4)
            
            # 阶段2: 向左上方观察
            self.set_head_position(105, 65, 0.25)
            time.sleep(0.6)
            
            # 阶段3: 向左下方观察
            self.set_head_position(80, 60, 0.2)
            time.sleep(0.5)
            
            # 阶段4: 回到中心，短暂停顿
            self.set_head_position(90, 90, 0.2)
            time.sleep(0.3)
            
            # 阶段5: 向右上方观察
            self.set_head_position(105, 115, 0.25)
            time.sleep(0.6)
            
            # 阶段6: 向右下方观察
            self.set_head_position(80, 120, 0.2)
            time.sleep(0.5)
            
            # 阶段7: 回到中心，向上观察
            self.set_head_position(110, 90, 0.2)
            time.sleep(0.4)
            
            # 阶段8: 向下观察
            self.set_head_position(75, 90, 0.2)
            time.sleep(0.4)
            
            # 阶段9: 回到正常位置
            self.look_forward(0.3)
            time.sleep(0.2)
            
            print("环视完成")
            return True
            
        except Exception as e:
            print(f"环视动作出错: {e}")
            return False
    
    def greet(self, duration: float = 2.0) -> bool:
        """
        打招呼动作 - 友好的问候动作序列
        
        Args:
            duration: 打招呼总时间(秒)
            
        Returns:
            bool: 动作是否成功
        """
        print("打招呼...")
        
        try:
            # 阶段1: 明显抬头注视，表示注意到对方
            self.set_head_position(100, 90, 0.2)
            time.sleep(0.4)
            
            # 阶段2: 明显点头，表示友好问候
            self.set_head_position(82, 90, 0.15)
            time.sleep(0.3)
            self.set_head_position(95, 90, 0.15)
            time.sleep(0.3)
            
            # 阶段3: 明显左摆，表示关注
            self.set_head_position(90, 75, 0.15)
            time.sleep(0.25)
            
            # 阶段4: 明显右摆，表示友好
            self.set_head_position(90, 105, 0.15)
            time.sleep(0.25)
            
            # 阶段5: 回到中心，保持友好姿态
            self.set_head_position(90, 90, 0.2)
            time.sleep(0.3)
            
            # 阶段6: 明显点头，表示欢迎
            self.set_head_position(80, 90, 0.1)
            time.sleep(0.2)
            self.set_head_position(90, 90, 0.1)
            time.sleep(0.2)
            
            print("打招呼完成")
            return True
            
        except Exception as e:
            print(f"打招呼动作出错: {e}")
            return False
    
    def goodbye(self, duration: float = 1.5) -> bool:
        """
        告别动作 - 点头然后轻微低头
        
        Args:
            duration: 告别总时间(秒)
            
        Returns:
            bool: 动作是否成功
        """
        print("告别...")
        
        try:
            # 点头
            self.nod(1, 0.3)
            time.sleep(0.2)
            
            # 轻微低头表示礼貌
            self.set_pitch(85, 0.3)
            time.sleep(0.5)
            self.set_pitch(90, 0.3)
            
            print("告别完成")
            return True
            
        except Exception as e:
            print(f"告别动作出错: {e}")
            return False
    
    def refuse(self, duration: float = 1.0) -> bool:
        """
        拒绝动作 - 快速摇头
        
        Args:
            duration: 拒绝动作时间(秒)
            
        Returns:
            bool: 动作是否成功
        """
        print("表示拒绝...")
        
        try:
            # 快速摇头
            self.shake_head(2, 0.2)
            time.sleep(0.2)
            
            # 轻微低头表示坚决
            self.set_pitch(85, 0.2)
            time.sleep(0.3)
            self.set_pitch(90, 0.2)
            
            print("拒绝动作完成")
            return True
            
        except Exception as e:
            print(f"拒绝动作出错: {e}")
            return False
    
    def breathing(self, cycles: int = 5, duration: float = 0.5) -> bool:
        """
        呼吸动作 - 模拟呼吸时的轻微头部动作
        
        Args:
            cycles: 呼吸周期数
            duration: 每个周期时间(秒)
            
        Returns:
            bool: 动作是否成功
        """
        print(f"开始呼吸动作 {cycles} 个周期...")
        
        try:
            for i in range(cycles):
                # 轻微仰头（吸气）
                self.set_pitch(92, 0.1)
                time.sleep(duration * 0.4)
                # 轻微低头（呼气）
                self.set_pitch(88, 0.1)
                time.sleep(duration * 0.6)
            
            # 回到正常位置
            self.set_pitch(90, 0.1)
            print("呼吸动作完成")
            return True
            
        except Exception as e:
            print(f"呼吸动作出错: {e}")
            return False
    
    def attention(self, direction: str = "center", duration: float = 1.0) -> bool:
        """
        注意力动作 - 表示正在注意某个方向
        
        Args:
            direction: 注意方向 ("left", "right", "up", "down", "center")
            duration: 注意时间(秒)
            
        Returns:
            bool: 动作是否成功
        """
        print(f"注意{['左', '右', '上', '下', '中心'][['left', 'right', 'up', 'down', 'center'].index(direction)]}...")
        
        try:
            if direction == "left":
                self.look_left(0.2)
            elif direction == "right":
                self.look_right(0.2)
            elif direction == "up":
                self.look_up(0.2)
            elif direction == "down":
                self.look_down(0.2)
            else:
                self.look_forward(0.2)
            
            time.sleep(duration)
            self.look_forward(0.2)
            
            print("注意力动作完成")
            return True
            
        except Exception as e:
            print(f"注意力动作出错: {e}")
            return False
    
    def listen_attentively(self, duration: float = 2.0) -> bool:
        """
        专注倾听动作 - 模拟认真倾听时的头部姿态
        
        Args:
            duration: 倾听时间(秒)
            
        Returns:
            bool: 动作是否成功
        """
        print("专注倾听...")
        
        try:
            # 阶段1: 明显前倾，表示专注
            self.set_head_position(82, 90, 0.2)
            time.sleep(0.4)
            
            # 阶段2: 明显左倾，表示思考
            self.set_head_position(85, 75, 0.15)
            time.sleep(0.3)
            
            # 阶段3: 明显右倾，表示理解
            self.set_head_position(85, 105, 0.15)
            time.sleep(0.3)
            
            # 阶段4: 回到专注姿态
            self.set_head_position(82, 90, 0.15)
            time.sleep(0.4)
            
            # 阶段5: 明显点头，表示理解
            self.set_head_position(78, 90, 0.1)
            time.sleep(0.2)
            self.set_head_position(82, 90, 0.1)
            time.sleep(0.2)
            
            # 回到正常位置
            self.look_forward(0.2)
            print("倾听完成")
            return True
            
        except Exception as e:
            print(f"倾听动作出错: {e}")
            return False
    
    def show_interest(self, duration: float = 1.5) -> bool:
        """
        表示兴趣动作 - 模拟对某事物感兴趣时的反应
        
        Args:
            duration: 兴趣表达时间(秒)
            
        Returns:
            bool: 动作是否成功
        """
        print("表示兴趣...")
        
        try:
            # 阶段1: 明显抬头注视，表示注意
            self.set_head_position(100, 90, 0.2)
            time.sleep(0.3)
            
            # 阶段2: 明显前倾，表示好奇
            self.set_head_position(85, 90, 0.15)
            time.sleep(0.25)
            
            # 阶段3: 明显左右摆动，表示探索
            self.set_head_position(88, 75, 0.1)
            time.sleep(0.2)
            self.set_head_position(88, 105, 0.1)
            time.sleep(0.2)
            
            # 阶段4: 回到注视姿态
            self.set_head_position(100, 90, 0.15)
            time.sleep(0.3)
            
            # 阶段5: 明显点头，表示认可
            self.set_head_position(90, 90, 0.1)
            time.sleep(0.2)
            
            print("兴趣表达完成")
            return True
            
        except Exception as e:
            print(f"兴趣表达动作出错: {e}")
            return False
    
    def show_concern(self, duration: float = 2.0) -> bool:
        """
        表示关心动作 - 模拟关心他人时的头部姿态
        
        Args:
            duration: 关心表达时间(秒)
            
        Returns:
            bool: 动作是否成功
        """
        print("表示关心...")
        
        try:
            # 阶段1: 明显低头，表示关切
            self.set_head_position(80, 90, 0.2)
            time.sleep(0.4)
            
            # 阶段2: 明显左倾，表示倾听
            self.set_head_position(82, 75, 0.15)
            time.sleep(0.3)
            
            # 阶段3: 明显右倾，表示理解
            self.set_head_position(82, 105, 0.15)
            time.sleep(0.3)
            
            # 阶段4: 回到关切姿态
            self.set_head_position(80, 90, 0.15)
            time.sleep(0.4)
            
            # 阶段5: 明显点头，表示安慰
            self.set_head_position(75, 90, 0.1)
            time.sleep(0.2)
            self.set_head_position(80, 90, 0.1)
            time.sleep(0.2)
            
            # 回到正常位置
            self.look_forward(0.2)
            print("关心表达完成")
            return True
            
        except Exception as e:
            print(f"关心表达动作出错: {e}")
            return False
    
    def show_agreement(self, duration: float = 1.0) -> bool:
        """
        表示同意动作 - 模拟同意或赞同时的反应
        
        Args:
            duration: 同意表达时间(秒)
            
        Returns:
            bool: 动作是否成功
        """
        print("表示同意...")
        
        try:
            # 阶段1: 明显点头，表示同意
            self.set_head_position(82, 90, 0.1)
            time.sleep(0.2)
            self.set_head_position(95, 90, 0.1)
            time.sleep(0.2)
            
            # 阶段2: 再次点头，强调同意
            self.set_head_position(80, 90, 0.1)
            time.sleep(0.15)
            self.set_head_position(90, 90, 0.1)
            time.sleep(0.15)
            
            # 阶段3: 明显仰头，表示肯定
            self.set_head_position(100, 90, 0.1)
            time.sleep(0.2)
            
            # 回到正常位置
            self.look_forward(0.1)
            print("同意表达完成")
            return True
            
        except Exception as e:
            print(f"同意表达动作出错: {e}")
            return False
    
    def show_disagreement(self, duration: float = 1.0) -> bool:
        """
        表示不同意动作 - 模拟不同意或反对时的反应
        
        Args:
            duration: 不同意表达时间(秒)
            
        Returns:
            bool: 动作是否成功
        """
        print("表示不同意...")
        
        try:
            # 阶段1: 明显摇头，表示不同意
            self.set_head_position(90, 75, 0.1)
            time.sleep(0.15)
            self.set_head_position(90, 105, 0.1)
            time.sleep(0.15)
            
            # 阶段2: 再次摇头，强调不同意
            self.set_head_position(90, 70, 0.1)
            time.sleep(0.15)
            self.set_head_position(90, 110, 0.1)
            time.sleep(0.15)
            
            # 阶段3: 回到中心，表示坚决
            self.set_head_position(90, 90, 0.1)
            time.sleep(0.2)
            
            print("不同意表达完成")
            return True
            
        except Exception as e:
            print(f"不同意表达动作出错: {e}")
            return False
    
    def emotional_sequence(self, emotion: str, duration: float = 3.0) -> bool:
        """
        情感序列动作 - 组合多个动作表达复杂情感
        
        Args:
            emotion: 情感类型 ("happy", "sad", "angry", "excited", "calm")
            duration: 总时间(秒)
            
        Returns:
            bool: 动作是否成功
        """
        print(f"表达{emotion}情感...")
        
        try:
            if emotion == "happy":
                # 快乐：点头 + 轻微摆动
                self.nod(2, 0.3)
                time.sleep(0.5)
                self.set_yaw(85, 0.2)
                time.sleep(0.3)
                self.set_yaw(95, 0.2)
                time.sleep(0.3)
                self.set_yaw(90, 0.2)
                
            elif emotion == "sad":
                # 悲伤：低头 + 轻微摇头
                self.set_pitch(85, 0.3)
                time.sleep(0.5)
                self.shake_head(1, 0.4)
                time.sleep(0.3)
                self.set_pitch(90, 0.3)
                
            elif emotion == "angry":
                # 愤怒：快速左右摆动
                self.confused(1.0)
                time.sleep(0.3)
                self.set_pitch(95, 0.2)
                time.sleep(0.5)
                self.set_pitch(90, 0.2)
                
            elif emotion == "excited":
                # 兴奋：快速点头 + 环视
                self.nod(3, 0.2)
                time.sleep(0.3)
                self.look_around(1.0)
                
            elif emotion == "calm":
                # 平静：缓慢呼吸动作
                self.breathing(3, 0.8)
                
            else:
                print(f"未知情感类型: {emotion}")
                return False
            
            print(f"{emotion}情感表达完成")
            return True
            
        except Exception as e:
            print(f"情感序列动作出错: {e}")
            return False
    
    def get_current_position(self) -> Tuple[Optional[float], Optional[float]]:
        """
        获取当前头部位置
        
        Returns:
            Tuple[Optional[float], Optional[float]]: (俯仰角, 左右角度)
        """
        return self.current_pitch, self.current_yaw
    
    def get_head_info(self) -> dict:
        """
        获取头部舵机信息
        
        Returns:
            dict: 头部舵机信息字典
        """
        info = {
            "pitch_servo": {
                "chip": self.pitch_chip,
                "channel": 0,
                "angle_range": f"{self.pitch_min_angle}-{self.pitch_max_angle}°",
                "center_angle": f"{self.pitch_center_angle}°",
                "current_angle": self.current_pitch,
                "is_ready": self.pitch_servo.is_ready() if self.pitch_servo else False
            },
            "yaw_servo": {
                "chip": self.yaw_chip,
                "channel": 0,
                "angle_range": f"{self.yaw_min_angle}-{self.yaw_max_angle}°",
                "center_angle": f"{self.yaw_center_angle}°",
                "current_angle": self.current_yaw,
                "is_ready": self.yaw_servo.is_ready() if self.yaw_servo else False
            }
        }
        return info
    
    def is_ready(self) -> bool:
        """
        检查头部舵机控制器是否就绪
        
        Returns:
            bool: 是否就绪
        """
        return (self.pitch_servo and self.pitch_servo.is_ready() and 
                self.yaw_servo and self.yaw_servo.is_ready())
    
    def stop(self) -> bool:
        """
        停止所有舵机
        
        Returns:
            bool: 停止是否成功
        """
        print("停止头部舵机...")
        pitch_success = self.pitch_servo.stop() if self.pitch_servo else False
        yaw_success = self.yaw_servo.stop() if self.yaw_servo else False
        return pitch_success and yaw_success
    
    def cleanup(self):
        """清理舵机资源"""
        print("清理头部舵机资源...")
        if self.pitch_servo:
            self.pitch_servo.cleanup()
        if self.yaw_servo:
            self.yaw_servo.cleanup()
    
    def __enter__(self):
        """上下文管理器入口"""
        return self
    
    def __exit__(self, exc_type, exc_val, exc_tb):
        """上下文管理器出口"""
        self.cleanup()


# 便捷函数接口
def create_head_controller(pitch_chip: int = 1, yaw_chip: int = 2) -> HeadServoController:
    """
    创建头部舵机控制器实例
    
    Args:
        pitch_chip: 俯仰角PWM芯片编号
        yaw_chip: 左右转动PWM芯片编号
        
    Returns:
        HeadServoController: 头部舵机控制器实例
    """
    return HeadServoController(pitch_chip, yaw_chip)


def quick_head_control(pitch: float, yaw: float, duration: float = 0.1) -> bool:
    """
    快速头部控制函数
    
    Args:
        pitch: 俯仰角 (60-110度)
        yaw: 左右角度 (60-120度)
        duration: 保持时间(秒)
        
    Returns:
        bool: 控制是否成功
    """
    with HeadServoController() as head:
        return head.set_head_position(pitch, yaw, duration)


# 使用示例和测试代码
if __name__ == "__main__":
    import argparse
    
    def main():
        """主函数"""
        parser = argparse.ArgumentParser(description='头部舵机控制器测试程序')
        parser.add_argument('--pitch', type=float, help='俯仰角 (60-110度)')
        parser.add_argument('--yaw', type=float, help='左右角度 (60-120度)')
        parser.add_argument('--duration', type=float, default=0.1, help='保持时间 (默认: 0.1秒)')
        parser.add_argument('--interactive', '-i', action='store_true', help='交互式模式')
        parser.add_argument('--demo', action='store_true', help='演示模式')
        parser.add_argument('--info', action='store_true', help='显示头部舵机信息')
        parser.add_argument('--reset', action='store_true', help='重置到中心位置')
        parser.add_argument('--nod', type=int, metavar='COUNT', help='点头指定次数')
        parser.add_argument('--shake', type=int, metavar='COUNT', help='摇头指定次数')
        
        args = parser.parse_args()
        
        try:
            if args.info:
                # 显示头部舵机信息
                print("=== 头部舵机信息 ===")
                print("俯仰角舵机: 芯片1, 角度范围60-110°, 90°正前方")
                print("左右转动舵机: 芯片2, 角度范围60-120°, 90°正中间")
                print("注意: 需要root权限才能访问PWM设备")
                print("使用方法: sudo python3 head_servo_controller.py --info")
                return 0
            
            if args.demo:
                # 演示模式
                print("=== 头部舵机控制器演示模式 ===")
                with HeadServoController() as head:
                    if not head.is_ready():
                        print("头部舵机初始化失败，请检查权限和设备")
                        return 1
                    
                    print("开始演示...")
                    
                    # 基本动作演示
                    print("\n1. 基本动作演示:")
                    head.look_forward(0.5)
                    time.sleep(0.5)
                    head.look_down(0.5)
                    time.sleep(0.5)
                    head.look_up(0.5)
                    time.sleep(0.5)
                    head.look_left(0.5)
                    time.sleep(0.5)
                    head.look_right(0.5)
                    time.sleep(0.5)
                    head.look_forward(0.5)
                    
                    # 组合动作演示
                    print("\n2. 组合动作演示:")
                    head.set_head_position(75, 75, 0.5)  # 低头右看
                    time.sleep(0.5)
                    head.set_head_position(105, 105, 0.5)  # 仰头左看
                    time.sleep(0.5)
                    head.look_forward(0.5)
                    
                    # 特殊动作演示
                    print("\n3. 特殊动作演示:")
                    head.nod(3, 0.3)
                    time.sleep(1)
                    head.shake_head(3, 0.3)
                    
                    # 拟人化动作演示
                    print("\n4. 拟人化动作演示:")
                    head.think(2.0)
                    time.sleep(0.5)
                    head.confused(1.0)
                    time.sleep(0.5)
                    head.surprised(1.0)
                    time.sleep(0.5)
                    head.look_around(2.0)
                    time.sleep(0.5)
                    head.greet(1.5)
                    time.sleep(0.5)
                    head.breathing(3, 0.6)
                    time.sleep(0.5)
                    
                    # 情感表达演示
                    print("\n5. 情感表达演示:")
                    head.emotional_sequence("happy", 2.0)
                    time.sleep(0.5)
                    head.emotional_sequence("excited", 2.0)
                    time.sleep(0.5)
                    head.emotional_sequence("calm", 2.0)
                    time.sleep(0.5)
                    
                    # 回到中心
                    head.look_forward(0.5)
                    print("\n演示完成")
            
            elif args.interactive:
                # 交互式模式
                print("=== 头部舵机控制器交互式模式 ===")
                with HeadServoController() as head:
                    if not head.is_ready():
                        print("头部舵机初始化失败，请检查权限和设备")
                        return 1
                    
                    print("头部舵机控制命令:")
                    print("  pitch <角度>  - 设置俯仰角 (60-110)")
                    print("  yaw <角度>    - 设置左右角度 (60-120)")
                    print("  pos <俯仰> <左右> - 同时设置两个角度")
                    print("  forward       - 看向正前方")
                    print("  down          - 低头")
                    print("  up            - 仰头")
                    print("  left          - 向左看")
                    print("  right         - 向右看")
                    print("  nod [次数]    - 点头 (默认3次)")
                    print("  shake [次数]  - 摇头 (默认3次)")
                    print("  reset         - 重置到中心")
                    print("  info          - 显示当前信息")
                    print("")
                    print("拟人化动作命令:")
                    print("  think [时间]  - 思考动作 (默认2秒)")
                    print("  confused [时间] - 困惑动作 (默认1.5秒)")
                    print("  surprised [时间] - 惊讶动作 (默认1秒)")
                    print("  lookaround [时间] - 环视动作 (默认3秒)")
                    print("  greet [时间]  - 打招呼动作 (默认2秒)")
                    print("  goodbye [时间] - 告别动作 (默认1.5秒)")
                    print("  refuse [时间] - 拒绝动作 (默认1秒)")
                    print("  breathing [周期] [时间] - 呼吸动作 (默认5周期,0.5秒)")
                    print("  attention <方向> [时间] - 注意力动作 (left/right/up/down/center)")
                    print("  emotion <类型> [时间] - 情感表达 (happy/sad/angry/excited/calm)")
                    print("")
                    print("新增拟人化动作:")
                    print("  listen [时间] - 专注倾听动作 (默认2秒)")
                    print("  interest [时间] - 表示兴趣动作 (默认1.5秒)")
                    print("  concern [时间] - 表示关心动作 (默认2秒)")
                    print("  agree [时间] - 表示同意动作 (默认1秒)")
                    print("  disagree [时间] - 表示不同意动作 (默认1秒)")
                    print("  q             - 退出")
                    print("-" * 50)
                    
                    while True:
                        try:
                            user_input = input("请输入命令: ").strip().lower()
                            
                            if user_input in ['q', 'quit', 'exit']:
                                break
                            
                            if user_input == '':
                                continue
                            
                            parts = user_input.split()
                            command = parts[0]
                            
                            if command == 'pitch' and len(parts) > 1:
                                angle = float(parts[1])
                                head.set_pitch(angle, args.duration)
                            
                            elif command == 'yaw' and len(parts) > 1:
                                angle = float(parts[1])
                                head.set_yaw(angle, args.duration)
                            
                            elif command == 'pos' and len(parts) > 2:
                                pitch = float(parts[1])
                                yaw = float(parts[2])
                                head.set_head_position(pitch, yaw, args.duration)
                            
                            elif command == 'forward':
                                head.look_forward(args.duration)
                            
                            elif command == 'down':
                                head.look_down(args.duration)
                            
                            elif command == 'up':
                                head.look_up(args.duration)
                            
                            elif command == 'left':
                                head.look_left(args.duration)
                            
                            elif command == 'right':
                                head.look_right(args.duration)
                            
                            elif command == 'nod':
                                count = int(parts[1]) if len(parts) > 1 else 3
                                head.nod(count, 0.3)
                            
                            elif command == 'shake':
                                count = int(parts[1]) if len(parts) > 1 else 3
                                head.shake_head(count, 0.3)
                            
                            elif command == 'reset':
                                head.reset_to_center(args.duration)
                            
                            elif command == 'info':
                                info = head.get_head_info()
                                print(f"当前俯仰角: {info['pitch_servo']['current_angle']}°")
                                print(f"当前左右角度: {info['yaw_servo']['current_angle']}°")
                            
                            # 拟人化动作命令
                            elif command == 'think':
                                duration = float(parts[1]) if len(parts) > 1 else 2.0
                                head.think(duration)
                            
                            elif command == 'confused':
                                duration = float(parts[1]) if len(parts) > 1 else 1.5
                                head.confused(duration)
                            
                            elif command == 'surprised':
                                duration = float(parts[1]) if len(parts) > 1 else 1.0
                                head.surprised(duration)
                            
                            elif command == 'lookaround':
                                duration = float(parts[1]) if len(parts) > 1 else 3.0
                                head.look_around(duration)
                            
                            elif command == 'greet':
                                duration = float(parts[1]) if len(parts) > 1 else 2.0
                                head.greet(duration)
                            
                            elif command == 'goodbye':
                                duration = float(parts[1]) if len(parts) > 1 else 1.5
                                head.goodbye(duration)
                            
                            elif command == 'refuse':
                                duration = float(parts[1]) if len(parts) > 1 else 1.0
                                head.refuse(duration)
                            
                            elif command == 'breathing':
                                cycles = int(parts[1]) if len(parts) > 1 else 5
                                duration = float(parts[2]) if len(parts) > 2 else 0.5
                                head.breathing(cycles, duration)
                            
                            elif command == 'attention':
                                direction = parts[1] if len(parts) > 1 else "center"
                                duration = float(parts[2]) if len(parts) > 2 else 1.0
                                head.attention(direction, duration)
                            
                            elif command == 'emotion':
                                emotion = parts[1] if len(parts) > 1 else "happy"
                                duration = float(parts[2]) if len(parts) > 2 else 3.0
                                head.emotional_sequence(emotion, duration)
                            
                            # 新增拟人化动作命令
                            elif command == 'listen':
                                duration = float(parts[1]) if len(parts) > 1 else 2.0
                                head.listen_attentively(duration)
                            
                            elif command == 'interest':
                                duration = float(parts[1]) if len(parts) > 1 else 1.5
                                head.show_interest(duration)
                            
                            elif command == 'concern':
                                duration = float(parts[1]) if len(parts) > 1 else 2.0
                                head.show_concern(duration)
                            
                            elif command == 'agree':
                                duration = float(parts[1]) if len(parts) > 1 else 1.0
                                head.show_agreement(duration)
                            
                            elif command == 'disagree':
                                duration = float(parts[1]) if len(parts) > 1 else 1.0
                                head.show_disagreement(duration)
                            
                            else:
                                print(" 未知命令，请重新输入")
                            
                        except ValueError:
                            print(" 输入错误: 请输入有效的数字")
                        except KeyboardInterrupt:
                            print("\n程序被中断")
                            break
            
            elif args.reset:
                # 重置模式
                with HeadServoController() as head:
                    if not head.is_ready():
                        print("头部舵机初始化失败，请检查权限和设备")
                        return 1
                    head.reset_to_center(args.duration)
            
            elif args.nod is not None:
                # 点头模式
                with HeadServoController() as head:
                    if not head.is_ready():
                        print("头部舵机初始化失败，请检查权限和设备")
                        return 1
                    head.nod(args.nod, 0.3)
            
            elif args.shake is not None:
                # 摇头模式
                with HeadServoController() as head:
                    if not head.is_ready():
                        print("头部舵机初始化失败，请检查权限和设备")
                        return 1
                    head.shake_head(args.shake, 0.3)
            
            elif args.pitch is not None or args.yaw is not None:
                # 单次控制模式
                with HeadServoController() as head:
                    if not head.is_ready():
                        print("头部舵机初始化失败，请检查权限和设备")
                        return 1
                    
                    if args.pitch is not None and args.yaw is not None:
                        head.set_head_position(args.pitch, args.yaw, args.duration)
                    elif args.pitch is not None:
                        head.set_pitch(args.pitch, args.duration)
                    elif args.yaw is not None:
                        head.set_yaw(args.yaw, args.duration)
            
            else:
                # 显示帮助
                print("头部舵机控制器使用示例:")
                print("\n1. 基本使用:")
                print("   head = HeadServoController()")
                print("   head.set_pitch(90)      # 设置俯仰角到90度")
                print("   head.set_yaw(90)        # 设置左右角度到90度")
                print("   head.set_head_position(90, 90)  # 同时设置两个角度")
                
                print("\n2. 便捷方法:")
                print("   head.look_forward()     # 看向正前方")
                print("   head.look_down()        # 低头")
                print("   head.look_up()          # 仰头")
                print("   head.look_left()        # 向左看")
                print("   head.look_right()       # 向右看")
                print("   head.nod(3)             # 点头3次")
                print("   head.shake_head(3)      # 摇头3次")
                
                print("\n3. 拟人化动作:")
                print("   head.think(2.0)         # 思考动作")
                print("   head.confused(1.5)      # 困惑动作")
                print("   head.surprised(1.0)     # 惊讶动作")
                print("   head.look_around(3.0)   # 环视动作")
                print("   head.greet(2.0)         # 打招呼动作")
                print("   head.goodbye(1.5)       # 告别动作")
                print("   head.refuse(1.0)        # 拒绝动作")
                print("   head.breathing(5, 0.5)  # 呼吸动作")
                print("   head.attention('left', 1.0)  # 注意力动作")
                print("   head.emotional_sequence('happy', 3.0)  # 情感表达")
                print("")
                print("4. 新增拟人化动作:")
                print("   head.listen_attentively(2.0)  # 专注倾听动作")
                print("   head.show_interest(1.5)       # 表示兴趣动作")
                print("   head.show_concern(2.0)        # 表示关心动作")
                print("   head.show_agreement(1.0)      # 表示同意动作")
                print("   head.show_disagreement(1.0)   # 表示不同意动作")
                
                print("\n4. 上下文管理器:")
                print("   with HeadServoController() as head:")
                print("       head.look_forward()")
                print("       head.nod(2)")
                print("       head.think(2.0)")
                
                print("\n5. 命令行测试:")
                print("   sudo python3 head_servo_controller.py --info")
                print("   sudo python3 head_servo_controller.py --pitch 90 --yaw 90")
                print("   sudo python3 head_servo_controller.py --interactive")
                print("   sudo python3 head_servo_controller.py --demo")
                print("   sudo python3 head_servo_controller.py --nod 3")
                print("   sudo python3 head_servo_controller.py --shake 3")
                print("   sudo python3 head_servo_controller.py --reset")
                
                print("\n6. 角度范围:")
                print("   俯仰角: 60-110度 (90度正前方, 60度低头, 110度仰头)")
                print("   左右角度: 60-120度 (90度正中间, 60度右转, 120度左转)")
                
                print("\n7. 平滑转动:")
                print("   - 所有舵机控制都使用平滑转动")
                print("   - 步长角度: 1.0度")
                print("   - 步延迟时间: 0.05秒")
                print("   - 提供更自然的头部运动")
                
                print("\n8. 拟人化动作说明:")
                print("   - think: 思考时的自然头部动作，包含低头、左右摆动和顿悟")
                print("   - confused: 困惑时的快速左右摆动")
                print("   - surprised: 惊讶时的快速仰头动作")
                print("   - look_around: 环视周围环境，模拟真实观察行为")
                print("   - greet: 友好的打招呼动作，包含注视、点头和摆动")
                print("   - goodbye: 礼貌的告别动作")
                print("   - refuse: 坚决的拒绝动作")
                print("   - breathing: 模拟呼吸的节律性动作")
                print("   - attention: 表示注意某个方向")
                print("   - emotional_sequence: 复合情感表达")
                print("   - listen_attentively: 专注倾听时的头部姿态")
                print("   - show_interest: 对某事物感兴趣时的反应")
                print("   - show_concern: 关心他人时的头部姿态")
                print("   - show_agreement: 同意或赞同时的反应")
                print("   - show_disagreement: 不同意或反对时的反应")
                
                print("\n9. 注意事项:")
                print("   - 需要root权限运行")
                print("   - 确保PWM设备可用")
                print("   - 俯仰角舵机使用芯片1")
                print("   - 左右转动舵机使用芯片2")
        
        except Exception as e:
            print(f"程序执行出错: {e}")
            return 1
        
        return 0
    
    sys.exit(main())
