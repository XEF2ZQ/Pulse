// One-shot diagnostic. No tuning setters, mode changes or sensor polling.
// clang-format off
#include <initguid.h>
#include <windows.h>
#include <powrprof.h>
// clang-format on
#include <cstring>
#include <iostream>
#include <set>
#include <vector>
#ifdef PULSE_ADLX_PROBE
#include <ADLX.h>
#include <IDisplaySettings.h>
#include <IDisplays.h>
#include <IDisplays3.h>
#include <IGPUTuning.h>
#endif

static bool displays() {
  bool complete = true;
  unsigned active = 0;
  DISPLAY_DEVICEW device{};
  device.cb = sizeof(device);
  for (DWORD i = 0; EnumDisplayDevicesW(nullptr, i, &device, 0); ++i) {
    std::wcout << L"Display adapter: " << device.DeviceString << L"; active="
               << ((device.StateFlags & DISPLAY_DEVICE_ACTIVE) != 0) << L'\n';
    if (device.StateFlags & DISPLAY_DEVICE_ACTIVE) {
      ++active;
      DEVMODEW current{};
      current.dmSize = sizeof(current);
      if (EnumDisplaySettingsExW(device.DeviceName, ENUM_CURRENT_SETTINGS,
                                 &current, 0)) {
        std::wcout << L"Current mode: " << current.dmPelsWidth << L'x'
                   << current.dmPelsHeight << L" @ "
                   << current.dmDisplayFrequency << L" Hz; bpp="
                   << current.dmBitsPerPel << L'\n';
        std::set<DWORD> rates;
        DEVMODEW mode{};
        mode.dmSize = sizeof(mode);
        for (DWORD index = 0;
             EnumDisplaySettingsExW(device.DeviceName, index, &mode, 0);
             ++index) {
          if (mode.dmPelsWidth == current.dmPelsWidth &&
              mode.dmPelsHeight == current.dmPelsHeight &&
              mode.dmBitsPerPel == current.dmBitsPerPel &&
              mode.dmDisplayFlags == current.dmDisplayFlags)
            rates.insert(mode.dmDisplayFrequency);
          mode = {};
          mode.dmSize = sizeof(mode);
        }
        std::wcout
            << L"Advertised matching-mode integer rates (not DRR proof):";
        for (DWORD rate : rates)
          std::wcout << L' ' << rate;
        std::wcout << L'\n';
      } else {
        std::wcout << L"Current mode unavailable\n";
        complete = false;
      }
    }
    device = {};
    device.cb = sizeof(device);
  }
  std::wcout << L"Active display adapters: " << active << L'\n';
  return complete && active != 0;
}

static bool displayPaths() {
  constexpr UINT32 flags = QDC_ONLY_ACTIVE_PATHS | QDC_VIRTUAL_MODE_AWARE |
                           QDC_VIRTUAL_REFRESH_RATE_AWARE;
  LONG result = ERROR_INSUFFICIENT_BUFFER;
  // Hotplug can race buffer sizing. Retry only a bounded number of times.
  for (unsigned attempt = 0; attempt != 3; ++attempt) {
    UINT32 count = 0, modeCount = 0;
    result = GetDisplayConfigBufferSizes(flags, &count, &modeCount);
    if (result != ERROR_SUCCESS)
      break;
    if (!count || count > 1024 || modeCount > 4096) {
      result = ERROR_INVALID_DATA;
      break;
    }
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(count);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount ? modeCount : 1);
    result = QueryDisplayConfig(flags, &count, paths.data(), &modeCount,
                                modes.data(), nullptr);
    if (result == ERROR_INSUFFICIENT_BUFFER)
      continue;
    if (result != ERROR_SUCCESS)
      break;
    paths.resize(count);
    for (const auto &path : paths) {
      const auto &target = path.targetInfo;
      std::wcout << L"Active path target " << target.id
                 << L": output technology=" << target.outputTechnology
                 << L"; Windows DRR boost flag="
                 << ((path.flags & DISPLAYCONFIG_PATH_BOOST_REFRESH_RATE) != 0)
                 << L"; path refresh=" << target.refreshRate.Numerator << L'/'
                 << target.refreshRate.Denominator << L" Hz\n";
    }
    std::wcout << L"DRR flag describes current configuration, not panel "
                  L"capability or measured residency.\n";
    return true;
  }
  std::wcout << L"Windows display-path query unavailable: " << result << L'\n';
  return false;
}

