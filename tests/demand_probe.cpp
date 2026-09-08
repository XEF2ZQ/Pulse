#include "productive_monitor.h"
#include <iostream>
#include <psapi.h>
using namespace pulse;
static bool exited = false;
static LRESULT CALLBACK windowProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_APP + 10) {
        exited = true;
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
static uint64_t cpuTime() {
    FILETIME c{}, e{}, k{}, u{};
    GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u);
    return (uint64_t(k.dwHighDateTime) << 32 | k.dwLowDateTime) +
           (uint64_t(u.dwHighDateTime) << 32 | u.dwLowDateTime);
}
int main(int argc, char **argv) {
    const unsigned seconds = argc > 1 ? static_cast<unsigned>(std::stoul(argv[1])) : 30;
    const DWORD target = argc > 2 ? std::stoul(argv[2]) : 0;
    WNDCLASSW wc{};
    wc.lpfnWndProc = windowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"PulseDemandReadOnlyProbe";
    if (!RegisterClassW(&wc))
        return 2;
    HWND window = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                  wc.hInstance, nullptr);
    if (!window)
        return 3;
    ProductiveMonitor monitor;
    monitor.attach(window, WM_APP + 10);
    const uint64_t start = GetTickCount64(), cpuStart = cpuTime();
    uint64_t next = start;
    bool previous = false;
    std::cout
        << "elapsed_ms,active,candidate,busy_one_core_percent,tracked,reads,scans,exit_wake\n";
    while (GetTickCount64() - start < seconds * 1000ull) {
        auto now = GetTickCount64();
        if (now >= next || exited) {
            if (target)
                monitor.foreground(target);
            monitor.collect(now, exited);
            if (monitor.active != previous || exited || monitor.interval(now) >= 1000) {
                std::cout << now - start << ',' << monitor.active << ',' << monitor.pending << ','
                          << monitor.cpu << ',' << monitor.tracked() << ',' << monitor.reads << ','
                          << monitor.scans << ',' << exited << '\n';
                std::cout.flush();
            }
            previous = monitor.active;
            exited = false;
            next = now + monitor.interval(now);
        }
        now = GetTickCount64();
        MsgWaitForMultipleObjectsEx(0, nullptr, static_cast<DWORD>(next > now ? next - now : 0),
                                    QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    const auto elapsed = GetTickCount64() - start;
    const double cpuMs = double(cpuTime() - cpuStart) / 10000.0;
    PROCESS_MEMORY_COUNTERS_EX memory{};
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory),
                         sizeof(memory));
    std::cout << "SUMMARY elapsed=" << elapsed << " cpu_ms=" << cpuMs
              << " one_core_percent=" << 100 * cpuMs / double(elapsed)
              << " private_mib=" << double(memory.PrivateUsage) / 1048576
              << " native=" << monitor.nativeScans() << " failures=" << monitor.failures
              << " access_failures=" << monitor.accessFailures << " tracked=" << monitor.tracked()
              << " reads=" << monitor.reads << " capacity=" << monitor.capacityDrops
              << " waits_failed=" << monitor.waitFailures << '\n';
    monitor.clear();
    DestroyWindow(window);
    return 0;
}
