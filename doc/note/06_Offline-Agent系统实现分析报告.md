# Offline-Agent 系统实现分析报告

## 1. 分析范围

本报告聚焦项目中的 `offline-agent/` 子系统，目标不是复述其所 vendor 的上游框架细节，而是帮助快速理解“这个仓库自己是怎样把离线语音链路拼起来的”。

本次重点阅读的入口文件如下：

- `offline-agent/start.sh`
- `offline-agent/voice/sherpa-onnx/sherpa-onnx/voice/sherpa-onnx-microphone.cc`
- `offline-agent/voice/sherpa-onnx/sherpa-onnx/voice/CMakeLists.txt`
- `offline-agent/llamacpp-ros/src/cli.cpp`
- `offline-agent/llamacpp-ros/src/StreamTaskDispatcher.cpp`
- `offline-agent/llamacpp-ros/CMakeLists.txt`
- `offline-agent/tts/tts_server/src/main.cpp`
- `offline-agent/tts/tts_server/src/MessageQueue.cpp`
- `offline-agent/tts/tts_server/src/TTSModel.cpp`
- `offline-agent/tts/tts_server/src/AudioPlayer.cpp`
- `offline-agent/tts/CMakeLists.txt`
- `offline-agent/zmq-comm-kit/include/ZmqClient.h`
- `offline-agent/zmq-comm-kit/include/ZmqServer.h`
- `offline-agent/zmq-comm-kit/src/ZmqClient.cpp`
- `offline-agent/zmq-comm-kit/src/ZmqServer.cpp`
- `offline-agent/monitor_processes.py`
- `src/agent/offline_main_.py`

需要特别说明的是：`offline-agent/` 并不是一个完整自洽的“离线总控工程”，而更像一组离线语音相关组件的集合。真正的动作执行消费者不在 `offline-agent/` 目录里，而在 `src/agent/offline_main_.py`。

## 2. 一句话结论

`offline-agent` 的本质是一个“离线语音 I/O 管线”：

- ASR 负责把麦克风语音转成文本并发到 ROS
- LLM 桥接节点负责把文本送入本地 `llama.cpp` 推理，并把输出拆成 TTS 文本和函数调用
- TTS 节点负责本地合成并播放
- Python 侧的 `offline_main_.py` 负责消费函数调用并复用现有 `FunctionExecutor`

所以它并不是一个完整重写的离线 Agent，而是把“离线语音交互入口”接到了现有机器人能力栈上。

## 3. 目录与职责分工

从仓库结构看，`offline-agent/` 主要由 5 个部分组成：

```text
offline-agent/
├── start.sh
├── voice/            # ASR，基于 sherpa-onnx 定制
├── llamacpp-ros/     # 本地 LLM 的 ROS 封装
├── tts/              # 本地 TTS 播放服务
├── zmq-comm-kit/     # ASR/TTS 之间的阻塞握手封装
└── monitor_processes.py
```

各模块职责如下：

- `voice/` 负责离线识别和句末上屏，将识别结果发到 `/llm_request`
- `llamacpp-ros/` 负责订阅 `/llm_request`、运行本地模型、向 `/tts/text_input` 和 `/llm/function_call` 发消息
- `tts/` 负责从 `/tts/text_input` 接收文本、合成音频并通过 ALSA 播放
- `zmq-comm-kit/` 负责做一个极简的 REQ/REP 握手，用来阻塞 ASR，避免 TTS 回灌到麦克风
- `monitor_processes.py` 只是资源观测脚本，不参与主链路

动作执行侧并不在这里，而是：

- `src/agent/offline_main_.py` 订阅 `/llm/function_call`
- `src/agent/function_executor.py` 提供具体能力映射

## 4. 总体运行链路

从代码真实实现看，这套离线链路可以抽象成如下过程：

```text
麦克风
  -> sherpa-onnx-microphone-test1
  -> ROS 话题 /llm_request
  -> llamacpp-ros
  -> ROS 话题 /tts/text_input
  -> tts_server
  -> ALSA 播放

并行分支：
llamacpp-ros
  -> ROS 话题 /llm/function_call
  -> src/agent/offline_main_.py
  -> FunctionExecutor
  -> 导航 / 表情 / 舵机 / 跟随等本地能力
```

