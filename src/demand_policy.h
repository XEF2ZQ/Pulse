#pragma once
#include <cmath>
#include <cstdint>
namespace pulse {
// Independent process-tree demand. Percentages are relative to ONE logical CPU.
class DemandPolicy {
    uint64_t candidate_ = 0, quiet_ = 0, validUntil_ = 0;
    bool editing_ = false;

  public:
    bool active = false;
    bool candidate() const { return candidate_ != 0; }
    void reset() {
        active = false;
        candidate_ = quiet_ = validUntil_ = 0;
        editing_ = false;
    }
    void expire(uint64_t now) {
        if (validUntil_ && now >= validUntil_)
            reset();
    }
    void update(uint64_t now, double cpu, bool known, bool interaction, bool alive,
                bool valid = true) {
        if (!alive) {
            reset();
            return;
        }
        if (!valid || !std::isfinite(cpu) || cpu < 0) {
            expire(now);
            return;
        }
        validUntil_ = now + 1500;
        const bool busy = cpu >= (interaction ? 35.0 : known ? 65.0 : 80.0);
        if (!active) {
            if (!busy) {
                candidate_ = 0;
                return;
            }
            if (!candidate_)
                candidate_ = now;
            const uint64_t evidence = interaction ? 0 : known ? 250 : 500;
            if (now - candidate_ >= evidence) {
                active = true;
                quiet_ = 0;
                editing_ = interaction;
            }
        } else if (cpu >= 20.0)
            quiet_ = 0;
        else {
            if (!quiet_)
                quiet_ = now;
            if (now - quiet_ >= (editing_ ? 125u : 500u))
                reset();
        }
    }
};
} // namespace pulse
