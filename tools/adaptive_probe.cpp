#include "power.h"
#include "process_placement.h"
#include <atomic>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <thread>
using namespace pulse;
struct Shared {
  volatile LONG stop = 0, mode = 0;
  volatile LONG64 units = 0, done = 0, sink = 0;
  volatile LONG64 locations[64]{};
  volatile LONG explicitCount = 0, group = 0;
  volatile LONG64 affinity = 0;
};
static LONG load(volatile LONG *p) {
  return InterlockedCompareExchange(p, 0, 0);
}
static LONG64 load(volatile LONG64 *p) {
  return InterlockedCompareExchange64(p, 0, 0);
}
static double ms() {
  LARGE_INTEGER q, f;
  QueryPerformanceCounter(&q);
  QueryPerformanceFrequency(&f);
  return 1000.0 * double(q.QuadPart) / double(f.QuadPart);
}
static std::string utf8(const std::wstring &text) {
  const int size = WideCharToMultiByte(
      CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
  std::string result(size, '\0');
  if (size)
    WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()),
                        result.data(), size, nullptr, nullptr);
  return result;
}
static uint64_t processCpu(HANDLE h) {
  FILETIME c, x, k, u;
  return GetProcessTimes(h, &c, &x, &k, &u)
             ? fileTimeValue(k) + fileTimeValue(u)
             : 0;
}
static void worker(Shared *s) {
  uint64_t value = 1234567;
  while (!load(&s->stop)) {
    auto mode = load(&s->mode);
    if (!mode) {
      Sleep(10);
      continue;
    }
    if (mode == 2 && InterlockedDecrement64(&s->units) < 0) {
      Sleep(1);
      continue;
    }
    for (unsigned i = 0; i < 50000; ++i)
      value = (value ^ (value >> 13)) * 0x9e3779b97f4a7c15ull + 17;
    PROCESSOR_NUMBER n{};
    GetCurrentProcessorNumberEx(&n);
    if (n.Group == 0 && n.Number < 64)
      InterlockedIncrement64(&s->locations[n.Number]);
    InterlockedExchange64(&s->sink, LONG64(value));
    InterlockedIncrement64(&s->done);
    if ((value & 1023) == 0) {
      ULONG ids[256]{}, count = 0;
      GROUP_AFFINITY affinity{};
      if (GetThreadSelectedCpuSets(GetCurrentThread(), ids, 256, &count))
        InterlockedExchange(&s->explicitCount, LONG(count));
      if (GetThreadGroupAffinity(GetCurrentThread(), &affinity)) {
        InterlockedExchange(&s->group, affinity.Group);
        InterlockedExchange64(&s->affinity, LONG64(affinity.Mask));
      }
    }
  }
}
static int child(const wchar_t *name, unsigned threads) {
  HANDLE map = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name);
  if (!map)
    return 2;
  auto shared = static_cast<Shared *>(
      MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared)));
  if (!shared) {
    CloseHandle(map);
    return 3;
  }
  std::vector<std::thread> workers;
  for (unsigned i = 0; i < std::clamp(threads, 1u, 24u); ++i)
    workers.emplace_back(worker, shared);
  for (auto &t : workers)
    t.join();
  UnmapViewOfFile(shared);
  CloseHandle(map);
  return 0;
}
static bool query(HANDLE h, std::vector<ULONG> &sets) {
  sets.resize(256);
  ULONG count = 0;
  if (!GetProcessDefaultCpuSets(h, sets.data(), 256, &count))
    return false;
  sets.resize(count);
  return true;
}
static void flags(HANDLE target, std::ostream &out, const char *label) {
  ULONG bytes = 0;
  GetSystemCpuSetInformation(nullptr, 0, &bytes, target, 0);
  if (!bytes || bytes > 1024 * 1024)
    return;
  std::vector<unsigned char> data(bytes);
  if (!GetSystemCpuSetInformation(
          reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(data.data()), bytes,
          &bytes, target, 0))
    return;
  out << label << " (id:flags):";
  for (size_t at = 0; at + sizeof(DWORD) * 2 <= bytes;) {
    SYSTEM_CPU_SET_INFORMATION row{};
    memcpy(&row, data.data() + at, std::min(sizeof(row), size_t(bytes) - at));
    if (row.Size < offsetof(SYSTEM_CPU_SET_INFORMATION, CpuSet.AllocationTag) ||
        row.Size > bytes - at)
      break;
    if (row.Type == CpuSetInformation)
      out << ' ' << row.CpuSet.Id << ':' << unsigned(row.CpuSet.AllFlags);
    at += row.Size;
  }
  out << '\n';
}
static bool runWork(Shared *s, LONG64 units, double &elapsed) {
  InterlockedExchange(&s->mode, 0);
  Sleep(30);
  InterlockedExchange64(&s->done, 0);
  InterlockedExchange64(&s->units, units);
  const double start = ms();
  InterlockedExchange(&s->mode, 2);
  while (load(&s->done) < units && ms() - start < 15000)
    Sleep(1);
  elapsed = ms() - start;
  InterlockedExchange(&s->mode, 0);
  return load(&s->done) >= units;
}
int wmain(int argc, wchar_t **argv) {
  if (argc == 4 && wcscmp(argv[1], L"--child") == 0)
    return child(argv[2], wcstoul(argv[3], nullptr, 10));
  if (argc == 3 && wcscmp(argv[1], L"--crash-owner") == 0) {
    adaptive::Controller controller;
    controller.isolated(wcstoul(argv[2], nullptr, 10));
    controller.topology(scheduler::discoverTopology());
    controller.config.profile = adaptive::Profile::Adaptive;
    controller.enabled = true;
    for (unsigned i = 0; i < 4; ++i) {
      controller.tick(GetTickCount64(), 0, true, false);
      Sleep(1000);
    }
    ExitProcess(77);
  }
  const bool transactionsOnly =
      argc == 3 && wcscmp(argv[2], L"--transactions-only") == 0;
  if (argc != 2 && !transactionsOnly) {
    std::wcerr << L"Usage: PulseAdaptiveProbe <output-folder> "
                  L"[--transactions-only]\n";
    return 2;
  }
  const std::filesystem::path folder = std::filesystem::absolute(argv[1]);
  std::filesystem::create_directories(folder);
  std::ofstream report(folder / L"summary.txt"),
      samples(folder / L"counter-samples.csv"),
      ab(folder / L"placement-ab.csv");
  SYSTEM_POWER_STATUS source{};
  GetSystemPowerStatus(&source);
  report << "ac=" << unsigned(source.ACLineStatus)
         << " battery_percent=" << unsigned(source.BatteryLifePercent)
         << " saver=" << unsigned(source.SystemStatusFlag) << "\n";
  const auto topology = scheduler::discoverTopology();
  std::ofstream(folder / L"topology.json") << topology.json();
  const auto selection = adaptive::select(topology);
  report << "physical=" << selection.physical
         << " logical=" << selection.four.size()
         << " groups=" << GetActiveProcessorGroupCount() << "\n";
  if (!selection.ready()) {
    report << "unsupported topology\n";
    return 3;
  }
  const auto mapName =
      L"Local\\PulseAdaptiveProbe-" + std::to_wstring(GetCurrentProcessId());
  HANDLE map = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                  0, sizeof(Shared), mapName.c_str());
  if (!map)
    return 4;
  auto shared = static_cast<Shared *>(
      MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared)));
  if (!shared) {
    CloseHandle(map);
    return 5;
  }
  new (shared) Shared{};
  std::wstring command =
      L"\"" + executablePath() + L"\" --child " + mapName + L" 1";
  STARTUPINFOW si{sizeof(si)};
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
                      CREATE_NO_WINDOW, nullptr, nullptr, &si, &process)) {
    UnmapViewOfFile(shared);
    CloseHandle(map);
    return 6;
  }
  bool ok = true;
  std::vector<ULONG> original;
  query(process.hProcess, original);
  DWORD_PTR oldAffinity = 0, system = 0;
  GetProcessAffinityMask(process.hProcess, &oldAffinity, &system);
  std::wstring transaction;
  const DWORD transactionError =
      adaptive::validateTransaction(process.hProcess, appFolder(), transaction);
  report << "transaction_error=" << transactionError << "\n";
  std::ofstream(folder / L"transactions.txt") << utf8(transaction);
  ok &= transactionError == 0;
  if (transactionsOnly) {
    InterlockedExchange(&shared->stop, 1);
    ok &= WaitForSingleObject(process.hProcess, 5000) == WAIT_OBJECT_0;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    UnmapViewOfFile(shared);
    CloseHandle(map);
    report << "transactions_only=1 passed=" << ok << '\n';
    return ok ? 0 : 1;
  }
  // Apply/readback and observed execution are separate evidence.
  const double applyStart = ms();
  ok &= SetProcessDefaultCpuSets(process.hProcess, selection.four.data(),
                                 DWORD(selection.four.size())) != FALSE;
  std::vector<ULONG> sets;
  ok &= query(process.hProcess, sets) && sets == selection.four;
  report << "apply_readback_ms=" << ms() - applyStart << "\n";
  flags(process.hProcess, report, "before_work");
  for (auto &value : shared->locations)
    InterlockedExchange64(&value, 0);
  InterlockedExchange(&shared->mode, 1);
  Sleep(500);
  InterlockedExchange(&shared->mode, 0);
  Sleep(30);
  std::set<unsigned> allowed;
  for (const auto &c : topology.cpus)
    if (c.cpuSetId && std::find(selection.four.begin(), selection.four.end(),
                                *c.cpuSetId) != selection.four.end())
      allowed.insert(c.key.index);
  LONG64 outside = 0, inside = 0;
  for (unsigned i = 0; i < 64; ++i) {
    auto count = load(&shared->locations[i]);
    if (allowed.contains(i))
      inside += count;
    else
      outside += count;
  }
  report << "observed_inside=" << inside << " outside=" << outside << "\n";
  flags(process.hProcess, report, "after_work");
  ok &= inside > 0 && outside == 0;
  report << "worker_explicit_cpu_sets=" << load(&shared->explicitCount)
         << " affinity=" << load(&shared->affinity)
         << " group=" << load(&shared->group) << "\n";
  for (unsigned i = 0; i < 64; ++i)
    if (load(&shared->locations[i]))
      report << "location_" << i << '=' << load(&shared->locations[i]) << '\n';
  // Explicit thread CPU Sets must survive a process-default change.
  ULONG explicitId = *topology.cpus.front().cpuSetId;
  ok &= SetThreadSelectedCpuSets(process.hThread, &explicitId, 1) != FALSE;
  ok &= SetProcessDefaultCpuSets(process.hProcess, nullptr, 0) != FALSE;
  ULONG count = 0, readIds[256]{};
  ok &= GetThreadSelectedCpuSets(process.hThread, readIds, 256, &count) &&
        count == 1 && readIds[0] == explicitId;
  SetThreadSelectedCpuSets(process.hThread, nullptr, 0);
  DWORD_PTR finalAffinity = 0;
  GetProcessAffinityMask(process.hProcess, &finalAffinity, &system);
  ok &= finalAffinity == oldAffinity;
  // Supported waitable timer, no timeBeginPeriod; report actual
  // jitter/granularity.
  HANDLE timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
  samples << "requested_ms,elapsed_ms,process_cpu_ms,system_busy_ms\n";
  InterlockedExchange(&shared->mode, 1);
  for (unsigned interval : {50u, 100u, 150u, 250u, 1000u})
    for (unsigned repeat = 0; repeat < 5; ++repeat) {
      FILETIME i0{}, k0{}, u0{}, i1{}, k1{}, u1{};
      GetSystemTimes(&i0, &k0, &u0);
      const auto c0 = processCpu(process.hProcess);
      const double start = ms();
      LARGE_INTEGER due{};
      due.QuadPart = -LONGLONG(interval) * 10000;
      if (!timer ||
          !SetWaitableTimerEx(timer, &due, 0, nullptr, nullptr, nullptr,
                              interval >= 1000 ? 50 : 0) ||
          WaitForSingleObject(timer, interval + 2000) != WAIT_OBJECT_0) {
        ok = false;
        break;
      }
      GetSystemTimes(&i1, &k1, &u1);
      samples << interval << ',' << ms() - start << ','
              << double(processCpu(process.hProcess) - c0) / 10000 << ','
              << double(fileTimeValue(k1) + fileTimeValue(u1) -
                        fileTimeValue(i1) - fileTimeValue(k0) -
                        fileTimeValue(u0) + fileTimeValue(i0)) /
                     10000
              << '\n';
    }
  InterlockedExchange(&shared->mode, 0);
  if (timer)
    CloseHandle(timer);
  std::ofstream trace(folder / L"workload-trace.csv");
  trace << "time_ms,cpu_one_lp_percent,compute_label,phase\n";
  const double traceStart = ms();
  double last = traceStart;
  auto lastCpu = processCpu(process.hProcess);
  struct Phase {
    const char *name;
    unsigned samples;
    LONG mode;
    bool compute;
  };
  for (const auto &phase :
       {Phase{"idle", 8, 0, false}, Phase{"single_compute", 16, 1, true},
        Phase{"release", 12, 0, false}, Phase{"short_burst", 1, 1, false},
        Phase{"idle_gap", 12, 0, false}, Phase{"compute_again", 16, 1, true}}) {
    InterlockedExchange(&shared->mode, phase.mode);
    for (unsigned n = 0; n < phase.samples; ++n) {
      Sleep(250);
      const double now = ms();
      const auto value = processCpu(process.hProcess);
      trace << now - traceStart << ','
            << double(value - lastCpu) / (100.0 * (now - last)) << ','
            << phase.compute << ',' << phase.name << '\n';
      last = now;
      lastCpu = value;
    }
  }
  InterlockedExchange(&shared->mode, 0);
  // Offline placement candidates; same fixed work, alternating order, no energy
  // claims.
  ab << "pair,mode,wall_ms,units\n";
  double warm = 0;
  runWork(shared, 1000, warm);
  for (unsigned pair = 0; pair < 4; ++pair)
    for (unsigned arm = 0; arm < 2; ++arm) {
      const bool restricted = arm == (pair % 2);
      ok &= SetProcessDefaultCpuSets(
                process.hProcess, restricted ? selection.four.data() : nullptr,
                restricted ? DWORD(selection.four.size()) : 0) != FALSE;
      double elapsed = 0;
      ok &= runWork(shared, 3000, elapsed);
      ab << pair << ',' << (restricted ? "four" : "all") << ',' << elapsed
         << ",3000\n";
    }
  SetProcessDefaultCpuSets(process.hProcess, nullptr, 0);
  // Exercise the actual coordinator, restricted to the child, not desktop apps.
  const auto start = GetTickCount64(),
             cpuStart = processCpu(GetCurrentProcess());
  DWORD handlesStart = 0;
  GetProcessHandleCount(GetCurrentProcess(), &handlesStart);
  adaptive::Controller controller;
  controller.isolated(process.dwProcessId);
  controller.topology(topology);
  controller.config.profile = adaptive::Profile::Adaptive;
  controller.enabled = true;
  bool selected = false, released = false;
  for (unsigned i = 0; i < 5; ++i) {
    controller.tick(GetTickCount64(), 0, true, false);
    query(process.hProcess, sets);
    selected |= !sets.empty();
    Sleep(1000);
  }
  const double rescueStart = ms();
  controller.tick(GetTickCount64(), process.dwProcessId, true, true);
  query(process.hProcess, sets);
  report << "foreground_rescue_ms=" << ms() - rescueStart
         << " clear=" << sets.empty() << "\n";
  ok &= sets.empty();
  // Strict mode permits compute rescue; no application-name hint is supplied.
  controller.release();
  controller.topology(topology);
  controller.config.profile = adaptive::Profile::StrictFour;
  for (unsigned i = 0; i < 3; ++i) {
    controller.tick(GetTickCount64(), 0, true, false);
    Sleep(1000);
  }
  query(process.hProcess, sets);
  const bool beforeSourceLoss = !sets.empty();
  // Inject an external source through the production eligibility predicate.
  // This exercises real release, not a physical charger transition.
  controller.tick(GetTickCount64(), 0,
                  adaptive::batteryEligible(true, true, true), false);
  query(process.hProcess, sets);
  report << "source_gate_had_assignment=" << beforeSourceLoss
         << " source_gate_restored=" << sets.empty() << '\n';
  ok &= beforeSourceLoss && sets.empty();
  for (unsigned i = 0; i < 3; ++i) {
    controller.tick(GetTickCount64(), 0,
                    adaptive::batteryEligible(true, true, false), false);
    Sleep(1000);
  }
  InterlockedExchange(&shared->mode, 1);
  for (unsigned i = 0; i < 4; ++i) {
    controller.tick(GetTickCount64(), 0, true, false);
    query(process.hProcess, sets);
    released |= controller.computeActive() && sets.empty();
    Sleep(1000);
  }
  InterlockedExchange(&shared->mode, 0);
  controller.release();
  DWORD handlesEnd = 0;
  GetProcessHandleCount(GetCurrentProcess(), &handlesEnd);
  report << "selected=" << selected << " compute_released=" << released
         << " coordinator_cpu_ms="
         << double(processCpu(GetCurrentProcess()) - cpuStart) / 10000
         << " duration_ms=" << GetTickCount64() - start
         << " handles_start=" << handlesStart << " handles_end=" << handlesEnd
         << "\n";
  const auto controllerReport = controller.report();
  std::ofstream(folder / L"controller.txt") << utf8(controllerReport);
  ok &= selected && released;
  // Simulated abrupt controller death with a still-live target process.
  command = L"\"" + executablePath() + L"\" --crash-owner " +
            std::to_wstring(process.dwProcessId);
  PROCESS_INFORMATION owner{};
  if (CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
                     CREATE_NO_WINDOW, nullptr, nullptr, &si, &owner)) {
    DWORD exit = 0;
    WaitForSingleObject(owner.hProcess, 10000);
    GetExitCodeProcess(owner.hProcess, &exit);
    query(process.hProcess, sets);
    bool owned = exit == 77 && !sets.empty();
    const auto recovery = adaptive::recover(appFolder(), owner.dwProcessId);
    query(process.hProcess, sets);
    report << "crash_owned=" << owned << " recovery=" << recovery
           << " restored=" << sets.empty() << "\n";
    ok &= owned && recovery == 0 && sets.empty();
    CloseHandle(owner.hThread);
    CloseHandle(owner.hProcess);
  } else
    ok = false;
  SetProcessDefaultCpuSets(process.hProcess,
                           original.empty() ? nullptr : original.data(),
                           DWORD(original.size()));
  InterlockedExchange(&shared->stop, 1);
  WaitForSingleObject(process.hProcess, 5000);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  UnmapViewOfFile(shared);
  CloseHandle(map);
  report << "passed=" << ok
         << "\nExploratory CPU-placement test only. Energy, browser/media and "
            "full power-tier A/B gates remain unmeasured.\n";
  std::cout << "probe passed=" << ok << "\n";
  return ok ? 0 : 1;
}
