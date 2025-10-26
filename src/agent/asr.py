import pyaudio
import dashscope
from dashscope.audio.asr import *
import threading
import time
from typing import Callable, Optional, List, Dict, Any


class ASRCallback(TranslationRecognizerCallback):
    """ASR回调处理类"""
    
    def __init__(self, on_transcription: Optional[Callable] = None, 
                 on_translation: Optional[Callable] = None,
                 on_open: Optional[Callable] = None,
                 on_close: Optional[Callable] = None,
                 on_sentence_end: Optional[Callable] = None,
                 on_wake_word: Optional[Callable] = None,
                 asr_instance=None):
        """
        初始化回调处理器
        
        Args:
            on_transcription: 转录结果回调函数，参数为(transcription_result, request_id, usage)
            on_translation: 翻译结果回调函数，参数为(translation_result, request_id, usage)
            on_open: 连接打开回调函数
            on_close: 连接关闭回调函数
            on_sentence_end: 句子结束回调函数，参数为(final_text, request_id)
        """
        self.on_transcription = on_transcription
        self.on_translation = on_translation
        self.on_open_callback = on_open
        self.on_close_callback = on_close
        self.on_sentence_end = on_sentence_end
        self.on_wake_word = on_wake_word
        self.asr_instance = asr_instance
        self.mic = None
        self.stream = None

    def on_open(self) -> None:
        """连接打开时的回调"""
        print("ASR连接已打开")
        self.mic = pyaudio.PyAudio()
        self.stream = self.mic.open(
            format=pyaudio.paInt16, channels=1, rate=16000, input=True
        )
        if self.on_open_callback:
            self.on_open_callback()

    def on_close(self) -> None:
        """连接关闭时的回调"""
        print("ASR连接已关闭")
        if self.stream:
            self.stream.stop_stream()
            self.stream.close()
        if self.mic:
            self.mic.terminate()
        self.stream = None
        self.mic = None
        if self.on_close_callback:
            self.on_close_callback()

    def on_event(
        self,
        request_id,
        transcription_result: TranscriptionResult,
        translation_result: TranslationResult,
        usage,
    ) -> None:
        """处理识别和翻译结果"""
        if translation_result is not None and self.on_translation:
            self.on_translation(translation_result, request_id, usage)
        
        if self.asr_instance and self.asr_instance.audio_paused:
            print("ASR音频处理已暂停")
            return
        
        if transcription_result is not None and self.on_transcription:
            text = transcription_result.text.strip()
            # 去除所有标点符号，只保留纯文本
            import re
            clean_text = re.sub(r'[^\w\s]', '', text)
            print(f"text: {text} -> clean: {clean_text}")
            
            # 更新最后音频时间
            import time
            self.asr_instance.last_audio_time = time.time()
            
            # 检查唤醒词
            if not self.asr_instance.wake_word_detected:
                print("ASR音频处理未暂停")
                for wake_word in self.asr_instance.wake_words:
                    if wake_word in clean_text:
                        print(f" 检测到唤醒词: {wake_word}")
                        self.asr_instance.wake_word_detected = True
                        self.asr_instance.last_audio_time = time.time()  # 重置计时器
                        if self.on_wake_word:
                            self.on_wake_word(wake_word)
                        return  # 唤醒词不传递给其他回调
            
            # 只有在唤醒词检测到后才处理其他语音
            if self.asr_instance.wake_word_detected:
                # 检查是否为句子结束
                if hasattr(transcription_result, 'is_sentence_end') and transcription_result.is_sentence_end:
                    if self.on_sentence_end and text:
                        print(f"检测到句子结束: {text}")
                        # 检查是否应该过滤音频（音乐播放时的过滤机制）
                        if self.asr_instance.should_filter_audio(text):
                            print(f" 音乐播放中，过滤非音乐控制指令: {text}")
                            return  # 过滤掉非音乐控制指令
                        self.on_sentence_end(text, request_id)
                print("调用转录回调")
                self.on_transcription(transcription_result, request_id, usage)


