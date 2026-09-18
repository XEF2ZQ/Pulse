#include "placement.h"
#include "placement_policy.h"
#include "power.h"
#include "process_discovery.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <filesystem>
#include <map>
#include <set>
#include <sstream>

namespace pulse::placement {
namespace {
constexpr size_t MaxThreads = 128, MaxSets = 64;
struct Record {
    DWORD pid = 0, tid = 0;
    uint64_t created = 0;
    DWORD count = 0;
    ULONG sets[MaxSets]{};
};
struct Journal {
    DWORD magic = 0x50534350, version = 1, owner = 0, count = 0;
    Record rows[MaxThreads]{};
    DWORD checksum = 0;
};
DWORD checksum(const Journal &j) {
    DWORD h = 2166136261u;
    auto p = reinterpret_cast<const unsigned char *>(&j);
    for (size_t i = 0; i < offsetof(Journal, checksum); ++i)
        h = (h ^ p[i]) * 16777619u;
    return h;
}
std::wstring journalPath(const std::wstring &folder) { return folder + L"\\state\\placement.bin"; }
bool query(HANDLE thread, std::vector<ULONG> &result) {
    ULONG n = 0;
    result.resize(MaxSets);
    if (!GetThreadSelectedCpuSets(thread, result.data(), static_cast<ULONG>(result.size()), &n) ||
        n > MaxSets)
        return false;
    result.resize(n);
    std::sort(result.begin(), result.end());
    return true;
}
bool matches(HANDLE thread, const Record &r) {
    std::vector<ULONG> v;
    if (!query(thread, v))
        return false;
    return v == std::vector<ULONG>(r.sets, r.sets + r.count);
}
DWORD restoreRecord(const Record &r) {
    HANDLE h =
        OpenThread(THREAD_QUERY_LIMITED_INFORMATION | THREAD_SET_LIMITED_INFORMATION, FALSE, r.tid);
    if (!h)
        return GetLastError() == ERROR_INVALID_PARAMETER ? 0 : GetLastError();
    DWORD error = 0;
    FILETIME c{}, x{}, k{}, u{};
    if (!GetThreadTimes(h, &c, &x, &k, &u))
        error = GetLastError();
    else if (!fileTimeValue(x) && GetProcessIdOfThread(h) == r.pid &&
             fileTimeValue(c) == r.created) {
        std::vector<ULONG> current;
        if (!query(h, current))
            error = GetLastError();
        else if (current == std::vector<ULONG>(r.sets, r.sets + r.count) &&
                 !SetThreadSelectedCpuSets(h, nullptr, 0))
            error = GetLastError();
    }
    CloseHandle(h);
    return error;
}
bool save(const Journal &input, const std::wstring &folder = appFolder()) {
    auto j = input;
    j.checksum = checksum(j);
    CreateDirectoryW((folder + L"\\state").c_str(), nullptr);
    const auto path = journalPath(folder), temp = path + L".tmp";
    HANDLE h = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_FLAG_WRITE_THROUGH, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    DWORD n = 0;
    bool ok = WriteFile(h, &j, sizeof(j), &n, nullptr) && n == sizeof(j) && FlushFileBuffers(h);
    CloseHandle(h);
    return ok && MoveFileExW(temp.c_str(), path.c_str(),
                             MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}
bool sameUser(HANDLE process) {
    HANDLE a = nullptr, b = nullptr;
    bool result = false;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &a) &&
        OpenProcessToken(process, TOKEN_QUERY, &b)) {
        std::array<unsigned char, 512> av{}, bv{};
        DWORD n = 0;
        if (GetTokenInformation(a, TokenUser, av.data(), static_cast<DWORD>(av.size()), &n) &&
            GetTokenInformation(b, TokenUser, bv.data(), static_cast<DWORD>(bv.size()), &n))
            result = EqualSid(reinterpret_cast<TOKEN_USER *>(av.data())->User.Sid,
                              reinterpret_cast<TOKEN_USER *>(bv.data())->User.Sid) != FALSE;
    }
    if (a)
        CloseHandle(a);
    if (b)
        CloseHandle(b);
    return result;
}
BOOL CALLBACK visible(HWND h, LPARAM data) {
    if (IsWindowVisible(h) && !IsIconic(h)) {
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        reinterpret_cast<std::set<DWORD> *>(data)->insert(pid);
    }
    return TRUE;
}
bool candidateProcess(DWORD pid, DWORD session, DWORD foreground, const std::set<DWORD> &windows) {
    if (!pid || pid == GetCurrentProcessId() || pid == foreground || windows.contains(pid))
        return false;
    DWORD found = 0;
    if (!ProcessIdToSessionId(pid, &found) || found != session)
        return false;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h)
        return false;
    wchar_t path[32768]{};
    DWORD n = 32768;
    bool ok = QueryFullProcessImageNameW(h, 0, path, &n) != FALSE;
    if (ok) {
        std::wstring full(path, n);
        std::transform(full.begin(), full.end(), full.begin(),
                       [](wchar_t c) { return wchar_t(towlower(c)); });
        auto name = full.substr(full.find_last_of(L"\\/") + 1);
        wchar_t windowsPath[MAX_PATH]{};
        GetWindowsDirectoryW(windowsPath, MAX_PATH);
        std::wstring root(windowsPath);
        std::transform(root.begin(), root.end(), root.begin(),
                       [](wchar_t c) { return wchar_t(towlower(c)); });
        root += L"\\";
        DWORD count = 0;
        const DWORD priority = GetPriorityClass(h);
        ok = !full.starts_with(root) && !sustainedExcluded(name) && !knownCompute(name) &&
             (priority == NORMAL_PRIORITY_CLASS || priority == BELOW_NORMAL_PRIORITY_CLASS ||
              priority == IDLE_PRIORITY_CLASS) &&
             GetProcessDefaultCpuSets(h, nullptr, 0, &count) && count == 0 && sameUser(h);
    }
    CloseHandle(h);
    return ok;
}
} // namespace
DWORD recover(const std::wstring &folder, DWORD expectedOwner) {
    auto path = journalPath(folder);
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0,
                           nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return GetLastError() == ERROR_FILE_NOT_FOUND ? 0 : GetLastError();
    Journal j{};
    DWORD n = 0;
    bool ok = GetFileSize(h, nullptr) == sizeof(j) && ReadFile(h, &j, sizeof(j), &n, nullptr) &&
              n == sizeof(j);
    CloseHandle(h);
    if (!ok || j.magic != 0x50534350 || j.version != 1 || j.count > MaxThreads ||
        checksum(j) != j.checksum)
        return ERROR_INVALID_DATA;
    if (expectedOwner && j.owner != expectedOwner)
        return 0;
    DWORD error = 0;
    for (DWORD i = 0; i < j.count; ++i) {
        const auto &r = j.rows[i];
        if (!r.count || r.count > MaxSets || !r.pid || !r.tid)
            return ERROR_INVALID_DATA;
    }
    for (DWORD i = 0; i < j.count; ++i) {
        DWORD e = restoreRecord(j.rows[i]);
        if (e && !error)
            error = e;
    }
    if (!error && !DeleteFileW(path.c_str()))
        error = GetLastError();
    return error;
}
struct Controller::Impl {
    struct Entry {
        HANDLE h = nullptr;
        Record record;
        Policy policy;
        uint64_t cpu = 0, at = 0;
        ~Entry() {
            if (h)
                CloseHandle(h);
        }
    };
    std::map<DWORD, std::unique_ptr<Entry>> threads;
    std::vector<ULONG> efficient;
    KAFFINITY fullMask = 0;
    uint64_t lastScan = 0, lastSample = 0, budgetStart = 0, scans = 0, reads = 0, writes = 0;
    unsigned budget = 0;
    DWORD session = 0, lastError = 0, isolatedTestPid = 0;
    bool supported = false, ownsJournal = false;
    ProcessDiscovery discovery;
    std::set<DWORD> ancestryExcluded, visibleProcesses;
    // Native enumeration is optional and read-only. Setters remain documented
    // Win32 CPU Set APIs. No syscall numbers, debug privilege or kernel driver.
    using NextThread = LONG(NTAPI *)(HANDLE, HANDLE, ACCESS_MASK, ULONG, ULONG, PHANDLE);
    NextThread nextThread = nullptr;
    Impl() {
        ProcessIdToSessionId(GetCurrentProcessId(), &session);
        nextThread = reinterpret_cast<NextThread>(
            GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtGetNextThread"));
    }
    bool journal(Entry *pending = nullptr) {
        Journal j;
        j.owner = GetCurrentProcessId();
        for (const auto &[id, e] : threads)
            if (e->policy.selected || e.get() == pending) {
                if (j.count >= MaxThreads)
                    return false;
                j.rows[j.count++] = e->record;
            }
        const bool ok = save(j);
        if (ok)
            ownsJournal = true;
        return ok;
    }
    bool release(Entry &e) {
        if (!e.policy.selected)
            return true;
        FILETIME c{}, x{}, k{}, u{};
        if (!GetThreadTimes(e.h, &c, &x, &k, &u)) {
            lastError = GetLastError();
            return false;
        }
        if (!fileTimeValue(x) && fileTimeValue(c) == e.record.created) {
            std::vector<ULONG> current;
            if (!query(e.h, current)) {
                lastError = GetLastError();
                return false;
            }
            if (current == std::vector<ULONG>(e.record.sets, e.record.sets + e.record.count)) {
                if (!SetThreadSelectedCpuSets(e.h, nullptr, 0)) {
                    lastError = GetLastError();
                    return false;
                }
                ++writes;
            }
        }
        e.policy.committed(false);
        return true;
    }
};
Controller::Controller(DWORD isolatedTestPid) : impl_(std::make_unique<Impl>()) {
    impl_->isolatedTestPid = isolatedTestPid;
}
Controller::~Controller() { release(); }
void Controller::topology(const scheduler::Topology &topology) {
    release();
    auto &p = *impl_;
    p.efficient.clear();
    p.fullMask = 0;
    p.supported = false;
    if (!topology.valid() || !topology.capabilities.heterogeneous ||
        !topology.capabilities.hasCpuSets || !p.nextThread)
        return;
    unsigned low = UINT_MAX;
    for (const auto &c : topology.cpus) {
        if (c.key.group != 0 || c.key.index >= 64)
            return;
        low = std::min(low, c.efficiency.value_or(UINT_MAX));
        p.fullMask |= KAFFINITY(1) << c.key.index;
    }
    for (const auto &c : topology.cpus)
        if (c.efficiency == low && c.cpuSetId && !(c.cpuSetFlags & 2))
            p.efficient.push_back(*c.cpuSetId);
    std::sort(p.efficient.begin(), p.efficient.end());
    p.supported = !p.efficient.empty() && p.efficient.size() <= MaxSets;
}
void Controller::release() {
    auto &p = *impl_;
    bool ok = true;
    for (auto &[id, e] : p.threads)
        if (!p.release(*e))
            ok = false;
    if (ok) {
        p.threads.clear();
        p.lastScan = 0;
        if (p.ownsJournal) {
            DeleteFileW(journalPath(appFolder()).c_str());
            p.ownsJournal = false;
        }
    } else
        p.journal();
}
void Controller::tick(uint64_t now, DWORD foreground, bool eligible, bool urgent) {
    auto &p = *impl_;
    if (!enabled || !eligible || urgent || !p.supported || p.lastError) {
        if (!p.threads.empty())
            release();
        return;
    }
    // Foreground release is event-driven, before the sample-rate gate.
    bool changed = false;
    for (auto &[id, e] : p.threads)
        if (e->record.pid == foreground && e->policy.selected) {
            p.release(*e);
            changed = true;
        }
    if (changed)
        p.journal();
    if (p.lastSample && now - p.lastSample < 1000)
        return;
    p.lastSample = now;
    if (!p.lastScan || now - p.lastScan >= 5000) {
        p.lastScan = now;
        ++p.scans;
        p.visibleProcesses.clear();
        EnumWindows(visible, reinterpret_cast<LPARAM>(&p.visibleProcesses));
        std::vector<ProcessIdentity> processes;
        if (!p.discovery.collect(processes)) {
            release();
            return;
        }
        p.ancestryExcluded.clear();
        std::map<DWORD, const ProcessIdentity *> byPid;
        for (const auto &process : processes)
            byPid[process.pid] = &process;
        for (const auto &process : processes) {
            const ProcessIdentity *parent = &process;
            for (unsigned depth = 0; depth < 32 && parent; ++depth) {
                const auto &name = parent->name;
                if (name == L"explorer.exe" || name == L"cmd.exe" || name == L"powershell.exe" ||
                    name == L"pwsh.exe" || name == L"windowsterminal.exe")
                    break;
                if (sustainedExcluded(name) || knownCompute(name)) {
                    p.ancestryExcluded.insert(process.pid);
                    break;
                }
                auto it = byPid.find(parent->parent);
                parent = it == byPid.end() || it->second == parent ? nullptr : it->second;
            }
        }
        unsigned examined = 0;
        for (const auto &process : processes) {
            if (p.threads.size() >= MaxThreads || examined >= 4096)
                break;
            if ((p.isolatedTestPid && process.pid != p.isolatedTestPid) ||
                p.ancestryExcluded.contains(process.pid) ||
                !candidateProcess(process.pid, p.session, foreground, p.visibleProcesses))
                continue;
            HANDLE ph = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, process.pid);
            if (!ph)
                continue;
            HANDLE previous = nullptr;
            while (p.threads.size() < MaxThreads && examined < 4096) {
                HANDLE h = nullptr;
                const LONG status = p.nextThread(
                    ph, previous, THREAD_QUERY_INFORMATION | THREAD_SET_LIMITED_INFORMATION, 0, 0,
                    &h);
                if (previous)
                    CloseHandle(previous);
                previous = nullptr;
                if (status != 0 || !h) {
                    if (h)
                        CloseHandle(h);
                    break;
                }
                previous = h;
                ++examined;
                const DWORD tid = GetThreadId(h);
                if (!tid || p.threads.contains(tid))
                    continue;
                FILETIME c{}, x{}, k{}, u{};
                GROUP_AFFINITY affinity{};
                std::vector<ULONG> original;
                const int priority = GetThreadPriority(h);
                if (GetProcessIdOfThread(h) != process.pid ||
                    priority == THREAD_PRIORITY_ERROR_RETURN ||
                    priority >= THREAD_PRIORITY_NORMAL || !GetThreadTimes(h, &c, &x, &k, &u) ||
                    fileTimeValue(x) || !query(h, original) || !original.empty() ||
                    !GetThreadGroupAffinity(h, &affinity) || affinity.Group != 0 ||
                    affinity.Mask != p.fullMask)
                    continue;
                HANDLE retained = nullptr;
                if (!DuplicateHandle(GetCurrentProcess(), h, GetCurrentProcess(), &retained, 0,
                                     FALSE, DUPLICATE_SAME_ACCESS))
                    continue;
                auto e = std::make_unique<Impl::Entry>();
                e->h = retained;
                e->record.pid = process.pid;
                e->record.tid = tid;
                e->record.created = fileTimeValue(c);
                e->record.count = static_cast<DWORD>(p.efficient.size());
                std::copy(p.efficient.begin(), p.efficient.end(), e->record.sets);
                e->at = now;
                e->cpu = fileTimeValue(k) + fileTimeValue(u);
                p.threads.emplace(tid, std::move(e));
            }
            if (previous)
                CloseHandle(previous);
            CloseHandle(ph);
        }
    }
    if (!p.budgetStart || now - p.budgetStart >= 60000) {
        p.budgetStart = now;
        p.budget = 0;
    }
    changed = false;
    for (auto it = p.threads.begin(); it != p.threads.end();) {
        if (p.lastError)
            break;
        auto &e = *it->second;
        FILETIME c{}, x{}, k{}, u{};
        ++p.reads;
        if (!GetThreadTimes(e.h, &c, &x, &k, &u) || fileTimeValue(x) ||
            fileTimeValue(c) != e.record.created) {
            if (!p.release(e)) {
                ++it;
                continue;
            }
            it = p.threads.erase(it);
            changed = true;
            continue;
        }
        if (now <= e.at) {
            ++it;
            continue;
        }
        const uint64_t total = fileTimeValue(k) + fileTimeValue(u);
        const bool valid = total >= e.cpu && now - e.at <= 3500;
        const double cpu =
            valid ? 100.0 * double(total - e.cpu) / (10000.0 * double(now - e.at)) : 0;
        e.cpu = total;
        e.at = now;
        const int priority = GetThreadPriority(e.h);
        const bool allowed =
            e.record.pid != foreground && !p.visibleProcesses.contains(e.record.pid) &&
            !p.ancestryExcluded.contains(e.record.pid) &&
            priority != THREAD_PRIORITY_ERROR_RETURN && priority < THREAD_PRIORITY_NORMAL;
        const auto action = e.policy.update(now, {allowed, false, valid, cpu});
        if (action == Policy::Action::Release) {
            p.release(e);
            changed = true;
        } else if (action == Policy::Action::SelectEfficient && p.budget < 8) {
            std::vector<ULONG> before;
            GROUP_AFFINITY affinity{};
            if (!candidateProcess(e.record.pid, p.session, foreground, p.visibleProcesses) ||
                !query(e.h, before) || !before.empty() || !GetThreadGroupAffinity(e.h, &affinity) ||
                affinity.Group != 0 || affinity.Mask != p.fullMask) {
                ++it;
                continue;
            }
            if (!p.journal(&e)) {
                p.lastError = GetLastError();
                ++it;
                continue;
            }
            if (SetThreadSelectedCpuSets(e.h, e.record.sets, e.record.count)) {
                // Mark owned even if subsequent verification fails so recovery can undo it.
                e.policy.committed(true);
                ++p.writes;
                ++p.budget;
                if (!matches(e.h, e.record)) {
                    p.lastError = ERROR_INVALID_STATE;
                    p.release(e);
                }
            } else
                p.lastError = GetLastError();
            changed = true;
        }
        if (!allowed && !e.policy.selected) {
            it = p.threads.erase(it);
            changed = true;
        } else
            ++it;
    }
    if (changed && !p.journal())
        p.lastError = GetLastError();
}
std::wstring Controller::status() const {
    const auto &p = *impl_;
    if (p.lastError)
        return L"Adaptive scheduler: recovery/check error " + std::to_wstring(p.lastError) +
               L"; new placements stopped";
    if (!enabled)
        return L"Adaptive scheduler: OFF";
    if (!p.supported)
        return L"Adaptive scheduler: topology unavailable; Windows scheduling";
    size_t selected = 0;
    for (const auto &[id, e] : p.threads)
        if (e->policy.selected)
            ++selected;
    return L"Adaptive scheduler: " + std::to_wstring(selected) +
           L" background threads on efficient CPU sets";
}
std::wstring Controller::report() const {
    const auto &p = *impl_;
    return status() + L"\nPlacement scans: " + std::to_wstring(p.scans) + L"\nPlacement reads: " +
           std::to_wstring(p.reads) + L"\nPlacement writes: " + std::to_wstring(p.writes) + L"\n";
}
int exercise(const std::wstring &output) {
    // Exercise only this diagnostic thread; never move unrelated user work.
    auto t = scheduler::discoverTopology();
    std::vector<ULONG> original, efficient;
    bool ok = t.valid() && query(GetCurrentThread(), original);
    unsigned low = UINT_MAX;
    for (const auto &c : t.cpus)
        low = std::min(low, c.efficiency.value_or(UINT_MAX));
    for (const auto &c : t.cpus)
        if (c.efficiency == low && c.cpuSetId && !(c.cpuSetFlags & 2))
            efficient.push_back(*c.cpuSetId);
    std::sort(efficient.begin(), efficient.end());
    bool applied = false, verified = false, restored = false;
    if (ok && t.capabilities.heterogeneous && !efficient.empty()) {
        applied = SetThreadSelectedCpuSets(GetCurrentThread(), efficient.data(),
                                           static_cast<ULONG>(efficient.size())) != FALSE;
        std::vector<ULONG> readback;
        verified = applied && query(GetCurrentThread(), readback) && readback == efficient;
        restored = SetThreadSelectedCpuSets(GetCurrentThread(),
                                            original.empty() ? nullptr : original.data(),
                                            static_cast<ULONG>(original.size())) &&
                   query(GetCurrentThread(), readback) && readback == original;
    }
    bool recovery = false, externalPreserved = false;
    if (restored && original.empty()) {
        const auto folder =
            appFolder() + L"\\placement-test-" + std::to_wstring(GetCurrentProcessId());
        CreateDirectoryW(folder.c_str(), nullptr);
        FILETIME c{}, x{}, k{}, u{};
        GetThreadTimes(GetCurrentThread(), &c, &x, &k, &u);
        Journal j;
        j.owner = GetCurrentProcessId();
        j.count = 1;
        auto &r = j.rows[0];
        r.pid = GetCurrentProcessId();
        r.tid = GetCurrentThreadId();
        r.created = fileTimeValue(c);
        r.count = static_cast<DWORD>(efficient.size());
        std::copy(efficient.begin(), efficient.end(), r.sets);
        if (save(j, folder) && SetThreadSelectedCpuSets(GetCurrentThread(), efficient.data(),
                                                        static_cast<ULONG>(efficient.size()))) {
            const DWORD result = recover(folder, j.owner);
            std::vector<ULONG> current;
            recovery = result == 0 && query(GetCurrentThread(), current) && current.empty();
        }
        std::vector<ULONG> performance;
        for (const auto &cpu : t.cpus)
            if (cpu.efficiency != low && cpu.cpuSetId && !(cpu.cpuSetFlags & 2))
                performance.push_back(*cpu.cpuSetId);
        std::sort(performance.begin(), performance.end());
        if (recovery && !performance.empty() && save(j, folder) &&
            SetThreadSelectedCpuSets(GetCurrentThread(), performance.data(),
                                     static_cast<ULONG>(performance.size()))) {
            const DWORD result = recover(folder, j.owner);
            std::vector<ULONG> current;
            externalPreserved =
                result == 0 && query(GetCurrentThread(), current) && current == performance;
        }
        SetThreadSelectedCpuSets(GetCurrentThread(), nullptr, 0);
        std::vector<ULONG> final;
        restored = query(GetCurrentThread(), final) && final.empty();
    }
    std::ofstream out{std::filesystem::path(output)};
    out << "own_thread_only=1\napplied=" << applied << "\nverified=" << verified
        << "\nrestored=" << restored << "\njournal_recovery=" << recovery
        << "\nexternal_assignment_preserved=" << externalPreserved << "\n";
    return applied && verified && restored && recovery && externalPreserved && out ? 0 : 1;
}
} // namespace pulse::placement
