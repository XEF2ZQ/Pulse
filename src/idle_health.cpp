#include "idle_health.h"
#include <algorithm>
#include <array>
#include <cfgmgr32.h>
#include <cmath>
#include <iomanip>
#include <map>
#include <objbase.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <powrprof.h>
#include <setupapi.h>
#include <sstream>
#include <vector>

namespace pulse::idle {
namespace {
struct Key {
  HKEY h = nullptr;
  ~Key() {
    if (h)
      RegCloseKey(h);
  }
};
struct Devices {
  HDEVINFO h = INVALID_HANDLE_VALUE;
  ~Devices() {
    if (h != INVALID_HANDLE_VALUE)
      SetupDiDestroyDeviceInfoList(h);
  }
};
struct Query {
  PDH_HQUERY h = nullptr;
  ~Query() {
    if (h)
      PdhCloseQuery(h);
  }
};
std::wstring property(HDEVINFO set, SP_DEVINFO_DATA &dev, DWORD which) {
  std::array<wchar_t, 4096> data{};
  DWORD type = 0;
  if (!SetupDiGetDeviceRegistryPropertyW(set, &dev, which, &type,
                                         reinterpret_cast<PBYTE>(data.data()),
                                         sizeof(data), nullptr) ||
      type != REG_SZ)
    return L"<unavailable>";
  data.back() = 0;
  return data.data();
}
void driverFlags(HDEVINFO set, SP_DEVINFO_DATA &dev, std::wostream &out) {
  Key key;
  key.h = SetupDiOpenDevRegKey(set, &dev, DICS_FLAG_GLOBAL, 0, DIREG_DRV,
                               KEY_QUERY_VALUE);
  if (key.h == INVALID_HANDLE_VALUE) {
    key.h = nullptr;
    out << L"  Driver settings unavailable: " << GetLastError() << L'\n';
    return;
  }
  // Names observed in the installed AMD driver and INF. Read only. Missing
  // values do not mean disabled and are never replaced with guessed defaults.
  for (const wchar_t *name :
       {L"DalFeatureEnablePsrSU", L"DalPSRAllowSMUOptimizations",
        L"DalReplayAllowSMUOptimizations", L"DalDisableReplayInAC",
        L"PP_GfxOffControl", L"DisableAllClockGating", L"DisableGfxClockGating",
        L"DisablePowerGating", L"DisableDynamicGfxMGPowerGating"}) {
    DWORD value = 0, size = sizeof(value);
    const auto status = RegGetValueW(key.h, nullptr, name, RRF_RT_REG_DWORD,
                                     nullptr, &value, &size);
    out << L"  " << name << L" = ";
    if (status == ERROR_SUCCESS)
      out << value;
    else
      out << L"unknown (Windows " << status << L')';
    out << L'\n';
  }
}
void devices(std::wostream &out) {
  Devices set{SetupDiGetClassDevsW(nullptr, L"PCI", nullptr,
                                   DIGCF_PRESENT | DIGCF_ALLCLASSES)};
  if (set.h == INVALID_HANDLE_VALUE) {
    out << L"PCI enumeration unavailable: " << GetLastError() << L'\n';
    return;
  }
  out << L"\nD: Present PCI devices (topology, not link residency)\n";
  unsigned displayCount = 0;
  for (DWORD i = 0; i < 1024; ++i) {
    SP_DEVINFO_DATA dev{sizeof(dev)};
    if (!SetupDiEnumDeviceInfo(set.h, i, &dev)) {
      if (GetLastError() != ERROR_NO_MORE_ITEMS)
        out << L"Enumeration error: " << GetLastError() << L'\n';
      break;
    }
    std::array<wchar_t, MAX_DEVICE_ID_LEN> id{}, parentId{};
    if (!SetupDiGetDeviceInstanceIdW(set.h, &dev, id.data(),
                                     static_cast<DWORD>(id.size()), nullptr))
      wcscpy_s(id.data(), id.size(), L"<unavailable>");
    DEVINST parent = 0;
    if (CM_Get_Parent(&parent, dev.DevInst, 0) != CR_SUCCESS ||
        CM_Get_Device_IDW(parent, parentId.data(),
                          static_cast<ULONG>(parentId.size()), 0) != CR_SUCCESS)
      wcscpy_s(parentId.data(), parentId.size(), L"<unavailable>");
    const auto cls = property(set.h, dev, SPDRP_CLASS);
    out << property(set.h, dev, SPDRP_DEVICEDESC) << L" [" << cls << L"]\n  "
        << id.data() << L"\n  parent: " << parentId.data();
    CM_POWER_DATA data{};
    DWORD type = 0, bytes = 0;
    if (SetupDiGetDeviceRegistryPropertyW(set.h, &dev, SPDRP_DEVICE_POWER_DATA,
                                          &type, reinterpret_cast<PBYTE>(&data),
                                          sizeof(data), &bytes) &&
        type == REG_BINARY && bytes == sizeof(data) &&
        data.PD_Size == sizeof(data) &&
        data.PD_MostRecentPowerState >= PowerDeviceD0 &&
        data.PD_MostRecentPowerState <= PowerDeviceD3)
      out << L"\n  last-reported D"
          << (data.PD_MostRecentPowerState - PowerDeviceD0)
          << L"; D3hot vs D3cold unknown";
    else
      out << L"\n  device power state unknown";
    out << L'\n';
    if (_wcsicmp(cls.c_str(), L"Display") == 0) {
      ++displayCount;
      if (std::wstring_view(id.data()).find(L"VEN_1002") !=
          std::wstring_view::npos)
        driverFlags(set.h, dev, out);
    }
    if (i == 1023)
      out << L"Device limit reached; inventory truncated.\n";
  }
  out << L"Present PCI display devices: " << displayCount
      << L". Count alone does not classify integrated/discrete or disabled "
         L"devices.\n"
      << L"Per-link ASPM/L1SS/LTR config and live residency: unknown; no "
         L"config-space reader installed.\n";
}
void firmware(std::wostream &out) {
  constexpr DWORD provider = 0x41435049; // 'ACPI' provider, per Win32 contract.
  constexpr DWORD facp = 0x50434146;     // Little-endian table signature FACP.
  UINT size = GetSystemFirmwareTable(provider, facp, nullptr, 0);
  Fadt result;
  if (size >= 116 && size <= 1024 * 1024) {
    std::vector<uint8_t> bytes(size);
    const UINT read =
        GetSystemFirmwareTable(provider, facp, bytes.data(), size);
    if (read && read <= size)
      result = parseFadt(std::span(bytes).first(read));
  }
  out << L"FADT ASPM prohibition: "
      << (!result.valid          ? L"unknown (missing/invalid table)"
          : result.aspmForbidden ? L"SET: firmware prohibits OS ASPM control"
                                 : L"clear")
      << L"\n_OSC grant: unknown. A clear FADT bit does not prove OS "
         L"ownership.\n";
}
void policies(std::wostream &out) {
  const GUID pci = {0x501a4d13,
                    0x42af,
                    0x4429,
                    {0x9f, 0xd1, 0xa8, 0x21, 0x8c, 0x26, 0x8e, 0x20}};
  const GUID aspm = {0xee12f906,
                     0xd277,
                     0x404b,
                     {0xb6, 0xda, 0xe5, 0xfa, 0x1a, 0x57, 0x6d, 0xf5}};
  GUID *raw = nullptr;
  const auto status = PowerGetActiveScheme(nullptr, &raw);
  if (status || !raw) {
    out << L"Active power scheme unavailable: " << status << L'\n';
    if (raw)
      LocalFree(raw);
    return;
  }
  const GUID scheme = *raw;
  LocalFree(raw);
  DWORD ac = 0, dc = 0;
  const auto a = PowerReadACValueIndex(nullptr, &scheme, &pci, &aspm, &ac);
  const auto d = PowerReadDCValueIndex(nullptr, &scheme, &pci, &aspm, &dc);
  out << L"ASPM policy AC=" << (a ? L"unknown" : std::to_wstring(ac)) << L" DC="
      << (d ? L"unknown" : std::to_wstring(dc))
      << L" (0 off, 1 moderate, 2 maximum); read errors=" << a << L'/' << d
      << L'\n';
  const GUID disk = {0x0012ee47,
                     0x9041,
                     0x4b5d,
                     {0x9b, 0x77, 0x53, 0x5f, 0xba, 0x8b, 0x14, 0x42}};
  const std::pair<const wchar_t *, const wchar_t *> nvme[] = {
      {L"Primary NVMe idle timeout", L"{d639518a-e56d-4345-8af2-b9f32fb26109}"},
      {L"Secondary NVMe idle timeout",
       L"{d3d55efd-c1ff-424e-9dc3-441be7833010}"},
      {L"Primary NVMe latency tolerance",
       L"{fc95af4d-40e7-4b6d-835a-56d131dbc80e}"},
      {L"Secondary NVMe latency tolerance",
       L"{dbc9e238-6de9-49e3-92cd-8c2b4946b472}"}};
  for (const auto &[name, id] : nvme) {
    GUID setting{};
    if (FAILED(CLSIDFromString(id, &setting)))
      continue;
    const auto aStatus =
        PowerReadACValueIndex(nullptr, &scheme, &disk, &setting, &ac);
    const auto dStatus =
        PowerReadDCValueIndex(nullptr, &scheme, &disk, &setting, &dc);
    out << name << L" (ms): AC=" << (aStatus ? L"unknown" : std::to_wstring(ac))
        << L" DC=" << (dStatus ? L"unknown" : std::to_wstring(dc))
        << L"; read errors=" << aStatus << L'/' << dStatus << L'\n';
  }
}
std::wstring processLabel(DWORD pid) {
  HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!h)
    return L"<exited or inaccessible>";
  std::array<wchar_t, 32768> path{};
  DWORD size = static_cast<DWORD>(path.size());
  const bool ok = QueryFullProcessImageNameW(h, 0, path.data(), &size) != FALSE;
  CloseHandle(h);
  if (!ok)
    return L"<unavailable>";
  std::wstring name(path.data(), size);
  return name.substr(name.find_last_of(L"\\/") + 1);
}
} // namespace