class ASR:
    """实时语音识别和翻译类"""
    
    def __init__(self, api_key: str, 
                 model: str = "gummy-realtime-v1",
                 sample_rate: int = 16000,
                 transcription_enabled: bool = True,
                 translation_enabled: bool = False,
                 translation_target_languages: List[str] = None):
        """
        初始化ASR实例
        
        Args:
            api_key: DashScope API密钥
            model: 使用的模型名称
            sample_rate: 采样率
            transcription_enabled: 是否启用转录
            translation_enabled: 是否启用翻译
            translation_target_languages: 翻译目标语言列表
        """
        if translation_target_languages is None:
            translation_target_languages = ["en"]
            
        dashscope.api_key = api_key
        
        self.model = model
        self.sample_rate = sample_rate
        self.transcription_enabled = transcription_enabled
        self.translation_enabled = translation_enabled
        self.translation_target_languages = translation_target_languages
        
        self.callback = None
        self.translator = None
        self.is_running = False
        self.audio_thread = None
        self.audio_paused = False  # 添加音频暂停标志
        self.wake_word_detected = False  # 唤醒词检测状态
        self.wake_words = ["小沫小沫", "小莫小莫", "小墨小墨", "小末小末", "小陌小陌", "小默小默"]  # 默认唤醒词，支持多个同音字
        self.last_audio_time = 0  # 最后一次收到音频的时间
        self.silence_timeout = 120  # 2分钟静音超时（秒）
        
        # 音乐播放状态管理
        self.music_playing = False  # 音乐播放状态
        self.music_filter_enabled = False  # 音乐过滤是否启用
        self.music_control_keywords = [
            "关闭","停止","停止音乐", "关闭音乐", "关掉音乐", "停止播放", "关闭播放",
            "下一首", "下一首歌", "切歌", "换歌", "跳过", "下一曲",
            "上一首", "上一首歌", "上一曲", "前一首", "返回上一首",
            "播放音乐", "开始播放", "继续播放", "恢复播放",
            "音量", "调音量", "声音", "大小声", "静音", "取消静音"
        ]
        
        # 连接保活相关
        self.keepalive_enabled = True  # 是否启用保活机制
        self.keepalive_interval = 0.1  # 保活间隔（秒）
        self.silence_data = b'\x00' * 3200  # 静音数据，用于保活
        
    def set_callbacks(self, on_transcription: Optional[Callable] = None,
                     on_translation: Optional[Callable] = None,
                     on_open: Optional[Callable] = None,
                     on_close: Optional[Callable] = None,
                     on_sentence_end: Optional[Callable] = None,
                     on_wake_word: Optional[Callable] = None):
        """
        设置回调函数
        
        Args:
            on_transcription: 转录结果回调函数
            on_translation: 翻译结果回调函数
            on_open: 连接打开回调函数
            on_close: 连接关闭回调函数
            on_sentence_end: 句子结束回调函数，参数为(final_text, request_id)
            on_wake_word: 唤醒词检测回调函数，参数为(wake_word_text)
        """
        self.callback = ASRCallback(
            on_transcription=on_transcription,
            on_translation=on_translation,
            on_open=on_open,
            on_close=on_close,
            on_sentence_end=on_sentence_end,
            on_wake_word=on_wake_word,
            asr_instance=self
        )
    
    def start(self):
        """启动ASR服务"""
        if self.is_running:
            print("ASR服务已在运行中")
            return
            
        if not self.callback:
            # 使用默认回调
            self.callback = ASRCallback()
            
        self.translator = TranslationRecognizerRealtime(
            model=self.model,
            format="pcm",
            sample_rate=self.sample_rate,
            transcription_enabled=self.transcription_enabled,
            translation_enabled=self.translation_enabled,
            translation_target_languages=self.translation_target_languages,
            callback=self.callback,
        )
        
        self.translator.start()
        self.is_running = True
        
        # 启动音频处理线程
        self.audio_thread = threading.Thread(target=self._audio_loop, daemon=True)
        self.audio_thread.start()
        
        print("ASR服务已启动，请开始说话...")
    
    def stop(self):
        """停止ASR服务"""
        if not self.is_running:
            print("ASR服务未在运行")
            return
            
        self.is_running = False
        
        if self.translator:
            self.translator.stop()
            self.translator = None
            
        if self.audio_thread and self.audio_thread.is_alive():
            self.audio_thread.join(timeout=2)
            
        print("ASR服务已停止")
    
    def _audio_loop(self):
        """音频处理循环"""
        last_keepalive_time = 0
        
        while self.is_running and self.callback and self.callback.stream:
            try:
                current_time = time.time()
                
                # 检查静音超时
                if self.wake_word_detected and self.last_audio_time > 0:
                    if current_time - self.last_audio_time > self.silence_timeout:
                        print(f" 静音超时({self.silence_timeout}秒)，重置唤醒状态")
                        self.wake_word_detected = False
                        self.last_audio_time = 0
                        print("等待唤醒词...")
                
                # 如果音频被暂停，使用保活机制保持连接
                if self.audio_paused:
                    # 清理音频缓冲区，避免积累
                    try:
                        self.callback.stream.read(1024, exception_on_overflow=False)
                    except:
                        pass
                    
                    # 保活机制：定期发送静音数据保持连接
                    if (self.keepalive_enabled and 
                        self.translator and 
                        self.is_running and
                        current_time - last_keepalive_time >= self.keepalive_interval):
                        try:
                            self.translator.send_audio_frame(self.silence_data)
                            last_keepalive_time = current_time
                            print("🔗 发送保活数据，保持ASR连接")
                        except Exception as keepalive_error:
                            print(f"保活数据发送失败: {keepalive_error}")
                            # 如果保活失败，可能是连接断开，尝试重连
                            if "has stopped" in str(keepalive_error) or "stopped" in str(keepalive_error):
                                print("保活失败，检测到连接断开，尝试重连...")
                                if self._attempt_reconnect():
                                    print("保活重连成功")
                                    last_keepalive_time = current_time
                                else:
                                    print("保活重连失败，退出音频循环")
                                    break
                    
                    time.sleep(0.05)  # 暂停时减少CPU占用
                    continue
                
                # 正常音频处理
                data = self.callback.stream.read(3200, exception_on_overflow=False)
                if self.translator and self.is_running:
                    self.translator.send_audio_frame(data)
                    last_keepalive_time = current_time  # 更新保活时间
                    
            except Exception as e:
                # 如果是连接已停止的错误，尝试自动重连
                if "has stopped" in str(e) or "stopped" in str(e):
                    print("ASR连接已停止，尝试自动重连...")
                    if self._attempt_reconnect():
                        print("ASR连接重连成功，继续音频处理")
                        continue
                    else:
                        print("ASR连接重连失败，音频处理循环退出")
                        break
                else:
                    print(f"音频处理错误: {e}")
                    # 对于其他错误，也尝试重连
                    if self._attempt_reconnect():
                        print("ASR连接重连成功，继续音频处理")
                        continue
                    else:
                        print("ASR连接重连失败，音频处理循环退出")
                        break
            time.sleep(0.01)  # 避免CPU占用过高
    
    def _check_network_connectivity(self):
        """
        检查网络连接状态
        
        Returns:
            bool: 网络是否可用
        """
        try:
            import socket
            # 尝试连接到DNS服务器
            socket.create_connection(("8.8.8.8", 53), timeout=3)
            return True
        except OSError:
            return False
    
    def _attempt_reconnect(self, max_retries=3, retry_delay=2.0):
        """
        尝试重新连接ASR服务
        
        Args:
            max_retries: 最大重试次数
            retry_delay: 重试延迟时间（秒）
            
        Returns:
            bool: 重连是否成功
        """
        for attempt in range(max_retries):
            try:
                print(f" ASR重连尝试 {attempt + 1}/{max_retries}")
                
                # 检查网络连接
                if not self._check_network_connectivity():
                    print(" 网络连接不可用，等待网络恢复...")
                    time.sleep(retry_delay)
                    continue
                
                # 清理旧的连接
                if self.translator:
                    try:
                        self.translator.stop()
                    except:
                        pass
                    self.translator = None
                
                # 等待一下再重连
                time.sleep(retry_delay)
                
                # 重新创建连接
                self.translator = TranslationRecognizerRealtime(
                    model=self.model,
                    format="pcm",
                    sample_rate=self.sample_rate,
                    transcription_enabled=self.transcription_enabled,
                    translation_enabled=self.translation_enabled,
                    translation_target_languages=self.translation_target_languages,
                    callback=self.callback,
                )
                
                self.translator.start()
                print(" ASR连接重连成功")
                return True
                
            except Exception as e:
                print(f" ASR重连失败 (尝试 {attempt + 1}/{max_retries}): {e}")
                if attempt < max_retries - 1:
                    time.sleep(retry_delay)
        
        print(" ASR重连失败，已达到最大重试次数")
        return False
    
    def is_active(self) -> bool:
        """检查ASR服务是否正在运行"""
        return self.is_running
    
    def pause_audio(self):
        """暂停音频处理，避免TTS播放时的回声"""
        self.audio_paused = True
        print("ASR音频处理已暂停")
    
    def resume_audio(self):
        """恢复音频处理"""
        self.audio_paused = False
        print("ASR音频处理已恢复")
    
    def reset_wake_word(self):
        """重置唤醒词状态，等待下次唤醒"""
        self.wake_word_detected = False
        self.last_audio_time = 0  # 重置计时器
        print("等待唤醒词...")
    
    def set_wake_words(self, wake_words: List[str]):
        """设置自定义唤醒词列表"""
        self.wake_words = wake_words
        print(f"唤醒词已更新: {', '.join(wake_words)}")
    
    def set_keepalive_config(self, enabled: bool = True, interval: float = 0.1):
        """
        设置保活机制配置
        
        Args:
            enabled: 是否启用保活机制
            interval: 保活间隔（秒）
        """
        self.keepalive_enabled = enabled
        self.keepalive_interval = interval
        print(f"保活机制配置: 启用={enabled}, 间隔={interval}秒")
    
    def set_music_playing(self, is_playing: bool):
        """
        设置音乐播放状态
        
        Args:
            is_playing: 是否正在播放音乐
        """
        self.music_playing = is_playing
        if is_playing:
            print(" 音乐开始播放，启用ASR过滤模式")
            self.music_filter_enabled = True
        else:
            print(" 音乐停止播放，关闭ASR过滤模式")
            self.music_filter_enabled = False
    
    def is_music_playing(self) -> bool:
        """
        检查是否正在播放音乐
        
        Returns:
            bool: 是否正在播放音乐
        """
        return self.music_playing
    
    def is_music_control_command(self, text: str) -> bool:
        """
        检查文本是否包含音乐控制指令
        
        Args:
            text: 要检查的文本
            
        Returns:
            bool: 是否包含音乐控制指令
        """
        text_lower = text.lower()
        for keyword in self.music_control_keywords:
            if keyword in text_lower:
                return True
        return False
    
    def should_filter_audio(self, text: str) -> bool:
        """
        判断是否应该过滤音频输入
        
        Args:
            text: 识别的文本
            
        Returns:
            bool: 是否应该过滤
        """
        # 如果音乐过滤未启用，不过滤
        if not self.music_filter_enabled:
            return False
        
        # 如果包含音乐控制指令，不过滤（允许处理）
        if self.is_music_control_command(text):
            return False
        
        # 其他情况在音乐播放时过滤
        return True

    def get_connection_status(self) -> Dict[str, Any]:
        """
        获取连接状态信息
        
        Returns:
            Dict: 包含连接状态信息的字典
        """
        return {
            "is_running": self.is_running,
            "audio_paused": self.audio_paused,
            "wake_word_detected": self.wake_word_detected,
            "keepalive_enabled": self.keepalive_enabled,
            "keepalive_interval": self.keepalive_interval,
            "last_audio_time": self.last_audio_time,
            "silence_timeout": self.silence_timeout,
            "music_playing": self.music_playing,
            "music_filter_enabled": self.music_filter_enabled
        }


