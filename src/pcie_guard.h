#pragma once
#include "pcie_policy.h"
#include <atomic>
#include <string>

namespace pulse::pcie {
constexpr UINT ChangedMessage = WM_APP + 11;
constexpr UINT_PTR GuardTimer = 44;

// Permanent DC preference, separate from temporary CPU-policy ownership.
// Off/exit stops enforcement; it does not roll back the requested DC setting.
class Guard {
  struct Watch {
    HKEY key = nullptr;
    HANDLE event = nullptr;
    PTP_WAIT wait = nullptr;
    bool armed = false;
    std::wstring path;
    void close();
    DWORD open(const std::wstring &name, Guard *owner);
    DWORD arm();
  } root_, setting_;
  HWND owner_ = nullptr;
  std::wstring ini_;
  bool running_ = false, enabled_ = true, timer_ = false;
  std::atomic<bool> posted_{false};
  PendingApply pending_;
  Result last_;
  DWORD watchError_ = 0;
  unsigned long long checks_ = 0, writes_ = 0;
  ULONGLONG lastWriteAt_ = 0;
  void schedule(UINT delay);
  DWORD observe();
  static void CALLBACK changed(PTP_CALLBACK_INSTANCE, void *, PTP_WAIT,
                               TP_WAIT_RESULT);

public:
  ~Guard() { stop(); }
  void start(HWND owner, const std::wstring &ini);
  void stop();
  void notify();
  void timer();
  DWORD toggle();
  bool enabled() const { return enabled_; }
  std::wstring status() const;
  std::wstring report() const;
};
} // namespace pulse::pcie
