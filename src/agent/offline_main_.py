#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import json
import sys
import os
import signal
import time
import threading

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

try:
    import rospy
    from std_msgs.msg import String
    ROS_AVAILABLE = True
    print("[离线函数执行器] ROS模块导入成功")
except ImportError as e:
    ROS_AVAILABLE = False
    print(f"[离线函数执行器] ROS模块导入失败: {e}")
    print("[离线函数执行器] ROS功能将不可用")
    sys.exit(1)

if ROS_AVAILABLE:
    try:
        rospy.init_node('offline_function_executor', anonymous=True)
    except rospy.exceptions.ROSException as e:
        if "has already been called" in str(e):
            print("[离线函数执行器] ROS节点已存在，继续使用现有节点")
        else:
            print(f"[离线函数执行器] ROS节点初始化异常: {e}")
    except Exception as e:
        print(f"[离线函数执行器] ROS节点初始化失败: {e}")

# 导入函数执行器
try:
    from function_executor import FunctionExecutor, get_global_executor
    print("[离线函数执行器] 函数执行器模块导入成功")
except ImportError as e:
    print(f"[离线函数执行器] 函数执行器模块导入失败: {e}")
    sys.exit(1)

# 导入emoji模块
try:
    import emoji
    EMOJI_AVAILABLE = True
    print("[离线函数执行器] Emoji模块导入成功")
except ImportError as e:
    EMOJI_AVAILABLE = False
    print(f"[离线函数执行器] Emoji模块导入失败: {e}")
    print("[离线函数执行器] Emoji功能将不可用")


