#pragma once
#include "demand_policy.h"
#include "process_discovery.h"
#include "workload.h"
#include <atomic>
#include <memory>
#include <unordered_map>
#include <unordered_set>
namespace pulse {
// Callbacks coalesce wakes only. Discovery, reduction and writes stay on the owner.
class ProductiveMonitor {
    struct ExitSignal {
        HWND window = nullptr;
        UINT message = 0;
        std::atomic<bool> queued{false};
        void notify() {
            if (window && !queued.exchange(true) && !PostMessageW(window, message, 0, 0))
                queued = false;
        }
    } signal_;
    struct Entry {
        DWORD pid = 0, parent = 0;
        uint64_t created = 0, sequence = 0, cpu = 0, at = 0, hotUntil = 0;
        HANDLE handle = nullptr;
        PTP_WAIT wait = nullptr;
        ExitSignal *signal = nullptr;
        bool known = false, excluded = false, alive = true, valid = false, fresh = false;
        double percent = 0;
        DemandPolicy policy;
        Entry *owner = nullptr;
        ~Entry() {
            if (wait) {
                SetThreadpoolWait(wait, nullptr, nullptr);
                WaitForThreadpoolWaitCallbacks(wait, TRUE);
                CloseThreadpoolWait(wait);
            }
            if (handle)
                CloseHandle(handle);
        }
        static void CALLBACK exited(PTP_CALLBACK_INSTANCE, void *context, PTP_WAIT,
                                    TP_WAIT_RESULT result) {
            if (result == WAIT_OBJECT_0)
                static_cast<Entry *>(context)->signal->notify();
        }
    };
    std::unordered_map<DWORD, std::unique_ptr<Entry>> entries_;
    std::unordered_map<DWORD, uint64_t> rejected_;
    std::vector<ProcessIdentity> snapshot_;
    std::vector<std::wstring> extra_;
    ProcessDiscovery discovery_;
    uint64_t discoveryAt_ = 0, allReadAt_ = 0, interactionUntil_ = 0, fastUntil_ = 0;
    DWORD session_ = 0, foreground_ = 0;
    static uint64_t value(FILETIME t) {
        return (uint64_t(t.dwHighDateTime) << 32) | t.dwLowDateTime;
    }
    static bool boundary(const std::wstring &n) {
        return n == L"explorer.exe" || n == L"cmd.exe" || n == L"powershell.exe" ||
               n == L"pwsh.exe" || n == L"windowsterminal.exe" || n == L"conhost.exe" ||
               n == L"openconsole.exe";
    }
    void discover(uint64_t now) {
        discoveryAt_ = now;
        ++scans;
        if (!discovery_.collect(snapshot_)) {
            ++failures;
            return;
        }
        std::unordered_set<DWORD> present, media, shells;
        std::unordered_map<DWORD, DWORD> parents;
        for (const auto &p : snapshot_) {
            present.insert(p.pid);
            parents.emplace(p.pid, p.parent);
            if (boundary(p.name))
                shells.insert(p.pid);
            else if (sustainedExcluded(p.name))
                media.insert(p.pid);
        }
        for (const auto &p : snapshot_) {
            if (!p.pid || p.pid == GetCurrentProcessId())
                continue;
            auto found = entries_.find(p.pid);
            if (found != entries_.end()) {
                auto &e = *found->second;
                if (e.alive && (!p.sequence || p.sequence == e.sequence))
                    continue;
                if (p.sequence && p.sequence == e.sequence)
                    continue;
                entries_.erase(found);
            }
            auto rejected = rejected_.find(p.pid);
            if (rejected != rejected_.end() && now < rejected->second)
                continue;
            if (entries_.size() >= 1024) {
                ++capacityDrops;
                continue;
            }
            DWORD session = 0;
            if (!ProcessIdToSessionId(p.pid, &session) || session != session_)
                continue;
            if (sustainedExcluded(p.name) || boundary(p.name))
                continue;
            bool known = knownCompute(p.name) ||
                         std::find(extra_.begin(), extra_.end(), p.name) != extra_.end();
            // A media/voice host's unknown helpers inherit its exclusion. A real
            // known compiler remains an independent candidate even under a host.
            if (!known) {
                DWORD ancestor = p.parent;
                bool excluded = false;
                for (int depth = 0; depth < 32 && ancestor; ++depth) {
                    if (shells.contains(ancestor))
                        break;
                    if (media.contains(ancestor)) {
                        excluded = true;
                        break;
                    }
                    auto a = parents.find(ancestor);
                    if (a == parents.end() || a->second == ancestor)
                        break;
                    ancestor = a->second;
                }
                if (excluded)
                    continue;
            }
            auto e = std::make_unique<Entry>();
            e->pid = p.pid;
            e->parent = p.parent;
            e->sequence = p.sequence;
            e->signal = &signal_;
            e->known = known;
            e->handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, p.pid);
            FILETIME c{}, x{}, k{}, u{};
            if (!e->handle || !GetProcessTimes(e->handle, &c, &x, &k, &u) || value(x)) {
                rejected_[p.pid] = now + 30000;
                ++accessFailures;
                continue;
            }
            e->created = value(c);
            e->cpu = value(k) + value(u);
            e->at = now;
            e->hotUntil = now + 750;
            fastUntil_ = std::max(fastUntil_, now + 750);
            DWORD priority = GetPriorityClass(e->handle);
            PROCESS_POWER_THROTTLING_STATE qos{};
            qos.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
            const bool eco =
                GetProcessInformation(e->handle, ProcessPowerThrottling, &qos, sizeof(qos)) &&
                (qos.ControlMask & qos.StateMask & PROCESS_POWER_THROTTLING_EXECUTION_SPEED);
            e->excluded = !known && (priority == IDLE_PRIORITY_CLASS || eco);
            if (!e->excluded) {
                e->wait = CreateThreadpoolWait(Entry::exited, e.get(), nullptr);
                if (e->wait)
                    SetThreadpoolWait(e->wait, e->handle, nullptr);
                else
                    ++waitFailures;
            }
            entries_.emplace(p.pid, std::move(e));
        }
        for (auto it = rejected_.begin(); it != rejected_.end();)
            if (!present.contains(it->first) || now >= it->second)
                it = rejected_.erase(it);
            else
                ++it;
        std::unordered_set<DWORD> liveParents;
        for (const auto &[pid, e] : entries_)
            if (e->alive)
                liveParents.insert(e->parent);
        for (auto it = entries_.begin(); it != entries_.end();)
            if (!it->second->alive && !liveParents.contains(it->first))
                it = entries_.erase(it);
            else
                ++it;
    }
    Entry *root(Entry &child) {
        Entry *current = &child;
        for (unsigned depth = 0; depth < 32; ++depth) {
            auto p = entries_.find(current->parent);
            if (p == entries_.end() || p->second.get() == current || p->second->excluded ||
                p->second->created > current->created)
                break;
            current = p->second.get();
        }
        return current;
    }

