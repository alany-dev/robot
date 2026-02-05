#include "monitor/mem_monitor.h"
#include "utils/read_file.h"
#include "monitor/MemInfo.h"

namespace monitor {
static constexpr float KBToGB = 1000 * 1000;

void MemMonitor::UpdateOnce(monitor::MonitorInfo* monitor_info) {
  ReadFile mem_file("/proc/meminfo");
  struct MenInfo mem_info;
  std::vector<std::string> mem_datas;
  while (mem_file.ReadLine(&mem_datas)) {
    if (mem_datas[0] == "MemTotal:") {
      mem_info.total = std::stoll(mem_datas[1]);
    } else if (mem_datas[0] == "MemFree:") {
      mem_info.free = std::stoll(mem_datas[1]);
    } else if (mem_datas[0] == "MemAvailable:") {
      mem_info.avail = std::stoll(mem_datas[1]);
    } else if (mem_datas[0] == "Buffers:") {
      mem_info.buffers = std::stoll(mem_datas[1]);
    } else if (mem_datas[0] == "Cached:") {
      mem_info.cached = std::stoll(mem_datas[1]);
    } else if (mem_datas[0] == "SwapCached:") {
      mem_info.swap_cached = std::stoll(mem_datas[1]);
    } else if (mem_datas[0] == "Active:") {
      mem_info.active = std::stoll(mem_datas[1]);
    } else if (mem_datas[0] == "Inactive:") {
      mem_info.in_active = std::stoll(mem_datas[1]);
    } else if (mem_datas[0] == "Active(anon):") {
      mem_info.active_anon = std::stoll(mem_datas[1]);
    } else if (mem_datas[0] == "Inactive(anon):") {
      mem_info.inactive_anon = std::stoll(mem_datas[1]);
    } else if (mem_datas[0] == "Active(file):") {
      mem_info.active_file = std::stoll(mem_datas[1]);
    } else if (mem_datas[0] == "Inactive(file):") {
      mem_info.inactive_file = std::stoll(mem_datas[1]);
    } else if (mem_datas[0] == "Dirty:") {
      mem_info.dirty = std::stoll(mem_datas[1]);
    } else if (mem_datas[0] == "Writeback:") {
      mem_info.writeback = std::stoll(mem_datas[1]);
    } else if (mem_datas[0] == "AnonPages:") {
      mem_info.anon_pages = std::stoll(mem_datas[1]);
    } else if (mem_datas[0] == "Mapped:") {
      mem_info.mapped = std::stoll(mem_datas[1]);
    } else if (mem_datas[0] == "KReclaimable:") {
      mem_info.kReclaimable = std::stoll(mem_datas[1]);
    } else if (mem_datas[0] == "SReclaimable:") {
      mem_info.sReclaimable = std::stoll(mem_datas[1]);
    } else if (mem_datas[0] == "SUnreclaim:") {
      mem_info.sUnreclaim = std::stoll(mem_datas[1]);
    }
    mem_datas.clear();
  }

  monitor_info->mem_info.used_percent = (mem_info.total - mem_info.avail) * 1.0 /
                                        mem_info.total * 100.0;
  monitor_info->mem_info.total = mem_info.total / KBToGB;
  monitor_info->mem_info.free = mem_info.free / KBToGB;
  monitor_info->mem_info.avail = mem_info.avail / KBToGB;
  monitor_info->mem_info.buffers = mem_info.buffers / KBToGB;
  monitor_info->mem_info.cached = mem_info.cached / KBToGB;
  monitor_info->mem_info.swap_cached = mem_info.swap_cached / KBToGB;
  monitor_info->mem_info.active = mem_info.active / KBToGB;
  monitor_info->mem_info.inactive = mem_info.in_active / KBToGB;
  monitor_info->mem_info.active_anon = mem_info.active_anon / KBToGB;
  monitor_info->mem_info.inactive_anon = mem_info.inactive_anon / KBToGB;
  monitor_info->mem_info.active_file = mem_info.active_file / KBToGB;
  monitor_info->mem_info.inactive_file = mem_info.inactive_file / KBToGB;
  monitor_info->mem_info.dirty = mem_info.dirty / KBToGB;
  monitor_info->mem_info.writeback = mem_info.writeback / KBToGB;
  monitor_info->mem_info.anon_pages = mem_info.anon_pages / KBToGB;
  monitor_info->mem_info.mapped = mem_info.mapped / KBToGB;
  monitor_info->mem_info.kreclaimable = mem_info.kReclaimable / KBToGB;
  monitor_info->mem_info.sreclaimable = mem_info.sReclaimable / KBToGB;
  monitor_info->mem_info.sunreclaim = mem_info.sUnreclaim / KBToGB;

  return;
}
}  // namespace monitor