class OfflineFunctionExecutor:
    """离线函数执行器类"""
    
    def __init__(self):
        """初始化离线函数执行器"""
        self.function_executor = None
        self.fc_sub = None
        self.is_shutting_down = False
        self.emoji_obj = None
        self.emoji_thread = None
        
        if EMOJI_AVAILABLE:
            try:
                self.emoji_obj = emoji.Emobj()
                self.emoji_thread = threading.Thread(target=self.emoji_obj.loop, daemon=True)
                self.emoji_thread.start()
                print("[离线函数执行器] Emoji表情控制初始化成功")
            except Exception as e:
                print(f"[离线函数执行器] Emoji表情控制初始化失败: {e}")
                self.emoji_obj = None
                self.emoji_thread = None
        
        try:
            if self.emoji_obj:
                self.function_executor = FunctionExecutor(emoji_obj=self.emoji_obj)
            else:
                self.function_executor = FunctionExecutor()
            print("[离线函数执行器] 函数执行器初始化成功")
            print(f"[离线函数执行器] 已注册的函数: {self.function_executor.get_registered_functions()}")
        except Exception as e:
            print(f"[离线函数执行器] 函数执行器初始化失败: {e}")
            sys.exit(1)
        
        try:
            self.fc_sub = rospy.Subscriber('/llm/function_call', String, self.function_call_callback)
            print("[离线函数执行器] 已订阅 /llm/function_call 话题")
        except Exception as e:
            print(f"[离线函数执行器] 订阅话题失败: {e}")
            self.fc_sub = None
    
        signal.signal(signal.SIGINT, self.signal_handler)
        signal.signal(signal.SIGTERM, self.signal_handler)
    
    def signal_handler(self, signum, frame):
        """信号处理函数"""
        print(f"\n[离线函数执行器] 收到信号 {signum}，开始关闭...")
        self.is_shutting_down = True
        
        if self.emoji_obj:
            print("[离线函数执行器] 停止表情显示...")
            self.emoji_obj.cmd_str = "exit"
        
        if self.emoji_thread and self.emoji_thread.is_alive():
            print("[离线函数执行器] 等待表情线程结束...")
            self.emoji_thread.join(timeout=3)
            if self.emoji_thread.is_alive():
                print("[离线函数执行器] 表情线程未能在3秒内结束，强制继续")
        
        rospy.signal_shutdown("收到退出信号")
    
    def parse_function_call(self, fc_string: str) -> dict:
        """
        解析函数调用字符串
        
        支持的格式：
        - navigate_to("厨房")  - 单个字符串参数
        - head_smile  - 无参数函数
        
        Args:
            fc_string: 函数调用字符串
            
        Returns:
            dict: 解析后的函数调用信息，包含 name 和 arguments
        """
        fc_string = fc_string.strip()
        if not fc_string:
            return {"name": "", "arguments": {}}
        
        if '(' in fc_string:
            paren_pos = fc_string.find('(')
            if paren_pos > 0 and fc_string.endswith(')'):
                func_name = fc_string[:paren_pos].strip()
                args_str = fc_string[paren_pos+1:-1].strip()
                
                # 解析单个字符串参数
                if args_str:
                    args_str = args_str.strip()
                    # 检查是否是带引号的字符串
                    if (args_str.startswith('"') and args_str.endswith('"')) or \
                       (args_str.startswith("'") and args_str.endswith("'")):
                        # 去掉引号
                        value = args_str[1:-1]
                        # 处理转义字符
                        value = value.replace('\\"', '"').replace("\\'", "'").replace('\\n', '\n').replace('\\t', '\t')
                        return {
                            "name": func_name,
                            "arguments": {"arg0": value}
                        }
                    else:
                        return {
                            "name": func_name,
                            "arguments": {"arg0": args_str}
                        }
                else:
                    return {
                        "name": func_name,
                        "arguments": {}
                    }
        
        return {
            "name": fc_string,
            "arguments": {}
        }
    
    def function_call_callback(self, msg):
        """
        函数调用回调函数
        
        Args:
            msg: ROS消息，包含函数调用字符串
        """
        if self.is_shutting_down:
            return
        
        fc_string = msg.data.strip()
        if not fc_string:
            return
        
        print(f"\n[离线函数执行器] 收到函数调用: {fc_string}")
        
        try:
            # 解析函数调用
            fc_info = self.parse_function_call(fc_string)
            func_name = fc_info["name"]
            arguments = fc_info["arguments"]
            
            print(f"[离线函数执行器] 解析结果 - 函数名: {func_name}, 参数: {arguments}")
            
            if self.function_executor:
                if arguments and "arg0" in arguments:
                    result = self.function_executor.execute_function(func_name, arguments["arg0"])
                elif arguments:
                    result = self.function_executor.execute_function(func_name, **arguments)
                else:
                    result = self.function_executor.execute_function(func_name)
                
                if result.get("success"):
                    print(f"[离线函数执行器] 函数执行成功: {func_name}")
                    if "result" in result:
                        print(f"[离线函数执行器] 执行结果: {result['result']}")
                else:
                    print(f"[离线函数执行器] 函数执行失败: {func_name}")
                    if "error" in result:
                        print(f"[离线函数执行器] 错误信息: {result['error']}")
            else:
                print("[离线函数执行器] 错误: 函数执行器未初始化")
        
        except Exception as e:
            print(f"[离线函数执行器] 处理函数调用时出错: {e}")
            import traceback
            traceback.print_exc()
    
    def run(self):
        """运行主循环"""
        print("\n" + "="*60)
        print("[离线函数执行器] 离线函数执行器已启动")
        print("[离线函数执行器] 等待函数调用...")
        print("="*60 + "\n")
        
        if ROS_AVAILABLE:
            rospy.spin()
        
        
        print("\n[离线函数执行器] 离线函数执行器已关闭")


def main():
    """主函数"""
    try:
        executor = OfflineFunctionExecutor()
        executor.run()
    except KeyboardInterrupt:
        print("\n[离线函数执行器] 收到键盘中断，退出...")
    except Exception as e:
        print(f"\n[离线函数执行器] 发生错误: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)


if __name__ == "__main__":
    main()