这条链路里最关键的通信接口只有 3 个 ROS 话题和 1 个 ZMQ 地址：

- `/llm_request`
- `/tts/text_input`
- `/llm/function_call`
- `tcp://localhost:6677`

其中 `tcp://localhost:6677` 不是业务数据通道，而是“ASR 和 TTS 的阻塞同步通道”。

## 5. 启动与进程编排

### 5.1 `start.sh` 的真实职责

`offline-agent/start.sh` 只做三件事：

1. 启动 ASR：`install/bin/sherpa-onnx-microphone-test1`
2. 启动 LLM：`install/bin/llamacpp-ros`
3. 启动 TTS：`install/bin/tts_server`

它还做了两个工程化处理：

- 设置 `LD_LIBRARY_PATH=/usr/local/lib:${LD_LIBRARY_PATH}`
- 用 `taskset` 固定 CPU 核心

当前脚本中的 CPU 绑定策略是：

- LLM 绑到 `0,1,2,3`
- TTS 绑到 `4,5,6,7`

这说明作者明显是在面向 8 核 ARM 板子做离线资源隔离，意图是减少 LLM 和 TTS 互相抢占 CPU。

### 5.2 `start.sh` 没有做的事情

这个脚本没有启动以下关键前置：

- `roscore`
- `src/agent/offline_main_.py`

因此它只负责把“离线语音三件套”拉起来，并没有把整条机器人执行闭环完整拉起。

换句话说，理论上完整运行至少需要这些进程：

```text
roscore
python3 src/agent/offline_main_.py
offline-agent/install/bin/sherpa-onnx-microphone-test1
offline-agent/install/bin/llamacpp-ros
offline-agent/install/bin/tts_server
```

如果只执行 `offline-agent/start.sh`：

- ASR 到 TTS 的语音对话链路理论上可以工作
- 但 `/llm/function_call` 没有人消费，机器人动作不会真正执行

### 5.3 退出清理

`start.sh` 通过 `trap cleanup SIGINT SIGTERM` 在退出时统一 `pkill`：

- `sherpa-onnx-microphone-test1`
- `llamacpp-ros`
- `tts_server`

这是一种直接但简单的进程收尾方式，适合单机场景，不适合更复杂的 supervisor 编排。

## 6. ASR 侧实现细节

### 6.1 真正要读哪份麦克风代码

仓库里有两个同名文件：

- `offline-agent/voice/sherpa-onnx/sherpa-onnx/csrc/sherpa-onnx-microphone.cc`
- `offline-agent/voice/sherpa-onnx/sherpa-onnx/voice/sherpa-onnx-microphone.cc`

真正被编译进可执行文件的是后者，因为 `voice/CMakeLists.txt` 明确把它编进了 `sherpa-onnx-microphone-test1`。

所以学习时不要先读 `csrc/` 下的上游版本，先读 `voice/` 下的这份定制版本。

### 6.2 这不是纯上游 sherpa 示例

`voice/sherpa-onnx-microphone.cc` 在上游麦克风识别样例基础上做了三类定制：

- 增加 ROS 节点初始化：`ros::init(..., "sherpa_onnx_microphone")`
- 增加 ROS 发布器：把识别结果发到 `/llm_request`
- 增加 ZMQ 客户端：连到 `tcp://localhost:6677`

也就是说，这个节点不是“识别结果打印到终端”那么简单，而是已经被改造成离线链路的正式入口。

### 6.3 核心逻辑

它的关键流程如下：

1. PortAudio 回调持续采集麦克风
2. 若 `wait == false`，音频帧被送进 `OnlineStream`
3. 主循环不断 `DecodeStream`
4. 一旦 `recognizer.IsEndpoint()` 成立，认为一句话结束
5. 把文本发布到 `/llm_request`
6. 立刻通过 ZMQ 向 TTS 发送 `"block"`
7. 等待 TTS 回复，回复后才把 `wait = false`
8. 重置 recognizer，开始下一轮识别

