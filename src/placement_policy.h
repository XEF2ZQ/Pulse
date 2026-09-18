#pragma once
#include <cmath>
#include <cstdint>
namespace pulse::placement {
// CPU time is normalized to one logical processor; no IPC/energy inference.
struct Evidence {
    bool eligible = false, urgent = false, valid = false;
    double cpu = 0;
};
class Policy {
    uint64_t quietSince_ = 0, retryAfter_ = 0;

  public:
    bool selected = false;
    enum class Action { None, SelectEfficient, Release };
    Action update(uint64_t now, const Evidence &e) {
        if (!e.eligible || e.urgent || !e.valid || !std::isfinite(e.cpu) || e.cpu < 0 ||
            e.cpu >= 50) {
            quietSince_ = 0;
            if (selected) {
                retryAfter_ = now + 15000;
                return Action::Release;
            }
            return Action::None;
        }
        if (selected)
            return Action::None;
        if (e.cpu < 1 || e.cpu > 20) {
            quietSince_ = 0;
            return Action::None;
        }
        if (!quietSince_)
            quietSince_ = now;
        if (now >= retryAfter_ && now - quietSince_ >= 5000)
            return Action::SelectEfficient;
        return Action::None;
    }
    void committed(bool value) {
        selected = value;
        quietSince_ = 0;
    }
};
} // namespace pulse::placement