std::wstring inventory() {
  std::wostringstream out;
  SYSTEMTIME now{};
  GetSystemTime(&now);
  out << L"Pulse platform idle snapshot (UTC) " << now.wYear << L'-'
      << now.wMonth << L'-' << now.wDay << L' ' << now.wHour << L':'
      << now.wMinute << L':' << now.wSecond
      << L"\nRead-only. No hardware writes or automatic "
         L"remediation.\n"
      << L"A PSR/replay: configuration observable; see guarded panel "
         L"readback below. Residency/control unverified.\n"
      << L"B DF C-states: unavailable; CPU idle counters cannot establish "
         L"fabric residency.\n"
      << L"C GFXOFF/gating: configuration/activity observable; "
         L"residency/control unverified.\n"
      << L"D PCIe: supported Windows policy; link residency and D3cold "
         L"unverified.\n";
  SYSTEM_POWER_STATUS source{};
  if (GetSystemPowerStatus(&source))
    out << L"Windows ACLineStatus=" << unsigned(source.ACLineStatus) << L'\n';
  else
    out << L"Windows power source unknown: " << GetLastError() << L'\n';
  policies(out);
  firmware(out);
  devices(out);
  out << L"\nNo counter for PSR, DF or GFXOFF residency is claimed by this "
         L"report.\n";
  return out.str();
}

