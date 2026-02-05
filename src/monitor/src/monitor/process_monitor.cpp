#include "monitor/process_monitor.h"
#include "utils/read_file.h"
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <cstring>
#include <algorithm>
#include <climits>

namespace monitor {

ProcessMonitor::ProcessMonitor() 
  : system_total_memory_kb_(0),
    last_system_cpu_time_(std::chrono::steady_clock::now()),
    last_system_cpu_total_(0) {
  system_total_memory_kb_ = GetSystemTotalMemory();
}

ProcessMonitor::~ProcessMonitor() {
}

bool ProcessMonitor::RegisterNode(const std::string& node_name,
                                   const std::string& process_name,
                                   int32_t pid) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (registered_nodes_.find(node_name) != registered_nodes_.end()) {
      return false;
    }
  } 
  
  ProcessInfo proc_info;
  proc_info.node_name = node_name;
  proc_info.process_name = process_name;
  proc_info.valid = false;
  proc_info.cpu_percent = 0.0f;
  proc_info.cpu_time = 0.0f;
  proc_info.mem_mb = 0.0f;
  proc_info.mem_percent = 0.0f;
  proc_info.last_cpu_time = 0;
  proc_info.thread_count = 0;
  // 初始化磁盘I/O统计
  proc_info.disk_read_rate = 0.0f;
  proc_info.disk_write_rate = 0.0f;
  proc_info.last_disk_read_bytes = 0;
  proc_info.last_disk_write_bytes = 0;
  proc_info.last_disk_read_ops = 0;
  proc_info.last_disk_write_ops = 0;
  proc_info.disk_read_bytes = 0;
  proc_info.disk_write_bytes = 0;
  proc_info.disk_read_ops = 0;
  proc_info.disk_write_ops = 0;
  
  proc_info.pid = pid;
  if (FindProcessByPid(pid)) {
    proc_info.valid = true;
  }
  
  
  if (proc_info.valid) {
    proc_info.last_update = std::chrono::steady_clock::now();
    proc_info.last_cpu_time = ReadProcessCpuTime(proc_info.pid);
    ReadProcessMemory(proc_info.pid, proc_info.mem_mb, proc_info.mem_percent, proc_info.thread_count);
    ReadProcessDiskIO(proc_info.pid, proc_info.last_disk_read_bytes, proc_info.last_disk_write_bytes,
                     proc_info.last_disk_read_ops, proc_info.last_disk_write_ops);
  }
  
  {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (registered_nodes_.find(node_name) != registered_nodes_.end()) {
      return false;
    }
    
    if (proc_info.valid) {
      registered_nodes_[node_name] = proc_info;
      return true;
    }
  }
  
  return false;
}

bool ProcessMonitor::UnregisterNode(const std::string& node_name) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = registered_nodes_.find(node_name);
  if (it != registered_nodes_.end()) {
    registered_nodes_.erase(it);
    return true;
  }
  return false;
}

void ProcessMonitor::UpdateAll() {
  std::lock_guard<std::mutex> lock(mutex_);
  
  auto now = std::chrono::steady_clock::now();
  
  for (auto& pair : registered_nodes_) {
    ProcessInfo& proc_info = pair.second;
    
    // 检查进程是否仍然存在
    if (!FindProcessByPid(proc_info.pid)) {
      proc_info.valid = false;
      continue;
    }
    
    proc_info.valid = true;
    UpdateProcess(proc_info);
    proc_info.last_update = now;
  }
}

bool ProcessMonitor::GetNodeInfo(const std::string& node_name, 
                                 monitor::NodeMonitorInfo& info) {
  std::lock_guard<std::mutex> lock(mutex_);
  
  auto it = registered_nodes_.find(node_name);
  if (it == registered_nodes_.end() || !it->second.valid) {
    return false;
  }
  
  const ProcessInfo& proc_info = it->second;
  info.node_name = proc_info.node_name;
  info.process_name = proc_info.process_name;
  info.pid = proc_info.pid;
  info.cpu_percent = proc_info.cpu_percent;
  info.cpu_time = proc_info.cpu_time;
  info.mem_mb = proc_info.mem_mb;
  info.mem_percent = proc_info.mem_percent;
  info.thread_count = proc_info.thread_count;
  // 磁盘I/O统计
  info.disk_read_rate = proc_info.disk_read_rate;
  info.disk_write_rate = proc_info.disk_write_rate;
  info.disk_read_bytes = proc_info.disk_read_bytes;
  info.disk_write_bytes = proc_info.disk_write_bytes;
  info.disk_read_ops = proc_info.disk_read_ops;
  info.disk_write_ops = proc_info.disk_write_ops;
  
  return true;
}

