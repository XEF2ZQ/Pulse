#include "pcie_policy.h"
#include <cstdio>
#include <vector>
using namespace pulse::pcie;
struct Fake {
  std::vector<BYTE> sources{0, 0, 0};
  std::vector<GUID> plans{GUID{1}, GUID{1}, GUID{1}};
  size_t sourceIndex = 0, planIndex = 0;
  DWORD dc = 0, readError = 0, writeError = 0, activateError = 0;
  unsigned reads = 0, writes = 0, activations = 0;
  bool rejectReadback = false;
  DWORD source(BYTE &out) {
    out = sources[sourceIndex < sources.size() ? sourceIndex++
                                               : sources.size() - 1];
    return 0;
  }
  DWORD active(GUID &out) {
    out = plans[planIndex < plans.size() ? planIndex++ : plans.size() - 1];
    return 0;
  }
  DWORD read(const GUID &, DWORD &out) {
    ++reads;
    out = rejectReadback && writes ? 0 : dc;
    return readError;
  }
  DWORD write(const GUID &, DWORD value) {
    ++writes;
    if (!writeError)
      dc = value;
    return writeError;
  }
  DWORD activate(const GUID &) {
    ++activations;
    return activateError;
  }
};
int main() {
  unsigned checks = 0, failures = 0;
  auto check = [&](bool ok, const char *name) {
    ++checks;
    if (!ok) {
      ++failures;
      std::printf("FAIL: %s\n", name);
    }
  };
  {
    Fake f;
    PendingApply p;
    auto r = enforce(f, p);
    check(r.outcome == Outcome::Corrected && f.dc == 2 && f.writes == 1 &&
              f.activations == 1 && !p.needed,
          "DC correction and readback");
    r = enforce(f, p);
    check(r.outcome == Outcome::Verified && f.writes == 1 && f.activations == 1,
          "already correct: no redundant write or activation");
  }
  for (BYTE source : {BYTE{1}, BYTE{255}}) {
    Fake f;
    f.sources = {source};
    PendingApply p;
    auto r = enforce(f, p);
    check(!f.reads && !f.writes && !f.activations,
          "AC/unknown performs no policy access");
    check(source == 1 ? r.outcome == Outcome::WaitingForBattery
                      : r.error == ERROR_NOT_READY,
          "AC/unknown distinguished");
  }
  {
    Fake f;
    f.sources = {0, 1};
    PendingApply p;
    auto r = enforce(f, p);
    check(r.outcome == Outcome::WaitingForBattery && !f.writes,
          "source changes before write");
  }
  {
    Fake f;
    f.sources = {0, 0, 1};
    PendingApply p;
    auto r = enforce(f, p);
    check(r.wrote && p.needed && !f.activations,
          "AC after DC write: no activation");
    f.sources = {0};
    f.sourceIndex = 0;
    r = enforce(f, p);
    check(r.outcome == Outcome::Corrected && !p.needed && f.writes == 1 &&
              f.activations == 1,
          "pending activation resumes without repeat write");
  }
  {
    Fake f;
    f.plans = {GUID{1}, GUID{2}};
    PendingApply p;
    auto r = enforce(f, p);
    check(r.error == ERROR_RETRY && !f.writes && !f.activations,
          "plan changed before write");
  }
  {
    Fake f;
    f.plans = {GUID{1}, GUID{1}, GUID{2}};
    PendingApply p;
    auto r = enforce(f, p);
    check(r.error == ERROR_RETRY && f.writes == 1 && !f.activations,
          "never reactivates old plan");
  }
  {
    Fake f;
    f.activateError = ERROR_ACCESS_DENIED;
    PendingApply p;
    auto r = enforce(f, p);
    check(r.error == ERROR_ACCESS_DENIED && p.needed,
          "activation failure retains pending apply");
    f.activateError = 0;
    r = enforce(f, p);
    check(!r.error && !p.needed && f.writes == 1,
          "activation retry without repeat write");
  }
  {
    Fake f;
    f.readError = ERROR_NOT_SUPPORTED;
    PendingApply p;
    auto r = enforce(f, p);
    check(r.error == ERROR_NOT_SUPPORTED && !f.writes,
          "unsupported setting not overwritten");
  }
  {
    Fake f;
    f.writeError = ERROR_ACCESS_DENIED;
    PendingApply p;
    auto r = enforce(f, p);
    check(r.error == ERROR_ACCESS_DENIED && !r.wrote && !f.activations,
          "write denial preserved");
  }
  {
    Fake f;
    f.rejectReadback = true;
    PendingApply p;
    auto r = enforce(f, p);
    check(r.error == ERROR_WRITE_FAULT && p.needed,
          "readback mismatch not called success");
  }
  {
    Fake f;
    f.dc = 99;
    PendingApply p;
    auto r = enforce(f, p);
    check(r.error == ERROR_INVALID_DATA && !f.writes, "unknown index rejected");
  }
  std::printf("%u checks, %u failures\n", checks, failures);
  return failures ? 1 : 0;
}
