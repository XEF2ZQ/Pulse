#include "placement.h"
#include "power.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
// Manual integration diagnostic. The fixture is the only candidate; its own
// executable is outside Windows, below-normal priority, with no visible window.
int wmain(int argc, wchar_t **argv) {
    if (argc == 2 && wcscmp(argv[1], L"--fixture") == 0) {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        const auto start = GetTickCount64();
        while (GetTickCount64() - start < 22000) {
            if (GetTickCount64() - start >= 10000 && GetTickCount64() - start < 14000) {
                YieldProcessor();
                continue;
            }
            const auto busy = GetTickCount64();
            // Use work intervals large enough for Windows thread CPU accounting.
            // 12 ms microbursts can be reported as zero CPU in individual samples;
            // the production policy correctly refuses that evidence.
            while (GetTickCount64() - busy < 120)
                YieldProcessor();
            Sleep(880);
        }
        return 0;
    }
    auto topology = pulse::scheduler::discoverTopology();
    if (!topology.valid() || !topology.capabilities.heterogeneous)
        return 2;
    std::wstring command = L"\"" + pulse::executablePath() + L"\" --fixture";
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION child{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &si, &child))
        return 3;
    // Establish eligibility before the first snapshot; otherwise the fixture
    // can miss a discovery cycle while its entry point is still starting.
    SetThreadPriority(child.hThread, THREAD_PRIORITY_BELOW_NORMAL);
    ResumeThread(child.hThread);
    pulse::placement::Controller controller(child.dwProcessId);
    controller.topology(topology);
    controller.enabled = true;
    FILETIME c{}, x{}, k0{}, u0{}, k1{}, u1{};
    GetProcessTimes(GetCurrentProcess(), &c, &x, &k0, &u0);
    bool selected = false, released = false;
    const auto start = GetTickCount64();
    uint64_t lastCpu = 0, lastAt = start;
    for (int i = 0; i < 18; ++i) {
        controller.tick(GetTickCount64(), 0, true, false);
        FILETIME tc{}, tx{}, tk{}, tu{};
        GetThreadTimes(child.hThread, &tc, &tx, &tk, &tu);
        const auto now = GetTickCount64();
        const auto cpu = pulse::fileTimeValue(tk) + pulse::fileTimeValue(tu);
        GROUP_AFFINITY affinity{};
        GetThreadGroupAffinity(child.hThread, &affinity);
        ULONG sets[64]{}, count = 0;
        if (GetThreadSelectedCpuSets(child.hThread, sets, 64, &count)) {
            if (count)
                selected = true;
            if (selected && !count && GetTickCount64() - start >= 10000)
                released = true;
        }
        std::wcout << L"sample_ms=" << now - start << L" cpu_percent="
                   << (now > lastAt
                           ? 100.0 * double(cpu - lastCpu) / (10000.0 * double(now - lastAt))
                           : 0)
                   << L" sets=" << count << L" affinity=" << affinity.Mask << L"\n";
        lastCpu = cpu;
        lastAt = now;
        Sleep(1000);
    }
    controller.release();
    GetProcessTimes(GetCurrentProcess(), &c, &x, &k1, &u1);
    const double cpuMs = double(pulse::fileTimeValue(k1) + pulse::fileTimeValue(u1) -
                                pulse::fileTimeValue(k0) - pulse::fileTimeValue(u0)) /
                         10000;
    std::wcout << L"isolated_fixture_only=1 controller_cpu_ms=" << cpuMs << L" elapsed_ms="
               << GetTickCount64() - start << L" selected=" << selected << L" released_on_load="
               << released << L"\n"
               << controller.report();
    WaitForSingleObject(child.hProcess, 6000);
    CloseHandle(child.hThread);
    CloseHandle(child.hProcess);
    return selected && released ? 0 : 1;
}
