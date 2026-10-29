#include "adaptive_config.h"
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace pulse;
using namespace pulse::adaptive;
static unsigned checks = 0;
#define CHECK(x)                                                               \
  do {                                                                         \
    ++checks;                                                                  \
    if (!(x))                                                                  \
      throw std::runtime_error(#x);                                            \
  } while (false)
static scheduler::Topology fixture(unsigned efficient, unsigned performance,
                                   unsigned smt = 2) {
  scheduler::Topology t;
  t.capabilities.topologyValid = true;
  t.capabilities.hasCpuSets = true;
  t.capabilities.heterogeneous = performance != 0;
  for (unsigned core = 0; core < efficient + performance; ++core)
    for (unsigned s = 0; s < smt; ++s) {
      scheduler::LogicalCpu c;
      c.core = core;
      c.key = {0, BYTE(core * smt + s)};
      c.cpuSetId = 256 + core * smt + s;
      c.efficiency = core < efficient ? 0 : 1;
      c.llc = core < efficient ? 0 : 1;
      t.cpus.push_back(c);
    }
  return t;
}
int main() {
  try {
    auto t = fixture(8, 4);
    auto selected = select(t);
    CHECK(selected.ready());
    CHECK(selected.four.size() == 8);
    CHECK(selected.physical == 4);
    CHECK(selected.domains == 1);
    std::reverse(t.cpus.begin(), t.cpus.end());
    CHECK(select(t).four == selected.four);
    for (unsigned smt : {1u, 2u, 4u}) {
      auto c = select(fixture(8, 4, smt));
      CHECK(c.ready());
      CHECK(c.four.size() == 4 * smt);
    }
    CHECK(!select(fixture(3, 4)).ready());
    CHECK(!select(fixture(8, 0)).ready());
    t = fixture(8, 4);
    t.capabilities.topologyValid = false;
    CHECK(!select(t).ready());
    t = fixture(8, 4);
    t.cpus[0].cpuSetId.reset();
    CHECK(!select(t).ready());
    t = fixture(8, 4);
    t.cpus[0].efficiency = 1;
    CHECK(!select(t).ready());
    t = fixture(4, 4);
    t.cpus[0].cpuSetFlags = 2;
    CHECK(!select(t).ready());
    t = fixture(4, 4);
    t.cpus[0].cpuSetFlags = 8;
    CHECK(!select(t).ready());
    t = fixture(4, 4);
    t.cpus[0].cpuSetFlags = 1;
    CHECK(select(t).ready()); // Parked is not unavailable.
    t = fixture(8, 4);
    for (auto &c : t.cpus)
      c.key.group = 1;
    CHECK(select(t).ready());
    t = fixture(6, 4);
    for (auto &c : t.cpus)
      if (c.core < 6)
        c.llc = c.core / 3;
    CHECK(!select(t).ready());
    t = fixture(12, 4);
    for (auto &c : t.cpus)
      if (c.core < 12)
        c.llc = c.core < 4 ? 3 : 4;
    CHECK(select(t).four.front() == 264);
    Evidence e{Role::Interactive,
               Override::Automatic,
               true,
               false,
               false,
               false,
               false,
               true};
    CHECK(arbitrate(Profile::Adaptive, Background::Off, e) == Action::Four);
    e.foreground = e.burst = true;
    CHECK(arbitrate(Profile::Adaptive, Background::Off, e) == Action::Restore);
    CHECK(arbitrate(Profile::StrictFour, Background::Off, e) == Action::Four);
    e.compute = true;
    CHECK(arbitrate(Profile::StrictFour, Background::Off, e) ==
          Action::Restore);
    e.compute = false;
    e.foreground = e.burst = false;
    for (auto role :
         {Role::Unknown, Role::Protected, Role::Critical, Role::Compute}) {
      e.role = role;
      CHECK(arbitrate(Profile::Adaptive, Background::Conservative, e) ==
            Action::Restore);
    }
    e.role = Role::Background;
    CHECK(arbitrate(Profile::Off, Background::Conservative, e) ==
          Action::Background);
    e.criticalPath = true;
    CHECK(arbitrate(Profile::Adaptive, Background::Aggressive, e) ==
          Action::Restore);
    for (auto rule : {Override::All, Override::Untouched}) {
      e = {Role::Interactive, rule, true, false, false, false, false, true};
      CHECK(arbitrate(Profile::Adaptive, Background::Aggressive, e) ==
            Action::Restore);
    }
    e = {Role::Protected, Override::Four, true,  false,
         false,           false,          false, true};
    CHECK(arbitrate(Profile::StrictFour, Background::Aggressive, e) ==
          Action::Restore);
    for (unsigned interval : {50u, 100u, 150u, 250u, 500u, 1000u}) {
      Demand d;
      for (uint64_t now = 100; now < 3000; now += interval)
        d.update(now, 0, true);
      CHECK(!d.compute);
      for (uint64_t now = 3000; now < 6000; now += interval)
        d.update(now, 95, true);
      CHECK(d.compute);
      for (uint64_t now = 6000; now < 10000; now += interval)
        d.update(now, 0, true);
      CHECK(!d.compute);
      d.update(10100, std::numeric_limits<double>::quiet_NaN(), true);
      CHECK(d.compute); // Fail-open restriction rescue.
    }
    Demand d;
    d.update(100, 95, true);
    d.update(100, 95, true);
    CHECK(d.compute);
    d = {};
    d.update(100, 95, true);
    d.update(8000, 0, true);
    CHECK(d.compute);
    for (unsigned bits = 0; bits < 16; ++bits) {
      const bool known = (bits & 1) != 0, battery = (bits & 2) != 0;
      const bool weak = (bits & 4) != 0, blocked = (bits & 8) != 0;
      CHECK(batteryEligible(known, battery, weak, blocked) == (bits == 3));
    }
    CHECK(tier(false, true, true) == PowerState::Efficiency);
    CHECK(tier(true, true, true) == PowerState::Compute);
    CHECK(tier(true, true, false) == PowerState::Burst);
    CHECK(tier(true, false, false) == PowerState::Efficiency);
    std::map<std::wstring, Override> rules;
    CHECK(parseRules(L"C:\\Apps\\tool.exe|2\r\nD:\\tool.exe|3", rules));
    CHECK(rules.size() == 2);
    for (auto bad : {L"tool.exe|2", L"C:\\tool.exe|4", L"C:\\*.exe|2",
                     L"C:\\a|2\nC:\\a|1", L"C:\\a|2\nwrong", L"C:\\a;exe|2"}) {
      CHECK(!parseRules(bad, rules));
      CHECK(rules.size() == 2);
    }
    const auto path = std::wstring(L"adaptive-test-") +
                      std::to_wstring(GetCurrentProcessId()) + L".ini";
    wchar_t full[MAX_PATH]{};
    GetFullPathNameW(path.c_str(), MAX_PATH, full, nullptr);
    CHECK(WritePrivateProfileStringW(L"Unrelated", L"Keep", L"yes", full));
    Config config;
    config.profile = Profile::StrictFour;
    config.background = Background::Conservative;
    config.rules = rules;
    config.tiers = false;
    CHECK(save(full, config));
    auto loaded = load(full);
    CHECK(loaded.profile == config.profile && loaded.rules == config.rules &&
          !loaded.tiers);
    wchar_t kept[16]{};
    GetPrivateProfileStringW(L"Unrelated", L"Keep", L"", kept, 16, full);
    CHECK(std::wstring(kept) == L"yes");
    CHECK(DeleteFileW(full));
    std::cout << checks << " adaptive checks passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << " after " << checks << " checks\n";
    return 1;
  }
}
