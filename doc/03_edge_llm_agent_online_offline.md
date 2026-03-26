# 端侧大模型在线 / 离线部署与机器人指令遵循

## 1. 这个功能到底解决什么问题

这部分解决的是“机器人如何稳定地听懂人话、理解意图、执行动作、再把结果说出来”。仓库给出的不是一个抽象聊天机器人，而是一条面向实体机器人执行的 Agent 链路：语音输入、唤醒控制、LLM 输出约束、函数调用执行、记忆更新、TTS 流式反馈，全都要能在真实设备上跑起来。

需要先说明边界：README 提到“微调、量化”，但仓库里最完整、最可复现的内容是在线/离线推理部署、模型接入、函数调用执行和全链路编排，不是完整训练平台。阅读时要分清“仓库已经实现的工程链路”和“项目介绍里覆盖的整体能力范围”。

## 2. 先看哪些文件

在线链路优先看：

- `src/agent/main.py`
- `src/agent/asr.py`
- `src/agent/llm.py`
- `src/agent/function_executor.py`
- `src/agent/navigation_controller.py`
- `src/agent/robot_following_controller.py`
- `src/agent/config_manager.py`
- `src/agent/start_agent.sh`

离线链路优先看：

- `offline-agent/start.sh`
- `offline-agent/llamacpp-ros/src/cli.cpp`
- `offline-agent/llamacpp-ros/src/StreamTaskDispatcher.cpp`
- `src/agent/offline_main_.py`
- `offline-agent/tts/README.md`

## 3. 启动路径 / 数据流 / 控制流

在线版本的主流程是：麦克风音频 -> ASR -> LLM 流式输出 JSON -> 提取 `response` 做 TTS -> 提取 `function` 做异步执行 -> 更新记忆。

典型启动方式：

```bash
bash src/agent/start_agent.sh
# 或
python3 src/agent/main.py
```

离线版本的主流程是：本地 ASR -> `llamacpp-ros` -> 本地 TTS / 函数调用 ROS 话题 -> `offline_main_.py` 执行机器人动作。

典型启动方式：

```bash
bash offline-agent/start.sh
python3 src/agent/offline_main_.py
```

## 4. 亮点实现细节

### 4.1 LLM 输出被强约束成可执行协议

`LLMAgent` 不是让模型随便回答，而是通过 system prompt 约束其输出 JSON，至少包含 `response` 和 `function` 列表。这一步非常关键，因为机器人系统最怕“看起来聪明，实际不可执行”的自由文本。这里的核心思想是：先把模型输出收敛成协议，再谈执行链路稳定性。

### 4.2 流式回复和函数执行是并行推进的

在线链路里的 `chat_with_tts_streaming()` 会边收流式结果边提取 `response` 给 TTS，同时异步触发函数执行和记忆更新。这意味着系统追求的不是“等模型全说完再行动”，而是尽快把可执行片段和可播报片段拆出来并并行推进，直接降低用户感知延迟。

### 4.3 ASR 与 TTS 之间做了状态协调

`ASR` 模块里有唤醒词门控、保活、重连，以及 TTS 期间暂停/恢复音频的逻辑。这个细节很工程化：如果没有这层状态机，机器人会把自己的播报又识别回去，形成回声闭环。

### 4.4 函数执行器不是简单 `if-else`

`FunctionExecutor` 统一注册头部动作、音乐控制、跟随、导航、WiFi 展示等能力，并且对头部动作采用队列 worker 处理。这里体现了“动作执行是系统资源，而不是字符串分发”的设计思路，避免多个高层指令互相抢占底层执行通道。

### 4.5 离线链路的重点是流式协议拆解

`StreamTaskDispatcher.cpp` 很值得细读。它要从流式、近似 JSON 的模型输出里，边读边拆出适合 TTS 的句段，同时抽取 `fc` 数组用于函数调用。换句话说，离线版的真正难点不是“调用本地模型”，而是把一个不稳定的流式文本输出，安全地转换成可播报、可执行的双通道结果。

### 4.6 配置管理是原子化的

`ConfigManager` 保存 API Key、模型名、TTS 配置时采用临时文件加原子替换。这种写法不显眼，但对设备端系统非常重要，可以避免掉电或异常退出把配置写坏。

## 5. 调试与验证方法

- 先单独验证在线和离线两条链路，不要一开始就混用。
- 人工构造几个固定 prompt，检查输出 JSON 是否稳定包含 `response` 和 `function`。
- 验证 TTS 期间 ASR 是否被暂停，否则容易出现自激回声。
- 对离线链路重点观察 `StreamTaskDispatcher` 是否能在长句、半截 JSON、标点缺失时稳定拆分。
- 如果动作不执行，先查 `/llm/function_call` 是否有正确消息，再看 `offline_main_.py` 与 `FunctionExecutor`。

## 6. 学习路线（30 分钟 / 2 小时 / 1 天）

### 30 分钟

先只看 `main.py`、`llm.py`、`function_executor.py`，理解“LLM 输出 JSON 协议 -> 机器人动作执行”的基本框架。

### 2 小时

再补 `asr.py`、`config_manager.py` 和离线侧 `cli.cpp`、`StreamTaskDispatcher.cpp`。到这一步，你应该能区分在线链路和离线链路的真正差异：不是模型大小，而是服务边界、协议拆解和设备管理方式不同。

### 1 天

自己设计一组指令样例，覆盖导航、头部动作、音乐、跟随。分别跑在线和离线版本，记录从说话到播报、从说话到动作执行的延迟，并观察失败路径。这样你才能真正掌握这条 Agent 工程链路。

## 7. 你学完后应该能回答什么问题

- 为什么机器人 Agent 必须强约束模型输出协议。
- 在线版本和离线版本分别把复杂度放在哪一层。
- `StreamTaskDispatcher.cpp` 为什么是离线版最值得学的代码之一。
- TTS 期间暂停 ASR 为什么是必须的系统设计。
- 这个仓库在哪些地方真正覆盖了“部署”，哪些地方并没有完整覆盖“训练/微调/量化生产流程”。
