#!/usr/bin/env python3
"""
语音助手主程序
集成ASR语音识别、LLM对话和TTS语音合成功能
"""

import time
import signal
import sys
import threading
import atexit
import subprocess
import os

# 导入ROS相关模块
try:
    import rospy
    ROS_AVAILABLE = True
    print("ROS模块导入成功")
except ImportError as e:
    ROS_AVAILABLE = False
    print(f"ROS模块导入失败: {e}")
    print("ROS功能将不可用")

from asr import ASR
from llm import LLMAgent
import emoji
from function_executor import FunctionExecutor
from config_manager import get_global_config_manager


# 全局变量
asr = None
agent = None
emoji_obj = None
function_executor = None
emoji_thread = None
is_shutting_down = False


def cleanup_resources():
    """清理所有资源"""
    global asr, agent, emoji_obj, function_executor, emoji_thread, is_shutting_down
    
    if is_shutting_down:
        return
        
    is_shutting_down = True
    print("\n开始清理资源...")
    
    try:
        # 停止ASR服务
        if asr:
            print(" 停止ASR服务...")
            asr.stop()
            asr = None
            
        # 停止emoji线程
        if emoji_obj:
            print(" 停止表情显示...")
            emoji_obj.cmd_str = "exit"  # 设置退出命令
            emoji_obj = None
            
        # 等待emoji线程结束
        if emoji_thread and emoji_thread.is_alive():
            print(" 等待表情线程结束...")
            emoji_thread.join(timeout=3)
            if emoji_thread.is_alive():
                print(" 表情线程未能在3秒内结束，强制继续")
                
        # 清理其他资源
        if agent:
            agent = None
        if function_executor:
            function_executor = None
            
        print(" 资源清理完成")
        
    except Exception as e:
        print(f" 清理资源时出错: {e}")
        # 播放错误提示音
        play_error_sound()


def cleanup_with_timeout(timeout=10):
    """带超时的清理函数"""
    import threading
    import time
    
    cleanup_done = threading.Event()
    
    def cleanup_worker():
        try:
            cleanup_resources()
        finally:
            cleanup_done.set()
    
    cleanup_thread = threading.Thread(target=cleanup_worker, daemon=True)
    cleanup_thread.start()
    
    if cleanup_done.wait(timeout):
        print(" 清理完成")
    else:
        print(f" 清理超时({timeout}秒)，强制退出")
        force_exit()


def signal_handler(signum, frame):
    """信号处理器"""
    print(f"\n 收到信号 {signum}，开始优雅关闭...")
    try:
        cleanup_with_timeout(timeout=10)
        sys.exit(0)
    except Exception as e:
        print(f"\n 优雅关闭过程中出错: {e}")
        # 播放错误提示音
        play_error_sound()
        sys.exit(1)


def force_exit():
    """强制退出"""
    print("\n 强制退出程序...")
    # 播放错误提示音
    play_error_sound()
    sys.exit(1)


def on_transcription(transcription_result, request_id, usage):
    """转录结果回调函数"""
    print(f"转录结果: {transcription_result.text}")


def on_translation(translation_result, request_id, usage):
    """翻译结果回调函数"""
    english_translation = translation_result.get_translation("en")
    print(f"翻译结果: {english_translation.text}")


def on_open():
    """ASR连接打开回调函数"""
    print("ASR连接已建立")


def on_close():
    """ASR连接关闭回调函数"""
    print("ASR连接已断开")
    # 如果连接意外断开且不是正常关闭，播放错误提示音
    if not is_shutting_down:
        print(" ASR连接意外断开")
        play_error_sound()


