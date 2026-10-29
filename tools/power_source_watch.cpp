#include "power_source.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <objbase.h>
static pulse::supply::Monitor monitor;
static std::wofstream output;
static void record() {
  SYSTEM_POWER_STATUS status{};
  bool valid = GetSystemPowerStatus(&status) != FALSE;
  const auto d = monitor.decide(valid, status);
  output << L"t=" << GetTickCount64() << L" windows_ac="
         << unsigned(status.ACLineStatus) << L" flags="
         << unsigned(status.BatteryFlag) << L" external=" << d.external
         << L" weak=" << d.weak << L" usb=" << d.usb << monitor.report()
         << std::endl;
}
static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) {
  using namespace pulse::supply;
  if (m == ChangedMessage) {
    if (w)
      monitor.failed(static_cast<HRESULT>(l));
    monitor.notify(true);
    return 0;
  }
  if (m == WM_TIMER) {
    if (w == 99) {
      PostQuitMessage(0);
      return 0;
    }
    if (w == RefreshTimer || w == SettleTimer) {
      monitor.timer(w);
      record();
      return 0;
    }
  }
  return DefWindowProcW(h, m, w, l);
}
int wmain(int argc, wchar_t **argv) {
  if (argc != 3)
    return 1;
  const unsigned seconds = wcstoul(argv[2], nullptr, 10);
  if (seconds < 1 || seconds > 300)
    return 1;
  output.open(std::filesystem::path(argv[1]));
  if (!output)
    return 2;
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  CoInitializeSecurity(nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT,
                       RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE,
                       nullptr);
  WNDCLASSW cls{};
  cls.hInstance = GetModuleHandleW(nullptr);
  cls.lpfnWndProc = proc;
  cls.lpszClassName = L"PulseSourceWatch";
  if (!RegisterClassW(&cls))
    return 3;
  HWND window = CreateWindowW(cls.lpszClassName, L"", 0, 0, 0, 0, 0,
                              HWND_MESSAGE, nullptr, cls.hInstance, nullptr);
  if (!window)
    return 4;
  monitor.start(window);
  record();
  SetTimer(window, 99, seconds * 1000, nullptr);
  MSG m{};
  while (GetMessageW(&m, nullptr, 0, 0) > 0) {
    TranslateMessage(&m);
    DispatchMessageW(&m);
  }
  record();
  monitor.stop();
  DestroyWindow(window);
  CoUninitialize();
  return 0;
}
