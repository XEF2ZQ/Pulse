#include "cpu_options.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>

static void check(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
int main() {
  using namespace pulse;
  wchar_t folder[MAX_PATH]{};
  if (!GetTempPathW(MAX_PATH, folder))
    return 2;
  const auto path = std::wstring(folder) + L"PulseCpuOptionsTest-" +
                    std::to_wstring(GetCurrentProcessId()) + L".ini";
  struct Cleanup {
    std::wstring path;
    ~Cleanup() {
      DeleteFileW(path.c_str());
      DeleteFileW((path + L".cpu.tmp").c_str());
    }
  } cleanup{path};
  try {
    cpu::Options o;
    check(o.valid() && !o.epp && !o.idleLimit && !o.idleThresholds &&
              !o.disableBoostAc && !o.disableBoostDc,
          "all options default off");
    for (unsigned i = 0; i <= 14; ++i)
      check(!o.manages(i), "off owns no extra settings");
    for (bool ac : {true, false}) {
      check(o.boost(PowerState::Efficiency, ac) == 0,
            "efficiency disables boost");
      check(o.boost(PowerState::Burst, ac) == 2, "default burst unchanged");
      check(o.boost(PowerState::Compute, ac) == 4, "default compute unchanged");
    }
    o.disableBoostDc = true;
    check(o.boost(PowerState::Burst, false) == 0 &&
              o.boost(PowerState::Compute, false) == 0,
          "DC cap wins over classifier");
    check(o.boost(PowerState::Burst, true) == 2,
          "DC preference does not affect AC");
    o.disableBoostAc = true;
    check(o.boost(PowerState::Compute, true) == 0, "AC cap wins");
    o.epp = true;
    o.idleThresholds = true;
    o.idleLimit = true;
    o.acEpp = 33;
    o.dcEpp = 77;
    o.acDemote = 15;
    o.dcDemote = 35;
    o.acPromote = 90;
    o.dcPromote = 70;
    o.acLimit = 1;
    o.dcLimit = 3;
    for (unsigned i = 8; i <= 12; ++i)
      check(o.manages(i), "enabled setting ownership");
    check(!o.manages(7) && !o.manages(13), "bounded settings");
    check(o.value(8, true) == 33 && o.value(9, false) == 77 &&
              o.value(10, true) == 15 && o.value(11, false) == 70 &&
              o.value(12, false) == 3,
          "source mapping");
    WritePrivateProfileStringW(L"Integration", L"Keep", L"unchanged",
                               path.c_str());
    check(cpu::save(path, o) && cpu::load(path) == o,
          "atomic preferences round trip");
    wchar_t keep[32]{};
    GetPrivateProfileStringW(L"Integration", L"Keep", L"", keep, 32,
                             path.c_str());
    check(std::wstring(keep) == L"unchanged", "preserve unrelated preferences");
    auto invalid = o;
    invalid.acLimit = 4;
    check(!invalid.valid() && !cpu::save(path, invalid),
          "reject unknown idle index");
    invalid = o;
    invalid.dcEpp = 101;
    check(!invalid.valid() && !cpu::save(path, invalid), "reject EPP overflow");
    invalid = o;
    invalid.dcDemote = 101;
    check(!invalid.valid(), "reject threshold overflow");
    check(cpu::load(path) == o,
          "invalid save leaves previous preferences intact");
    WritePrivateProfileStringW(L"CpuOptions", L"EppAC", L"-1", path.c_str());
    check(cpu::load(path) == cpu::Options{},
          "invalid configuration fails to inactive defaults");
    WritePrivateProfileStringW(L"CpuOptions", L"EppAC", L"abc", path.c_str());
    check(cpu::load(path) == cpu::Options{},
          "malformed EPP is not performance zero");
    check(cpu::save(path, o), "reset test preferences");
    WritePrivateProfileStringW(L"CpuOptions", L"CustomEpp", L"2", path.c_str());
    check(cpu::load(path) == cpu::Options{},
          "invalid boolean disables the option set");
    cpu::Capability c;
    check(!c.knownVendor() && !c.registerWriter,
          "unknown hardware grants no access");
    c.amd = true;
    c.cppcMsr = true;
    check(c.knownVendor() && !c.registerWriter,
          "CPUID support does not grant a register writer");
    std::cout << "CPU options policy and persistence checks passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
