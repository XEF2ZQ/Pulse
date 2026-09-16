#pragma once
#include "efficiency.h"
#include <cstdint>

namespace efficiency {
struct Observation {
    bool desired = false;
    bool pending = false;
    bool protectedSetting = false;
};
// A slot owns a recoverable setting independently of the CPU power controller.
// Backend must persist the original value BEFORE a write and clear it ONLY after restoration.
struct Backend {
    virtual ~Backend() = default;
    virtual DWORD observe(Observation &) = 0;
    virtual DWORD capture() = 0;
    virtual DWORD apply() = 0;
    virtual DWORD restore() = 0;
};
struct Slot {
    State state = State::Off;
    DWORD error = 0;
    bool owned = false;
    uint64_t next = 0;
    unsigned writes = 0;
    uint64_t window = 0;
    unsigned windowWrites = 0;
    uint64_t pendingSince = 0;

    void step(bool requested, uint64_t now, Backend &backend) {
        if (!requested) {
            if (owned) {
                error = backend.restore();
                if (error) {
                    state = State::RestoreFailed;
                    next = now + 30000;
                    return;
                }
                owned = false;
            }
            state = State::Off;
            error = 0;
            next = 0;
            windowWrites = 0;
            pendingSince = 0;
            return;
        }
        if (now < next)
            return;
        if (state == State::RestoreFailed) {
            error = backend.restore();
            if (error) {
                next = now + 30000;
                return;
            }
            owned = false;
            state = State::Off;
        }
        if (state == State::Retry) {
            windowWrites = 0;
            window = now;
            pendingSince = 0;
        }
        Observation observation;
        error = backend.observe(observation);
        if (error) {
            state = State::Retry;
            next = now + 30000;
            return;
        }
        if (observation.protectedSetting) {
            error = ERROR_ACCESS_DISABLED_BY_POLICY;
            state = State::Blocked;
            next = now + 5000; // retry on a configuration event, not a protection-bypass loop
            return;
        }
        if (observation.pending) {
            if (!pendingSince)
                pendingSince = now;
            if (now - pendingSince >= 30000) {
                error = WAIT_TIMEOUT;
                state = State::Retry;
                next = now + 30000;
                return;
            }
            state = State::Pending;
            next = now + 5000;
            return;
        }
        if (!owned) {
            error = backend.capture();
            if (error) {
                state = State::Retry;
                next = now + 30000;
                return;
            }
            owned = true;
        }
        pendingSince = 0;
        if (observation.desired) {
            state = State::Applied;
            next = now + 2000;
            return;
        }
        if (now - window >= 60000) {
            window = now;
            windowWrites = 0;
        }
        // A servicing/management tug-of-war must not become a new source of CPU load.
        if (windowWrites >= 4) {
            error = ERROR_RETRY;
            state = State::Retry;
            next = now + 30000;
            return;
        }
        ++windowWrites;
        ++writes;
        error = backend.apply();
        if (error) {
            state = State::Retry;
            next = now + 30000;
            return;
        }
        state = State::Pending;
        next = now + 2000; // one delayed readback; no steady-state polling
    }
};
} // namespace efficiency
