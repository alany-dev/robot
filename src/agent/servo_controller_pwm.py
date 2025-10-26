#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
基于periphery PWM库的舵机控制类
"""

import time
import sys
from typing import Optional

# 使用periphery PWM库
try:
    from periphery import PWM
    print("使用periphery PWM库")
except ImportError:
    print("错误: 未找到periphery库")
    print("请安装periphery库:")
    print("  pip install python-periphery")
    sys.exit(1)

class ServoControllerPWM:
    """基于periphery PWM库的舵机控制器类"""
    
    def __init__(self, chip: int = 0, channel: int = 0, frequency: float = 50.0):
        """
        初始化舵机控制器
        
        Args:
            chip: PWM芯片编号 (默认0)
            channel: PWM通道编号 (默认0)
            frequency: PWM频率，默认50Hz
        """
        self.chip = chip
        self.channel = channel
        self.frequency = frequency
        self.pwm_obj = None
        self.is_initialized = False
        self.current_angle = None
        
        # 舵机PWM参数
        self.min_pulse_width = 0.5  # 最小脉宽(ms) - 对应0度
        self.max_pulse_width = 2.5  # 最大脉宽(ms) - 对应180度
        self.pulse_range = self.max_pulse_width - self.min_pulse_width
        
        # 平滑转动参数
        self.smooth_step_angle = 1.0  # 每步转动角度(度)
        self.smooth_step_delay = 0.02  # 每步延迟时间(秒)
        
        # 初始化PWM
        self._init_pwm()
    
    def _init_pwm(self) -> bool:
        """
        初始化PWM设备
        
        Returns:
            bool: 初始化是否成功
        """
        try:
            # 创建PWM对象
            self.pwm_obj = PWM(self.chip, self.channel)
            
            # 设置PWM参数
            self.pwm_obj.frequency = self.frequency
            self.pwm_obj.polarity = "normal"
            
            # 初始占空比为0
            self.pwm_obj.duty_cycle = 0.0
            
            # 启用PWM
            self.pwm_obj.enable()
            
            self.is_initialized = True
            print(f"PWM舵机控制器初始化成功 - 芯片: {self.chip}, 通道: {self.channel}, 频率: {self.frequency}Hz")
            return True
            
        except Exception as e:
            print(f"PWM舵机控制器初始化失败: {e}")
            print("可能的原因:")
            print("1. 需要root权限: sudo python3 script.py")
            print("2. PWM设备被占用")
            print("3. PWM设备未启用")
            self.is_initialized = False
            return False
    
    def _angle_to_duty_cycle(self, angle: float) -> float:
        """
        将角度转换为占空比
        
        Args:
            angle: 舵机角度 (0-180度)
            
        Returns:
            float: 占空比 (0.0-1.0)
        """
        # 计算脉宽
        pulse_width_ms = self.min_pulse_width + (angle / 180.0) * self.pulse_range
        
        # 计算占空比
        duty_cycle = pulse_width_ms / (1000.0 / self.frequency)
        
        # 限制占空比范围
        duty_cycle = max(0.0, min(1.0, duty_cycle))
        
        return duty_cycle
    
    def set_angle(self, angle: float, duration: float = 0.1) -> bool:
        """
        设置舵机角度
        
        Args:
            angle: 舵机角度 (0-180度)
            duration: 保持时间(秒)，默认0.1秒
            
        Returns:
            bool: 设置是否成功
        """
        # 角度范围检查
        if not (0 <= angle <= 180):
            print(f"错误: 角度必须在0-180度之间，当前输入: {angle}")
            return False
        
        if not self.is_initialized:
            print("PWM舵机控制器未初始化")
            return False
        
        try:
            # 计算占空比
            duty_cycle = self._angle_to_duty_cycle(angle)
            
            print(f"设置舵机角度: {angle}° (占空比: {duty_cycle:.3f})")
            
            # 设置PWM占空比
            self.pwm_obj.duty_cycle = duty_cycle
            
            # 保持指定时间
            time.sleep(duration)
            
            self.current_angle = angle
            print(f" 舵机已成功旋转到 {angle}°")
            return True
            
        except Exception as e:
            print(f"设置舵机角度时出错: {e}")
            return False
    
    def set_angle_smooth(self, target_angle: float, step_angle: float = None, step_delay: float = None) -> bool:
        """
        平滑设置舵机角度（逐步转动）
        
        Args:
            target_angle: 目标角度 (0-180度)
            step_angle: 每步转动角度(度)，默认使用类参数
            step_delay: 每步延迟时间(秒)，默认使用类参数
            
        Returns:
            bool: 设置是否成功
        """
        # 角度范围检查
        if not (0 <= target_angle <= 180):
            print(f"错误: 角度必须在0-180度之间，当前输入: {target_angle}")
            return False
        
        if not self.is_initialized:
            print("PWM舵机控制器未初始化")
            return False
        
        # 使用默认参数或传入参数
        step_angle = step_angle if step_angle is not None else self.smooth_step_angle
        step_delay = step_delay if step_delay is not None else self.smooth_step_delay
        
        # 获取当前角度
        current_angle = self.current_angle if self.current_angle is not None else 90.0
        
        try:
            print(f"平滑转动: {current_angle}° → {target_angle}° (步长: {step_angle}°, 延迟: {step_delay}s)")
            
            # 计算转动方向和步数
            angle_diff = target_angle - current_angle
            steps = int(abs(angle_diff) / step_angle)
            
            if steps == 0:
                print("目标角度与当前角度相同，无需转动")
                return True
            
            # 计算每步的角度增量
            angle_increment = angle_diff / steps
            
            # 逐步转动
            for i in range(steps + 1):
                # 计算当前步的角度
                current_step_angle = current_angle + (angle_increment * i)
                
                # 计算占空比
                duty_cycle = self._angle_to_duty_cycle(current_step_angle)
                
                # 设置PWM占空比
                self.pwm_obj.duty_cycle = duty_cycle
                
                # 延迟
                if i < steps:  # 最后一步不需要延迟
                    time.sleep(step_delay)
            
            self.current_angle = target_angle
            return True
            
        except Exception as e:
            print(f"平滑设置舵机角度时出错: {e}")
            return False
    
    def set_smooth_params(self, step_angle: float = None, step_delay: float = None):
        """
        设置平滑转动参数
        
        Args:
            step_angle: 每步转动角度(度)
            step_delay: 每步延迟时间(秒)
        """
        if step_angle is not None:
            self.smooth_step_angle = max(0.1, min(10.0, step_angle))  # 限制在0.1-10度之间
            print(f"平滑步长设置为: {self.smooth_step_angle}°")
        
        if step_delay is not None:
            self.smooth_step_delay = max(0.001, min(1.0, step_delay))  # 限制在1ms-1s之间
            print(f"平滑延迟设置为: {self.smooth_step_delay}s")
    
    def get_current_angle(self) -> Optional[float]:
        """
        获取当前舵机角度
        
        Returns:
            Optional[float]: 当前角度，如果未知则返回None
        """
        return self.current_angle
    
    def get_pwm_info(self) -> dict:
        """
        获取PWM信息
        
        Returns:
            dict: PWM信息字典
        """
        if not self.is_initialized:
            return {}
        
        return {
            "chip": self.chip,
            "channel": self.channel,
            "frequency": self.frequency,
            "current_angle": self.current_angle,
            "duty_cycle": self.pwm_obj.duty_cycle if self.pwm_obj else 0.0
        }
    
    def is_ready(self) -> bool:
        """
        检查舵机控制器是否就绪
        
        Returns:
            bool: 是否就绪
        """
        return self.is_initialized
    
    def stop(self) -> bool:
        """
        停止舵机（设置占空比为0）
        
        Returns:
            bool: 停止是否成功
        """
        if not self.is_initialized:
            return False
        
        try:
            self.pwm_obj.duty_cycle = 0.0
            print("舵机已停止")
            return True
            
        except Exception as e:
            print(f"停止舵机时出错: {e}")
            return False
    
    def cleanup(self):
        """清理PWM资源"""
        try:
            if self.pwm_obj:
                self.pwm_obj.disable()
                self.pwm_obj.close()
                print("PWM资源已清理")
        except Exception as e:
            print(f"清理PWM资源时出错: {e}")
    
    def __enter__(self):
        """上下文管理器入口"""
        return self
    
    def __exit__(self, exc_type, exc_val, exc_tb):
        """上下文管理器出口"""
        self.cleanup()


# 便捷函数接口
def create_servo_controller_pwm(chip: int = 0, channel: int = 0, frequency: float = 50.0) -> ServoControllerPWM:
    """
    创建基于periphery PWM的舵机控制器实例
    
    Args:
        chip: PWM芯片编号
        channel: PWM通道编号
        frequency: PWM频率
        
    Returns:
        ServoControllerPWM: 舵机控制器实例
    """
    return ServoControllerPWM(chip, channel, frequency)


def quick_servo_control_pwm(chip: int, channel: int, angle: float, duration: float = 0.1) -> bool:
    """
    快速舵机控制函数（基于periphery PWM）
    
    Args:
        chip: PWM芯片编号
        channel: PWM通道编号
        angle: 舵机角度 (0-180度)
        duration: 保持时间(秒)
        
    Returns:
        bool: 控制是否成功
    """
    with ServoControllerPWM(chip, channel) as servo:
        return servo.set_angle(angle, duration)


def quick_servo_control_smooth_pwm(chip: int, channel: int, angle: float, step_angle: float = 1.0, step_delay: float = 0.02) -> bool:
    """
    快速平滑舵机控制函数（基于periphery PWM）
    
    Args:
        chip: PWM芯片编号
        channel: PWM通道编号
        angle: 舵机角度 (0-180度)
        step_angle: 每步转动角度(度)
        step_delay: 每步延迟时间(秒)
        
    Returns:
        bool: 控制是否成功
    """
    with ServoControllerPWM(chip, channel) as servo:
        return servo.set_angle_smooth(angle, step_angle, step_delay)


# 使用示例和测试代码
if __name__ == "__main__":
    import argparse
    
    def main():
        """主函数"""
        parser = argparse.ArgumentParser(description='基于periphery PWM的舵机控制器测试程序')
        parser.add_argument('--chip', type=int, default=2, help='PWM芯片编号 (默认: 0)')
        parser.add_argument('--channel', type=int, default=0, help='PWM通道编号 (默认: 0)')
        parser.add_argument('--angle', type=float, help='舵机角度 (0-180度)')
        parser.add_argument('--duration', type=float, default=0.1, help='保持时间 (默认: 0.1秒)')
        parser.add_argument('--smooth', action='store_true', help='使用平滑转动模式')
        parser.add_argument('--step-angle', type=float, default=1.0, help='平滑转动步长角度 (默认: 1.0度)')
        parser.add_argument('--step-delay', type=float, default=0.02, help='平滑转动步延迟时间 (默认: 0.02秒)')
        parser.add_argument('--interactive', '-i', action='store_true', help='交互式模式')
        parser.add_argument('--demo', action='store_true', help='演示模式')
        parser.add_argument('--info', action='store_true', help='显示PWM信息')
        
        args = parser.parse_args()
        
        try:
            if args.info:
                # 显示PWM信息
                print("=== PWM信息 ===")
                print(f"PWM芯片: {args.chip}")
                print(f"PWM通道: {args.channel}")
                print("注意: 需要root权限才能访问PWM设备")
                print("使用方法: sudo python3 servo_controller_pwm.py --info")
                return 0
            
            if args.demo:
                # 演示模式
                print("=== 基于periphery PWM的舵机控制器演示模式 ===")
                with ServoControllerPWM(args.chip, args.channel) as servo:
                    if not servo.is_ready():
                        print("PWM初始化失败，请检查权限和设备")
                        return 1
                    
                    print(f"使用PWM芯片: {servo.chip}, 通道: {servo.channel}")
                    print(f"PWM频率: {servo.frequency}Hz")
                    
                    if args.smooth:
                        print("使用平滑转动模式")
                        servo.set_smooth_params(args.step_angle, args.step_delay)
                    
                    # 演示不同角度
                    angles = [0, 45, 90, 135, 180, 90, 0]
                    for angle in angles:
                        print(f"\n旋转到 {angle}°...")
                        if args.smooth:
                            servo.set_angle_smooth(angle, args.step_angle, args.step_delay)
                        else:
                            servo.set_angle(angle, 0.5)
                        time.sleep(0.5)
                    
                    print("\n演示完成")
            
            elif args.interactive:
                # 交互式模式
                print("=== 基于periphery PWM的舵机控制器交互式模式 ===")
                with ServoControllerPWM(args.chip, args.channel) as servo:
                    if not servo.is_ready():
                        print("PWM初始化失败，请检查权限和设备")
                        return 1
                    
                    print(f"使用PWM芯片: {servo.chip}, 通道: {servo.channel}")
                    
                    if args.smooth:
                        print("使用平滑转动模式")
                        servo.set_smooth_params(args.step_angle, args.step_delay)
                    
                    print("输入角度范围: 0-180度")
                    print("输入 'q' 退出")
                    print("输入 's' 切换平滑/普通模式")
                    print("-" * 30)
                    
                    smooth_mode = args.smooth
                    
                    while True:
                        try:
                            user_input = input(f"请输入舵机角度 ({'平滑' if smooth_mode else '普通'}模式): ").strip()
                            
                            if user_input.lower() in ['q', 'quit', 'exit']:
                                break
                            
                            if user_input.lower() == 's':
                                smooth_mode = not smooth_mode
                                print(f"切换到{'平滑' if smooth_mode else '普通'}模式")
                                continue
                            
                            if user_input == '':
                                continue
                            
                            angle = float(user_input)
                            if smooth_mode:
                                servo.set_angle_smooth(angle, args.step_angle, args.step_delay)
                            else:
                                servo.set_angle(angle, args.duration)
                            
                        except ValueError:
                            print(" 输入错误: 请输入有效的数字")
                        except KeyboardInterrupt:
                            print("\n程序被中断")
                            break
            
            elif args.angle is not None:
                # 单次控制模式
                with ServoControllerPWM(args.chip, args.channel) as servo:
                    if not servo.is_ready():
                        print("PWM初始化失败，请检查权限和设备")
                        return 1
                    
                    if args.smooth:
                        servo.set_smooth_params(args.step_angle, args.step_delay)
                        servo.set_angle_smooth(args.angle, args.step_angle, args.step_delay)
                    else:
                        servo.set_angle(args.angle, args.duration)
            
            else:
                # 显示帮助
                print("基于periphery PWM的舵机控制器类使用示例:")
                print(f"\n当前使用的PWM库: periphery")
                print("\n1. 基本使用:")
                print("   servo = ServoControllerPWM(chip=0, channel=0)")
                print("   servo.set_angle(90)  # 旋转到90度")
                print("   servo.stop()         # 停止")
                
                print("\n2. 平滑转动:")
                print("   servo.set_angle_smooth(90)  # 平滑旋转到90度")
                print("   servo.set_smooth_params(0.5, 0.01)  # 设置步长0.5度，延迟10ms")
                
                print("\n3. 上下文管理器:")
                print("   with ServoControllerPWM(0, 0) as servo:")
                print("       servo.set_angle(0)")
                print("       servo.set_angle_smooth(180)  # 平滑转动")
                
                print("\n4. 便捷函数:")
                print("   quick_servo_control_pwm(0, 0, 90)  # 快速控制到90度")
                print("   quick_servo_control_smooth_pwm(0, 0, 90)  # 平滑控制到90度")
                
                print("\n5. 命令行测试:")
                print("   sudo python3 servo_controller_pwm.py --info")
                print("   sudo python3 servo_controller_pwm.py --angle 90")
                print("   sudo python3 servo_controller_pwm.py --angle 90 --smooth  # 平滑转动")
                print("   sudo python3 servo_controller_pwm.py --interactive")
                print("   sudo python3 servo_controller_pwm.py --demo --smooth")
                
                print("\n6. 安装periphery库:")
                print("   pip install python-periphery")
                
                print("\n7. 平滑转动参数说明:")
                print("   --step-angle: 每步转动角度，越小越平滑 (默认: 1.0度)")
                print("   --step-delay: 每步延迟时间，越大越慢 (默认: 0.02秒)")
                print("   示例: --smooth --step-angle 0.5 --step-delay 0.01")
                
                print("\n8. 注意事项:")
                print("   - 需要root权限运行")
                print("   - 确保PWM设备可用")
                print("   - 检查PWM芯片和通道编号")
                print("   - 平滑转动适合需要缓慢移动的场景")
        
        except Exception as e:
            print(f"程序执行出错: {e}")
            return 1
        
        return 0
    
    sys.exit(main())
