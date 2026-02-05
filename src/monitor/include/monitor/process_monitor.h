#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <mutex>
#include <fstream>
#include <sstream>
#include <chrono>
#include "monitor/NodeMonitorInfo.h"

namespace monitor {

struct ProcessInfo {
  int32_t pid;
  std::string process_name;
  std::string node_name;
  std::chrono::steady_clock::time_point last_update;
  
  // CPU 统计
  uint64_t last_cpu_time;
  float cpu_percent;
  float cpu_time;
  
  // 内存统计
  float mem_mb;
  float mem_percent;
  int32_t thread_count;
  
  // 磁盘I/O统计
  uint64_t last_disk_read_bytes;
  uint64_t last_disk_write_bytes;
  uint64_t last_disk_read_ops;
  uint64_t last_disk_write_ops;
  float disk_read_rate;
  float disk_write_rate;
  uint64_t disk_read_bytes;
  uint64_t disk_write_bytes;
  uint64_t disk_read_ops;
  uint64_t disk_write_ops;
  
  bool valid;
};

class ProcessMonitor {
 public:
  ProcessMonitor();
  ~ProcessMonitor();
  
  bool RegisterNode(const std::string& node_name, 
                    const std::string& process_name, 
                    int32_t pid = -1);
  
  bool UnregisterNode(const std::string& node_name);
  
  void UpdateAll();
  
  bool GetNodeInfo(const std::string& node_name, monitor::NodeMonitorInfo& info);
  
  void GetAllNodeInfo(std::vector<monitor::NodeMonitorInfo>& infos);
  
  bool IsRegistered(const std::string& node_name);
  
 private:
  
  bool FindProcessByPid(int32_t pid);
  
  void UpdateProcess(ProcessInfo& proc_info);
  
  uint64_t ReadProcessCpuTime(int32_t pid);
  
  bool ReadProcessMemory(int32_t pid, float& mem_mb, float& mem_percent, int32_t& thread_count);
  
  bool ReadProcessDiskIO(int32_t pid, uint64_t& read_bytes, uint64_t& write_bytes,
                         uint64_t& read_ops, uint64_t& write_ops);
  
  uint64_t GetSystemTotalMemory();
  
  std::mutex mutex_;
  //注册表
  std::unordered_map<std::string, ProcessInfo> registered_nodes_;
  uint64_t system_total_memory_kb_;
  std::chrono::steady_clock::time_point last_system_cpu_time_;
  uint64_t last_system_cpu_total_;
};

}  // namespace monitor
