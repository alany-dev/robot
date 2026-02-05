#pragma once

#include <string>
#include "monitor/MonitorInfo.h"

namespace monitor {
class MonitorInter {
 public:
  MonitorInter() {}
  virtual ~MonitorInter() {}
  virtual void UpdateOnce(monitor::MonitorInfo* monitor_info) = 0;
  virtual void Stop() = 0;
};
}  // namespace monitor