std::wstring activity(unsigned seconds, bool requireBattery, DWORD &error) {
  error = ERROR_SUCCESS;
  std::wostringstream out;
  if (seconds < 1 || seconds > 30) {
    error = ERROR_INVALID_PARAMETER;
    return L"Invalid sample duration.\n";
  }
  Query query;
  auto rc = PdhOpenQueryW(nullptr, 0, &query.h);
  if (rc) {
    error = rc;
    return L"PDH query unavailable.\n";
  }
  PDH_HCOUNTER gpu = nullptr;
  rc = PdhAddEnglishCounterW(
      query.h, L"\\GPU Engine(*)\\Utilization Percentage", 0, &gpu);
  if (rc) {
    error = rc;
    return L"GPU engine counter unavailable.\n";
  }
  std::map<std::wstring, double> peak;
  const wchar_t *cpuNames[] = {L"% Idle Time", L"% DPC Time",
                               L"% Interrupt Time"};
  std::array<PDH_HCOUNTER, 3> cpu{};
  std::array<double, 3> cpuTotal{};
  std::array<unsigned, 3> cpuCount{};
  std::array<PDH_STATUS, 3> cpuError{};
  for (size_t i = 0; i < cpu.size(); ++i) {
    const auto path =
        std::wstring(L"\\Processor Information(_Total)\\") + cpuNames[i];
    cpuError[i] = PdhAddEnglishCounterW(query.h, path.c_str(), 0, &cpu[i]);
    if (cpuError[i])
      cpu[i] = nullptr;
  }
  unsigned validIntervals = 0, invalidItems = 0, sleepCount = 0;
  for (unsigned step = 0; step <= seconds; ++step) {
    SYSTEM_POWER_STATUS source{};
    const bool sourceValid = GetSystemPowerStatus(&source) != FALSE;
    if (requireBattery && !confirmedBattery(sourceValid, source.ACLineStatus)) {
      error = ERROR_NOT_READY;
      out << L"Stopped: battery source no longer confirmed.\n";
      break;
    }
    rc = PdhCollectQueryData(query.h);
    if (rc) {
      error = rc;
      break;
    }
    if (step) {
      for (size_t i = 0; i < cpu.size(); ++i) {
        if (!cpu[i])
          continue;
        PDH_FMT_COUNTERVALUE value{};
        cpuError[i] = PdhGetFormattedCounterValue(cpu[i], PDH_FMT_DOUBLE,
                                                  nullptr, &value);
        if (!cpuError[i] &&
            (value.CStatus == PDH_CSTATUS_VALID_DATA ||
             value.CStatus == PDH_CSTATUS_NEW_DATA) &&
            std::isfinite(value.doubleValue) && value.doubleValue >= 0) {
          cpuTotal[i] += value.doubleValue;
          ++cpuCount[i];
        } else if (!cpuError[i])
          cpuError[i] = value.CStatus ? value.CStatus : PDH_INVALID_DATA;
      }
      bool obtained = false;
      for (unsigned attempt = 0; attempt < 3; ++attempt) {
        DWORD bytes = 0, count = 0;
        rc = PdhGetFormattedCounterArrayW(gpu, PDH_FMT_DOUBLE, &bytes, &count,
                                          nullptr);
        if (rc != PDH_MORE_DATA || bytes == 0 || bytes > 16 * 1024 * 1024) {
          error = rc ? rc : ERROR_INVALID_DATA;
          break;
        }
        std::vector<uint8_t> buffer(bytes);
        auto items =
            reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W *>(buffer.data());
        rc = PdhGetFormattedCounterArrayW(gpu, PDH_FMT_DOUBLE, &bytes, &count,
                                          items);
        // Provider instances can change; repeat sizing from zero, not from a
        // size returned for an insufficient nonzero buffer.
        if (rc == PDH_MORE_DATA)
          continue;
        if (rc || count > buffer.size() / sizeof(*items)) {
          error = rc ? rc : ERROR_INVALID_DATA;
          break;
        }
        for (DWORD i = 0; i < count; ++i) {
          const auto &v = items[i].FmtValue;
          if ((v.CStatus != PDH_CSTATUS_VALID_DATA &&
               v.CStatus != PDH_CSTATUS_NEW_DATA) ||
              !std::isfinite(v.doubleValue) || v.doubleValue < 0 ||
              !items[i].szName) {
            ++invalidItems;
            continue;
          }
          auto &maximum = peak[items[i].szName];
          maximum = std::max(maximum, v.doubleValue);
        }
        obtained = true;
        ++validIntervals;
        break;
      }
      if (!obtained) {
        if (!error)
          error = ERROR_RETRY;
        break;
      }
    }
    if (step < seconds) {
      Sleep(1000);
      ++sleepCount;
    }
  }
  std::vector<std::pair<std::wstring, double>> ranked(peak.begin(), peak.end());
  std::sort(ranked.begin(), ranked.end(),
            [](const auto &a, const auto &b) { return a.second > b.second; });
  out << L"\nGPU activity: " << validIntervals << L" valid intervals; "
      << sleepCount << L" scheduled one-second waits; " << invalidItems
      << L" invalid/newly missing items.\n"
      << L"Peak per engine (not summed). Candidate activity, not proof of an "
         L"idle blocker.\n";
  for (size_t i = 0; i < std::min<size_t>(ranked.size(), 20); ++i) {
    const auto &[name, value] = ranked[i];
    if (value < 0.01)
      continue;
    out << std::fixed << std::setprecision(3) << value << L"%  " << name;
    if (const auto pid = enginePid(name))
      out << L"  " << processLabel(*pid);
    out << L'\n';
  }
  if (error)
    out << L"Activity incomplete; Windows/PDH status=" << error << L'\n';
  out << L"\nB: CPU activity proxies (not DF residency or per-process wakeup "
         L"attribution)\n";
  for (size_t i = 0; i < cpu.size(); ++i) {
    out << cpuNames[i] << L": ";
    if (cpuCount[i])
      out << cpuTotal[i] / cpuCount[i] << L"% mean over " << cpuCount[i]
          << L" intervals";
    else
      out << L"unknown";
    out << L"; last PDH status=" << cpuError[i] << L'\n';
  }
  return out.str();
}
} // namespace pulse::idle
