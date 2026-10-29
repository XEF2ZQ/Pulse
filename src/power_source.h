#pragma once
#include <string>
#include <windows.h>

struct IWbemServices;
namespace pulse::supply {
constexpr UINT ChangedMessage = WM_APP + 12;
constexpr UINT_PTR RefreshTimer = 45, SettleTimer = 46;
struct Evidence {
  bool asusValid = false;
  DWORD asusMode = 0;
};
inline Evidence parseAsus(DWORD raw, DWORD bytes) {
  if (bytes < sizeof(DWORD) || (raw & 0xffff0000) != 0x10000 || (raw & 0xfff8))
    return {};
  return {true, raw & 7};
}
struct Decision {
  bool known = false, external = false, weak = false, usb = false;
};
inline Decision classify(bool valid, const SYSTEM_POWER_STATUS &windows,
                         Evidence asus) {
  if (!valid || windows.ACLineStatus > 1)
    return {};
  const bool usb = asus.asusValid && (asus.asusMode & 2) != 0;
  if (windows.ACLineStatus == 1)
    return {true, true, false, usb};
  // A confirmed charger or a live charging flag is positive supply evidence.
  // A negative battery rate or an arbitrary USB device is never evidence of
  // absence/presence.
  const bool charger = asus.asusValid && (asus.asusMode & 3) != 0;
  const bool charging =
      windows.BatteryFlag != 255 && (windows.BatteryFlag & 8) != 0;
  const bool external = asus.asusValid ? charger : charging;
  return {true, external, external, usb};
}
class Monitor {
  class Sink;
  HWND owner_ = nullptr;
  HANDLE asus_ = INVALID_HANDLE_VALUE;
  IWbemServices *services_ = nullptr;
  Sink *sink_ = nullptr;
  Evidence evidence_;
  DWORD readError_ = ERROR_NOT_READY;
  HRESULT eventError_ = E_PENDING;
  unsigned long long reads_ = 0, events_ = 0;
  bool scheduled_ = false;
  void subscribe();

public:
  ~Monitor() { stop(); }
  void start(HWND owner);
  void stop();
  void refresh();
  void notify(bool settle = false);
  void timer(UINT_PTR id);
  void failed(HRESULT error) {
    eventError_ = error;
    evidence_ = {};
  }
  Decision decide(bool valid, const SYSTEM_POWER_STATUS &windows) const;
  std::wstring report() const;
};
} // namespace pulse::supply
