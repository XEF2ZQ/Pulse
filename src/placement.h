#pragma once
#include "scheduler_topology.h"
#include <memory>
namespace pulse::placement {
class Controller {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    explicit Controller(DWORD isolatedTestPid = 0);
    ~Controller();
    Controller(const Controller &) = delete;
    Controller &operator=(const Controller &) = delete;
    bool enabled = false;
    void topology(const scheduler::Topology &topology);
    void tick(uint64_t now, DWORD foreground, bool eligible, bool urgent);
    void release();
    std::wstring status() const;
    std::wstring report() const;
};
DWORD recover(const std::wstring &folder, DWORD expectedOwner = 0);
int exercise(const std::wstring &output);
} // namespace pulse::placement
