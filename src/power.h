#pragma once
#include <windows.h>
#include "workload.h"
#include <string>
#include <vector>

namespace pulse {
std::wstring executablePath();
std::wstring appFolder();
std::wstring guidText(const GUID& g);
std::wstring errorText(DWORD code);
uint64_t fileTimeValue(const FILETIME& time);
struct Setting { GUID id; const wchar_t* name; DWORD ac, dc; bool dynamic; };
const std::vector<Setting>& settings();
struct Row { GUID scheme; DWORD setting, ac, dc, mask; };
struct Snapshot {
    DWORD magic = 0x50554C53, version = 2, owner = 0, count = 0;
    GUID scheme{}, acMode{}, dcMode{};
    Row rows[48]{};
    DWORD checksum = 0;
};
class Power {
public:
    Power();
    bool available() const;
    DWORD mode(bool ac, GUID& out) const;
    DWORD setMode(bool ac, const GUID& value) const;
    DWORD effectiveMode(GUID& out) const;
    DWORD activeScheme(GUID& out) const;
    DWORD read(const GUID& scheme, DWORD index, bool ac, DWORD& value) const;
    DWORD capture(DWORD sourceMask = 3);
    DWORD configure();
    DWORD apply(PowerState state);
    DWORD apply(bool burst) { return apply(burst?PowerState::Burst:PowerState::Efficiency); }
    DWORD verify(PowerState state, bool full = false) const;
    DWORD verify(bool burst, bool full = false) const { return verify(burst?PowerState::Burst:PowerState::Efficiency,full); }
    DWORD restore(DWORD expectedOwner = 0);
    bool hasJournal() const;
    bool owned = false;
    Snapshot saved{};
    double lastApplyMs = 0;
    uint64_t writes = 0;
    std::wstring report() const;
private:
    using GetMode = DWORD (WINAPI*)(GUID*);
    using SetMode = DWORD (WINAPI*)(const GUID*);
    GetMode getAc = nullptr, getDc = nullptr, getEffective = nullptr;
    SetMode setAc = nullptr, setDc = nullptr;
    DWORD write(const GUID& scheme, DWORD index, bool ac, DWORD value);
    DWORD saveJournal();
    DWORD loadJournal(Snapshot& out) const;
};
}