void ProcessMonitor::GetAllNodeInfo(std::vector<monitor::NodeMonitorInfo>& infos) {
  std::lock_guard<std::mutex> lock(mutex_);
  
  infos.clear();
  for (const auto& pair : registered_nodes_) {
    if (pair.second.valid) {
      monitor::NodeMonitorInfo info;
      const ProcessInfo& proc_info = pair.second;
      info.node_name = proc_info.node_name;
      info.process_name = proc_info.process_name;
      info.pid = proc_info.pid;
      info.cpu_percent = proc_info.cpu_percent;
      info.cpu_time = proc_info.cpu_time;
      info.mem_mb = proc_info.mem_mb;
      info.mem_percent = proc_info.mem_percent;
      info.thread_count = proc_info.thread_count;
      // 磁盘I/O统计
      info.disk_read_rate = proc_info.disk_read_rate;
      info.disk_write_rate = proc_info.disk_write_rate;
      info.disk_read_bytes = proc_info.disk_read_bytes;
      info.disk_write_bytes = proc_info.disk_write_bytes;
      info.disk_read_ops = proc_info.disk_read_ops;
      info.disk_write_ops = proc_info.disk_write_ops;
      infos.push_back(info);
    }
  }
}

bool ProcessMonitor::IsRegistered(const std::string& node_name) {
  std::lock_guard<std::mutex> lock(mutex_);
  return registered_nodes_.find(node_name) != registered_nodes_.end();
}

bool ProcessMonitor::FindProcessByPid(int32_t pid) {
  std::string pid_path = "/proc/" + std::to_string(pid);
  struct stat st;
  return (stat(pid_path.c_str(), &st) == 0 && S_ISDIR(st.st_mode));
}

void ProcessMonitor::UpdateProcess(ProcessInfo& proc_info) {
  uint64_t current_cpu_time = ReadProcessCpuTime(proc_info.pid);
  auto now = std::chrono::steady_clock::now();
  auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      now - proc_info.last_update).count();
  
  long sys_clk_tck = sysconf(_SC_CLK_TCK);
  if (sys_clk_tck <= 0) {
    sys_clk_tck = 100;  
  }
  
  if (duration_ms > 0 && proc_info.last_cpu_time > 0) {
    uint64_t cpu_diff = current_cpu_time - proc_info.last_cpu_time;
    float duration_sec = duration_ms / 1000.0f;
    proc_info.cpu_percent = ((cpu_diff / (float)sys_clk_tck) / duration_sec) * 100.0f;
    
    if (proc_info.cpu_percent < 0.0f) {
      proc_info.cpu_percent = 0.0f;
    }
  
  }
  proc_info.last_cpu_time = current_cpu_time;
  proc_info.cpu_time = current_cpu_time / (float)sys_clk_tck;
  
  ReadProcessMemory(proc_info.pid, proc_info.mem_mb, proc_info.mem_percent, proc_info.thread_count);
  
  uint64_t current_read_bytes, current_write_bytes;
  uint64_t current_read_ops, current_write_ops;
  if (ReadProcessDiskIO(proc_info.pid, current_read_bytes, current_write_bytes,
                        current_read_ops, current_write_ops)) {
    proc_info.disk_read_bytes = current_read_bytes;
    proc_info.disk_write_bytes = current_write_bytes;
    proc_info.disk_read_ops = current_read_ops;
    proc_info.disk_write_ops = current_write_ops;
    
    if (duration_ms > 0) {
      float duration_sec = duration_ms / 1000.0f;
      if (proc_info.last_disk_read_bytes > 0) {
        proc_info.disk_read_rate = (current_read_bytes - proc_info.last_disk_read_bytes) / 1024.0f / duration_sec;
      }
      if (proc_info.last_disk_write_bytes > 0) {
        proc_info.disk_write_rate = (current_write_bytes - proc_info.last_disk_write_bytes) / 1024.0f / duration_sec;
      }
    }
    proc_info.last_disk_read_bytes = current_read_bytes;
    proc_info.last_disk_write_bytes = current_write_bytes;
    proc_info.last_disk_read_ops = current_read_ops;
    proc_info.last_disk_write_ops = current_write_ops;
  }
}

