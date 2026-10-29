#pragma once
#include "adaptive_config.h"
#include "process_discovery.h"
#include <memory>
namespace pulse::adaptive {
class Controller {
  struct Impl;
  std::unique_ptr<Impl> impl_;

public:
  Controller();
  ~Controller();
  Controller(const Controller &) = delete;
  Controller &operator=(const Controller &) = delete;
  bool enabled = false;
  Config config;
  void topology(const scheduler::Topology &);
  void tick(uint64_t now, DWORD foreground, bool eligible, bool burst);
  void release();
  void notify() noexcept;
  bool computeActive() const;
  std::wstring status() const;
  std::wstring report() const;
  // Validation confines all mutations to one owned test child.
  void isolated(DWORD pid);
  void attach(HWND window, UINT message);
  void catalog(const std::vector<ProcessIdentity> &processes);
};
DWORD recover(const std::wstring &folder, DWORD owner = 0);
// Controlled-process API test; never invoked by normal startup.
DWORD validateTransaction(HANDLE ownedChild, const std::wstring &folder,
                          std::wstring &report);
} // namespace pulse::adaptive