static bool pciePolicy() {
  GUID *scheme = nullptr;
  DWORD error = PowerGetActiveScheme(nullptr, &scheme);
  if (error || !scheme) {
    std::wcout << L"Power scheme query unavailable: " << error << L'\n';
    if (scheme)
      LocalFree(scheme);
    return false;
  }
  DWORD ac = 0, dc = 0;
  const DWORD acError =
      PowerReadACValueIndex(nullptr, scheme, &GUID_PCIEXPRESS_SETTINGS_SUBGROUP,
                            &GUID_PCIEXPRESS_ASPM_POLICY, &ac);
  const DWORD dcError =
      PowerReadDCValueIndex(nullptr, scheme, &GUID_PCIEXPRESS_SETTINGS_SUBGROUP,
                            &GUID_PCIEXPRESS_ASPM_POLICY, &dc);
  LocalFree(scheme);
  std::wcout << L"PCIe ASPM policy (0 off, 1 moderate, 2 maximum): AC result="
             << acError;
  if (!acError)
    std::wcout << L", value=" << ac;
  std::wcout << L"; DC result=" << dcError;
  if (!dcError)
    std::wcout << L", value=" << dc;
  std::wcout << L"\nPolicy is not proof of link residency and does not control "
                L"the iGPU internal fabric.\n";
  return !acError && !dcError && ac <= 2 && dc <= 2;
}

#ifdef PULSE_ADLX_PROBE
template <class T> static T symbol(HMODULE module, const char *name) {
  const auto address = GetProcAddress(module, name);
  T result{};
  static_assert(sizeof(result) == sizeof(address));
  std::memcpy(&result, &address, sizeof(result));
  return result;
}

static bool support(const wchar_t *name, ADLX_RESULT result, adlx_bool value) {
  std::wcout << name << L": result=" << static_cast<int>(result) << L"; "
             << (result == ADLX_OK ? (value ? L"supported" : L"unsupported")
                                   : L"unknown (query failed)")
             << L'\n';
  return result == ADLX_OK;
}

static int adlxProbe() {
  // Do not search the working directory or load a downloaded driver DLL.
  HMODULE module =
      LoadLibraryExW(L"amdadlx64.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (!module) {
    std::wcout << L"ADLX load failed: Windows " << GetLastError() << L'\n';
    return 2;
  }
  auto init = symbol<ADLXInitialize_Fn>(module, ADLX_INIT_FUNCTION_NAME);
  auto terminate =
      symbol<ADLXTerminate_Fn>(module, ADLX_TERMINATE_FUNCTION_NAME);
  auto version =
      symbol<ADLXQueryVersion_Fn>(module, ADLX_QUERY_VERSION_FUNCTION_NAME);
  if (!init || !terminate || !version) {
    std::wcout << L"Required ADLX exports unavailable\n";
    FreeLibrary(module);
    return 2;
  }
  const char *text = nullptr;
  auto r = version(&text);
  std::wcout << L"ADLX version query result=" << static_cast<int>(r);
  if (r == ADLX_OK && text)
    std::wcout << L"; version=" << text;
  std::wcout << L'\n';
  adlx::IADLXSystem *system = nullptr; // Owned by the ADLX session.
  r = init(ADLX_FULL_VERSION, &system);
  std::wcout << L"ADLX initialize result=" << static_cast<int>(r) << L'\n';
  if (r != ADLX_OK || !system) {
    // No incompatible-driver override and no speculative version fallback.
    if (r == ADLX_OK)
      terminate();
    FreeLibrary(module);
    return 2;
  }
  bool complete = true;
  {
    // Release every reference before terminating/unloading the session.
    adlx::IADLXGPUListPtr gpus;
    r = system->GetGPUs(&gpus);
    std::wcout << L"GPU enumeration result=" << static_cast<int>(r) << L'\n';
    if (r == ADLX_OK && gpus) {
      if (gpus->Begin() == gpus->End())
        complete = false;
      adlx::IADLXGPUTuningServicesPtr tuning;
      const auto tuningResult = system->GetGPUTuningServices(&tuning);
      std::wcout << L"Tuning service result=" << static_cast<int>(tuningResult)
                 << L'\n';
      for (adlx_uint i = gpus->Begin(); i != gpus->End(); ++i) {
        adlx::IADLXGPUPtr gpu;
        r = gpus->At(i, &gpu);
        if (r != ADLX_OK || !gpu) {
          complete = false;
          continue;
        }
        const char *name = nullptr;
        r = gpu->Name(&name);
        std::wcout << L"GPU " << i << L": "
                   << (r == ADLX_OK && name ? name : "unknown") << L'\n';
        if (tuningResult == ADLX_OK && tuning) {
          adlx_bool supported = false;
          r = tuning->IsSupportedManualGFXTuning(gpu, &supported);
          complete =
              support(L"Manual graphics clocks", r, supported) && complete;
          supported = false;
          r = tuning->IsSupportedManualVRAMTuning(gpu, &supported);
          complete = support(L"Manual memory clocks", r, supported) && complete;
          supported = false;
          r = tuning->IsSupportedManualPowerTuning(gpu, &supported);
          complete = support(L"Manual power tuning", r, supported) && complete;
        } else {
          complete = false;
        }
      }
    } else {
      complete = false;
    }
    adlx::IADLXDisplayServicesPtr service;
    r = system->GetDisplaysServices(&service);
    std::wcout << L"Display service result=" << static_cast<int>(r) << L'\n';
    if (r == ADLX_OK && service) {
      adlx::IADLXDisplayListPtr list;
      r = service->GetDisplays(&list);
      std::wcout << L"Display enumeration result=" << static_cast<int>(r)
                 << L'\n';
      if (r == ADLX_OK && list) {
        if (list->Begin() == list->End())
          complete = false;
        for (adlx_uint i = list->Begin(); i != list->End(); ++i) {
          adlx::IADLXDisplayPtr display;
          if (list->At(i, &display) != ADLX_OK || !display) {
            complete = false;
            continue;
          }
          adlx::IADLXDisplayServices3Ptr extended(service);
          if (extended) {
            adlx::IADLXDisplayDynamicRefreshRateControlPtr drr;
            r = extended->GetDynamicRefreshRateControl(display, &drr);
            std::wcout << L"Display " << i << L" dynamic refresh query result="
                       << static_cast<int>(r) << L'\n';
            if (r == ADLX_OK && drr) {
              adlx_bool supported = false;
              r = drr->IsSupported(&supported);
              complete =
                  support(L"AMD dynamic refresh control", r, supported) &&
                  complete;
              if (r == ADLX_OK && supported) {
                adlx_bool enabled = false;
                r = drr->IsEnabled(&enabled);
                std::wcout << L"Dynamic refresh enabled query result="
                           << static_cast<int>(r);
                if (r == ADLX_OK)
                  std::wcout << L"; enabled=" << enabled;
                else
                  complete = false;
                std::wcout << L'\n';
              }
            } else {
              complete = false;
            }
          } else {
            std::wcout << L"AMD dynamic refresh interface unavailable (not "
                          L"proof Windows DRR is unsupported)\n";
          }
          adlx::IADLXDisplayVariBrightPtr vb;
          r = service->GetVariBright(display, &vb);
          std::wcout << L"Display " << i << L" Vari-Bright query result="
                     << static_cast<int>(r) << L'\n';
          if (r == ADLX_OK && vb) {
            adlx_bool supported = false;
            r = vb->IsSupported(&supported);
            complete = support(L"Vari-Bright", r, supported) && complete;
            if (r == ADLX_OK && supported) {
              adlx_bool enabled = false;
              r = vb->IsEnabled(&enabled);
              std::wcout << L"Vari-Bright enabled query result="
                         << static_cast<int>(r);
              if (r == ADLX_OK)
                std::wcout << L"; enabled=" << enabled;
              else
                complete = false;
              std::wcout << L'\n';
            }
          } else {
            complete = false;
          }
        }
      } else {
        complete = false;
      }
    } else {
      complete = false;
    }
  }
  r = terminate();
  std::wcout << L"ADLX terminate result=" << static_cast<int>(r) << L'\n';
  FreeLibrary(module);
  return complete && r == ADLX_OK ? 0 : 2;
}
#endif

