#pragma once

#include <string>
#include "monitor/monitor_inter.h"
#include "monitor/CpuLoad.h"

namespace monitor {
class CpuLoadMonitor : public MonitorInter {
 public:
  CpuLoadMonitor() {}
  void UpdateOnce(monitor::MonitorInfo* monitor_info) override;
  void Stop() override {}

 private:
  float load_avg_1_;
  float load_avg_3_;
  float load_avg_15_;
};

}  // namespace monitor
