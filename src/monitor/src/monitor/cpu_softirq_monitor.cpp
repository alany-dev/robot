#include "monitor/cpu_softirq_monitor.h"
#include "utils/read_file.h"
#include "utils/utils.h"
#include "monitor/CpuSoftIrq.h"

namespace monitor {
void CpuSoftIrqMonitor::UpdateOnce(monitor::MonitorInfo* monitor_info) {
  ReadFile softirqs_file(std::string("/proc/softirqs"));
  std::vector<std::string> one_softirq;
  std::vector<std::vector<std::string>> softirq;
  while (softirqs_file.ReadLine(&one_softirq)) {
    softirq.push_back(one_softirq);
    one_softirq.clear();
  }

  for (int i = 0; i < 8; i++) {
    std::string name = softirq[0][i];
    struct SoftIrq info;
    info.cpu_name = name;
    info.hi = std::stoll(softirq[1][i + 1]);
    info.timer = std::stoll(softirq[2][i + 1]);
    info.net_tx = std::stoll(softirq[3][i + 1]);
    info.net_rx = std::stoll(softirq[4][i + 1]);
    info.block = std::stoll(softirq[5][i + 1]);
    info.irq_poll = std::stoll(softirq[6][i + 1]);
    info.tasklet = std::stoll(softirq[7][i + 1]);
    info.sched = std::stoll(softirq[8][i + 1]);
    info.hrtimer = std::stoll(softirq[9][i + 1]);
    info.rcu = std::stoll(softirq[10][i + 1]);
    info.timepoint = std::chrono::steady_clock::now();

    auto iter = cpu_softirqs_.find(name);
    if (iter != cpu_softirqs_.end()) {
      struct SoftIrq& old = (*iter).second;
      double period = Utils::SteadyTimeSecond(info.timepoint, old.timepoint);
      monitor::CpuSoftIrq one_softirq_msg;
      one_softirq_msg.cpu_name = info.cpu_name;
      one_softirq_msg.hi = (info.hi - old.hi) / period;
      one_softirq_msg.timer = (info.timer - old.timer) / period;
      one_softirq_msg.net_tx = (info.net_tx - old.net_tx) / period;
      one_softirq_msg.net_rx = (info.net_rx - old.net_rx) / period;
      one_softirq_msg.block = (info.block - old.block) / period;
      one_softirq_msg.irq_poll = (info.irq_poll - old.irq_poll) / period;
      one_softirq_msg.tasklet = (info.tasklet - old.tasklet) / period;
      one_softirq_msg.sched = (info.sched - old.sched) / period;
      one_softirq_msg.hrtimer = (info.hrtimer - old.hrtimer) / period;
      one_softirq_msg.rcu = (info.rcu - old.rcu) / period;
      monitor_info->cpu_softirq.push_back(one_softirq_msg);
    }
    cpu_softirqs_[name] = info;
  }
  return;
}

}  // namespace monitor