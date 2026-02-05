#pragma once

#include <unistd.h>
#include <string>
#include <fstream>

namespace monitor {

class MonitorHelper {
 public:

  static int32_t GetCurrentPid() {
    return static_cast<int32_t>(getpid());
  }
  

  static std::string GetCurrentProcessName() {
    std::string proc_name;
    std::string pid_path = "/proc/" + std::to_string(getpid()) + "/comm";
    std::ifstream comm_file(pid_path);
    if (comm_file.is_open()) {
      std::getline(comm_file, proc_name);
    }
    return proc_name;
  }
  

  static bool AutoRegister(const std::string& node_name, 
                          const std::string& service_name = "/monitor/register_node");
  
  static bool AutoUnregister(const std::string& node_name,
                              const std::string& service_name = "/monitor/unregister_node");
};

}  // namespace monitor
