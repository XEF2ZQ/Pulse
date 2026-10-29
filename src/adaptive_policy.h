#pragma once
#include "policy.h"
#include "scheduler_topology.h"
#include "workload.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace pulse::adaptive {
enum class Profile : unsigned { Off, Adaptive, StrictFour };
enum class Background : unsigned { Off, Conservative, Aggressive };
enum class Override : unsigned { Automatic, All, Four, Untouched };
enum class Role {
  Unknown,
  Interactive,
  Background,
  Compute,
  Critical,
  Protected
};
enum class Action { Restore, Four, Background };
inline bool batteryEligible(bool known, bool windowsBattery, bool weakCharger,
                            bool blocked = false) {
  return known && windowsBattery && !weakCharger && !blocked;
}
struct Selection {
  std::vector<ULONG> four, efficient, background;
  unsigned physical = 0, domains = 0;
  uint64_t fingerprint = 0;
  std::wstring reason = L"No verified efficient-four topology";
  bool ready() const { return physical == 4 && !four.empty(); }
};
// Uses GLPI physical cores and verified cache domains, never numerical LP
// indices.
inline Selection select(const scheduler::Topology &t) {
  Selection out;
  out.fingerprint = t.fingerprint;
  if (!t.valid() || !t.capabilities.hasCpuSets || !t.capabilities.heterogeneous)
    return out;
  struct Core {
    WORD group;
    unsigned id, cache, efficiency;
    std::vector<ULONG> ids;
    bool usable = true;
  };
  std::map<unsigned, Core> cores;
  unsigned low = UINT_MAX;
  for (const auto &c : t.cpus) {
    if (!c.efficiency || !c.cpuSetId || !c.llc)
      return out;
    auto [it, inserted] = cores.try_emplace(
        c.core, Core{c.key.group, c.core, *c.llc, *c.efficiency, {}});
    auto &core = it->second;
    if (!inserted && (core.group != c.key.group || core.cache != *c.llc ||
                      core.efficiency != *c.efficiency))
      return out;
    core.usable &=
        (c.cpuSetFlags & (2u | 8u)) == 0; // Allocated or real-time reserved.
    core.ids.push_back(*c.cpuSetId);
    low = std::min(low, *c.efficiency);
  }
  std::map<std::pair<WORD, unsigned>, std::vector<Core>> islands;
  for (auto &[id, c] : cores)
    if (c.usable && c.efficiency == low) {
      std::sort(c.ids.begin(), c.ids.end());
      islands[{c.group, c.cache}].push_back(c);
      out.efficient.insert(out.efficient.end(), c.ids.begin(), c.ids.end());
    }
  std::vector<std::vector<Core>> ranked;
  for (auto &[key, group] : islands) {
    std::sort(group.begin(), group.end(),
              [](const auto &a, const auto &b) { return a.ids < b.ids; });
    ranked.push_back(group);
  }
  std::sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) {
    if (a.size() != b.size())
      return a.size() > b.size();
    return a.front().ids < b.front().ids;
  });
  // Fail open instead of crossing a processor group or inventing cache
  // locality.
  for (const auto &island : ranked)
    if (island.size() >= 4) {
      for (size_t i = 0; i < 4; ++i)
        out.four.insert(out.four.end(), island[i].ids.begin(),
                        island[i].ids.end());
      out.physical = 4;
      out.domains = 1;
      break;
    }
  if (!out.ready()) {
    out.reason = L"Fewer than four usable efficient physical cores in one LLC; "
                 L"unrestricted";
    return out;
  }
  std::sort(out.four.begin(), out.four.end());
  std::sort(out.efficient.begin(), out.efficient.end());
  out.background = out.four;
  out.reason = L"Four physical cores in lowest OS efficiency class / one "
               L"verified LLC; LP-E identity unknown";
  return out;
}
struct Evidence {
  Role role = Role::Unknown;
  Override rule = Override::Automatic;
  bool safe = false, foreground = false, burst = false, compute = false,
       criticalPath = false;
  bool eligible = false;
};
inline Action arbitrate(Profile profile, Background background,
                        const Evidence &e) {
  if (!e.eligible || !e.safe || e.role == Role::Protected ||
      e.role == Role::Critical || e.rule == Override::Untouched ||
      e.rule == Override::All || e.compute || e.role == Role::Compute ||
      e.criticalPath)
    return Action::Restore;
  if (profile == Profile::Off && background == Background::Off)
    return Action::Restore;
  if (e.foreground && e.burst && profile != Profile::StrictFour)
    return Action::Restore;
  if (e.rule == Override::Four && profile != Profile::Off)
    return Action::Four;
  if (e.role == Role::Interactive && profile != Profile::Off)
    return Action::Four;
  if (e.role == Role::Background && !e.foreground &&
      background != Background::Off)
    return Action::Background;
  return Action::Restore;
}
// CPU occupancy is relative to one logical processor, NOT an IPC/capacity
// estimate. Unknown intervals fail open. Existing 80%/500ms demand threshold is
// the baseline.
class Demand {
  uint64_t high_ = 0, quiet_ = 0, stamp_ = 0;

public:
  bool compute = false, pending = false;
  void update(uint64_t now, double occupancy, bool valid, bool hint = false) {
    if (!valid || !std::isfinite(occupancy) || occupancy < 0 ||
        (stamp_ && (now <= stamp_ || now - stamp_ > 6000))) {
      compute = true;
      pending = false;
      high_ = quiet_ = 0;
      stamp_ = now;
      return;
    }
    stamp_ = now;
    const double x = std::min(occupancy, 6400.0);
    // EWMA/CUSUM candidates run only in tools/replay_adaptive.py. Keep the
    // resident baseline free of unused smoothing work and telemetry
    // allocations.
    if (x >= (hint ? 65.0 : 80.0)) {
      quiet_ = 0;
      if (!high_)
        high_ = now;
      pending = true;
      if (now - high_ >= (hint ? 250u : 500u))
        compute = true;
    } else {
      high_ = 0;
      pending = false;
      if (x < 20.0) {
        if (!quiet_)
          quiet_ = now;
        if (now - quiet_ >= 2000)
          compute = false;
      } else
        quiet_ = 0;
    }
  }
};
inline PowerState tier(bool adaptive, bool burst, bool compute) {
  return !adaptive ? PowerState::Efficiency
         : compute ? PowerState::Compute
         : burst   ? PowerState::Burst
                   : PowerState::Efficiency;
}
} // namespace pulse::adaptive