  public:
    uint64_t scans = 0, reads = 0, failures = 0, accessFailures = 0, capacityDrops = 0,
             waitFailures = 0;
    double cpu = 0;
    bool active = false, pending = false;
    ProductiveMonitor() {
        ProcessIdToSessionId(GetCurrentProcessId(), &session_);
        entries_.reserve(512);
        snapshot_.reserve(512);
    }
    ~ProductiveMonitor() { clear(); }
    ProductiveMonitor(const ProductiveMonitor &) = delete;
    ProductiveMonitor &operator=(const ProductiveMonitor &) = delete;
    void attach(HWND window, UINT message) {
        signal_.window = window;
        signal_.message = message;
    }
    void clear() {
        entries_.clear();
        rejected_.clear();
        discoveryAt_ = allReadAt_ = interactionUntil_ = fastUntil_ = 0;
        cpu = 0;
        active = pending = false;
        signal_.queued = false;
    }
    void refreshSoon() { discoveryAt_ = 0; }
    void foreground(DWORD pid) { foreground_ = pid; }
    void interact(DWORD pid, uint64_t now) {
        foreground_ = pid;
        // First event establishes a fresh CPU baseline; repeated slider events
        // extend the probe without repeatedly erasing its evidence window.
        if (now >= interactionUntil_) {
            auto found = entries_.find(pid);
            if (found != entries_.end()) {
                FILETIME c{}, x{}, k{}, u{};
                auto &e = *found->second;
                ++reads;
                if (GetProcessTimes(e.handle, &c, &x, &k, &u) && !value(x)) {
                    e.cpu = value(k) + value(u);
                    e.at = now;
                    e.valid = false;
                    e.percent = 0;
                }
            }
        }
        interactionUntil_ = now + 1250;
        fastUntil_ = std::max(fastUntil_, now + 1250);
    }
    bool hasTools() const { return active || pending; }
    uint64_t interval(uint64_t now) const {
        return now < interactionUntil_ ? 125 : active || pending || now < fastUntil_ ? 250 : 1000;
    }
    size_t tracked() const { return entries_.size(); }
    uint64_t nativeScans() const { return discovery_.nativeCalls; }
    void configure(const std::wstring &list) {
        extra_.clear();
        size_t start = 0;
        while (start < list.size()) {
            size_t end = list.find(L';', start);
            auto n = list.substr(start, end == std::wstring::npos ? end : end - start);
            std::transform(n.begin(), n.end(), n.begin(),
                           [](wchar_t c) { return wchar_t(towlower(c)); });
            if (!n.empty() && n.find_first_of(L"\\/: ") == std::wstring::npos)
                extra_.push_back(n);
            if (end == std::wstring::npos)
                break;
            start = end + 1;
        }
    }
    void collect(uint64_t now, bool exitWake = false) {
        signal_.queued = false;
        if (!discoveryAt_ || now - discoveryAt_ >= (active || pending ? 1000u : 3000u))
            discover(now);
        const bool all = !allReadAt_ || now - allReadAt_ >= (active || pending ? 1000u : 3000u);
        if (all)
            allReadAt_ = now;
        struct Group {
            double cpu = 0;
            bool alive = false, known = false, valid = false, incomplete = false, fresh = false,
                 interaction = false;
        };
        std::unordered_map<Entry *, Group> groups;
        groups.reserve(entries_.size());
        for (auto &[pid, ptr] : entries_) {
            auto &e = *ptr;
            e.fresh = false;
            e.owner = nullptr;
            if (e.excluded)
                continue;
            if (e.alive &&
                (all || exitWake || now < e.hotUntil || e.policy.active || e.policy.candidate() ||
                 (pid == foreground_ && now < interactionUntil_))) {
                FILETIME c{}, x{}, k{}, u{};
                ++reads;
                if (GetProcessTimes(e.handle, &c, &x, &k, &u)) {
                    const uint64_t total = value(k) + value(u);
                    e.alive = value(x) == 0;
                    if (now > e.at && total >= e.cpu) {
                        e.percent = 100.0 * double(total - e.cpu) / (10000.0 * double(now - e.at));
                        e.valid = true;
                        e.fresh = true;
                        if (e.percent >= 10)
                            e.hotUntil = now + 1000;
                    }
                    e.cpu = total;
                    e.at = now;
                } else {
                    e.valid = false;
                    ++failures;
                }
            }
            e.owner = root(e);
            // Discovery can attach a previously independent worker to its parent.
            // Only the current root owns demand; stale child evidence must not
            // keep that child's fast sampling enabled after the tree settles.
            if (e.owner != &e)
                e.policy.reset();
            auto &g = groups[e.owner];
            g.alive |= e.alive;
            g.known |= e.known;
            g.interaction |= pid == foreground_ && now < interactionUntil_;
            g.fresh |= e.fresh;
            if (e.valid && now >= e.at && now - e.at <= 500) {
                g.valid = true;
                if (e.alive)
                    g.cpu += e.percent;
            } else if (e.alive)
                g.incomplete = true;
        }
        active = pending = false;
        cpu = 0;
        // Missing members cannot prove that a tree is idle. Observed busy work
        // is still a safe lower bound; otherwise expire the bounded evidence lease.
        for (auto &[owner, g] : groups) {
            const bool valid = g.valid && (!g.incomplete || g.cpu >= (owner->policy.active ? 20.0
                                                                      : g.interaction      ? 35.0
                                                                      : g.known            ? 65.0
                                                                                           : 80.0));
            if (!g.alive)
                owner->policy.reset();
            else if (g.fresh)
                owner->policy.update(now, g.cpu, g.known, g.interaction, true, valid);
            else
                owner->policy.expire(now);
            active |= owner->policy.active;
            pending |= owner->policy.candidate();
            cpu += g.cpu;
        }
        for (auto &[pid, e] : entries_)
            if (e->owner && (e->owner->policy.active || e->owner->policy.candidate()))
                e->hotUntil = now + 1000;
        if (pending)
            fastUntil_ = now + 1000;
    }
};
} // namespace pulse
