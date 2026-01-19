#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import psutil
import time
import matplotlib.pyplot as plt
import matplotlib.animation as animation
from collections import deque
from datetime import datetime
import sys

# 要监控的进程名称
PROCESS_NAMES = [
    "sherpa-onnx-microphone",
    "llamacpp-ros",
    "tts_server"
]

# 数据存储（每个进程一个队列）
MAX_DATA_POINTS = 100  # 最多保存100个数据点
cpu_data = {name: deque(maxlen=MAX_DATA_POINTS) for name in PROCESS_NAMES}
mem_data = {name: deque(maxlen=MAX_DATA_POINTS) for name in PROCESS_NAMES}
time_data = deque(maxlen=MAX_DATA_POINTS)

def find_processes():
    """查找要监控的进程"""
    processes = {}
    for proc in psutil.process_iter(['pid', 'name', 'cmdline']):
        try:
            name = proc.info['name']
            cmdline = ' '.join(proc.info['cmdline']) if proc.info['cmdline'] else ''
            
            for target_name in PROCESS_NAMES:
                if target_name in name or target_name in cmdline:
                    if target_name not in processes:
                        processes[target_name] = []
                    processes[target_name].append(proc)
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            continue
    return processes

def get_process_stats(processes):
    """获取进程的 CPU 和内存使用率"""
    stats = {}
    for name in PROCESS_NAMES:
        cpu_total = 0.0
        mem_total = 0.0
        
        if name in processes:
            for proc in processes[name]:
                try:
                    cpu_total += proc.cpu_percent(interval=0.1)
                    mem_info = proc.memory_info()
                    mem_total += mem_info.rss / (1024 * 1024)  # 转换为 MB
                except (psutil.NoSuchProcess, psutil.AccessDenied):
                    continue
        
        stats[name] = {
            'cpu': cpu_total,
            'mem': mem_total
        }
    
    return stats

def update_data(frame):
    """更新数据并绘制图表"""
    processes = find_processes()
    stats = get_process_stats(processes)
    
    current_time = datetime.now().strftime('%H:%M:%S')
    time_data.append(current_time)
    
    # 更新数据
    for name in PROCESS_NAMES:
        cpu_data[name].append(stats[name]['cpu'])
        mem_data[name].append(stats[name]['mem'])
    
    # 清除之前的图表
    plt.clf()
    
    # 创建两个子图
    fig = plt.gcf()
    fig.set_size_inches(12, 8)
    
    # CPU 使用率图表
    plt.subplot(2, 1, 1)
    for name in PROCESS_NAMES:
        if len(cpu_data[name]) > 0:
            plt.plot(list(cpu_data[name]), label=name, linewidth=2)
    plt.title('CPU Usage (%)', fontsize=14, fontweight='bold')
    plt.ylabel('CPU %', fontsize=12)
    plt.legend(loc='upper left', fontsize=10)
    plt.grid(True, alpha=0.3)
    plt.ylim(bottom=0)
    
    # 内存使用图表
    plt.subplot(2, 1, 2)
    for name in PROCESS_NAMES:
        if len(mem_data[name]) > 0:
            plt.plot(list(mem_data[name]), label=name, linewidth=2)
    plt.title('Memory Usage (MB)', fontsize=14, fontweight='bold')
    plt.ylabel('Memory (MB)', fontsize=12)
    plt.xlabel('Time', fontsize=12)
    plt.legend(loc='upper left', fontsize=10)
    plt.grid(True, alpha=0.3)
    plt.ylim(bottom=0)
    
    plt.tight_layout()
    
    # 打印当前状态
    print(f"\n[{current_time}] Process Stats:")
    for name in PROCESS_NAMES:
        print(f"  {name:30s} CPU: {stats[name]['cpu']:6.2f}%  MEM: {stats[name]['mem']:8.2f} MB")

def main():
    """主函数"""
    print("=" * 60)
    print("Process Monitor - Monitoring CPU and Memory Usage")
    print("=" * 60)
    print(f"Monitoring processes: {', '.join(PROCESS_NAMES)}")
    print("Press Ctrl+C to stop monitoring")
    print("=" * 60)
    
    # 检查进程是否存在
    processes = find_processes()
    found = [name for name in PROCESS_NAMES if name in processes]
    if not found:
        print("\nWarning: No target processes found!")
        print("Please make sure the processes are running.")
        sys.exit(1)
    
    print(f"\nFound processes: {', '.join(found)}")
    
    # 创建图表
    fig = plt.figure(figsize=(12, 8))
    fig.suptitle('Process Monitor - CPU and Memory Usage', fontsize=16, fontweight='bold')
    
    # 使用动画更新数据
    ani = animation.FuncAnimation(fig, update_data, interval=1000, blit=False)
    
    try:
        plt.show()
    except KeyboardInterrupt:
        print("\n\nMonitoring stopped by user.")
        sys.exit(0)

if __name__ == "__main__":
    main()