可以把这段逻辑概括成：

“句末识别触发 LLM 请求，然后在 TTS 播放完成前冻结 ASR 输入。”

### 6.4 防串音的实现方式

这里没有做回声消除，也没有做复杂的 VAD/TTS 同步，而是用了一个非常直接的策略：

- TTS 开始播报期间，ASR 不再把采样数据喂给识别器
- TTS 播完后，通过 ZMQ 回复，ASR 再继续收音

优点是实现简单、依赖少、易于落地。  
代价是整个链路是同步阻塞的，TTS 如果卡住，ASR 就会一直等下去。

### 6.5 一个容易忽略的实现点

`RecordCallback()` 并没有真正暂停麦克风设备，只是通过 `wait` 开关决定“要不要把采样送入识别器”。  
这意味着：

- 音频流还在跑
- 只是识别输入被丢弃

这比停止/重启设备更轻，但也意味着它不是严格意义上的录音设备级暂停。

## 7. LLM 桥接层实现细节

### 7.1 定位

`offline-agent/llamacpp-ros/` 的定位不是改造 `llama.cpp` 内核，而是给它套了一层 ROS 输入输出外壳。

`CMakeLists.txt` 显示该节点依赖本地预编译库：

- `libserver-context.a`
- `libcommon.a`
- `libcpp-httplib.a`
- `libmtmd.so`
- `libllama.so`
- `libggml.so`
- `libggml-cpu.so`
- `libggml-base.so`

这说明作者并不在这里重新构建整个 `llama.cpp` 生态，而是消费一组已经准备好的本地库。

### 7.2 `cli.cpp` 做了什么

`src/cli.cpp` 做的事情可以分成 4 步：

1. 初始化 ROS 节点 `llamacpp_ros`
2. 订阅 `/llm_request`
3. 发布 `/tts/text_input` 和 `/llm/function_call`
4. 初始化 `server_context`，加载 GGUF 模型，启动推理线程

当收到一条 `/llm_request` 消息时：

- 它保留已有 `system` 消息
- 清空旧的对话内容
- 只把当前这句 ASR 文本作为新的 user 输入
- 用 `SERVER_TASK_TYPE_COMPLETION` 发起一次推理

这意味着当前离线模式更接近“单轮问答”，不是长期多轮记忆对话。

### 7.3 流式输出契约

`cli.cpp` 把 `defaults.stream = true`，然后把增量 token 交给 `StreamTaskDispatcher`。  
这里的关键前提是：模型输出必须是约定好的 JSON。

系统实际上假设模型会输出类似下面的结构：

```json
{
  "res": "好的，我现在带你去厨房。",
  "fc": ["navigate_to(\"厨房\")", "head_smile"]
}
```

其中：

- `res` 是要说给用户听的话
- `fc` 是要执行的函数调用列表

这套离线链路并没有做一个通用的工具调用协议，而是用了“字符串化函数调用”作为中间层。

### 7.4 `StreamTaskDispatcher` 的职责

`StreamTaskDispatcher.cpp` 做两件事：

1. 尝试从流式增量里提取 `res`
2. 在完整 JSON 收齐后解析 `fc`

它对 TTS 的处理方式不是“等全句完整再发”，而是：

- 把 `res` 的增量内容拼进缓冲区
- 以中文标点分段
- 一旦遇到 `：。 ，、？！；` 就先发一段到 `/tts/text_input`
- 最终调用 `publishToTTS("END")` 作为结束标记

因此它追求的是“边生成边播报”的体感，而不是严格的句子完整性。

### 7.5 这个分发器的几个关键局限

这一层是整个离线链路里最脆弱的部分之一。

#### 1. 增量 JSON 提取非常脆弱

`extractResDelta()` 只是简单地在缓冲中找 `"res":"`。  
它不是一个真正的增量 JSON 解析器，因此在这些情况下容易出问题：

- 字段跨 chunk 方式变化
- 出现转义引号
- 模型输出格式稍有偏离
- 输出前面带有额外解释文本

