#include "efficiency_policy.h"
#include <iostream>
#include <string>

using namespace efficiency;
struct Fake : Backend {
    bool current = false, original = false, journal = false, protectedSetting = false,
         pending = false;
    DWORD readError = 0, captureError = 0, applyError = 0, restoreError = 0;
    unsigned reads = 0, writes = 0, restores = 0;
    std::string operations;
    DWORD observe(Observation &out) override {
        ++reads;
        out = {current, pending, protectedSetting};
        return readError;
    }
    DWORD capture() override {
        operations += 'C';
        if (captureError)
            return captureError;
        if (journal)
            return ERROR_ALREADY_EXISTS;
        original = current;
        journal = true;
        return 0;
    }
    DWORD apply() override {
        operations += 'W';
        ++writes;
        if (!journal)
            return ERROR_INVALID_STATE;
        current = true; // simulate a partial change even when the API then fails
        return applyError;
    }
    DWORD restore() override {
        operations += 'R';
        ++restores;
        if (restoreError)
            return restoreError;
        if (journal)
            current = original;
        journal = false;
        return 0;
    }
};
int main() {
    unsigned failures = 0, checks = 0;
    auto check = [&](bool condition, const char *title) {
        ++checks;
        if (!condition) {
            ++failures;
            std::cerr << "FAIL: " << title << '\n';
        }
    };
    {
        Fake f;
        Slot s;
        s.step(false, 100, f);
        check(!f.reads && !f.writes && !f.journal, "disabled path does not observe or mutate");
        s.step(true, 100, f);
        check(f.operations == "CW" && s.state == State::Pending,
              "snapshot precedes change; never claim success before readback");
        s.step(true, 101, f);
        check(f.reads == 1 && f.writes == 1, "events coalesce during verification delay");
        s.step(true, 2100, f);
        check(s.state == State::Applied && f.writes == 1, "verified state is idempotent");
        f.current = false;
        s.step(true, 4200, f);
        check(f.writes == 2 && f.operations == "CWW",
              "external reset corrected without replacing baseline");
        s.step(false, 4201, f);
        check(!f.current && !f.journal && !s.owned && s.state == State::Off,
              "deselect restores baseline even inside debounce");
    }
    {
        Fake f;
        Slot s;
        f.protectedSetting = true;
        s.step(true, 100, f);
        s.step(true, 10000, f);
        check(s.state == State::Blocked && s.error == ERROR_ACCESS_DISABLED_BY_POLICY &&
                  !f.writes && !f.journal && f.reads == 2,
              "protected setting causes no writes even on a later recheck");
        s.step(false, 11000, f);
        f.protectedSetting = false;
        s.step(true, 12000, f);
        check(f.writes == 1, "explicit deselect/reselect permits a fresh attempt");
    }
    {
        Fake f;
        Slot s;
        f.captureError = ERROR_DISK_FULL;
        s.step(true, 100, f);
        check(!f.writes && !s.owned && s.error == ERROR_DISK_FULL,
              "failed durable snapshot prevents mutation");
    }
    {
        Fake f;
        Slot s;
        f.applyError = ERROR_ACCESS_DENIED;
        s.step(true, 100, f);
        check(s.owned && f.journal && f.current && s.state == State::Retry,
              "partial mutation retains recovery ownership");
        f.restoreError = ERROR_ACCESS_DENIED;
        s.step(false, 101, f);
        check(s.state == State::RestoreFailed && s.owned && f.journal,
              "failed restore is visible and never clears journal");
        f.restoreError = 0;
        s.step(false, 102, f);
        check(!s.owned && !f.journal && !f.current,
              "retry restores original after partial failure");
    }
    {
        Fake f;
        Slot s;
        f.current = true;
        s.step(true, 100, f);
        check(f.journal && !f.writes,
              "already-disabled setting captures its actual baseline without writing");
        f.current = false;
        s.step(true, 2200, f);
        s.step(false, 2300, f);
        check(f.current, "originally-disabled baseline is preserved on restore");
    }
    {
        Fake f;
        Slot s;
        for (unsigned i = 0; i < 20; ++i) {
            f.current = false;
            s.step(true, 100 + i * 500, f);
        }
        check(f.writes == 4 && s.state == State::Retry && s.error == ERROR_RETRY,
              "continuous external interference cannot cause unlimited writes");
        f.current = false;
        s.step(true, 40000, f);
        check(f.writes == 5 && s.state == State::Pending,
              "external resets resume correction automatically after backoff");
        s.step(false, 51000, f);
        check(!f.journal && !f.current, "conflict-blocked setting still restores");
    }
    {
        Fake f;
        Slot s;
        f.pending = true;
        for (unsigned i = 0; i < 8; ++i)
            s.step(true, 100 + i * 5000, f);
        check(s.state == State::Retry && s.error == WAIT_TIMEOUT && !f.writes,
              "stuck service transition switches to low-frequency automatic retry");
    }
    {
        Fake f;
        Slot s;
        f.readError = ERROR_ACCESS_DENIED;
        s.step(true, 100, f);
        check(!f.writes && !f.journal && s.state == State::Retry,
              "unknown state never treated as success");
    }
    std::cout << checks << " checks, " << failures << " failures; no system settings touched\n";
    return failures ? 1 : 0;
}
