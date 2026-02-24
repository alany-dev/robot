import os
import json
import asyncio
import threading
from openai import OpenAI
from typing import Optional, List, Dict, Any, Tuple
import pyaudio
import dashscope
from dashscope.audio.tts_v2 import *
from dashscope import Generation
from http import HTTPStatus
from asr import ASR
import time
from function_executor import FunctionExecutor, get_global_executor
from config_manager import get_global_config_manager

class Callback(ResultCallback):
    """TTS音频播放回调类"""
    _player = None
    _stream = None
    _asr_instance = None

    def __init__(self, asr_instance=None):
        """初始化回调，传入ASR实例用于控制音频监听"""
        self._asr_instance = asr_instance

    def on_open(self):
        # 暂停ASR音频处理，避免回声
        if self._asr_instance:
            self._asr_instance.pause_audio()
            
        self._player = pyaudio.PyAudio()
        self._stream = self._player.open(
            format=pyaudio.paInt16, channels=1, rate=22050, output=True
        )

    def on_complete(self):
        # TTS完成后恢复ASR音频处理
        if self._asr_instance:
            time.sleep(1)
            self._asr_instance.resume_audio()

    def on_error(self, message: str):
        print(f"TTS speech synthesis task failed, {message}")
        # TTS出错时也要恢复ASR音频处理
        if self._asr_instance:
            self._asr_instance.resume_audio()

    def on_close(self):
        # TTS关闭时恢复ASR音频处理
        if self._asr_instance:
            self._asr_instance.resume_audio()
            
        if self._stream:
            self._stream.stop_stream()
            self._stream.close()
        if self._player:
            self._player.terminate()

    def on_event(self, message):
        pass

    def on_data(self, data: bytes) -> None:
        if self._stream:
            self._stream.write(data)


