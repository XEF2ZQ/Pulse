#pragma once
#include <windows.h>
#include <intrin.h>
#include <array>
#include <cstring>
#include <string>
namespace pulse::undervolt {
enum class LockStatus { Unknown, Locked, Unlocked };
struct Capability {
    std::wstring name = L"Unknown CPU", vendor, units = L"Unavailable";
    unsigned family = 0, model = 0, stepping = 0;
    bool amd = false, intel = false, driverInstalled = false;
    // Identity and an installed transport are not proof of firmware access.
    LockStatus lock = LockStatus::Unknown;
    bool independentDomains = false, readback = false, writer = false;
    bool canApply() const {
        return lock == LockStatus::Unlocked && independentDomains && writer && readback;
    }
    bool canMonitor() const { return canApply(); }
};
inline Capability detect() {
    Capability c;
    int regs[4]{};
    __cpuid(regs, 0);
    char vendor[13]{};
    memcpy(vendor, &regs[1], 4);
    memcpy(vendor + 4, &regs[3], 4);
    memcpy(vendor + 8, &regs[2], 4);
    c.amd = strcmp(vendor, "AuthenticAMD") == 0;
    c.intel = strcmp(vendor, "GenuineIntel") == 0;
    c.vendor = std::wstring(vendor, vendor + strlen(vendor));
    __cpuid(regs, 1);
    const unsigned a = static_cast<unsigned>(regs[0]);
    unsigned family = (a >> 8) & 15;
    c.family = family == 15 ? family + ((a >> 20) & 255) : family;
    c.model = (a >> 4) & 15;
    if (family == 6 || family == 15)
        c.model |= ((a >> 16) & 15) << 4;
    c.stepping = a & 15;
    __cpuid(regs, static_cast<int>(0x80000000u));
    if (static_cast<unsigned>(regs[0]) >= 0x80000004u) {
        char name[49]{};
        for (unsigned i = 0; i < 3; ++i) {
            __cpuid(regs, static_cast<int>(0x80000002u + i));
            memcpy(name + i * 16, regs, 16);
        }
        c.name = std::wstring(name, name + strlen(name));
    }
    c.units = c.amd     ? L"Curve Optimizer steps (not mV)"
              : c.intel ? L"millivolts (mV)"
                        : L"Unsupported CPU vendor";
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (manager) {
        SC_HANDLE service = OpenServiceW(manager, L"PawnIO", SERVICE_QUERY_STATUS);
        if (service) {
            c.driverInstalled = true;
            CloseServiceHandle(service);
        }
        CloseServiceHandle(manager);
    }
    // No speculative MSR/mailbox writes, no inference from marketing names.
    // The installed G Helper transport has no verified per-domain CO getter.
    return c;
}
inline bool parseOffset(const std::wstring &text, int &value) {
    if (text.empty() || text.size() > 5)
        return false;
    size_t i = text[0] == L'-' ? 1 : 0;
    if (i == text.size())
        return false;
    int v = 0;
    for (; i < text.size(); ++i) {
        if (text[i] < L'0' || text[i] > L'9')
            return false;
        v = v * 10 + int(text[i] - L'0');
    }
    if (text[0] == L'-')
        v = -v;
    if (v > 0 || v < -250)
        return false;
    value = v;
    return true;
}
} // namespace pulse::undervolt
