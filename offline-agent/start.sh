#!/bin/bash

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"
echo -e "当前目录: $SCRIPT_DIR"

export LD_LIBRARY_PATH=/usr/local/lib:${LD_LIBRARY_PATH}

cleanup() {
    echo -e "\n正在停止所有服务..."
    
    pkill -f "sherpa-onnx-microphone-test1" 2>/dev/null && echo -e "ASR 服务已停止" || true
    
    pkill -f "llamacpp-ros" 2>/dev/null && echo -e "LLM 服务已停止" || true
    
    pkill -f "tts_server" 2>/dev/null && echo -e "TTS 服务已停止" || true

    sleep 3

    pkill -f "llamacpp-ros" 2>/dev/null && echo -e "LLM 服务已停止" || true
    
    exit 0
}

trap cleanup SIGINT SIGTERM


# 1. 启动ASR服务 
echo -e "========== 启动 ASR 服务 =========="

# 查找ASR可执行文件
ASR_BIN="install/bin/sherpa-onnx-microphone-test1"
if [ ! -f "$ASR_BIN" ]; then
    echo -e "未找到 ASR 可执行文件: $ASR_BIN"
    echo -e "请先编译并安装 voice/sherpa-onnx"
    exit 1  
fi

if [ ! -z "$ASR_BIN" ]; then
    
    echo -e "启动 ASR: $ASR_BIN"
    cd "$SCRIPT_DIR"
    echo -e "当前目录: $SCRIPT_DIR"
    "$ASR_BIN" \
        --tokens="$SCRIPT_DIR/voice/models/sherpa-onnx-streaming-zipformer-small-bilingual-zh-en-2023-02-16/tokens.txt" \
        --encoder="$SCRIPT_DIR/voice/models/sherpa-onnx-streaming-zipformer-small-bilingual-zh-en-2023-02-16/encoder-epoch-99-avg-1.int8.onnx" \
        --decoder="$SCRIPT_DIR/voice/models/sherpa-onnx-streaming-zipformer-small-bilingual-zh-en-2023-02-16/decoder-epoch-99-avg-1.onnx" \
        --joiner="$SCRIPT_DIR/voice/models/sherpa-onnx-streaming-zipformer-small-bilingual-zh-en-2023-02-16/joiner-epoch-99-avg-1.int8.onnx"  &
    ASR_PID=$!
    echo -e "ASR 服务已启动"
    sleep 8

fi

# 2. 启动LLM服务 (llamacpp-ros)
echo -e "\n========== 启动 LLM 服务 =========="

LLM_BIN="install/bin/llamacpp-ros"
if [ ! -f "$LLM_BIN" ]; then
    echo -e "未找到 LLM 可执行文件: $LLM_BIN"
    echo -e "请先编译并安装 llamacpp-ros"
    LLM_BIN=""
    exit 1
fi
LLM_MODEL="$SCRIPT_DIR/llamacpp-ros/models/Qwen3-0.6B-BF16.gguf"
if [ ! -z "$LLM_BIN" ]; then
    echo -e "启动 LLM: $LLM_BIN"
    echo -e "模型路径: $LLM_MODEL"
    cd "$SCRIPT_DIR"
    taskset -c 0,1,2,3  "$LLM_BIN" -m "$LLM_MODEL" &

    LLM_PID=$!
    echo -e "LLM 服务已启动 (PID: $LLM_PID)"
    sleep 20

fi

# 3. 启动TTS服务 (tts_server)
echo -e "\n========== 启动 TTS 服务 =========="

TTS_BIN="install/bin/tts_server"
if [ ! -f "$TTS_BIN" ]; then
    echo -e "未找到 TTS 可执行文件: $TTS_BIN"
    echo -e "请先编译并安装 tts"
    TTS_BIN=""
    exit 1
fi

if [ ! -z "$TTS_BIN" ]; then
    TTS_MODEL="$SCRIPT_DIR/tts/models/single_speaker_fast.bin"
  
    echo -e "启动 TTS: $TTS_BIN"
    echo -e "模型路径: $TTS_MODEL"
    cd "$SCRIPT_DIR"
    taskset -c 4,5,6,7  "$TTS_BIN" "$TTS_MODEL"  &
    TTS_PID=$!
    echo -e " TTS 服务已启动"
    sleep 2
    
fi

echo -e "\n所有服务已启动，按 Ctrl+C 停止所有服务"
wait
