#pragma once
#include "workload.h"
#include <algorithm>
#include <string_view>

namespace pulse::cpu {
inline constexpr bool validIdleInterval(unsigned us) {
  return us >= 100 && us <= 50000;
}
inline bool parseIdleInterval(std::wstring_view text, unsigned &out) {
  if (text.empty() || text.size() > 5)
    return false;
  unsigned value = 0;
  for (const wchar_t c : text) {
    if (c < L'0' || c > L'9')
      return false;
    value = value * 10 + unsigned(c - L'0');
  }
  if (!validIdleInterval(value))
    return false;
  out = value;
  return true;
}
// A minimum time between OS policy adjustments, not a polling timer or a
// physical C-state transition deadline. Never make an existing interval slower.
inline constexpr unsigned idleIntervalTarget(unsigned original,
                                             unsigned requested,
                                             PowerState state, bool ac,
                                             bool weakCharger) {
  return !ac && !weakCharger && state == PowerState::Efficiency &&
                 validIdleInterval(requested)
             ? std::min(original, requested)
             : original;
}
inline constexpr bool validJournalValue(unsigned index, unsigned value) {
  return index == 13 ? value >= 1 && value <= 200000 : value <= 100;
}
} // namespace pulse::cpu
