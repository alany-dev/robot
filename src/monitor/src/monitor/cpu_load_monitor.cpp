#include "monitor/cpu_load_monitor.h"
#include "utils/read_file.h"

namespace monitor {
void CpuLoadMonitor::UpdateOnce(monitor::MonitorInfo* monitor_info) {
  ReadFile cpu_load_file(std::string("/proc/loadavg"));
  std::vector<std::string> cpu_load;
  cpu_load_file.ReadLine(&cpu_load);
  load_avg_1_ = std::stof(cpu_load[0]);
  load_avg_3_ = std::stof(cpu_load[1]);
  load_avg_15_ = std::stof(cpu_load[2]);

  monitor_info->cpu_load.load_avg_1 = load_avg_1_;
  monitor_info->cpu_load.load_avg_3 = load_avg_3_;
  monitor_info->cpu_load.load_avg_15 = load_avg_15_;

  return;
}

}  // namespace monitor