def play_startup_sound():
    """播放启动提示音"""
    try:
        # 获取项目根目录
        current_dir = os.path.dirname(os.path.abspath(__file__))
        sound_file = os.path.join(current_dir, "..", "config", "sound", "agent.mp3")
        sound_file = os.path.abspath(sound_file)
        
        # 检查文件是否存在
        if not os.path.exists(sound_file):
            print(f" 启动音效文件不存在: {sound_file}")
            return
            
        print(" 播放启动提示音...")
        
        # 使用ffplay播放音效，不显示界面，播放完成后自动退出
        subprocess.run([
            "ffplay", 
            "-nodisp", 
            "-autoexit", 
            sound_file
        ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            
    except Exception as e:
        print(f" 播放启动音效失败: {e}")
        print(" 请确保系统已安装 ffmpeg 工具包")


def play_error_sound():
    """播放错误提示音"""
    try:
        # 获取项目根目录
        current_dir = os.path.dirname(os.path.abspath(__file__))
        sound_file = os.path.join(current_dir, "..", "config", "sound", "aggent_error.mp3")
        sound_file = os.path.abspath(sound_file)
        
        # 检查文件是否存在
        if not os.path.exists(sound_file):
            print(f" 错误音效文件不存在: {sound_file}")
            return
            
        print(" 播放错误提示音...")
        
        # 使用ffplay播放音效，不显示界面，播放完成后自动退出
        subprocess.run([
            "ffplay", 
            "-nodisp", 
            "-autoexit", 
            sound_file
        ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            
    except Exception as e:
        print(f" 播放错误音效失败: {e}")
        print(" 请确保系统已安装 ffmpeg 工具包")


def on_wake_word(wake_word_text):
    """唤醒词检测回调函数"""
    print(f" 唤醒词激活: {wake_word_text}")
    # 可以在这里播放唤醒提示音或显示提示
    print("小沫已激活，请说话...")


def on_sentence_end(final_text, request_id):
    """句子结束回调函数"""
    # 检查是否正在关闭程序
    if is_shutting_down:
        print(" 程序正在关闭，忽略新的语音输入")
        return
        
    
    # 定义休息相关的关键词
    rest_keywords = [
        "休息", "睡觉", "晚安", "再见", "拜拜", 
        "不聊了", "不说了", "我要休息", "我要睡觉", "我要走了", 
        "先这样", "就这样", "好了", "够了", "结束对话", "停止对话",
        "我要去休息", "我要去睡觉", "我要去忙", "我要去工作",
        "休息一下", "睡一下", "小憩", "打盹", "午休", "夜宵"
    ]
    
    # 定义音乐控制相关的关键词
    music_control_keywords = [
        "关闭","停止","停止音乐", "关闭音乐", "关掉音乐", "停止播放", "关闭播放",
        "下一首", "下一首歌", "切歌", "换歌", "跳过", "下一曲",
        "上一首", "上一首歌", "上一曲", "前一首", "返回上一首",
        "播放音乐", "开始播放", "继续播放", "恢复播放",
        "音量", "调音量", "声音", "大小声", "静音", "取消静音"
    ]
    
    text_lower = final_text.lower()
    
    # 检查是否包含音乐控制关键词
    if asr.is_music_control_command(final_text):
        print(f" 检测到音乐控制关键词: {final_text}")
        # 如果正在播放音乐，立即唤醒ASR处理音乐控制指令
        if asr.is_music_playing():
            print(" 音乐播放中，立即处理音乐控制指令...")
            # 不重置唤醒词状态，直接处理音乐控制指令
        else:
            print(" 当前未播放音乐，正常处理指令...")
    
    # 检查是否包含休息相关关键词
    for keyword in rest_keywords:
        if keyword in text_lower:
            print(f" 检测到休息关键词: {keyword}")
            print(" 重置唤醒词状态，进入休息模式...")
            asr.reset_wake_word()
            return  # 检测到休息关键词时直接返回，不进行LLM处理
    
    # 再次检查是否正在关闭程序
    if is_shutting_down:
        print(" 程序正在关闭，取消LLM处理")
        return
    
    # 立即暂停ASR，避免LLM推理期间和TTS播放期间被误监听
    asr.pause_audio()
    
    try:
        # 在这里可以处理完整的句子，比如发送到LLM或其他处理逻辑
        res = agent.chat_with_tts_streaming(final_text)
        print(f"LLM回复: {res}")
    except Exception as e:
        print(f" LLM/TTS处理出错: {e}")
        # 播放错误提示音
        play_error_sound()
        # 恢复ASR音频监听
        asr.resume_audio()
        return
    
    # 处理完一句话后，重置唤醒词状态，等待下次唤醒
    # asr.reset_wake_word()


def main():
    """主函数"""
    global asr, agent, emoji_obj, function_executor, emoji_thread
    
    # 初始化ROS节点（如果可用）
    if ROS_AVAILABLE:
        try:
            rospy.init_node('agent_main', anonymous=True)
            print(" ROS节点初始化成功")
        except Exception as e:
            print(f" ROS节点初始化失败: {e}")
    
    # 注册信号处理器
    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)
    
    # 注册退出时的清理函数
    atexit.register(cleanup_resources)
    
    print(" 启动语音助手...")
    
    try:
        # 加载配置
        config_mgr = get_global_config_manager()
        api_key = config_mgr.get_api_key()
        
        # 检查 API key 是否已配置
        if not api_key:
            print(" API key 未配置！")
            print(" 请通过 Web 界面 (http://机器人IP:5000/settings) 配置 API key")
            play_error_sound()
            sys.exit(1)
        
        print(f" 使用 API key: {api_key[:8]}...{api_key[-4:]}")
        
        # 创建ASR实例
        try:
            asr = ASR(api_key=api_key)
            # 配置保活机制，确保TTS播放时ASR连接不会断开
            asr.set_keepalive_config(enabled=True, interval=1.0)  # 每1秒发送一次保活数据，提高连接稳定性
        except Exception as e:
            print(f" ASR初始化失败: {e}")
            play_error_sound()
            sys.exit(1)
        
        # 创建表情控制对象
        try:
            emoji_obj = emoji.Emobj()
            emoji_thread = threading.Thread(target=emoji_obj.loop, daemon=True)  # 设置为守护线程
            emoji_thread.start()
        except Exception as e:
            print(f" 表情控制初始化失败: {e}")
            play_error_sound()
            sys.exit(1)
        
        # 创建函数执行器，传入表情控制对象和ASR实例
        try:
            function_executor = FunctionExecutor(emoji_obj=emoji_obj, asr_instance=asr)
        except Exception as e:
            print(f" 函数执行器初始化失败: {e}")
            play_error_sound()
            sys.exit(1)
        
        # 创建LLM实例，传入ASR实例和函数执行器
        try:
            agent = LLMAgent(
                api_key=api_key,
                asr_instance=asr, 
                function_executor=function_executor
            )
        except Exception as e:
            print(f" LLM初始化失败: {e}")
            play_error_sound()
            sys.exit(1)

        # 设置回调函数
        asr.set_callbacks(
            on_transcription=on_transcription,
            on_open=on_open,
            on_close=on_close,
            on_sentence_end=on_sentence_end,  # 添加句子结束回调
            on_wake_word=on_wake_word  # 添加唤醒词回调
        )
        
        # 启动ASR服务
        try:
            # 播放启动提示音
            play_startup_sound()

            asr.start()
            print(" 语音助手已启动，请说'小沫小沫'（或同音字）来唤醒...")

        except Exception as e:
            print(f" ASR服务启动失败: {e}")
            play_error_sound()
            sys.exit(1)
        
        print(" 按 Ctrl+C 或发送 SIGTERM 信号来优雅关闭程序")
        
        # 保持运行，添加更短的睡眠时间以便更快响应终止信号
        while not is_shutting_down:
            time.sleep(0.1)
            
    except KeyboardInterrupt:
        print("\n 收到键盘中断信号...")
        cleanup_resources()
    except Exception as e:
        print(f"\n 程序运行出错: {e}")
        # 播放错误提示音
        play_error_sound()
        cleanup_resources()
        sys.exit(1)
    finally:
        print(" 语音助手已停止")


if __name__ == "__main__":
    main()