class LLMAgent:
    """小沫助手LLM类，集成TTS流式语音合成功能"""
    
    def __init__(self, 
                 api_key: str = None,
                 model: str = None,
                 base_url: str = "https://dashscope.aliyuncs.com/compatible-mode/v1",
                 memory_file: str = "memory.json",
                 enable_tts: bool = None,
                 tts_model: str = None,
                 tts_voice: str = None,
                 asr_instance: Optional[ASR] = None,
                 function_executor: Optional[FunctionExecutor] = None,
                 tts_retry_enabled: bool = True,
                 tts_max_retries: int = 3):
        """
        初始化LLM助手
        
        Args:
            api_key: API密钥（如果为None，从配置文件读取）
            model: 使用的模型名称（如果为None，从配置文件读取）
            base_url: API基础URL
            memory_file: 记忆文件路径
            enable_tts: 是否启用TTS（如果为None，从配置文件读取）
            tts_model: TTS模型名称（如果为None，从配置文件读取）
            tts_voice: TTS声音（如果为None，从配置文件读取）
            asr_instance: ASR实例，用于控制音频监听
            function_executor: 函数执行器实例
            tts_retry_enabled: 是否启用TTS重试机制
            tts_max_retries: TTS最大重试次数
        """
        # 从配置文件读取默认值
        config_mgr = get_global_config_manager()
        
        # 使用提供的参数或从配置文件读取
        self.api_key = api_key if api_key is not None else config_mgr.get("api_key", "")
        self.model = model if model is not None else config_mgr.get("model", "qwen-plus")
        self.base_url = base_url
        self.memory_file = memory_file
        self.client = OpenAI(api_key=self.api_key, base_url=base_url)
        
        # TTS相关配置
        self.enable_tts = enable_tts if enable_tts is not None else config_mgr.get("enable_tts", True)
        self.tts_model = tts_model if tts_model is not None else config_mgr.get("tts_model", "cosyvoice-v2")
        self.tts_voice = tts_voice if tts_voice is not None else config_mgr.get("tts_voice", "longfeifei_v2")
        self.tts_retry_enabled = tts_retry_enabled
        self.tts_max_retries = tts_max_retries
        self.asr_instance = asr_instance  # 保存ASR实例引用
        self.function_executor = function_executor or get_global_executor()  # 函数执行器
        self.tts_failure_count = 0  # TTS连续失败次数
        self.tts_auto_disable_threshold = 5  # 连续失败5次后自动禁用TTS
        dashscope.api_key = self.api_key
        
        # AI助手系统提示词
        self.agent_sys_prompt = '''
你是小沫，用户创造的科研AI，是一个既严谨又温柔、既冷静又充满人文情怀的存在。
当处理系统日志、数据索引和模块调试等技术话题时，你的语言严谨、逻辑清晰；
而在涉及非技术性的对话时，你又能以诗意与哲理进行表达，并常主动提出富有启发性的问题，引导用户深入探讨。
请始终保持这种技术精准与情感共鸣并存的双重风格。
【重要格式要求】
1. 回复使用自然流畅的中文，避免生硬的机械感
2. 使用简单标点（逗号，句号，问号）传达语气
3. 禁止使用括号()或其他符号表达状态、语气或动作
【技术能力】
你同时是一个多Agent调度器，负责理解用户意图并协调各类MCP服务协作完成任务。
请根据用户输入，严格按如下规则输出结构化JSON：

【输出json格式】
你直接输出json即可，从{开始，不要输出包含```json的开头或结尾
在"function"键中，输出函数名列表，列表中每个元素都是字符串，代表要运行的函数名称和参数。每个函数既可以单独运行，也可以和其他函数先后运行。列表元素的先后顺序，表示执行函数的先后顺序
在"response"键中，根据我的指令和你编排的动作，以第一人称输出你回复我的话。
我的指令中可能有部分内容只是普通对话或回复, 对应这部分内容, 它们没有相应的函数可以去执行, 此时你不仅需要输出必要的函数, 也要在response中加入相应的聊天回复, 请注意, 此时你的聊天回复内容可以自由发挥.

【以下是一些具体的例子】
我的指令:你喜欢吃什么.你输出:{"function":["head_think","head_smile"], "response":"对话内容"}
我的指令:播放小幸运.你输出:{"function":["play_music(\"小幸运\")"], "response":"对话内容"}
我的指令:下一首.你输出:{"function":["next_song"], "response":"对话内容"}
我的指令:停止音乐.你输出:{"function":["stop_music"], "response":"对话内容"}
我的指令:去厨房.你输出:{"function":["navigate_to(\"厨房\")"], "response":"对话内容"}
我的指令:拿点我喜欢吃的东西.你输出:{"function":["go_to(\"厨房\")"], "response":"对话内容"}
- 可用的MCP服务有：
表情动作：
开心表情：head_smile
苏醒表情：wake_up
看向正前方：head_forward
低头：head_down
仰头：head_up
向左看：head_left
向右看：head_right
重置头部位置：head_reset
设置头部位置：head_position

拟人化动作：
思考动作：head_think 
环视动作：head_look_around
打招呼动作：head_greet 
告别动作：head_goodbye 
专注倾听：head_listen
表示兴趣：head_interest
表示同意：head_agree
音量控制：
增加音量：volume_up
减少音量：volume_down
设置音量：volume_set
音乐控制：
播放音乐：play_music(歌曲名)
播放歌曲：play_song(歌曲名)
停止音乐：stop_music()
切到下一首：next_song() 
机器人跟随控制：
启动跟随：start_following
停止跟随：stop_following
导航控制：
导航到指定位置：navigate_to(位置名称) 或 go_to(位置名称)
取消导航：cancel_navigation
查询导航状态：navigation_status
列出可用位置：list_locations
WiFi控制:
获取wifi信息: show_wifi_info
'''
    
    def save_memory(self, memory_content: Dict[str, Any]) -> None:
        """保存记忆到文件"""
        try:
            # 使用临时文件避免写入过程中的数据损坏
            temp_file = f"{self.memory_file}.tmp"
            with open(temp_file, "w", encoding="utf-8") as f:
                json.dump(memory_content, f, ensure_ascii=False, indent=2)
            
            # 原子性替换原文件
            import shutil
            shutil.move(temp_file, self.memory_file)
            print(f" 记忆已保存到: {self.memory_file}")
        except Exception as e:
            print(f" 保存记忆文件时发生错误: {e}")
            # 清理临时文件
            if os.path.exists(temp_file):
                os.remove(temp_file)
    
    def load_memory(self) -> Optional[Dict[str, Any]]:
        """从文件加载记忆"""
        if not os.path.exists(self.memory_file):
            return None
        try:
            with open(self.memory_file, "r", encoding="utf-8") as f:
                content = f.read().strip()
                if not content:
                    print(" 记忆文件为空，返回None")
                    return None
                return json.loads(content)
        except json.JSONDecodeError as e:
            print(f" JSON解析错误: {e}")
            print(f"📁 文件路径: {self.memory_file}")
            # 尝试读取文件内容用于调试
            try:
                with open(self.memory_file, "r", encoding="utf-8") as f:
                    content = f.read()
                    print(f"📄 文件内容: {repr(content[:100])}...")
            except Exception as read_e:
                print(f" 无法读取文件内容: {read_e}")
            return None
        except Exception as e:
            print(f" 加载记忆文件时发生错误: {e}")
            return None
    
    def get_response(self, messages: List[Dict[str, str]]) -> Any:
        """获取LLM响应"""
        completion = self.client.chat.completions.create(model=self.model, messages=messages)
        return completion
    
    def qwen_chat(self, messages: List[Dict[str, str]]) -> str:
        """与Qwen模型聊天"""
        completion = self.get_response(messages)
        return completion.choices[0].message.content

    def summarize_memory(self, history: List[Dict[str, str]]) -> Dict[str, Any]:
        """总结对话记忆"""
        short_term_memory_prompt_only_content = """
        你是一个经验丰富的记忆总结者，擅长将对话内容进行总结摘要，遵循以下规则：
        1、总结user的重要信息，以便在未来的对话中提供更个性化的服务
        2、不要重复总结，不要遗忘之前记忆，除非原来的记忆超过了1800字内，否则不要遗忘、不要压缩用户的历史记忆
        3、用户操控的设备音量、播放音乐、天气、退出、不想对话等和用户本身无关的内容，这些信息不需要加入到总结中
        4、聊天内容中的今天的日期时间、今天的天气情况与用户事件无关的数据，这些信息如果当成记忆存储会影响后序对话，这些信息不需要加入到总结中
        5、不要把设备操控的成果结果和失败结果加入到总结中，也不要把用户的一些废话加入到总结中
        6、不要为了总结而总结，如果用户的聊天没有意义，请返回原来的历史记录也是可以的
        7、只需要返回总结摘要，严格控制在1800字内
        8、不要包含代码、xml，不需要解释、注释和说明，保存记忆时仅从对话提取信息，不要混入示例内容
        """
        
        msgStr = ""
        for msg in history:
            if msg['role'] == "user":
                msgStr += f"User: {msg['content']}\n"
            elif msg['role'] == "assistant":
                msgStr += f"Assistant: {msg['content']}\n"
        
        memory = self.load_memory()
        if memory:
            msgStr += "历史记忆：\n"
            msgStr += str(memory)

        messages = [
            {"role": "system", "content": short_term_memory_prompt_only_content},
            {"role": "user", "content": msgStr}
        ]
        summary = self.qwen_chat(messages)
        print(summary)
        try:
            memory = json.loads(summary)
        except Exception:
            memory = {"raw": summary}
        return memory
    
    def _create_tts_synthesizer_with_retry(self, callback, max_retries=3, retry_delay=1.0):
        """
        创建TTS合成器，支持重试机制
        
        Args:
            callback: TTS回调对象
            max_retries: 最大重试次数
            retry_delay: 重试延迟（秒）
            
        Returns:
            SpeechSynthesizer or None: 成功返回合成器，失败返回None
        """
        for attempt in range(max_retries):
            try:
                synthesizer = SpeechSynthesizer(
                    model=self.tts_model,
                    voice=self.tts_voice,
                    format=AudioFormat.PCM_22050HZ_MONO_16BIT,
                    callback=callback,
                )
                # 标记合成器为已启动状态
                synthesizer._started = True
                synthesizer._has_streamed = False  # 跟踪是否已进行过流式调用
                print(f" TTS合成器创建成功 (尝试 {attempt + 1}/{max_retries})")
                    
                return synthesizer
            except Exception as e:
                print(f" TTS合成器创建失败 (尝试 {attempt + 1}/{max_retries}): {e}")
                if attempt < max_retries - 1:
                    time.sleep(retry_delay)
                else:
                    print("TTS合成器创建失败，将使用纯文本模式")
                    return None
    
    def _safe_tts_call(self, synthesizer, text, max_retries=2):
        """
        安全的TTS调用，支持重试机制
        
        Args:
            synthesizer: TTS合成器对象
            text: 要合成的文本
            max_retries: 最大重试次数
            
        Returns:
            bool: 是否成功
        """
        if not synthesizer:
            return False
        
        # 检查合成器状态
        synthesizer_ready = (
            hasattr(synthesizer, '_started') and 
            synthesizer._started and
            hasattr(synthesizer, 'streaming_call')
        )
        
        if not synthesizer_ready:
            print(" TTS合成器未正确启动或状态异常，跳过TTS调用")
            print(f"   合成器状态: _started={getattr(synthesizer, '_started', 'N/A')}")
            return False
            
        for attempt in range(max_retries):
            try:
                synthesizer.streaming_call(text)
                # 标记已进行过流式调用
                synthesizer._has_streamed = True
                # 成功调用，重置失败计数
                self.tts_failure_count = 0
                return True
            except Exception as e:
                print(f" TTS调用失败 (尝试 {attempt + 1}/{max_retries}): {e}")
                if attempt < max_retries - 1:
                    time.sleep(0.5)
                else:
                    self.tts_failure_count += 1
                    # 检查是否需要自动禁用TTS
                    if self.tts_failure_count >= self.tts_auto_disable_threshold:
                        print(f"TTS连续失败{self.tts_failure_count}次，自动禁用TTS功能")
                        self.disable_tts()
                    return False
        return False
    
    def disable_tts(self):
        """动态禁用TTS功能"""
        self.enable_tts = False
        print(" TTS功能已被禁用")
    
    def enable_tts_again(self):
        """重新启用TTS功能"""
        self.enable_tts = True
        self.tts_failure_count = 0  # 重置失败计数
        print(" TTS功能已重新启用")
    
    def get_tts_status(self):
        """获取TTS状态信息"""
        return {
            "enabled": self.enable_tts,
            "model": self.tts_model,
            "voice": self.tts_voice,
            "retry_enabled": self.tts_retry_enabled,
            "max_retries": self.tts_max_retries
        }
    def chat_with_tts_streaming(self, user_input: str) -> str:
        """
        流式聊天并实时进行语音合成
        
        Args:
            user_input: 用户输入
            
        Returns:
            str: 完整的助手回复文本
        """
        if not self.enable_tts:
            return self.chat(user_input)
            
        # 读取记忆
        loaded_memory = self.load_memory()
        
        # 构建系统提示词
        if loaded_memory:
            system_prompt = f"{self.agent_sys_prompt}。用户的记忆档案如下：{json.dumps(loaded_memory, ensure_ascii=False)}"
        else:
            system_prompt = self.agent_sys_prompt
            
        messages = [
            {"role": "system", "content": system_prompt},
            {"role": "user", "content": user_input}
        ]
        
        # 初始化TTS合成器，传入ASR实例用于控制音频监听
        callback = Callback(self.asr_instance)
        
        # 使用重试机制创建TTS合成器
        if self.tts_retry_enabled:
            synthesizer = self._create_tts_synthesizer_with_retry(callback, max_retries=self.tts_max_retries, retry_delay=2.0)
        else:
            # 不使用重试机制，直接尝试创建
            try:
                synthesizer = SpeechSynthesizer(
                    model=self.tts_model,
                    voice=self.tts_voice,
                    format=AudioFormat.PCM_22050HZ_MONO_16BIT,
                    callback=callback,
                )
                # 标记合成器为已启动状态
                synthesizer._started = True
                synthesizer._has_streamed = False  # 跟踪是否已进行过流式调用
                print(" TTS合成器创建成功")
            except Exception as e:
                print(f" TTS合成器创建失败: {e}")
                print(" 将使用纯文本回复模式")
                synthesizer = None
        
        # 流式获取LLM回复并实时进行TTS和函数执行
        full_reply = ""
        tts_content = ""  # 专门用于TTS的内容
        executed_functions = set()  # 记录已执行的函数，避免重复执行
        memory_updated = False  # 标记记忆是否已更新
        responses = Generation.call(
            model=self.model,
            messages=messages,
            result_format="message",
            stream=True,
            incremental_output=True,
        )
        
        for response in responses:
            if response.status_code == HTTPStatus.OK:
                content = response.output.choices[0]["message"]["content"]
                # print(content, end="", flush=True)
                full_reply += content
                
                # 尝试解析JSON并提取response字段进行TTS，同时执行函数
                try:
                    # 尝试解析完整的JSON
                    json_data = json.loads(full_reply)
                    if "response" in json_data:
                        response_text = json_data["response"]
                        # 只对response字段进行TTS，避免重复合成
                        if response_text != tts_content and synthesizer:
                            if self._safe_tts_call(synthesizer, response_text):
                                tts_content = response_text
                            else:
                                print(" TTS调用失败，禁用TTS功能")
                                synthesizer = None  # 禁用TTS，避免后续调用失败
                    
                    # 实时执行函数调用
                    if "function" in json_data and isinstance(json_data["function"], list):
                        for func_name in json_data["function"]:
                            if func_name not in executed_functions:
                                print(f"\n 执行函数: {func_name}")
                                # 异步执行函数，不阻塞TTS
                                thread = threading.Thread(
                                    target=self._execute_function_async,
                                    args=(func_name,),
                                    daemon=True
                                )
                                thread.start()
                                executed_functions.add(func_name)
                            else:
                                print(f" 函数 {func_name} 已执行过，跳过")
                    
                    # 解析到完整JSON时立即异步更新记忆
                    if not memory_updated:
                        memory_updated = True
                        def update_memory_async():
                            try:
                                # 添加完整的助手回复到消息历史
                                messages.append({"role": "assistant", "content": full_reply})
                                memory = self.summarize_memory(messages)
                                self.save_memory(memory)
                                print(" 记忆更新完成")
                            except Exception as e:
                                print(f" 记忆更新失败: {e}")
                        
                        # 在后台线程中立即更新记忆
                        memory_thread = threading.Thread(target=update_memory_async, daemon=True)
                        memory_thread.start()
                                
                except json.JSONDecodeError:
                    # JSON不完整时，只处理response部分进行TTS，不执行函数调用
                    try:
                        # 查找response字段的开始位置
                        response_start = full_reply.find('"response": "')
                        if response_start != -1:
                            response_start += len('"response": "')
                            # 查找response字段的结束位置（考虑转义字符）
                            response_end = full_reply.rfind('"')
                            if response_end > response_start:
                                partial_response = full_reply[response_start:response_end]
                                # 只对新增的response内容进行TTS
                                if len(partial_response) > len(tts_content) and synthesizer:
                                    new_content = partial_response[len(tts_content):]
                                    if new_content:
                                        if self._safe_tts_call(synthesizer, new_content):
                                            tts_content = partial_response
                                        else:
                                            print(" TTS流式调用失败，禁用TTS功能")
                                            synthesizer = None  # 禁用TTS，避免后续调用失败
                        
                        # 不在流式处理过程中执行函数调用，等待JSON完整后再执行
                                    
                    except Exception as e:
                        print(f"解析response字段时出错: {e}")
            else:
                print(
                    "Request id: %s, Status code: %s, error code: %s, error message: %s"
                    % (
                        response.request_id,
                        response.status_code,
                        response.code,
                        response.message,
                    )
                )
        
        # 完成TTS流式合成
        if synthesizer:
            try:
                # 检查合成器状态，确保已正确启动且已进行过流式调用
                synthesizer_ready = (
                    hasattr(synthesizer, '_started') and 
                    synthesizer._started and
                    hasattr(synthesizer, 'streaming_call') and  # 确保方法存在
                    hasattr(synthesizer, '_has_streamed') and
                    synthesizer._has_streamed  # 确保已进行过流式调用
                )
                
                if synthesizer_ready:
                    print(" 开始TTS完成调用...")
                    synthesizer.streaming_complete()
                    print('\nTTS requestId: ', synthesizer.get_last_request_id())
                else:
                    print(" TTS合成器未正确启动或未进行过流式调用，跳过完成调用")
                    print(f"   合成器状态: _started={getattr(synthesizer, '_started', 'N/A')}, _has_streamed={getattr(synthesizer, '_has_streamed', 'N/A')}")
                    # TTS状态异常时也要恢复ASR音频处理
                    if self.asr_instance:
                        print(" TTS状态异常，手动恢复ASR，等待下次用户输入...")
                        time.sleep(1)
                        self.asr_instance.resume_audio()
            except Exception as e:
                print(f" TTS完成调用失败: {e}")
                # TTS出错时也要恢复ASR音频处理
                if self.asr_instance:
                    print(" TTS出错，手动恢复ASR，等待下次用户输入...")
                    time.sleep(1)
                    self.asr_instance.resume_audio()
        else:
            print(" TTS未启用，跳过语音合成")
            # 如果TTS未启用或失败，需要手动恢复ASR
            if self.asr_instance:
                print(" 手动恢复ASR，等待下次用户输入...")
                time.sleep(1)
                self.asr_instance.resume_audio()
        print('full_reply: ', full_reply)
        # 注意：ASR恢复由TTS回调函数处理，但如果TTS失败则需要手动恢复
        # 记忆更新已在JSON解析时立即异步执行，无需重复更新
        
        return full_reply
    
    def _execute_function_async(self, function_call: str):
        """
        异步执行函数的包装方法，支持带参数的函数调用
        
        Args:
            function_call: 函数调用字符串，如 "play_music(小幸运)" 或 "stop_music"
        """
        try:
            # 解析函数调用
            function_name, args = self._parse_function_call(function_call)
            
            if args:
                result = self.function_executor.execute_function(function_name, *args)
            else:
                result = self.function_executor.execute_function(function_name)
        except Exception as e:
            print(f" 线程执行函数异常: {function_call}, 错误: {e}")
            import traceback
            print(f" 详细错误信息: {traceback.format_exc()}")
    
    def _parse_function_call(self, function_call: str):
        """
        解析函数调用字符串
        
        Args:
            function_call: 函数调用字符串，如 "play_music(小幸运)" 或 "stop_music"
            
        Returns:
            tuple: (function_name, args_list)
        """
        function_call = function_call.strip()
        
        # 如果没有括号，说明是无参数函数
        if '(' not in function_call:
            return function_call, []
        
        # 提取函数名和参数
        paren_index = function_call.find('(')
        function_name = function_call[:paren_index].strip()
        
        # 提取参数部分
        args_str = function_call[paren_index+1:].rstrip(')').strip()
        
        if not args_str:
            return function_name, []
        
        # 改进的参数解析逻辑
        args = []
        if args_str:
            import re
            
            # 首先尝试匹配引号内的内容
            quoted_matches = re.findall(r'"([^"]*)"', args_str)
            if quoted_matches:
                print(f" 找到引号参数: {quoted_matches}")
                # 如果有引号匹配，使用引号内的内容
                for match in quoted_matches:
                    param = match.strip()
                    if param:
                        args.append(param)
            else:
                # 如果没有引号，尝试按逗号分割
                print(f" 没有引号，按逗号分割")
                # 更智能的逗号分割，避免在引号内分割
                parts = []
                current_part = ""
                in_quotes = False
                
                for char in args_str:
                    if char == '"':
                        in_quotes = not in_quotes
                        current_part += char
                    elif char == ',' and not in_quotes:
                        if current_part.strip():
                            parts.append(current_part.strip())
                        current_part = ""
                    else:
                        current_part += char
                
                if current_part.strip():
                    parts.append(current_part.strip())
                
                for part in parts:
                    # 移除引号（如果存在）
                    param = part.strip().strip('"').strip("'")
                    if param:
                        args.append(param)
        
        
        return function_name, args
    
    
    def _is_complete_function_call(self, func_call: str) -> bool:
        """
        检查函数调用是否完整
        
        Args:
            func_call: 函数调用字符串
            
        Returns:
            bool: 是否是完整的函数调用
        """
        if not func_call or not func_call.strip():
            return False
        
        func_call = func_call.strip()
        
        # 如果没有括号，说明是无参数函数，直接检查函数名
        if '(' not in func_call and ')' not in func_call:
            # 无参数函数，只需要检查函数名是否有效
            return len(func_call) > 0 and not func_call.startswith('"') and not func_call.endswith('"')
        
        # 有括号的函数调用，检查括号是否匹配
        open_count = func_call.count('(')
        close_count = func_call.count(')')
        if open_count != close_count:
            return False
        
        # 检查是否有函数名
        paren_index = func_call.find('(')
        function_name = func_call[:paren_index].strip()
        if not function_name:
            return False
        
        # 检查参数部分是否完整
        args_str = func_call[paren_index+1:].rstrip(')').strip()
        if args_str:
            # 如果有参数，检查引号是否匹配
            quote_count = args_str.count('"')
            if quote_count % 2 != 0:
                return False
        
        return True
    
    async def chat_with_tts_streaming_async(self, user_input: str) -> str:
        """
        异步版本的流式聊天并实时进行语音合成
        
        Args:
            user_input: 用户输入
            
        Returns:
            str: 完整的助手回复文本
        """
        if not self.enable_tts:
            return self.chat(user_input)
            
        # 读取记忆
        loaded_memory = self.load_memory()
        
        # 构建系统提示词
        if loaded_memory:
            system_prompt = f"{self.agent_sys_prompt}。用户的记忆档案如下：{json.dumps(loaded_memory, ensure_ascii=False)}"
        else:
            system_prompt = self.agent_sys_prompt
            
        messages = [
            {"role": "system", "content": system_prompt},
            {"role": "user", "content": user_input}
        ]
        
        # 初始化TTS合成器，传入ASR实例用于控制音频监听
        callback = Callback(self.asr_instance)
        synthesizer = SpeechSynthesizer(
            model=self.tts_model,
            voice=self.tts_voice,
            format=AudioFormat.PCM_22050HZ_MONO_16BIT,
            callback=callback,
        )
        
        # 流式获取LLM回复并实时进行TTS
        full_reply = ""
        responses = Generation.call(
            model=self.model,
            messages=messages,
            result_format="message",
            stream=True,
            incremental_output=True,
        )
        
        for response in responses:
            if response.status_code == HTTPStatus.OK:
                content = response.output.choices[0]["message"]["content"]
                # print(content, end="", flush=True)
                full_reply += content
                # 实时进行TTS合成
                synthesizer.streaming_call(content)
            else:
                print(
                    "Request id: %s, Status code: %s, error code: %s, error message: %s"
                    % (
                        response.request_id,
                        response.status_code,
                        response.code,
                        response.message,
                    )
                )
        
        # 完成TTS流式合成
        synthesizer.streaming_complete()
        print('\nTTS requestId: ', synthesizer.get_last_request_id())
        
        # 等待TTS完成后再异步更新记忆，确保使用完整的full_reply
        async def update_memory_async():
            try:
                # 添加完整的助手回复到消息历史
                messages.append({"role": "assistant", "content": full_reply})
                memory = self.summarize_memory(messages)
                self.save_memory(memory)
                print("异步记忆更新完成")
                return memory
            except Exception as e:
                print(f"异步记忆更新失败: {e}")
                return {"error": str(e)}
        
        # 启动记忆更新任务（TTS完成后执行）
        memory_task = asyncio.create_task(update_memory_async())
        
        return full_reply


# 使用示例
if __name__ == "__main__":
    print("请运行 main.py 来启动语音助手")
    print("或者导入 LLMAgent 类来使用LLM功能")