#### 2. 完整 JSON 解析没有异常兜底

`parseFunctionCalls()` 直接 `json::parse(json_str)`，没有 try/catch。  
如果模型输出不是合法 JSON，节点有较高概率直接抛异常终止。

#### 3. TTS 分段时丢掉了标点本身

`processBufferedText()` 在遇到标点后发送的是“标点前文本”，标点字符本身没有被发送给 TTS。  
这会带来两个影响：

- 合成文本更干净
- 但也可能损失停顿和语气信息

#### 4. `processComplete()` 没用传入的 `full_content`

函数签名里虽然有 `full_content` 参数，但最终解析 `fc` 时实际使用的是成员变量 `accumulated_content_`。  
这不一定立刻出 bug，但会让代码可读性下降，也说明这里的实现还比较草。

## 8. TTS 侧实现细节

### 8.1 节点职责

`offline-agent/tts/tts_server/src/main.cpp` 把 TTS 节点组织成三个角色：

- ROS 订阅者：接收 `/tts/text_input`
- 合成线程：文本转音频
- 播放线程：音频写入 ALSA

同时它还启动了一个 ZMQ REP 服务：

- 地址：`tcp://*:6677`

这个 REP 服务不负责传输 TTS 内容，只负责给 ASR 一个“你可以恢复识别了”的回复。

### 8.2 双队列设计

`MessageQueue.cpp` 里实现的是一个双队列结构：

- 文本队列：给合成线程消费
- 音频队列：给播放线程消费

这样做的好处是把“模型推理”和“声卡播放”解耦，避免一边播放一边阻塞文本接收。

### 8.3 `END` 标记的真实用途

LLM 流式播报结束后会向 `/tts/text_input` 再发一条 `"END"`。  
TTS 节点把它视为一个特殊控制信号：

- `first_msg = true`
- 推入一个空音频消息
- 等播放线程处理到 `is_last` 时，再向 ASR 回复

因此这里的 `END` 不是给用户听的，而是用来闭合一次“ASR 发起 -> TTS 完成 -> ASR 恢复”的同步周期。

### 8.4 合成与播放实现

从 `TTSModel.cpp` 和 `AudioPlayer.cpp` 看，TTS 侧是一个很直接的本地实现：

- `TTSModel` 通过 `ttsLoadModel()` 加载二进制模型
- `SynthesizerTrn::infer()` 负责产出 PCM 数据
- `AudioPlayer` 使用 ALSA 的 `default` PCM 设备播放
- 播放是阻塞式 `snd_pcm_writei`

播放参数也比较固定：

- 单声道
- `S16_LE`
- 采样率按 `16000 * speed` 设置

如果发生 underrun，会最多重试 3 次。

### 8.5 一个值得注意的工程事实

`main.cpp` 虽然 `#include "TextProcessor.h"`，但当前主流程里并没有真正把 `TextProcessor` 接进来。  
也就是说，目前 TTS 主节点基本是“收到什么文本就按什么文本合成”，文本清洗和更细粒度切句能力没有真正生效。

## 9. ZMQ 阻塞握手机制

### 9.1 为什么还要加 ZMQ

从表面看，ASR、LLM、TTS 都已经走 ROS 话题了，似乎不需要额外再加一层通信。  
但作者额外引入 `zmq-comm-kit/`，是因为它要解决的不是“数据传输”，而是“严格的一问一答式阻塞同步”。

具体握手链路如下：

```text
ASR 识别到一句完整文本
  -> 发布 /llm_request
  -> 通过 ZMQ REQ 发送 "block"
  -> 等待 TTS 回复

LLM 逐段发布 /tts/text_input
  -> TTS 播放全部结束
  -> 通过 ZMQ REP 回复 "[tts -> voice]play end success"

ASR 收到回复
  -> wait = false
  -> 恢复继续识别
```

### 9.2 这个设计的本质

它本质上是在用 REQ/REP 把 ASR 和 TTS 人为串成一个同步状态机。

好处：

- 实现足够简单
- 不依赖 AEC
- 很适合板端快速闭环

代价：

