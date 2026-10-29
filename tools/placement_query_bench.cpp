// Read-only microbenchmark of the old/new CPU-set query allocation pattern.
// Not a whole-controller or battery-energy benchmark. No process settings
// change.
#include <algorithm>
#include <iostream>
#include <vector>
#include <windows.h>

static double now() {
  LARGE_INTEGER t{}, f{};
  QueryPerformanceCounter(&t);
  QueryPerformanceFrequency(&f);
  return 1000.0 * double(t.QuadPart) / double(f.QuadPart);
}
static bool query(std::vector<ULONG> &ids, unsigned &allocations) {
  constexpr ULONG capacity = 256;
  allocations += ids.capacity() < capacity;
  ids.resize(capacity);
  ULONG count = 0;
  if (!GetProcessDefaultCpuSets(GetCurrentProcess(), ids.data(), capacity,
                                &count) ||
      count > capacity)
    return false;
  ids.resize(count);
  std::sort(ids.begin(), ids.end());
  return true;
}
int main() {
  constexpr unsigned iterations = 50000;
  std::vector<ULONG> scratch;
  scratch.reserve(256);
  unsigned warmAllocations = 0;
  for (unsigned i = 0; i < 1000; ++i)
    if (!query(scratch, warmAllocations))
      return 1;
  std::cout << "pair,mode,queries,wall_ms,buffer_growths\n";
  for (unsigned pair = 0; pair < 10; ++pair) {
    for (unsigned arm = 0; arm < 2; ++arm) {
      const bool reuse = arm == pair % 2;
      unsigned allocations = 0;
      const double start = now();
      for (unsigned i = 0; i < iterations; ++i) {
        if (reuse) {
          if (!query(scratch, allocations))
            return 2;
        } else {
          std::vector<ULONG> temporary;
          if (!query(temporary, allocations))
            return 3;
        }
      }
      std::cout << pair << ',' << (reuse ? "reuse" : "temporary") << ','
                << iterations << ',' << now() - start << ',' << allocations
                << '\n';
    }
  }
  return 0;
}
