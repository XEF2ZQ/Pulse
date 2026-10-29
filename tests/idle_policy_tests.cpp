#include "cpu_options.h"
#include <iostream>
#include <limits>
int main() {
  using namespace pulse;
  unsigned failures = 0, checks = 0;
  auto check = [&](bool ok) {
    ++checks;
    if (!ok) {
      ++failures;
      std::cerr << "Failed " << checks << '\n';
    }
  };
  for (unsigned requested : {100u, 1000u, 10000u, 50000u})
    for (unsigned original : {1u, 50u, 100u, 1000u, 50000u, 200000u})
      for (auto state :
           {PowerState::Efficiency, PowerState::Burst, PowerState::Compute})
        for (bool ac : {false, true})
          for (bool weak : {false, true}) {
            const auto value =
                cpu::idleIntervalTarget(original, requested, state, ac, weak);
            check(value <= original);
            check(value == (!ac && !weak && state == PowerState::Efficiency
                                ? std::min(original, requested)
                                : original));
          }
  for (auto text : {L"", L"-1", L"0", L"99", L"50001", L"100000", L"1ms",
                    L"100.0", L" 100", L"1e3"}) {
    unsigned parsed = 1234;
    check(!cpu::parseIdleInterval(text, parsed) && parsed == 1234);
  }
  for (auto text : {L"100", L"1000", L"50000"}) {
    unsigned parsed = 0;
    check(cpu::parseIdleInterval(text, parsed) &&
          cpu::validIdleInterval(parsed));
  }
  check(!cpu::validJournalValue(13, 0));
  check(cpu::validJournalValue(13, 200000));
  check(!cpu::validJournalValue(13, 200001));
  check(!cpu::validJournalValue(13, std::numeric_limits<unsigned>::max()));
  check(!cpu::validJournalValue(12, 101));
  cpu::Options options;
  check(!options.earlyIdle && options.idleUs == 1000);
  options.earlyIdle = true;
  check(options.manages(13) && !options.manages(14));
  std::cout << checks << " idle checks; " << failures << " failures\n";
  return failures ? 1 : 0;
}