int wmain(int argc, wchar_t **argv) {
  bool windowsOnly = false, requireBattery = false;
  for (int i = 1; i < argc; ++i) {
    if (wcscmp(argv[i], L"--windows-only") == 0 && !windowsOnly)
      windowsOnly = true;
    else if (wcscmp(argv[i], L"--require-battery") == 0 && !requireBattery)
      requireBattery = true;
    else {
      std::wcerr
          << L"Usage: PulseGpuProbe [--windows-only] [--require-battery]\n";
      return 1;
    }
  }
  std::wcout << L"PulseGpuProbe 0.1: read-only, one shot\n";
  SYSTEM_POWER_STATUS power{};
  bool complete = GetSystemPowerStatus(&power) != FALSE;
  if (complete)
    std::wcout << L"Power source: "
               << (power.ACLineStatus == 0   ? L"battery"
                   : power.ACLineStatus == 1 ? L"AC"
                                             : L"unknown")
               << L'\n';
  else
    std::wcout << L"Power source: unknown; Windows " << GetLastError() << L'\n';
  if (power.ACLineStatus > 1)
    complete = false;
  if (requireBattery && (!complete || power.ACLineStatus != 0)) {
    std::wcout
        << L"Battery-only check refused: battery power is not confirmed.\n";
    return 3;
  }
  const BYTE originalSource = power.ACLineStatus;
  complete = displays() && complete;
  complete = displayPaths() && complete;
  complete = pciePolicy() && complete;
  int result = complete ? 0 : 2;
  if (!windowsOnly) {
#ifdef PULSE_ADLX_PROBE
    const int adlxResult = adlxProbe();
    if (adlxResult)
      result = adlxResult;
#else
    std::wcout << L"ADLX probe not built; configure PULSE_ADLX_SDK_ROOT to "
                  L"enable it.\n";
    result = 2;
#endif
  }
  if (!GetSystemPowerStatus(&power) || power.ACLineStatus != originalSource) {
    std::wcout << L"Power-source snapshot changed or became unavailable; "
                  L"discard source-specific comparisons.\n";
    result = requireBattery ? 3 : 2;
  }
  std::wcout << L"No settings changed. "
             << (result ? L"Report incomplete or source changed."
                        : L"Requested queries completed.")
             << L'\n';
  return result;
}