- REQ/REP 强耦合
- TTS 异常时，ASR 会一直卡住
- 不适合多路并发或更复杂的对话状态管理

### 9.3 ZMQ 封装本身非常轻

`ZmqClient` 和 `ZmqServer` 只是对 `libzmq` 做了一个很薄的封装：

- `ZmqClient::request()` = 发送请求 + 等待回复
- `ZmqServer::receive()` = 等待请求
- `ZmqServer::send()` = 返回回复

它没有更复杂的连接管理、超时策略或重试状态机，因此系统可靠性主要取决于上层节点是否稳定。

## 10. 与主 Python Agent 的集成点

### 10.1 真正的函数调用消费者不在 `offline-agent/`

`offline-agent` 自身只会把函数调用字符串发布到 `/llm/function_call`。  
真正订阅并执行这些调用的是 `src/agent/offline_main_.py`。

这一步非常关键，因为它说明离线 Agent 没有重新发明一套动作执行系统，而是直接复用项目原有的 Python 执行层。

### 10.2 `offline_main_.py` 做了什么

它的工作流程非常清晰：

1. 初始化 ROS 节点 `offline_function_executor`
2. 可选启动 `emoji.Emobj()` 线程
3. 创建 `FunctionExecutor`
4. 订阅 `/llm/function_call`
5. 收到消息后做字符串解析
6. 调用 `FunctionExecutor.execute_function(...)`

因此，离线模式实际上把动作执行层复用了过来：

- 表情
- 导航
- 跟随
- 头部控制
- 其他本地函数

### 10.3 当前函数调用协议的限制

`offline_main_.py` 的 `parse_function_call()` 支持的格式非常简单：

- `head_smile`
- `navigate_to("厨房")`

它对“单个字符串参数”的支持是最完整的。  
如果未来希望支持复杂参数结构，例如 JSON 对象、多参数、命名参数，这层协议需要重做。

因此现在的函数调用接口本质上是：

- 易接入
- 易读
- 但表达力有限

## 11. 构建产物与安装路径

从各自的 `CMakeLists.txt` 可以看出，三个核心运行节点都会被安装到统一目录：

```text
offline-agent/install/bin/
```

对应关系如下：

- `voice/.../voice/CMakeLists.txt` 安装 `sherpa-onnx-microphone-test1`
- `llamacpp-ros/CMakeLists.txt` 安装 `llamacpp-ros`
- `tts/CMakeLists.txt` 安装 `tts_server`

这意味着 `start.sh` 并不关心构建过程，只关心最终产物是否已经落在 `offline-agent/install/bin/`。

换句话说，`offline-agent` 的运行方式是“消费现成产物”，而不是在脚本里触发编译。

## 12. 当前仓库状态与直接可运行性

这是阅读这个子系统时最容易误判的地方。  
从仓库当前状态看，它的“理论链路”是完整的，但“开箱即跑”并不成立。

### 12.1 当前已存在的资源

当前仓库里可以确认存在：

- ASR 模型：`offline-agent/voice/models/sherpa-onnx-streaming-zipformer-small-bilingual-zh-en-2023-02-16/...`
- TTS 模型：`offline-agent/tts/models/single_speaker_fast.bin`
- `llamacpp-ros` 的本地预编译库：`offline-agent/llamacpp-ros/lib/`

其中 `llamacpp-ros/lib/` 下已有：

- `libcommon.a`
- `libcpp-httplib.a`
- `libserver-context.a`
- `libllama.so` 及其版本符号链接
- `libggml.so`、`libggml-cpu.so`、`libggml-base.so`
- `libmtmd.so`

### 12.2 当前缺失的关键项

当前仓库里可以确认缺失：

- `offline-agent/install/` 目录不存在
- `offline-agent/install/bin/` 目录不存在
- `offline-agent/llamacpp-ros/models/` 目录不存在
- `start.sh` 期待的 `offline-agent/llamacpp-ros/models/Qwen3-0.6B-BF16.gguf` 不在仓库中

因此，仅从当前仓库状态出发，直接执行 `offline-agent/start.sh` 大概率会在以下位置失败：