# 使用示例
if __name__ == "__main__":
    def on_transcription(transcription_result, request_id, usage):
        print(f"转录结果: {transcription_result.text}")
    
    def on_translation(translation_result, request_id, usage):
        english_translation = translation_result.get_translation("en")
        print(f"翻译结果: {english_translation.text}")
    
    def on_open():
        print("ASR连接已建立")
    
    def on_close():
        print("ASR连接已断开")
    
    def on_sentence_end(final_text, request_id):
        """句子结束回调函数"""
        print(f" 句子识别完成: {final_text}")
        # 在这里可以处理完整的句子，比如发送到LLM或其他处理逻辑
        # 例如：process_complete_sentence(final_text)
    
    # 创建ASR实例
    asr = ASR(api_key="sk-bd00cc29ad454ee49a32574fd2ea10a6")
    
    # 设置自定义唤醒词（可选，默认已包含小沫相关词汇）
    # asr.set_wake_words(["小沫小沫", "小莫小莫", "小墨小墨"])
    
    # 设置回调函数
    asr.set_callbacks(
        on_transcription=on_transcription,
        on_open=on_open,
        on_close=on_close,
        on_sentence_end=on_sentence_end  # 添加句子结束回调
    )
    
    try:
        # 启动ASR服务
        asr.start()
        
        # 保持运行
        while True:
            time.sleep(1)
            
    except KeyboardInterrupt:
        print("\n正在停止ASR服务...")
        asr.stop()