# 端侧大模型在线 / 离线部署与机器人指令遵循：深度版学习结果

## 1. 30 分钟路线的实际收获：先抓住整条 Agent 链路

我替你先把 `src/agent/main.py`、`src/agent/asr.py`、`src/agent/llm.py`、`src/agent/function_executor.py` 串起来之后，可以先给你一个总图：

- `main.py` 负责把 ASR、LLM、函数执行器、表情模块组起来，并处理启动、异常和退出。
- `asr.py` 负责实时语音识别、唤醒词门控、静音超时、保活和重连。
- `llm.py` 负责把用户输入发给模型，并强约束成 JSON，再把 `response` 送 TTS，把 `function` 送执行器。
- `function_executor.py` 负责把函数名真正落到机器人动作、音乐、导航、跟随、WiFi 显示等执行能力上。

如果你是小白，先记一个最重要概念：机器人 Agent 和聊天机器人最大的不同，不是“能不能对话”，而是“模型输出必须变成稳定可执行的协议”。这个仓库恰恰在做这件事。

## 2. 2 小时路线的实际收获：在线链路的难点不在模型，而在系统编排

### 2.1 `main.py` 做的不是简单调用，而是整个会话状态编排

`main.py` 启动时会：

- 初始化 ROS 节点。
- 从 `ConfigManager` 读取 API key。
- 创建 `ASR`，并设置 keepalive。
- 创建表情线程。
- 创建 `FunctionExecutor`。
- 创建 `LLMAgent`。
- 把 `on_sentence_end`、`on_wake_word` 等回调注册到 ASR。

这说明主程序的重点不是“跑一个模型”，而是把语音、动作、UI、配置、退出流程全组织起来。

### 2.2 `asr.py` 里最值得看的不是识别接口，而是状态机

`ASRCallback::on_event()` 里做了三层判断：

1. 如果当前 `audio_paused`，直接跳过文本处理。
2. 如果还没被唤醒，就先在去标点后的文本里找唤醒词。
3. 唤醒后，只有在 `is_sentence_end` 时才触发上层完整句处理。

而 `_audio_loop()` 还维护了：

- 静音超时 120 秒后自动退出唤醒状态。
- TTS 期间如果 `audio_paused=True`，就持续读麦克风清空缓冲，并定期发静音包做 keepalive。
- 一旦连接报 stopped，就自动 `_attempt_reconnect()`。

这段代码非常工程化。它解决的是“实时流式语音系统会掉线、会回声、会卡死”的现实问题，不是纯算法问题。

### 2.3 `llm.py` 里最关键的是 JSON 协议 + 流式拆分

`LLMAgent` 的 `agent_sys_prompt` 明确规定模型输出 JSON，核心字段包括：

- `function`：函数调用列表。
- `response`：要对用户说的话。

`chat_with_tts_streaming()` 的做法是：

- 先加载 memory，把历史记忆拼进 system prompt。
- 用 `Generation.call(..., stream=True, incremental_output=True)` 拉流式结果。
- 一边累计 `full_reply`，一边尝试 `json.loads(full_reply)`。
- 如果已经能解析成完整 JSON，就把 `response` 交给 TTS，把 `function` 逐个扔进异步线程执行。
- 如果还不是完整 JSON，就用字符串方式尽量抽取 `response` 片段，先做增量 TTS。
- 完整 JSON 出现后，再在后台线程里更新 memory。

这说明作者不是等模型整个回答完才动作，而是尽量把“能播报的部分”和“能执行的部分”提前拆出来并行推进。

### 2.4 `function_executor.py` 体现的不是花哨动作，而是资源调度

`FunctionExecutor` 初始化时会尝试接入：

- 头部舵机控制器。
- 云音乐模块。
- 跟随控制器。
- 导航控制器。
- ROS 头部控制话题订阅。

更关键的是，头部动作不是收到命令就立即抢占执行，而是进 `head_action_queue`，由独立 worker 串行消费。这个设计非常值得学，因为舵机动作本质上是物理资源，多个动作并发只会互相打架。

## 3. 1 天路线的实际收获：离线链路到底难在哪

离线链路不是简单“把云模型换成本地模型”。真正的难点在协议拆分。

