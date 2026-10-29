#include "process_placement.h"
#include "power.h"
#include <array>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace pulse::adaptive {
namespace {
constexpr unsigned MaxProcesses = 64, MaxSets = 256;
struct Handle {
  HANDLE value = nullptr;
  explicit Handle(HANDLE h = nullptr) : value(h) {}
  ~Handle() {
    if (value && value != INVALID_HANDLE_VALUE)
      CloseHandle(value);
  }
  Handle(const Handle &) = delete;
  Handle &operator=(const Handle &) = delete;
};
struct Record {
  DWORD pid = 0, count = 0;
  uint64_t created = 0;
  ULONG sets[MaxSets]{};
  DWORD qosOwned = 0, control = 0, state = 0;
};
struct Journal {
  DWORD magic = 0x41505350, version = 1, owner = 0, count = 0;
  Record rows[MaxProcesses]{};
  DWORD checksum = 0;
};
DWORD checksum(const Journal &j) {
  DWORD hash = 2166136261u;
  auto bytes = reinterpret_cast<const unsigned char *>(&j);
  for (size_t i = 0; i < offsetof(Journal, checksum); ++i)
    hash = (hash ^ bytes[i]) * 16777619u;
  return hash;
}
std::wstring journalPath(const std::wstring &folder) {
  return folder + L"\\state\\process-placement.bin";
}
bool query(HANDLE h, std::vector<ULONG> &values) {
  ULONG count = 0;
  values.resize(MaxSets);
  if (!GetProcessDefaultCpuSets(h, values.data(), MaxSets, &count) ||
      count > MaxSets)
    return false;
  values.resize(count);
  std::sort(values.begin(), values.end());
  return true;
}
bool qos(HANDLE h, PROCESS_POWER_THROTTLING_STATE &value) {
  value = {PROCESS_POWER_THROTTLING_CURRENT_VERSION, 0, 0};
  return GetProcessInformation(h, ProcessPowerThrottling, &value,
                               sizeof(value)) != FALSE;
}
DWORD restoreRecord(const Record &r, HANDLE retained = nullptr) {
  Handle opened(retained
                    ? nullptr
                    : OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION |
                                      PROCESS_SET_LIMITED_INFORMATION |
                                      PROCESS_SET_INFORMATION | SYNCHRONIZE,
                                  FALSE, r.pid));
  HANDLE h = retained ? retained : opened.value;
  if (!h)
    return GetLastError() == ERROR_INVALID_PARAMETER ? 0 : GetLastError();
  FILETIME c{}, x{}, k{}, u{};
  if (!GetProcessTimes(h, &c, &x, &k, &u))
    return GetLastError();
  if (fileTimeValue(x) || fileTimeValue(c) != r.created)
    return 0;
  DWORD error = 0;
  std::vector<ULONG> current;
  if (!query(h, current))
    error = GetLastError();
  else if (current == std::vector<ULONG>(r.sets, r.sets + r.count)) {
    if (!SetProcessDefaultCpuSets(h, nullptr, 0))
      error = GetLastError();
    else if (!query(h, current) || !current.empty())
      error = ERROR_INVALID_STATE;
  }
  if (r.qosOwned) {
    PROCESS_POWER_THROTTLING_STATE value{};
    constexpr auto bit = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    if (!qos(h, value)) {
      if (!error)
        error = GetLastError();
    } else if ((value.ControlMask & bit) && (value.StateMask & bit)) {
      value.ControlMask = (value.ControlMask & ~bit) | (r.control & bit);
      value.StateMask = (value.StateMask & ~bit) | (r.state & bit);
      if (!SetProcessInformation(h, ProcessPowerThrottling, &value,
                                 sizeof(value)) &&
          !error)
        error = GetLastError();
      PROCESS_POWER_THROTTLING_STATE actual{};
      if (!error &&
          (!qos(h, actual) || (actual.ControlMask & bit) != (r.control & bit) ||
           (actual.StateMask & bit) != (r.state & bit)))
        error = ERROR_INVALID_STATE;
    }
  }
  return error;
}
bool save(Journal &j, const std::wstring &folder) {
  j.checksum = checksum(j);
  CreateDirectoryW((folder + L"\\state").c_str(), nullptr);
  auto path = journalPath(folder), temp = path + L".tmp";
  Handle h(CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                       FILE_FLAG_WRITE_THROUGH, nullptr));
  if (h.value == INVALID_HANDLE_VALUE)
    return false;
  DWORD bytes = 0;
  if (!WriteFile(h.value, &j, sizeof(j), &bytes, nullptr) ||
      bytes != sizeof(j) || !FlushFileBuffers(h.value))
    return false;
  CloseHandle(h.value);
  h.value = nullptr;
  return MoveFileExW(temp.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) !=
         FALSE;
}
bool sameUser(HANDLE h) {
  Handle a, b;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &a.value) ||
      !OpenProcessToken(h, TOKEN_QUERY, &b.value))
    return false;
  std::array<unsigned char, 1024> av{}, bv{};
  DWORD bytes = 0;
  if (!GetTokenInformation(a.value, TokenUser, av.data(), DWORD(av.size()),
                           &bytes) ||
      !GetTokenInformation(b.value, TokenUser, bv.data(), DWORD(bv.size()),
                           &bytes))
    return false;
  return EqualSid(reinterpret_cast<TOKEN_USER *>(av.data())->User.Sid,
                  reinterpret_cast<TOKEN_USER *>(bv.data())->User.Sid) != FALSE;
}
bool browser(const std::wstring &n) {
  return n == L"chrome.exe" || n == L"msedge.exe" || n == L"brave.exe" ||
         n == L"vivaldi.exe" || n == L"thorium.exe";
}
bool protectedName(const std::wstring &n) {
  if (sustainedExcluded(n) && !browser(n))
    return true;
  for (auto value : {L"system",
                     L"registry",
                     L"smss.exe",
                     L"csrss.exe",
                     L"wininit.exe",
                     L"winlogon.exe",
                     L"services.exe",
                     L"svchost.exe",
                     L"lsass.exe",
                     L"ctfmon.exe",
                     L"msmpeng.exe",
                     L"nissrv.exe",
                     L"searchhost.exe",
                     L"sihost.exe",
                     L"fontdrvhost.exe",
                     L"mspdbsrv.exe",
                     L"clangd.exe",
                     L"rust-analyzer.exe",
                     L"vmmem.exe",
                     L"vmmemwsl.exe",
                     L"vmwp.exe",
                     L"steam.exe",
                     L"epicgameslauncher.exe"})
    if (n == value)
      return true;
  return n.find(L"anticheat") != std::wstring::npos ||
         n.find(L"battleye") != std::wstring::npos;
}
bool everyday(const std::wstring &n) {
  return browser(n) || n == L"notepad.exe" || n == L"winword.exe" ||
         n == L"excel.exe" || n == L"powerpnt.exe" || n == L"notepad++.exe";
}
bool maintenance(const std::wstring &n) {
  return n == L"onedrive.exe" || n == L"dropbox.exe";
}
BOOL CALLBACK windows(HWND h, LPARAM data) {
  if (IsWindowVisible(h)) {
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    reinterpret_cast<std::set<DWORD> *>(data)->insert(pid);
  }
  return TRUE;
}
} // namespace
DWORD recover(const std::wstring &folder, DWORD owner) {
  const auto path = journalPath(folder);
  Handle h(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                       OPEN_EXISTING, 0, nullptr));
  if (h.value == INVALID_HANDLE_VALUE)
    return GetLastError() == ERROR_FILE_NOT_FOUND ? 0 : GetLastError();
  auto j = std::make_unique<Journal>();
  DWORD bytes = 0;
  if (GetFileSize(h.value, nullptr) != sizeof(*j) ||
      !ReadFile(h.value, j.get(), sizeof(*j), &bytes, nullptr) ||
      bytes != sizeof(*j) || j->magic != 0x41505350 || j->version != 1 ||
      j->count > MaxProcesses || j->checksum != checksum(*j))
    return ERROR_INVALID_DATA;
  CloseHandle(h.value);
  h.value = nullptr;
  if (owner && j->owner != owner)
    return 0;
  std::set<DWORD> ids;
  for (DWORD i = 0; i < j->count; ++i) {
    const auto &r = j->rows[i];
    if (!r.pid || !r.created || !r.count || r.count > MaxSets ||
        r.qosOwned > 1 || !ids.insert(r.pid).second)
      return ERROR_INVALID_DATA;
  }
  DWORD error = 0;
  for (DWORD i = 0; i < j->count; ++i) {
    auto e = restoreRecord(j->rows[i]);
    if (e && !error)
      error = e;
  }
  if (!error && !DeleteFileW(path.c_str()))
    error = GetLastError();
  return error;
}
struct Controller::Impl {
  struct Entry {
    Handle h;
    DWORD pid = 0, parent = 0;
    uint64_t created = 0, cpu = 0, at = 0, born = 0, lastChange = 0,
             lastHot = 0;
    std::wstring path, name;
    Demand demand;
    Record record;
    bool owned = false, conflict = false, hasWindow = false, known = false;
    double occupancy = 0;
    Override rule = Override::Automatic;
    PTP_WAIT wait = nullptr;
    Impl *owner = nullptr;
    bool ioBusy = true;
    IO_COUNTERS io{};
    ~Entry() {
      if (wait) {
        SetThreadpoolWait(wait, nullptr, nullptr);
        WaitForThreadpoolWaitCallbacks(wait, TRUE);
        CloseThreadpoolWait(wait);
      }
    }
    static void CALLBACK exited(PTP_CALLBACK_INSTANCE, void *context, PTP_WAIT,
                                TP_WAIT_RESULT result) {
      auto &p = *static_cast<Entry *>(context)->owner;
      if (result == WAIT_OBJECT_0 && p.window && !p.wakeQueued.exchange(true))
        if (!PostMessageW(p.window, p.message, 0, 0))
          p.wakeQueued = false;
    }
  };
  std::map<DWORD, std::unique_ptr<Entry>> entries;
  ProcessDiscovery discovery;
  std::vector<ProcessIdentity> snapshot;
  std::vector<ULONG>
      queryScratch; // One allocation; reused by the GUI-thread hot loop.
  Selection selection;
  std::wstring folder = appFolder(),
               reason = L"Placement is opt-in; acceptance measurements pending";
  uint64_t scanAt = 0, sampleAt = 0, budgetAt = 0, scans = 0, reads = 0,
           writes = 0, restores = 0, conflicts = 0, skips = 0, ticks = 0;
  unsigned budget = 0;
  DWORD session = 0, error = 0, fixture = 0;
  bool compute = false;
  HWND window = nullptr;
  UINT message = 0;
  std::atomic<bool> wakeQueued{false};
  const std::vector<ProcessIdentity> *catalog = nullptr;
  Impl() {
    ProcessIdToSessionId(GetCurrentProcessId(), &session);
    snapshot.reserve(512);
    queryScratch.reserve(MaxSets);
  }
  ~Impl() {
    entries.clear();
  } // Drain wait callbacks before their owner fields die.
  bool journal() {
    auto j = std::make_unique<Journal>();
    j->owner = GetCurrentProcessId();
    for (const auto &[id, e] : entries)
      if (e->owned)
        j->rows[j->count++] = e->record;
    if (!j->count) {
      if (DeleteFileW(journalPath(folder).c_str()) ||
          GetLastError() == ERROR_FILE_NOT_FOUND)
        return true;
      return false;
    }
    return save(*j, folder);
  }
  bool release(Entry &e) {
    if (!e.owned)
      return true;
    std::vector<ULONG> current;
    if (query(e.h.value, current) &&
        current !=
            std::vector<ULONG>(e.record.sets, e.record.sets + e.record.count) &&
        !e.conflict) {
      e.conflict = true;
      ++conflicts;
    }
    const auto result = restoreRecord(e.record, e.h.value);
    if (result) {
      error = result;
      return false;
    }
    e.owned = false;
    ++restores;
    return true;
  }
  bool safe(Entry &e) {
    BOOL critical = TRUE, job = TRUE;
    PROCESS_PROTECTION_LEVEL_INFORMATION protection{};
    if (!IsProcessCritical(e.h.value, &critical) || critical ||
        !GetProcessInformation(e.h.value, ProcessProtectionLevelInfo,
                               &protection, sizeof(protection)) ||
        protection.ProtectionLevel != PROTECTION_LEVEL_NONE ||
        !IsProcessInJob(e.h.value, nullptr, &job) || (job && e.pid != fixture))
      return false;
    // Restrict to verified single-group, full-affinity applications.
    // Multi-group planning is represented, but runtime management fails open
    // pending validation.
    if (GetActiveProcessorGroupCount() != 1)
      return false;
    DWORD_PTR process = 0, system = 0;
    return GetProcessAffinityMask(e.h.value, &process, &system) &&
           process == system && process != 0;
  }
  void discover(uint64_t now, const Config &config, DWORD foreground) {
    scanAt = now;
    ++scans;
    if (catalog && !catalog->empty())
      snapshot = *catalog;
    else if (!discovery.collect(snapshot)) {
      ++skips;
      return;
    }
    std::set<DWORD> visible;
    EnumWindows(windows, reinterpret_cast<LPARAM>(&visible));
    // Foreground first; the cap cannot silently crowd it out behind
    // maintenance.
    std::stable_sort(snapshot.begin(), snapshot.end(),
                     [&](const auto &a, const auto &b) {
                       return (a.pid == foreground) > (b.pid == foreground);
                     });
    for (const auto &p : snapshot) {
      if (entries.contains(p.pid)) {
        entries[p.pid]->hasWindow = visible.contains(p.pid);
        continue;
      }
      if (entries.size() >= MaxProcesses)
        break;
      if (!p.pid || p.pid == GetCurrentProcessId() ||
          (fixture && p.pid != fixture) || protectedName(p.name))
        continue;
      DWORD found = 0;
      if (!ProcessIdToSessionId(p.pid, &found) || found != session)
        continue;
      auto e = std::make_unique<Entry>();
      e->h.value = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION |
                                   PROCESS_SET_LIMITED_INFORMATION |
                                   PROCESS_SET_INFORMATION | SYNCHRONIZE,
                               FALSE, p.pid);
      if (!e->h.value) {
        ++skips;
        continue;
      }
      e->pid = p.pid;
      e->parent = p.parent;
      e->name = p.name;
      e->hasWindow = visible.contains(p.pid);
      wchar_t path[32768];
      DWORD size = 32768;
      if (!QueryFullProcessImageNameW(e->h.value, 0, path, &size) ||
          !sameUser(e->h.value) || !safe(*e)) {
        ++skips;
        continue;
      }
      e->path = lower(std::wstring(path, size));
      wchar_t win[MAX_PATH]{};
      GetWindowsDirectoryW(win, MAX_PATH);
      if (e->path.starts_with(lower(std::wstring(win)) + L"\\"))
        continue;
      auto rule = config.rules.find(e->path);
      if (rule != config.rules.end())
        e->rule = rule->second;
      if (e->rule == Override::Untouched)
        continue;
      e->known = knownCompute(e->name);
      // Unknown software stays observed, not restricted. Browser helper roles
      // without a top-level window are never inferred from the root's name.
      std::vector<ULONG> original;
      if (!query(e->h.value, original) || !original.empty()) {
        ++skips;
        continue;
      }
      FILETIME c{}, x{}, k{}, u{};
      if (!GetProcessTimes(e->h.value, &c, &x, &k, &u) || fileTimeValue(x))
        continue;
      e->created = fileTimeValue(c);
      e->cpu = fileTimeValue(k) + fileTimeValue(u);
      e->at = e->born = now;
      GetProcessIoCounters(e->h.value, &e->io);
      e->owner = this;
      e->wait = CreateThreadpoolWait(Entry::exited, e.get(), nullptr);
      if (e->wait)
        SetThreadpoolWait(e->wait, e->h.value, nullptr);
      entries.emplace(p.pid, std::move(e));
    }
  }
};
Controller::Controller() : impl_(std::make_unique<Impl>()) {}
Controller::~Controller() { release(); }
void Controller::isolated(DWORD pid) { impl_->fixture = pid; }
void Controller::attach(HWND window, UINT message) {
  impl_->window = window;
  impl_->message = message;
}
void Controller::catalog(const std::vector<ProcessIdentity> &processes) {
  impl_->catalog = &processes;
}
void Controller::topology(const scheduler::Topology &topology) {
  release();
  impl_->selection = select(topology);
  impl_->scanAt = 0;
}
void Controller::notify() noexcept { impl_->scanAt = 0; }
void Controller::release() {
  auto &p = *impl_;
  bool changed = false;
  for (auto it = p.entries.begin(); it != p.entries.end();) {
    if (p.release(*it->second)) {
      changed = true;
      it = p.entries.erase(it);
    } else
      ++it;
  }
  if (changed && !p.journal())
    p.error = GetLastError();
  p.compute = false;
  p.scanAt = p.sampleAt = 0;
}
void Controller::tick(uint64_t now, DWORD foreground, bool eligible,
                      bool burst) {
  auto &p = *impl_;
  ++p.ticks;
  if (p.wakeQueued.exchange(false))
    p.sampleAt = 0;
  if (!enabled || !eligible || !p.selection.ready()) {
    if (!p.entries.empty())
      release();
    return;
  }
  // Bound all candidate CPU reads to at most 64 per second. Foreground rescue
  // is checked immediately and is independent of the sampling deadline.
  for (auto &[id, e] : p.entries)
    if (e->owned && id == foreground && burst &&
        config.profile != Profile::StrictFour) {
      if (p.release(*e)) {
        e->lastChange = now;
        if (!p.journal())
          p.error = GetLastError();
      }
    }
  if (p.sampleAt && now >= p.sampleAt && now - p.sampleAt < 1000)
    return;
  p.sampleAt = now;
  p.compute = false;
  bool changed = false;
  for (auto it = p.entries.begin(); it != p.entries.end();) {
    auto &e = *it->second;
    if (WaitForSingleObject(e.h.value, 0) != WAIT_TIMEOUT) {
      e.owned = false;
      it = p.entries.erase(it);
      changed = true;
    } else
      ++it;
  }
  if (!p.scanAt || now < p.scanAt || now - p.scanAt >= 5000)
    p.discover(now, config, foreground);
  if (!p.budgetAt || now - p.budgetAt >= 60000) {
    p.budgetAt = now;
    p.budget = 0;
  }
  for (auto &[id, ptr] : p.entries) {
    auto &e = *ptr;
    FILETIME c{}, x{}, k{}, u{};
    ++p.reads;
    const bool read = GetProcessTimes(e.h.value, &c, &x, &k, &u) != FALSE;
    const auto total = fileTimeValue(k) + fileTimeValue(u);
    const bool valid = read && !fileTimeValue(x) &&
                       fileTimeValue(c) == e.created && now > e.at &&
                       total >= e.cpu && now - e.at <= 6000;
    e.occupancy =
        valid ? double(total - e.cpu) / (100.0 * double(now - e.at)) : 0;
    if (now != e.at)
      e.demand.update(now, e.occupancy, valid, e.known);
    e.cpu = total;
    e.at = now;
    // I/O evidence is used only by background eligibility. A candidate that
    // loses its window gets a fresh baseline before it can be restricted.
    if (!e.hasWindow && maintenance(e.name)) {
      IO_COUNTERS io{};
      e.ioBusy = !GetProcessIoCounters(e.h.value, &io) ||
                 io.ReadTransferCount != e.io.ReadTransferCount ||
                 io.WriteTransferCount != e.io.WriteTransferCount;
      e.io = io;
    } else {
      e.ioBusy = true;
      e.io = {};
    }
    if (e.demand.compute)
      e.lastHot = now;
    // Invalid samples release placement but do not manufacture a global boost.
    if (valid && e.demand.compute && !sustainedExcluded(e.name))
      p.compute = true;
  }
  std::set<DWORD> dependencies;
  for (const auto &[id, ptr] : p.entries)
    if (ptr->demand.compute) {
      DWORD parent = ptr->parent;
      for (unsigned depth = 0; depth < 8 && parent; ++depth) {
        dependencies.insert(parent);
        auto found = p.entries.find(parent);
        if (found == p.entries.end() || found->second->parent == parent)
          break;
        parent = found->second->parent;
      }
    }
  for (auto &[id, ptr] : p.entries) {
    auto &e = *ptr;
    const DWORD priority = GetPriorityClass(e.h.value);
    const bool normal = priority == NORMAL_PRIORITY_CLASS ||
                        priority == BELOW_NORMAL_PRIORITY_CLASS ||
                        priority == IDLE_PRIORITY_CLASS;
    const bool background = !e.hasWindow &&
                            (priority == BELOW_NORMAL_PRIORITY_CLASS ||
                             priority == IDLE_PRIORITY_CLASS) &&
                            maintenance(e.name);
    Role role = Role::Unknown;
    if (e.known || e.demand.compute)
      role = Role::Compute;
    else if ((e.hasWindow && everyday(e.name)) || p.fixture == id)
      role = Role::Interactive;
    else if (background)
      role = Role::Background;
    Evidence evidence{role,
                      e.rule,
                      normal && !e.conflict,
                      id == foreground,
                      burst,
                      e.demand.compute,
                      dependencies.contains(id) ||
                          dependencies.contains(e.parent),
                      eligible};
    auto action = arbitrate(config.profile, config.background, evidence);
    if (background && (e.ioBusy || burst))
      action = Action::Restore;
    // Loss of role confidence, access or topology is an immediate release.
    if (p.error || e.conflict || now < e.born || now - e.born < 2000)
      action = Action::Restore;
    if (action == Action::Restore) {
      if (e.owned && p.release(e)) {
        changed = true;
        e.lastChange = now;
      }
      continue;
    }
    const auto &target =
        action == Action::Four ? p.selection.four : p.selection.background;
    if (e.owned) {
      auto &actual = p.queryScratch;
      if (!query(e.h.value, actual) || actual.size() != e.record.count ||
          !std::equal(actual.begin(), actual.end(), e.record.sets)) {
        e.conflict = true;
        ++p.conflicts;
        // Release our independent QoS bit if still owned; preserve CPU Sets
        // takeover.
        if (p.release(e))
          changed = true;
      }
      continue;
    }
    const uint64_t dwell =
        background && config.background == Background::Aggressive ? 5000
                                                                  : 10000;
    if (p.budget >= 16 || (e.lastChange && now - e.lastChange < dwell) ||
        (e.lastHot && now - e.lastHot < 2000))
      continue;
    auto &original = p.queryScratch;
    if (!p.safe(e) || !query(e.h.value, original) || !original.empty()) {
      e.conflict = true;
      ++p.skips;
      continue;
    }
    // Recheck live allocation before each new restriction, using target
    // process.
    ULONG bytes = 0;
    GetSystemCpuSetInformation(nullptr, 0, &bytes, e.h.value, 0);
    if (!bytes || bytes > 1024 * 1024) {
      ++p.skips;
      continue;
    }
    std::vector<unsigned char> raw(bytes);
    if (!GetSystemCpuSetInformation(
            reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(raw.data()), bytes,
            &bytes, e.h.value, 0)) {
      ++p.skips;
      continue;
    }
    std::set<ULONG> usable;
    for (size_t at = 0; at + sizeof(DWORD) * 2 <= bytes;) {
      SYSTEM_CPU_SET_INFORMATION item{};
      memcpy(&item, raw.data() + at,
             std::min(sizeof(item), size_t(bytes) - at));
      if (item.Size <
              offsetof(SYSTEM_CPU_SET_INFORMATION, CpuSet.AllocationTag) ||
          item.Size > bytes - at)
        break;
      if (item.Type == CpuSetInformation && !(item.CpuSet.AllFlags & (2u | 8u)))
        usable.insert(item.CpuSet.Id);
      at += item.Size;
    }
    if (!std::all_of(target.begin(), target.end(),
                     [&](ULONG s) { return usable.contains(s); })) {
      ++p.skips;
      continue;
    }
    Record record{};
    record.pid = id;
    record.created = e.created;
    record.count = DWORD(target.size());
    std::copy(target.begin(), target.end(), record.sets);
    PROCESS_POWER_THROTTLING_STATE before{};
    if (config.eco && background) {
      if (!qos(e.h.value, before) ||
          (before.ControlMask & PROCESS_POWER_THROTTLING_EXECUTION_SPEED)) {
        ++p.skips;
        continue;
      }
      record.qosOwned = 1;
      record.control = before.ControlMask;
      record.state = before.StateMask;
    }
    e.record = record;
    e.owned = true;
    if (!p.journal()) {
      e.owned = false;
      p.error = GetLastError();
      break;
    }
    if (!SetProcessDefaultCpuSets(e.h.value, target.data(),
                                  DWORD(target.size())))
      p.error = GetLastError();
    else {
      ++p.writes;
      ++p.budget;
      e.lastChange = now;
    }
    auto &actual = p.queryScratch;
    if (!p.error && (!query(e.h.value, actual) || actual != target))
      p.error = ERROR_INVALID_STATE;
    if (!p.error && record.qosOwned) {
      before.ControlMask |= PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
      before.StateMask |= PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
      if (!SetProcessInformation(e.h.value, ProcessPowerThrottling, &before,
                                 sizeof(before)))
        p.error = GetLastError();
      PROCESS_POWER_THROTTLING_STATE actualQos{};
      if (!p.error && (!qos(e.h.value, actualQos) ||
                       actualQos.ControlMask != before.ControlMask ||
                       actualQos.StateMask != before.StateMask))
        p.error = ERROR_INVALID_STATE;
    }
    if (p.error) {
      p.release(e);
      changed = true;
      break;
    }
  }
  if (changed && !p.journal())
    p.error = GetLastError();
  if (p.error)
    release();
}
bool Controller::computeActive() const { return impl_->compute; }
std::wstring Controller::status() const {
  const auto &p = *impl_;
  size_t count = 0;
  for (const auto &[id, e] : p.entries)
    count += e->owned;
  if (p.error)
    return L"Process placement stopped; recovery/check error " +
           std::to_wstring(p.error);
  if (!enabled)
    return L"Process placement OFF / Windows scheduling";
  if (!p.selection.ready())
    return p.selection.reason;
  return L"Process CPU Sets: " + std::to_wstring(count) +
         L" controlled; acceptance-gated experimental";
}
std::wstring Controller::report() const {
  const auto &p = *impl_;
  std::wostringstream out;
  out << status() << L"\n"
      << p.selection.reason << L"\nPhysical efficient cores: "
      << p.selection.physical << L"; selected IDs:";
  for (auto id : p.selection.four)
    out << L' ' << id;
  out << L"\nTier control: " << config.tiers << L"; process compute evidence: "
      << p.compute << L"\nPlacement tracked: " << p.entries.size()
      << L"; scans: " << p.scans << L"; reads: " << p.reads << L"; writes: "
      << p.writes << L"; restores: " << p.restores << L"\nConflicts: "
      << p.conflicts << L"; skipped: " << p.skips << L"; coordinator calls: "
      << p.ticks
      << L"\nLifecycle: coalesced process-exit waits and shared discovery / "
         L"5-second reconciliation; no kernel ETW session"
      << L"\nOEM/firmware power ceilings: externally owned; no PPT writes"
      << L"\nCPU Sets are soft requests; explicit thread sets and affinity "
         L"remain authoritative.\n";
  return out.str();
}
DWORD validateTransaction(HANDLE child, const std::wstring &folder,
                          std::wstring &report) {
  // Explicit harness entry point. Caller owns the child; never a normal control
  // path.
  if (GetFileAttributesW(journalPath(folder).c_str()) !=
      INVALID_FILE_ATTRIBUTES)
    return ERROR_ALREADY_EXISTS;
  const auto selected = select(scheduler::discoverTopology());
  if (!selected.ready())
    return ERROR_NOT_SUPPORTED;
  std::vector<ULONG> original;
  if (!query(child, original) || !original.empty())
    return ERROR_INVALID_STATE;
  FILETIME c{}, x{}, k{}, u{};
  if (!GetProcessTimes(child, &c, &x, &k, &u))
    return GetLastError();
  PROCESS_POWER_THROTTLING_STATE old{};
  if (!qos(child, old))
    return GetLastError();
  auto j = std::make_unique<Journal>();
  j->owner = GetCurrentProcessId();
  j->count = 1;
  auto &r = j->rows[0];
  r.pid = GetProcessId(child);
  r.created = fileTimeValue(c);
  r.count = DWORD(selected.four.size());
  std::copy(selected.four.begin(), selected.four.end(), r.sets);
  r.control = old.ControlMask;
  r.state = old.StateMask;
  r.qosOwned = 1;
  DWORD error = 0;
  if (!save(*j, folder))
    return GetLastError();
  if (!SetProcessDefaultCpuSets(child, r.sets, r.count))
    error = GetLastError();
  auto next = old;
  next.ControlMask |= PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
  next.StateMask |= PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
  if (!error && !SetProcessInformation(child, ProcessPowerThrottling, &next,
                                       sizeof(next)))
    error = GetLastError();
  PROCESS_POWER_THROTTLING_STATE appliedQos{};
  if (!error &&
      (!qos(child, appliedQos) || appliedQos.ControlMask != next.ControlMask ||
       appliedQos.StateMask != next.StateMask))
    error = ERROR_INVALID_STATE;
  std::vector<ULONG> current;
  if (!error && (!query(child, current) || current != selected.four))
    error = ERROR_INVALID_STATE;
  const DWORD restored = recover(folder, j->owner);
  if (!error)
    error = restored;
  PROCESS_POWER_THROTTLING_STATE actual{};
  if (!error && (!query(child, current) || !current.empty() ||
                 !qos(child, actual) || actual.ControlMask != old.ControlMask ||
                 actual.StateMask != old.StateMask))
    error = ERROR_INVALID_STATE;
  report += L"Journal CPU Sets + EcoQoS restoration: " +
            std::to_wstring(error) + L"\n";
  if (!error) {
    r.qosOwned = 0;
    if (!save(*j, folder) ||
        !SetProcessDefaultCpuSets(child, selected.efficient.data(),
                                  DWORD(selected.efficient.size())))
      error = GetLastError();
    if (!error)
      error = recover(folder, j->owner);
    if (!error && (!query(child, current) || current != selected.efficient))
      error = ERROR_INVALID_STATE;
    report += L"External CPU Sets preserved: " + std::to_wstring(error) + L"\n";
    // Only this fixture's deliberate external assignment is cleared here.
    SetProcessDefaultCpuSets(child, nullptr, 0);
  }
  if (!error) {
    ++r.created;
    if (!save(*j, folder) || !SetProcessDefaultCpuSets(child, r.sets, r.count))
      error = GetLastError();
    if (!error)
      error = recover(folder, j->owner);
    if (!error && (!query(child, current) || current != selected.four))
      error = ERROR_INVALID_STATE;
    report += L"Mismatched creation identity left untouched: " +
              std::to_wstring(error) + L"\n";
    SetProcessDefaultCpuSets(child, nullptr, 0);
  }
  return error;
}
} // namespace pulse::adaptive
