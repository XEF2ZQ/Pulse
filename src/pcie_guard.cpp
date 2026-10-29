#include "pcie_guard.h"
#include <objbase.h>
#include <powrprof.h>

namespace pulse::pcie {
namespace {
constexpr wchar_t SchemesKey[] =
    L"SYSTEM\\CurrentControlSet\\Control\\Power\\User\\PowerSchemes";
std::wstring bare(const GUID &guid) {
  wchar_t buffer[40]{};
  if (!StringFromGUID2(guid, buffer, 40))
    return {};
  return std::wstring(buffer + 1, 36);
}
struct Windows {
  DWORD source(BYTE &value) {
    SYSTEM_POWER_STATUS status{};
    if (!GetSystemPowerStatus(&status))
      return GetLastError();
    value = status.ACLineStatus;
    return ERROR_SUCCESS;
  }
  DWORD active(GUID &value) {
    GUID *allocated = nullptr;
    DWORD error = PowerGetActiveScheme(nullptr, &allocated);
    if (!error && !allocated)
      error = ERROR_INVALID_DATA;
    if (!error && allocated)
      value = *allocated;
    if (allocated)
      LocalFree(allocated);
    return error;
  }
  DWORD read(const GUID &scheme, DWORD &value) {
    return PowerReadDCValueIndex(nullptr, &scheme,
                                 &GUID_PCIEXPRESS_SETTINGS_SUBGROUP,
                                 &GUID_PCIEXPRESS_ASPM_POLICY, &value);
  }
  DWORD write(const GUID &scheme, DWORD value) {
    return PowerWriteDCValueIndex(nullptr, &scheme,
                                  &GUID_PCIEXPRESS_SETTINGS_SUBGROUP,
                                  &GUID_PCIEXPRESS_ASPM_POLICY, value);
  }
  DWORD activate(const GUID &scheme) {
    return PowerSetActiveScheme(nullptr, &scheme);
  }
};
} // namespace
void Guard::Watch::close() {
  if (wait) {
    SetThreadpoolWait(wait, nullptr, nullptr);
    WaitForThreadpoolWaitCallbacks(wait, TRUE);
    CloseThreadpoolWait(wait);
    wait = nullptr;
  }
  if (key) {
    RegCloseKey(key);
    key = nullptr;
  }
  if (event) {
    CloseHandle(event);
    event = nullptr;
  }
  armed = false;
  path.clear();
}
DWORD Guard::Watch::open(const std::wstring &name, Guard *owner) {
  if (path == name && key)
    return arm();
  close();
  DWORD error =
      RegOpenKeyExW(HKEY_LOCAL_MACHINE, name.c_str(), 0, KEY_NOTIFY, &key);
  if (error)
    return error;
  event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!event) {
    error = GetLastError();
    close();
    return error;
  }
  wait = CreateThreadpoolWait(Guard::changed, owner, nullptr);
  if (!wait) {
    error = GetLastError();
    close();
    return error;
  }
  path = name;
  return arm();
}
DWORD Guard::Watch::arm() {
  if (armed && WaitForSingleObject(event, 0) == WAIT_TIMEOUT)
    return ERROR_SUCCESS;
  // The wait callback only posts a message. Drain it before reusing this
  // one-shot event.
  SetThreadpoolWait(wait, nullptr, nullptr);
  WaitForThreadpoolWaitCallbacks(wait, TRUE);
  ResetEvent(event);
  armed = false;
  const DWORD error = RegNotifyChangeKeyValue(key, FALSE,
                                              REG_NOTIFY_CHANGE_LAST_SET |
                                                  REG_NOTIFY_CHANGE_NAME |
                                                  REG_NOTIFY_THREAD_AGNOSTIC,
                                              event, TRUE);
  if (error) {
    close();
    return error;
  }
  armed = true;
  SetThreadpoolWait(wait, event, nullptr);
  return ERROR_SUCCESS;
}
void CALLBACK Guard::changed(PTP_CALLBACK_INSTANCE, void *context, PTP_WAIT,
                             TP_WAIT_RESULT) {
  auto *guard = static_cast<Guard *>(context);
  if (!guard->posted_.exchange(true)) {
    if (!PostMessageW(guard->owner_, ChangedMessage, 0, 0))
      guard->posted_ = false;
  }
}
void Guard::start(HWND owner, const std::wstring &ini) {
  stop();
  owner_ = owner;
  ini_ = ini;
  enabled_ = GetPrivateProfileIntW(L"Preferences", L"PcieBatteryGuard", 1,
                                   ini.c_str()) != 0;
  running_ = true;
  notify();
}
void Guard::stop() {
  running_ = false;
  if (owner_)
    KillTimer(owner_, GuardTimer);
  timer_ = false;
  setting_.close();
  root_.close();
  posted_ = false;
  owner_ = nullptr;
}
void Guard::schedule(UINT delay) {
  if (!running_ || !enabled_ || timer_)
    return;
  timer_ = SetCoalescableTimer(owner_, GuardTimer, delay, nullptr, 100) != 0;
  if (!timer_)
    watchError_ = GetLastError();
}
void Guard::notify() {
  posted_ = false;
  const auto elapsed = GetTickCount64() - lastWriteAt_;
  schedule(lastWriteAt_ && elapsed < 1000 ? static_cast<UINT>(1000 - elapsed)
                                          : 100);
}
DWORD Guard::observe() {
  DWORD error = root_.open(SchemesKey, this);
  if (error)
    return error;
  Windows api;
  BYTE source = 255;
  if ((error = api.source(source)))
    return error;
  if (source != 0) {
    setting_.close();
    return source == 1 ? ERROR_SUCCESS : ERROR_NOT_READY;
  }
  GUID scheme{};
  if ((error = api.active(scheme)))
    return error;
  return setting_.open(std::wstring(SchemesKey) + L"\\" + bare(scheme) + L"\\" +
                           bare(GUID_PCIEXPRESS_SETTINGS_SUBGROUP) + L"\\" +
                           bare(GUID_PCIEXPRESS_ASPM_POLICY),
                       this);
}
void Guard::timer() {
  if (owner_)
    KillTimer(owner_, GuardTimer);
  timer_ = false;
  if (!running_ || !enabled_)
    return;
  // Observe before writing, so an external reset during readback cannot be
  // missed.
  watchError_ = observe();
  Windows api;
  ++checks_;
  last_ = enforce(api, pending_);
  if (last_.wrote) {
    ++writes_;
    lastWriteAt_ = GetTickCount64();
  }
  // No periodic sampling on success. Errors retry slowly without spinning.
  if (watchError_ || last_.error)
    schedule(30000);
}
DWORD Guard::toggle() {
  if (!running_)
    return ERROR_INVALID_STATE;
  const bool next = !enabled_;
  if (!WritePrivateProfileStringW(L"Preferences", L"PcieBatteryGuard",
                                  next ? L"1" : L"0", ini_.c_str()))
    return GetLastError() ? GetLastError() : ERROR_WRITE_FAULT;
  enabled_ = next;
  if (!next) {
    KillTimer(owner_, GuardTimer);
    timer_ = false;
    setting_.close();
    root_.close();
    posted_ = false;
  } else
    notify();
  return ERROR_SUCCESS;
}
std::wstring Guard::status() const {
  if (!enabled_)
    return L"Off (saved PCIe setting retained)";
  if (!running_)
    return L"Not running";
  if (watchError_ || last_.error)
    return L"Needs attention — Windows " +
           std::to_wstring(watchError_ ? watchError_ : last_.error);
  if (!checks_)
    return L"Starting";
  if (last_.outcome == Outcome::WaitingForBattery)
    return L"On — waiting for battery";
  return L"On — maximum battery savings / watching";
}
std::wstring Guard::report() const {
  return L"\nPCIe battery guard: " + status() + L"\nPCIe checks: " +
         std::to_wstring(checks_) + L"\nPCIe DC writes: " +
         std::to_wstring(writes_) +
         L"\nPCIe guard owns only DC ASPM index 2; AC is untouched. Off/exit "
         L"retains this persistent preference.\n";
}
} // namespace pulse::pcie
