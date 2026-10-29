#include "power_source.h"
#include <cstdio>
int main() {
  using namespace pulse::supply;
  unsigned checks = 0, failures = 0;
  auto check = [&](bool ok, const char *name) {
    ++checks;
    if (!ok) {
      ++failures;
      std::printf("FAIL %s\n", name);
    }
  };
  SYSTEM_POWER_STATUS s{};
  s.ACLineStatus = 0;
  s.BatteryFlag = 2;
  check(!classify(true, s, {}).external, "battery remains battery");
  s.BatteryFlag = 10;
  auto d = classify(true, s, {});
  check(d.known && d.weak && d.external && !d.usb,
        "charging while Windows offline");
  s.BatteryFlag = 255;
  check(!classify(true, s, {}).external,
        "unknown battery flags are not charging");
  s.BatteryFlag = 2;
  d = classify(true, s, {true, 6});
  check(d.external && d.weak && d.usb,
        "ASUS USB charger while battery discharges");
  s.BatteryFlag = 0;
  check(classify(true, s, {true, 2}).external,
        "charge limit does not hide charger");
  check(!classify(true, s, {true, 0}).external, "unplugged ASUS");
  s.BatteryFlag = 10;
  check(!classify(true, s, {true, 0}).external,
        "fresh ASUS disconnect wins over stale charging bit");
  s.ACLineStatus = 1;
  d = classify(true, s, {true, 0});
  check(d.external && !d.weak, "normal Windows AC remains AC");
  s.ACLineStatus = 255;
  check(!classify(true, s, {true, 6}).known,
        "unknown Windows source blocks routing");
  check(!classify(false, s, {true, 6}).external, "API failure blocks routing");
  check(parseAsus(65542, 16).asusValid && parseAsus(65542, 16).asusMode == 6,
        "observed ASUS response parsed");
  for (DWORD raw : {0u, 0xffffffffu, 0x10008u, 0x20002u, 0x10002u}) {
    check(!parseAsus(raw, 0).asusValid, "truncated ASUS response rejected");
    if (raw != 0x10002u)
      check(!parseAsus(raw, 16).asusValid,
            "unsupported/reserved response rejected");
  }
  std::printf("%u checks; %u failures\n", checks, failures);
  return failures ? 1 : 0;
}
