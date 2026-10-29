#pragma once
#include "profile_config.h"
#include "workload.h"
#include "idle_policy.h"
#include <cstring>
#include <intrin.h>

namespace pulse::cpu {
struct Options {
  bool epp = false, disableBoostAc = false, disableBoostDc = false;
  unsigned acEpp = 50, dcEpp = 80;
  bool idleThresholds = false, idleLimit = false;
  bool earlyIdle = false;
  unsigned idleUs = 1000;
  unsigned acDemote = 12, dcDemote = 40, acPromote = 100, dcPromote = 60;
  unsigned acLimit = 0, dcLimit = 0;
  bool valid() const {
    return acEpp <= 100 && dcEpp <= 100 && acDemote <= 100 && dcDemote <= 100 &&
           acPromote <= 100 && dcPromote <= 100 && acLimit <= 3 && dcLimit <= 3 &&
           validIdleInterval(idleUs);
  }
  bool manages(unsigned index) const {
    return (index == 8 || index == 9)     ? epp
           : (index == 10 || index == 11) ? idleThresholds
           : index == 12                  ? idleLimit
           : index == 13                  ? earlyIdle
                                          : false;
  }
  DWORD value(unsigned index, bool ac) const {
    switch (index) {
    case 8:
    case 9:
      return ac ? acEpp : dcEpp;
    case 10:
      return ac ? acDemote : dcDemote;
    case 11:
      return ac ? acPromote : dcPromote;
    case 12:
      return ac ? acLimit : dcLimit;
    case 13:
      return idleUs;
    default:
      return 0;
    }
  }
  bool boostDisabled(bool ac) const {
    return ac ? disableBoostAc : disableBoostDc;
  }
  DWORD boost(PowerState state, bool ac) const {
    return boostDisabled(ac) || state == PowerState::Efficiency ? 0u
           : state == PowerState::Compute                       ? 4u
                                                                : 2u;
  }
  bool operator==(const Options &) const = default;
};
struct Capability {
  bool intel = false, amd = false, hwp = false, hwpEpp = false, eist = false;
  bool cppcMsr = false, cpb = false;
  bool knownVendor() const { return intel || amd; }
  // CPUID is feature enumeration, not MSR enable/lock state or an actuator.
  bool registerWriter = false;
};
inline Capability detect() {
  Capability c;
  int r[4]{};
  __cpuid(r, 0);
  const unsigned basic = static_cast<unsigned>(r[0]);
  char vendor[13]{};
  memcpy(vendor, r + 1, 4);
  memcpy(vendor + 4, r + 3, 4);
  memcpy(vendor + 8, r + 2, 4);
  c.intel = strcmp(vendor, "GenuineIntel") == 0;
  c.amd = strcmp(vendor, "AuthenticAMD") == 0;
  if (c.intel && basic >= 1) {
    __cpuid(r, 1);
    c.eist = (r[2] & (1 << 7)) != 0;
  }
  if (c.intel && basic >= 6) {
    __cpuid(r, 6);
    c.hwp = (r[0] & (1 << 7)) != 0;
    c.hwpEpp = (r[0] & (1 << 10)) != 0;
  }
  __cpuid(r, static_cast<int>(0x80000000u));
  const unsigned extended = static_cast<unsigned>(r[0]);
  if (c.amd && extended >= 0x80000008u) {
    __cpuid(r, static_cast<int>(0x80000008u));
    c.cppcMsr = (r[1] & (1 << 27)) != 0;
  }
  if (c.amd && extended >= 0x80000007u) {
    __cpuid(r, static_cast<int>(0x80000007u));
    c.cpb = (r[3] & (1 << 9)) != 0;
  }
  return c;
}
inline Options load(const std::wstring &path) {
  Options o;
  auto read = [&](const wchar_t *key, unsigned fallback, unsigned &out) {
    wchar_t text[16]{};
    GetPrivateProfileStringW(L"CpuOptions", key,
                             std::to_wstring(fallback).c_str(), text, 16,
                             path.c_str());
    return parsePercent(text, out);
  };
  const std::pair<const wchar_t *, bool *> flags[] = {
      {L"CustomEpp", &o.epp},
      {L"DisableBoostAC", &o.disableBoostAc},
      {L"DisableBoostDC", &o.disableBoostDc},
      {L"IdleThresholds", &o.idleThresholds},
      {L"IdleLimit", &o.idleLimit},
      {L"EarlyIdle", &o.earlyIdle}};
  for (const auto &[key, destination] : flags) {
    unsigned value = 0;
    if (!read(key, 0, value) || value > 1)
      return Options{};
    *destination = value == 1;
  }
  const std::pair<const wchar_t *, unsigned *> numbers[] = {
      {L"EppAC", &o.acEpp},         {L"EppDC", &o.dcEpp},
      {L"DemoteAC", &o.acDemote},   {L"DemoteDC", &o.dcDemote},
      {L"PromoteAC", &o.acPromote}, {L"PromoteDC", &o.dcPromote},
      {L"IdleLimitAC", &o.acLimit}, {L"IdleLimitDC", &o.dcLimit}};
  for (const auto &[key, destination] : numbers)
    if (!read(key, *destination, *destination))
      return Options{};
  wchar_t interval[16]{};
  GetPrivateProfileStringW(L"CpuOptions", L"IdleIntervalUs", L"1000", interval,
                           16, path.c_str());
  if (!parseIdleInterval(interval, o.idleUs)) return Options{};
  return o.valid() ? o : Options{};
}
inline bool save(const std::wstring &path, const Options &o) {
  if (!o.valid())
    return false;
  const auto temp = path + L".cpu.tmp";
  if (!CopyFileW(path.c_str(), temp.c_str(), FALSE)) {
    if (GetLastError() != ERROR_FILE_NOT_FOUND)
      return false;
    HANDLE f = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE)
      return false;
    CloseHandle(f);
  }
  const std::pair<const wchar_t *, unsigned> values[] = {
      {L"CustomEpp", o.epp},
      {L"DisableBoostAC", o.disableBoostAc},
      {L"DisableBoostDC", o.disableBoostDc},
      {L"EppAC", o.acEpp},
      {L"EppDC", o.dcEpp},
      {L"IdleThresholds", o.idleThresholds},
      {L"IdleLimit", o.idleLimit},
      {L"EarlyIdle", o.earlyIdle},
      {L"IdleIntervalUs", o.idleUs},
      {L"DemoteAC", o.acDemote},
      {L"DemoteDC", o.dcDemote},
      {L"PromoteAC", o.acPromote},
      {L"PromoteDC", o.dcPromote},
      {L"IdleLimitAC", o.acLimit},
      {L"IdleLimitDC", o.dcLimit}};
  for (const auto &[key, value] : values)
    if (!WritePrivateProfileStringW(
            L"CpuOptions", key, std::to_wstring(value).c_str(), temp.c_str()))
      return false;
  WritePrivateProfileStringW(nullptr, nullptr, nullptr, temp.c_str());
  if (!MoveFileExW(temp.c_str(), path.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    return false;
  WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str());
  return true;
}
} // namespace pulse::cpu
