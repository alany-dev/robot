#!/bin/bash
# 直接启动Agent脚本

echo "=== 启动AI Agent (独立模式) ==="

# 进入agent目录
cd /home/orangepi/robot/src/agent

# 设置Python路径
export PYTHONPATH="/home/orangepi/robot/src/agent:$PYTHONPATH"

# 检查依赖
echo " 检查Python依赖..."
python3 -c "import dashscope, pyaudio" 2>/dev/null
if [ $? -ne 0 ]; then
    echo " 缺少必要的Python依赖，请先安装："
    echo "pip3 install -r requirements.txt"
    exit 1
fi

# 检查音频设备
echo " 检查音频设备..."
python3 -c "import pyaudio; p = pyaudio.PyAudio(); print(f'检测到{p.get_device_count()}个音频设备'); p.terminate()"

# 执行PWM权限修复（如果需要）
echo "🔧 执行PWM权限修复..."
bash fix.sh

# 启动agent
echo " 启动AI Agent..."
python3 main.py