uint64_t ProcessMonitor::ReadProcessCpuTime(int32_t pid) {
  std::string stat_path = "/proc/" + std::to_string(pid) + "/stat";
  ReadFile stat_file(stat_path);
  
  std::vector<std::string> tokens;
  if (!stat_file.ReadLine(&tokens)) {
    return 0;
  }
  
  if (tokens.size() >= 17) {
    uint64_t utime = std::stoull(tokens[13]);
    uint64_t stime = std::stoull(tokens[14]);
    return utime + stime;
  }
  
  return 0;
}

bool ProcessMonitor::ReadProcessMemory(int32_t pid, float& mem_mb, 
                                        float& mem_percent, int32_t& thread_count) {
  std::string status_path = "/proc/" + std::to_string(pid) + "/status";
  ReadFile status_file(status_path);
  
  std::vector<std::string> status_datas;
  uint64_t vm_rss_kb = 0;
  bool found_vmrss = false;
  bool found_threads = false;
  
  while (status_file.ReadLine(&status_datas)) {
    if (status_datas.empty()) {
      continue;
    }
    
    if (status_datas[0] == "VmRSS:") {
      if (status_datas.size() >= 2) {
        vm_rss_kb = std::stoull(status_datas[1]);
        found_vmrss = true;
      }
    } else if (status_datas[0] == "Threads:") {
      if (status_datas.size() >= 2) {
        thread_count = std::stoi(status_datas[1]);
        found_threads = true;
      }
    }
    
    if (found_vmrss && found_threads) {
      break;
    }
    
    status_datas.clear();
  }
  
  if (!found_vmrss) {
    return false;
  }
  
  mem_mb = vm_rss_kb / 1024.0f;
  mem_percent = (vm_rss_kb * 100.0f) / system_total_memory_kb_;
  
  return true;
}

bool ProcessMonitor::ReadProcessDiskIO(int32_t pid, uint64_t& read_bytes, uint64_t& write_bytes,
                                       uint64_t& read_ops, uint64_t& write_ops) {
  std::string io_path = "/proc/" + std::to_string(pid) + "/io";
  ReadFile io_file(io_path);
  
  read_bytes = 0;
  write_bytes = 0;
  read_ops = 0;
  write_ops = 0;
  
  std::vector<std::string> io_datas;
  bool found_read_bytes = false;
  bool found_write_bytes = false;
  bool found_read_ops = false;
  bool found_write_ops = false;
  bool file_readable = false; 
  
  while (io_file.ReadLine(&io_datas)) {
    file_readable = true;  
    
    if (io_datas.empty()) {
      continue;
    }
     
    if (io_datas[0] == "read_bytes:") {
      if (io_datas.size() >= 2) {
        read_bytes = std::stoull(io_datas[1]);
        found_read_bytes = true;
      }
    } else if (io_datas[0] == "write_bytes:") {
      if (io_datas.size() >= 2) {
        write_bytes = std::stoull(io_datas[1]);
        found_write_bytes = true;
      }
    } else if (io_datas[0] == "syscr:") {
      // syscr = system read calls (读取操作次数)
      if (io_datas.size() >= 2) {
        read_ops = std::stoull(io_datas[1]);
        found_read_ops = true;
      }
    } else if (io_datas[0] == "syscw:") {
      // syscw = system write calls (写入操作次数)
      if (io_datas.size() >= 2) {
        write_ops = std::stoull(io_datas[1]);
        found_write_ops = true;
      }
    }
    
    if (found_read_bytes && found_write_bytes && found_read_ops && found_write_ops) {
      break;
    }
    
    io_datas.clear();
  }
  
  return file_readable;
}

uint64_t ProcessMonitor::GetSystemTotalMemory() {
  ReadFile meminfo_file("/proc/meminfo");
  
  std::vector<std::string> mem_datas;
  while (meminfo_file.ReadLine(&mem_datas)) {
    if (mem_datas.empty()) {
      continue;
    }
    
    if (mem_datas[0] == "MemTotal:") {
      if (mem_datas.size() >= 2) {
        return std::stoull(mem_datas[1]);
      }
    }
    
    mem_datas.clear();
  }
  
  return 0;
}

}  // namespace monitor