`offline-agent/start.sh` 启动三个本地服务：

- sherpa-onnx ASR
- `llamacpp-ros`
- `tts_server`

而且用 `taskset` 把 LLM 和 TTS 绑到不同 CPU 核心，这说明作者已经意识到本地推理服务之间会互相抢 CPU。

`offline-agent/llamacpp-ros/src/cli.cpp` 的工作是：

- 订阅 `/llm_request`。
- 把 ASR 文本塞进本地 llama.cpp 推理上下文。
- 流式读取输出。
- 把播报内容发布到 `/tts/text_input`。
- 把函数调用发布到 `/llm/function_call`。

其中最精彩的是 `StreamTaskDispatcher.cpp`：

- `processDelta()` 持续吃模型增量输出。
- `extractResDelta()` 试图从流里抽取 `res` 字段文本。
- `processBufferedText()` 按中文标点切片，把一段段能说的话尽早送给 TTS。
- `processComplete()` 在完整 JSON 到齐后，解析 `fc` 数组，再统一回调函数调用。

这就把一个本来不稳定的“流式模型文本”拆成了两个更稳定的通道：

- 可播报文本流。
- 可执行函数流。

最后 `src/agent/offline_main_.py` 订阅 `/llm/function_call`，把函数调用字符串解析成 `name + arguments`，再转交给同一个 `FunctionExecutor`。这让在线和离线两条链路在“动作执行层”上保持了统一。

## 4. 原文问题逐一回答

### 4.1 为什么机器人 Agent 必须强约束模型输出协议

因为机器人不是只要“说得像样”就行，它还要真的动。自由文本输出很难稳定解析，今天模型说“去厨房”，明天可能说“我这就带你去厨房哦”，执行层很难判定哪部分是动作，哪部分是寒暄。强约束成 JSON 后，`response` 和 `function` 职责明确，系统才能稳定执行。

### 4.2 在线版本和离线版本分别把复杂度放在哪一层

在线版本把复杂度更多放在云服务编排和流式 TTS/ASR 协调上，模型能力本身较强，所以本地更像 orchestrator。离线版本则把复杂度前移到本地协议拆解、进程调度和资源管理上，因为本地小模型和本地服务没有云端那么“省心”。

### 4.3 `StreamTaskDispatcher.cpp` 为什么是离线版最值得学的代码之一

因为它解决了离线 Agent 最难的工程问题：模型输出不是一次性、稳定、完整的 JSON，而是不断增长的文本流。它要在 JSON 还没完全闭合前，先找出能安全播报的文本；等完整结果到齐后，再抽出函数调用。这种“半结构化流”的在线解析，是很多本地 Agent 真正难的地方。

### 4.4 TTS 期间暂停 ASR 为什么是必须的系统设计

如果不暂停，机器人会把自己刚说的话重新听进去，形成自激回路。作者在 `Callback.on_open()` 暂停 ASR，在 `on_complete()`、`on_error()`、`on_close()` 恢复 ASR，再配合 `ASR._audio_loop()` 的 keepalive 保持连接不断。这是一套完整闭环，不是单点 patch。

### 4.5 这个仓库在哪些地方真正覆盖了“部署”，哪些地方并没有完整覆盖“训练/微调/量化生产流程”

真实已覆盖的是：在线/离线模型接入、流式 ASR/TTS、函数调用、动作执行、配置持久化、本地服务启动编排。没有完整覆盖的是：从原始语料开始做 SFT/对齐训练、量化导出流水线、模型评估基线、自动化训练脚本平台。README 提到“微调、量化”更像项目能力范围说明，而不是这份仓库已经交付出的完整训练平台。

## 5. 给小白的补充背景

- ASR 是语音识别，TTS 是语音合成，LLM 是语言理解与规划。三者拼起来才是完整语音 Agent。
- “函数调用”不是只有 OpenAI function calling 那一种形式，本质是把模型输出变成可执行协议。
- 实时语音系统的难点常常不在识别率，而在回声、延迟、连接保活、异常恢复。
- 离线部署不是把云服务搬到本地那么简单，本地 CPU、内存、线程调度和服务编排都会成为问题。