- 找不到 `install/bin/*`
- 找不到 GGUF 模型

### 12.3 运行前还需要的外部条件

即便补齐了二进制和模型，真正运行前至少还需要：

- 可用的 ROS Master
- 可用的 ALSA 播放设备
- 可用的麦克风输入设备
- `/usr/local/lib/libzmq_component.so`
- 单独启动 `src/agent/offline_main_.py`

所以更准确的判断应该是：

“这套代码已经把离线语音闭环设计出来了，但仓库当前更像可学习、可移植的工程骨架，而不是立刻一键运行的发布包。”

## 13. 优点、短板与风险

### 13.1 优点

- 架构简单直接，链路短，容易定位问题
- 尽量本地化，不依赖在线服务
- ROS 只承担最必要的话题传递，复杂同步用 ZMQ 单独解决
- 动作执行层直接复用现有 Python Agent，不重复造轮子
- `taskset`、双队列、流式 TTS 这些设计都明显考虑了板端体验

### 13.2 短板

- LLM 输出格式强依赖固定 JSON，鲁棒性不足
- `StreamTaskDispatcher` 不是可靠的增量 JSON 解析器
- 函数调用协议只适合简单字符串参数
- `start.sh` 没有纳入 `roscore` 和 `offline_main_.py`
- TTS 文本处理链尚未完全接入

### 13.3 主要风险

- 模型输出一旦不合法，`json::parse` 可能直接让节点退出
- TTS 不回复时，ASR 会被 REQ/REP 永久阻塞
- 标点在分段中被丢弃，可能影响播报自然度
- 构建产物和模型路径缺失会造成“代码完整但运行失败”的错觉

## 14. 建议的学习顺序

如果目标是快速学懂，不建议从 vendor 源码海洋开始。  
更高效的顺序如下：

1. 先读 `offline-agent/start.sh`  
   看清楚系统到底起了哪几个进程、依赖哪些模型、哪些进程其实没被拉起。

2. 再读 `offline-agent/voice/sherpa-onnx/sherpa-onnx/voice/sherpa-onnx-microphone.cc`  
   这一步能理解离线链路是如何从麦克风进入 ROS 的，以及为什么要加 ZMQ 阻塞。

3. 然后读 `offline-agent/llamacpp-ros/src/cli.cpp`  
   看清 LLM 节点如何订阅 `/llm_request`，以及为什么它更像单轮任务而不是长期会话。

4. 接着读 `offline-agent/llamacpp-ros/src/StreamTaskDispatcher.cpp`  
   这是理解“流式文本播报”和“函数调用拆分”最关键的一层。

5. 再读 `offline-agent/tts/tts_server/src/main.cpp`  
   看清 `"END"`、双线程、ZMQ 回复是怎么闭环的。

6. 最后读 `src/agent/offline_main_.py`  
   到这一步才能真正看懂离线模式为什么能接入已有机器人动作系统。

7. 如果还要继续深挖，再分别看  
   `offline-agent/tts/tts_server/src/TTSModel.cpp`、`AudioPlayer.cpp`、`MessageQueue.cpp`、`offline-agent/monitor_processes.py`

## 15. 最终结论

从工程视角看，`offline-agent` 最有价值的地方，不是它各自选用了哪种 ASR、LLM、TTS，而是它把三者通过 ROS 和一个极简 ZMQ 握手拼成了一条可在板端工作的离线语音闭环。

这套实现的思路非常明确：

- 用 ROS 传递业务文本
- 用 ZMQ 做严格同步
- 用 Python 复用现有动作执行能力

因此，学习这个子系统时最重要的不是钻进每个 vendor 内核，而是先吃透这 4 个问题：

1. 语音文本是如何进入 `/llm_request` 的
2. LLM 输出为什么必须符合 `{res, fc}` 结构
3. TTS 为什么需要 `END` 和 ZMQ 回复
4. `/llm/function_call` 最终是如何落到 `FunctionExecutor` 的

只要这 4 个点吃透，整个 `offline-agent` 的实现骨架就基本掌握了。
