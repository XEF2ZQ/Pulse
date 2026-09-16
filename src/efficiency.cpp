#include "efficiency.h"
#include "efficiency_policy.h"
#include <shellapi.h>
#include <sddl.h>
#include <winevt.h>
#include <wbemidl.h>
#include <taskschd.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace efficiency {
using Microsoft::WRL::ComPtr;
namespace {
constexpr wchar_t JournalKey[] = L"SOFTWARE\\Pulse\\EfficiencyOptions";
constexpr wchar_t JournalNames[2][16] = {L"WindowsUpdate", L"Realtime"};
struct Shared {
    volatile LONG pid = 0;
    volatile LONG requested = 0;
    volatile LONG stop = 0;
    volatile LONG states[2]{};
    volatile LONG errors[2]{};
    volatile LONG writes[2]{};
};
LONG read(volatile LONG &value) { return InterlockedCompareExchange(&value, 0, 0); }
std::wstring name(const std::wstring &id, const wchar_t *suffix) {
    return L"Local\\Pulse.Efficiency." + id + suffix;
}
struct Handle {
    HANDLE value = nullptr;
    ~Handle() {
        if (value)
            CloseHandle(value);
    }
};
struct Key {
    HKEY value = nullptr;
    ~Key() {
        if (value)
            RegCloseKey(value);
    }
};
struct Snapshot {
    DWORD version = 1;
    DWORD first = 0;  // service start type / DisableRealtimeMonitoring
    DWORD second = 0; // service was running
    DWORD third = 0;  // delayed auto start
};
DWORD journal(unsigned index, Snapshot &snapshot, bool write) {
    Key key;
    LSTATUS result;
    if (write) {
        result = RegCreateKeyExW(HKEY_LOCAL_MACHINE, JournalKey, 0, nullptr, 0,
                                 KEY_SET_VALUE | KEY_QUERY_VALUE, nullptr, &key.value, nullptr);
        if (result)
            return result;
        // Never overwrite an unrecovered baseline.
        DWORD size = 0;
        result = RegQueryValueExW(key.value, JournalNames[index], nullptr, nullptr, nullptr, &size);
        if (result != ERROR_FILE_NOT_FOUND)
            return result ? result : ERROR_ALREADY_EXISTS;
        result = RegSetValueExW(key.value, JournalNames[index], 0, REG_BINARY,
                                reinterpret_cast<const BYTE *>(&snapshot), sizeof(snapshot));
        if (!result)
            result = RegFlushKey(key.value);
    } else {
        result = RegOpenKeyExW(HKEY_LOCAL_MACHINE, JournalKey, 0, KEY_QUERY_VALUE, &key.value);
        if (result)
            return result;
        DWORD type = 0, size = sizeof(snapshot);
        result = RegQueryValueExW(key.value, JournalNames[index], nullptr, &type,
                                  reinterpret_cast<BYTE *>(&snapshot), &size);
        if (!result && (type != REG_BINARY || size != sizeof(snapshot) || snapshot.version != 1))
            return ERROR_INVALID_DATA;
    }
    return result;
}
DWORD clearJournal(unsigned index) {
    Key key;
    LSTATUS result = RegOpenKeyExW(HKEY_LOCAL_MACHINE, JournalKey, 0, KEY_SET_VALUE, &key.value);
    if (result)
        return result;
    result = RegDeleteValueW(key.value, JournalNames[index]);
    if (!result)
        result = RegFlushKey(key.value);
    return result;
}
bool missing(DWORD error) { return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND; }

class Update final : public Backend {
    SC_HANDLE manager_ = nullptr, service_ = nullptr;
    HMODULE library_ = nullptr;
    decltype(&SubscribeServiceChangeNotifications) subscribe_ = nullptr;
    decltype(&UnsubscribeServiceChangeNotifications) unsubscribe_ = nullptr;
    PSC_NOTIFICATION_REGISTRATION property_ = nullptr, status_ = nullptr;
    HANDLE wake_;
    static void CALLBACK changed(DWORD, void *context) { SetEvent(context); }
    DWORD open(bool control, bool watch = true) {
        if (!service_) {
            if (!manager_)
                manager_ = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
            if (!manager_)
                return GetLastError();
            const DWORD access =
                SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS |
                (control ? SERVICE_CHANGE_CONFIG | SERVICE_STOP | SERVICE_START : 0);
            service_ = OpenServiceW(manager_, L"wuauserv", access);
            if (!service_)
                return GetLastError();
        }
        if (!wake_ || !watch)
            return 0;
        if (!library_) {
            library_ = LoadLibraryExW(L"sechost.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (!library_)
                return GetLastError();
            // The SDK declares these APIs but supplies no import library.
            auto sub = GetProcAddress(library_, "SubscribeServiceChangeNotifications");
            auto unsub = GetProcAddress(library_, "UnsubscribeServiceChangeNotifications");
            static_assert(sizeof(sub) == sizeof(subscribe_));
            memcpy(&subscribe_, &sub, sizeof(sub));
            memcpy(&unsubscribe_, &unsub, sizeof(unsub));
        }
        if (!subscribe_ || !unsubscribe_)
            return ERROR_NOT_SUPPORTED;
        DWORD e = 0;
        if (!property_)
            e = subscribe_(service_, SC_EVENT_PROPERTY_CHANGE, changed, wake_, &property_);
        if (!e && !status_)
            e = subscribe_(service_, SC_EVENT_STATUS_CHANGE, changed, wake_, &status_);
        return e;
    }
    DWORD query(Snapshot &value, DWORD &state) {
        DWORD bytes = 0;
        QueryServiceConfigW(service_, nullptr, 0, &bytes);
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || !bytes)
            return GetLastError();
        std::vector<BYTE> buffer(bytes);
        auto config = reinterpret_cast<QUERY_SERVICE_CONFIGW *>(buffer.data());
        if (!QueryServiceConfigW(service_, config, bytes, &bytes))
            return GetLastError();
        SERVICE_STATUS_PROCESS status{};
        if (!QueryServiceStatusEx(service_, SC_STATUS_PROCESS_INFO,
                                  reinterpret_cast<BYTE *>(&status), sizeof(status), &bytes))
            return GetLastError();
        SERVICE_DELAYED_AUTO_START_INFO delayed{};
        if (!QueryServiceConfig2W(service_, SERVICE_CONFIG_DELAYED_AUTO_START_INFO,
                                  reinterpret_cast<BYTE *>(&delayed), sizeof(delayed), &bytes))
            return GetLastError();
        value.first = config->dwStartType;
        state = status.dwCurrentState;
        value.second = state == SERVICE_RUNNING;
        value.third = delayed.fDelayedAutostart != FALSE;
        return 0;
    }

  public:
    explicit Update(HANDLE wake) : wake_(wake) {}
    ~Update() override {
        if (property_)
            unsubscribe_(property_);
        if (status_)
            unsubscribe_(status_);
        if (service_)
            CloseServiceHandle(service_);
        if (manager_)
            CloseServiceHandle(manager_);
        if (library_)
            FreeLibrary(library_);
    }
    DWORD inspect(Snapshot &value, DWORD &state) {
        DWORD e = open(false);
        return e ? e : query(value, state);
    }
    DWORD observe(Observation &out) override {
        DWORD e = open(true);
        if (e)
            return e;
        Snapshot value;
        DWORD state = 0;
        if ((e = query(value, state)))
            return e;
        out.desired = value.first == SERVICE_DISABLED && state == SERVICE_STOPPED;
        out.pending = state == SERVICE_STOP_PENDING || state == SERVICE_START_PENDING;
        return 0;
    }
    DWORD capture() override {
        Snapshot value;
        DWORD state = 0;
        DWORD e = query(value, state);
        if (e)
            return e;
        if (state != SERVICE_STOPPED && state != SERVICE_RUNNING)
            return ERROR_BUSY;
        return journal(0, value, true);
    }
    DWORD apply() override {
        Snapshot value;
        DWORD state = 0;
        DWORD e = query(value, state);
        if (e)
            return e;
        if (value.first != SERVICE_DISABLED &&
            !ChangeServiceConfigW(service_, SERVICE_NO_CHANGE, SERVICE_DISABLED, SERVICE_NO_CHANGE,
                                  nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr))
            return GetLastError();
        if (state != SERVICE_STOPPED && state != SERVICE_STOP_PENDING) {
            SERVICE_STATUS status{};
            if (!ControlService(service_, SERVICE_CONTROL_STOP, &status)) {
                e = GetLastError();
                if (e != ERROR_SERVICE_NOT_ACTIVE)
                    return e;
            }
        }
        return 0;
    }
    DWORD restore() override {
        Snapshot value;
        DWORD e = journal(0, value, false);
        if (missing(e))
            return 0;
        if (e)
            return e;
        if (value.first < SERVICE_AUTO_START || value.first > SERVICE_DISABLED ||
            value.second > 1 || value.third > 1)
            return ERROR_INVALID_DATA;
        if ((e = open(true, false)))
            return e;
        if (!ChangeServiceConfigW(service_, SERVICE_NO_CHANGE, value.first, SERVICE_NO_CHANGE,
                                  nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr))
            return GetLastError();
        if (value.first == SERVICE_AUTO_START) {
            SERVICE_DELAYED_AUTO_START_INFO delayed{static_cast<BOOL>(value.third)};
            if (!ChangeServiceConfig2W(service_, SERVICE_CONFIG_DELAYED_AUTO_START_INFO, &delayed))
                return GetLastError();
        }
        if (value.second && value.first != SERVICE_DISABLED &&
            !StartServiceW(service_, 0, nullptr)) {
            e = GetLastError();
            if (e != ERROR_SERVICE_ALREADY_RUNNING)
                return e;
        }
        Snapshot check;
        DWORD state = 0;
        if ((e = query(check, state)))
            return e;
        if (check.first != value.first ||
            (value.first == SERVICE_AUTO_START && check.third != value.third))
            return ERROR_INVALID_STATE;
        // A previously stopped trigger-start service remains free to start after ownership is
        // released.
        return clearJournal(0);
    }
};

struct Bstr {
    BSTR value;
    explicit Bstr(const wchar_t *text) : value(SysAllocString(text)) {}
    ~Bstr() { SysFreeString(value); }
};
class Realtime final : public Backend {
    ComPtr<IWbemServices> service_;
    EVT_HANDLE subscription_ = nullptr;
    HANDLE wake_;
    volatile LONG watchError_ = 0;
    static DWORD WINAPI changed(EVT_SUBSCRIBE_NOTIFY_ACTION action, void *context,
                                EVT_HANDLE event) {
        auto *self = static_cast<Realtime *>(context);
        if (action == EvtSubscribeActionError)
            InterlockedExchange(&self->watchError_,
                                static_cast<LONG>(reinterpret_cast<ULONG_PTR>(event)));
        SetEvent(self->wake_);
        return ERROR_SUCCESS;
    }
    DWORD connect() {
        if (service_)
            return 0;
        ComPtr<IWbemLocator> locator;
        HRESULT hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&locator));
        if (FAILED(hr))
            return static_cast<DWORD>(hr);
        Bstr space(L"ROOT\\Microsoft\\Windows\\Defender");
        hr = locator->ConnectServer(space.value, nullptr, nullptr, nullptr,
                                    WBEM_FLAG_CONNECT_USE_MAX_WAIT, nullptr, nullptr, &service_);
        if (FAILED(hr))
            return static_cast<DWORD>(hr);
        hr = CoSetProxyBlanket(service_.Get(), RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                               RPC_C_AUTHN_LEVEL_PKT_PRIVACY, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr,
                               EOAC_NONE);
        if (FAILED(hr)) {
            service_.Reset();
            return static_cast<DWORD>(hr);
        }
        return 0;
    }
    DWORD boolean(const wchar_t *cls, const wchar_t *field, bool &result) {
        DWORD e = connect();
        if (e)
            return e;
        Bstr query((std::wstring(L"SELECT ") + field + L" FROM " + cls).c_str());
        Bstr language(L"WQL");
        ComPtr<IEnumWbemClassObject> enumerator;
        HRESULT hr = service_->ExecQuery(language.value, query.value,
                                         WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                                         nullptr, &enumerator);
        if (FAILED(hr))
            return static_cast<DWORD>(hr);
        ComPtr<IWbemClassObject> object;
        ULONG count = 0;
        hr = enumerator->Next(5000, 1, &object, &count);
        if (FAILED(hr))
            return static_cast<DWORD>(hr);
        if (count != 1)
            return hr == WBEM_S_TIMEDOUT ? WAIT_TIMEOUT : ERROR_NOT_FOUND;
        VARIANT value;
        VariantInit(&value);
        hr = object->Get(field, 0, &value, nullptr, nullptr);
        if (SUCCEEDED(hr) && value.vt != VT_BOOL)
            hr = WBEM_E_TYPE_MISMATCH;
        if (SUCCEEDED(hr))
            result = value.boolVal != VARIANT_FALSE;
        VariantClear(&value);
        return FAILED(hr) ? static_cast<DWORD>(hr) : 0;
    }
    DWORD set(bool disabled) {
        DWORD e = connect();
        if (e)
            return e;
        Bstr cls(L"MSFT_MpPreference"), method(L"Set");
        ComPtr<IWbemClassObject> object, definition, input;
        HRESULT hr = service_->GetObject(cls.value, 0, nullptr, &object, nullptr);
        if (SUCCEEDED(hr))
            hr = object->GetMethod(method.value, 0, &definition, nullptr);
        if (SUCCEEDED(hr))
            hr = definition->SpawnInstance(0, &input);
        VARIANT value;
        VariantInit(&value);
        value.vt = VT_BOOL;
        value.boolVal = disabled ? VARIANT_TRUE : VARIANT_FALSE;
        if (SUCCEEDED(hr))
            hr = input->Put(L"DisableRealtimeMonitoring", 0, &value, CIM_BOOLEAN);
        ComPtr<IWbemCallResult> call;
        if (SUCCEEDED(hr))
            hr = service_->ExecMethod(cls.value, method.value, WBEM_FLAG_RETURN_IMMEDIATELY,
                                      nullptr, input.Get(), nullptr, &call);
        if (FAILED(hr))
            return static_cast<DWORD>(hr);
        ComPtr<IWbemClassObject> output;
        hr = call->GetResultObject(10000, &output);
        if (hr == WBEM_S_TIMEDOUT)
            return WAIT_TIMEOUT;
        if (FAILED(hr) || !output)
            return FAILED(hr) ? static_cast<DWORD>(hr) : ERROR_INVALID_DATA;
        VariantClear(&value);
        VariantInit(&value);
        hr = output->Get(L"ReturnValue", 0, &value, nullptr, nullptr);
        DWORD result = ERROR_INVALID_DATA;
        if (SUCCEEDED(hr) && (value.vt == VT_I4 || value.vt == VT_UI4))
            result = value.ulVal;
        VariantClear(&value);
        return FAILED(hr) ? static_cast<DWORD>(hr) : result;
    }

  public:
    explicit Realtime(HANDLE wake) : wake_(wake) {}
    ~Realtime() override {
        if (subscription_)
            EvtClose(subscription_);
    }
    DWORD inspect(bool &preference, bool &enabled, bool &tamper) {
        DWORD e = boolean(L"MSFT_MpPreference", L"DisableRealtimeMonitoring", preference);
        if (!e)
            e = boolean(L"MSFT_MpComputerStatus", L"RealTimeProtectionEnabled", enabled);
        if (!e)
            e = boolean(L"MSFT_MpComputerStatus", L"IsTamperProtected", tamper);
        return e;
    }
    DWORD observe(Observation &out) override {
        if (read(watchError_))
            return static_cast<DWORD>(read(watchError_));
        if (wake_ && !subscription_) {
            subscription_ = EvtSubscribe(
                nullptr, nullptr, L"Microsoft-Windows-Windows Defender/Operational",
                L"*[System[(EventID=5000 or EventID=5001 or EventID=5007 or EventID=5013)]]",
                nullptr, this, changed, EvtSubscribeToFutureEvents);
            if (!subscription_)
                return GetLastError();
        }
        bool preference = false, enabled = true, tamper = true;
        DWORD e = inspect(preference, enabled, tamper);
        if (!e) {
            out.desired = preference && !enabled;
            out.protectedSetting = tamper;
        }
        return e;
    }
    DWORD capture() override {
        bool preference = false;
        DWORD e = boolean(L"MSFT_MpPreference", L"DisableRealtimeMonitoring", preference);
        if (e)
            return e;
        Snapshot snapshot;
        snapshot.first = preference;
        return journal(1, snapshot, true);
    }
    DWORD apply() override { return set(true); }
    DWORD restore() override {
        Snapshot snapshot;
        DWORD e = journal(1, snapshot, false);
        if (missing(e))
            return 0;
        if (e)
            return e;
        if (snapshot.first > 1 || snapshot.second || snapshot.third)
            return ERROR_INVALID_DATA;
        bool preference = false;
        if ((e = boolean(L"MSFT_MpPreference", L"DisableRealtimeMonitoring", preference)))
            return e;
        if (preference != (snapshot.first != 0)) {
            if ((e = set(snapshot.first != 0)))
                return e;
            if ((e = boolean(L"MSFT_MpPreference", L"DisableRealtimeMonitoring", preference)))
                return e;
            if (preference != (snapshot.first != 0))
                return ERROR_INVALID_STATE;
        }
        return clearJournal(1);
    }
};
const wchar_t *stateText(State state) {
    switch (state) {
    case State::Off:
        return L"Off";
    case State::Starting:
        return L"Starting";
    case State::Applied:
        return L"Verified off / watching";
    case State::Pending:
        return L"Verifying";
    case State::Blocked:
        return L"Blocked or unavailable";
    case State::RestoreFailed:
        return L"Restore needs attention";
    case State::Retry:
        return L"Waiting to retry automatically";
    }
    return L"Unknown";
}
bool elevated() {
    Handle token;
    TOKEN_ELEVATION value{};
    DWORD size = 0;
    return OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.value) &&
           GetTokenInformation(token.value, TokenElevation, &value, sizeof(value), &size) &&
           value.TokenIsElevated;
}
std::wstring imagePath(HANDLE process) {
    wchar_t path[32768];
    DWORD size = static_cast<DWORD>(std::size(path));
    return QueryFullProcessImageNameW(process, 0, path, &size) ? std::wstring(path, size) : L"";
}
std::wstring preferencePath() {
    return (std::filesystem::path(imagePath(GetCurrentProcess())).parent_path() / L"Pulse.ini")
        .wstring();
}
std::wstring userSid(HANDLE process) {
    Handle token;
    if (!OpenProcessToken(process, TOKEN_QUERY, &token.value))
        return {};
    DWORD bytes = 0;
    GetTokenInformation(token.value, TokenUser, nullptr, 0, &bytes);
    std::vector<BYTE> buffer(bytes);
    if (!GetTokenInformation(token.value, TokenUser, buffer.data(), bytes, &bytes))
        return {};
    LPWSTR text = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(buffer.data())->User.Sid, &text))
        return {};
    std::wstring result = text;
    LocalFree(text);
    return result;
}
HRESULT runInstalledTask(const std::wstring &sid, const std::wstring &session, HWND owner) {
    ComPtr<ITaskService> service;
    HRESULT hr = CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&service));
    VARIANT empty;
    VariantInit(&empty);
    if (SUCCEEDED(hr))
        hr = service->Connect(empty, empty, empty, empty);
    ComPtr<ITaskFolder> folder;
    Bstr root(L"\\");
    if (SUCCEEDED(hr))
        hr = service->GetFolder(root.value, &folder);
    ComPtr<IRegisteredTask> task;
    Bstr taskName((L"Pulse Efficiency " + sid).c_str());
    if (SUCCEEDED(hr))
        hr = folder->GetTask(taskName.value, &task);
    if (FAILED(hr))
        return hr;
    VARIANT args;
    VariantInit(&args);
    args.vt = VT_ARRAY | VT_BSTR;
    args.parray = SafeArrayCreateVector(VT_BSTR, 0, 3);
    if (!args.parray)
        return E_OUTOFMEMORY;
    std::array<std::wstring, 3> values{session, std::to_wstring(GetCurrentProcessId()),
                                       std::to_wstring(reinterpret_cast<UINT_PTR>(owner))};
    for (LONG i = 0; i < 3 && SUCCEEDED(hr); ++i) {
        Bstr value(values[i].c_str());
        hr = SafeArrayPutElement(args.parray, &i, value.value);
    }
    ComPtr<IRunningTask> running;
    if (SUCCEEDED(hr))
        hr = task->Run(args, &running);
    VariantClear(&args);
    return hr;
}
} // namespace

bool recoveryPending() {
    for (unsigned i = 0; i < 2; ++i) {
        Snapshot snapshot;
        DWORD e = journal(i, snapshot, false);
        if (!missing(e))
            return true; // unreadable/corrupt state must also be surfaced
    }
    return false;
}
DWORD Client::start(HWND owner) {
    if (process_ && WaitForSingleObject(process_, 0) == WAIT_TIMEOUT)
        return 0;
    if (shared_) {
        UnmapViewOfFile(shared_);
        shared_ = nullptr;
    }
    for (HANDLE *h : {&mapping_, &command_, &ready_, &process_})
        if (*h) {
            CloseHandle(*h);
            *h = nullptr;
        }
    GUID guid{};
    HRESULT hr = CoCreateGuid(&guid);
    if (FAILED(hr))
        return static_cast<DWORD>(hr);
    wchar_t id[40];
    StringFromGUID2(guid, id, 40);
    session_ = id;
    Handle token;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.value))
        return GetLastError();
    DWORD size = 0;
    GetTokenInformation(token.value, TokenUser, nullptr, 0, &size);
    std::vector<BYTE> data(size);
    if (!GetTokenInformation(token.value, TokenUser, data.data(), size, &size))
        return GetLastError();
    LPWSTR sid = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(data.data())->User.Sid, &sid))
        return GetLastError();
    std::wstring callerSid = sid;
    std::wstring acl = L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;" + callerSid + L")";
    LocalFree(sid);
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(acl.c_str(), SDDL_REVISION_1,
                                                              &descriptor, nullptr))
        return GetLastError();
    SECURITY_ATTRIBUTES security{sizeof(security), descriptor, FALSE};
    mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE, &security, PAGE_READWRITE, 0,
                                  sizeof(Shared), name(session_, L".data").c_str());
    DWORD e = mapping_ ? 0 : GetLastError();
    if (!e) {
        command_ = CreateEventW(&security, FALSE, FALSE, name(session_, L".command").c_str());
        if (!command_)
            e = GetLastError();
    }
    if (!e) {
        ready_ = CreateEventW(&security, TRUE, FALSE, name(session_, L".ready").c_str());
        if (!ready_)
            e = GetLastError();
    }
    LocalFree(descriptor);
    if (e)
        return e;
    shared_ = MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared));
    if (!shared_)
        return GetLastError();
    ZeroMemory(shared_, sizeof(Shared));
    hr = runInstalledTask(callerSid, session_, owner);
    if (SUCCEEDED(hr)) {
        DWORD wait = WaitForSingleObject(ready_, 10000);
        if (wait != WAIT_OBJECT_0) {
            InterlockedExchange(&static_cast<Shared *>(shared_)->stop, 1);
            SetEvent(command_);
            return wait == WAIT_TIMEOUT ? WAIT_TIMEOUT : GetLastError();
        }
        process_ = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                               static_cast<DWORD>(read(static_cast<Shared *>(shared_)->pid)));
        if (!process_) {
            InterlockedExchange(&static_cast<Shared *>(shared_)->stop, 1);
            SetEvent(command_);
            return GetLastError();
        }
        return 0;
    }
    // Portable mode remains available when the optional one-time broker setup is absent.
    if (hr != HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) &&
        hr != HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND))
        return static_cast<DWORD>(hr);
    std::wstring args = L"--efficiency-helper " + session_ + L" --parent " +
                        std::to_wstring(GetCurrentProcessId()) + L" --owner " +
                        std::to_wstring(reinterpret_cast<UINT_PTR>(owner));
    std::wstring exe = imagePath(GetCurrentProcess());
    SHELLEXECUTEINFOW launch{sizeof(launch)};
    launch.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    launch.hwnd = owner;
    launch.lpVerb = elevated() ? L"open" : L"runas";
    launch.lpFile = exe.c_str();
    launch.lpParameters = args.c_str();
    launch.nShow = SW_HIDE;
    if (!ShellExecuteExW(&launch))
        return GetLastError();
    process_ = launch.hProcess;
    return process_ ? 0 : ERROR_INVALID_HANDLE;
}
Client::~Client() {
    stop();
    if (shared_)
        UnmapViewOfFile(shared_);
    for (HANDLE h : {mapping_, command_, ready_, process_})
        if (h)
            CloseHandle(h);
}
bool Client::selected(Option option) const {
    return shared_ && (read(static_cast<Shared *>(shared_)->requested) &
                       (1L << static_cast<unsigned>(option)));
}
DWORD Client::toggle(HWND owner, Option option) {
    error_ = start(owner);
    if (error_)
        return error_;
    auto &shared = *static_cast<Shared *>(shared_);
    unsigned index = static_cast<unsigned>(option);
    LONG requested = read(shared.requested) ^ (1L << index);
    if (!WritePrivateProfileStringW(L"Preferences", L"EfficiencyOptions",
                                    std::to_wstring(requested).c_str(), preferencePath().c_str()))
        return GetLastError();
    InterlockedExchange(&shared.states[index], static_cast<LONG>(State::Starting));
    InterlockedExchange(&shared.requested, requested);
    SetEvent(command_);
    return 0;
}
DWORD Client::resume(HWND owner) {
    LONG requested = static_cast<LONG>(GetPrivateProfileIntW(L"Preferences", L"EfficiencyOptions",
                                                             0, preferencePath().c_str())) &
                     3;
    if (!requested && !recoveryPending())
        return 0;
    error_ = start(owner);
    if (error_)
        return error_;
    InterlockedExchange(&static_cast<Shared *>(shared_)->requested, requested);
    SetEvent(command_);
    return 0;
}
std::wstring Client::status(Option option) const {
    if (error_)
        return L"Not started (" + std::to_wstring(error_) + L")";
    if (!shared_)
        return error_ ? L"Not started (" + std::to_wstring(error_) + L")" : L"Off";
    const unsigned index = static_cast<unsigned>(option);
    auto &shared = *static_cast<Shared *>(shared_);
    if (process_ && WaitForSingleObject(process_, 0) == WAIT_OBJECT_0 && read(shared.requested)) {
        DWORD exit = 0;
        GetExitCodeProcess(process_, &exit);
        return L"Helper stopped (" + std::to_wstring(exit) + L"); check recovery";
    }
    std::wstring text = stateText(static_cast<State>(read(shared.states[index])));
    DWORD e = static_cast<DWORD>(read(shared.errors[index]));
    if (e == ERROR_ACCESS_DISABLED_BY_POLICY)
        text += L" — Tamper Protection";
    else if (e == ERROR_RETRY)
        text += L" — repeated external resets";
    else if (e)
        text += L" (" + std::to_wstring(e) + L")";
    return text;
}
std::wstring Client::report() const {
    std::wostringstream out;
    out << L"\nEfficiency options (persistent selections, independent of battery control)\n"
        << L"Windows Update: requested=" << selected(Option::Update) << L" "
        << status(Option::Update) << L"\n"
        << L"Realtime: requested=" << selected(Option::Realtime) << L" " << status(Option::Realtime)
        << L"\n";
    if (shared_) {
        auto &shared = *static_cast<Shared *>(shared_);
        out << L"Apply calls: update=" << read(shared.writes[0]) << L" realtime="
            << read(shared.writes[1]) << L"\n";
    }
    return out.str();
}
void Client::stop() {
    if (!shared_)
        return;
    auto &shared = *static_cast<Shared *>(shared_);
    InterlockedExchange(&shared.requested, 0);
    InterlockedExchange(&shared.stop, 1);
    if (command_)
        SetEvent(command_);
    // The helper also owns a handle to this process and restores if the controller crashes.
    if (process_)
        WaitForSingleObject(process_, 15000);
}

int helper(const std::wstring &session, DWORD parent, HWND owner) {
    GUID parsed{};
    if (session.size() != 38 || FAILED(CLSIDFromString(session.c_str(), &parsed)) || !elevated())
        return ERROR_ACCESS_DENIED;
    Handle parentProcess{
        OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, parent)};
    if (!parentProcess.value || parent == GetCurrentProcessId())
        return ERROR_INVALID_PARAMETER;
    auto ownPath = imagePath(GetCurrentProcess()), parentPath = imagePath(parentProcess.value);
    DWORD windowProcess = 0;
    GetWindowThreadProcessId(owner, &windowProcess);
    const bool sameImage = !ownPath.empty() && !_wcsicmp(ownPath.c_str(), parentPath.c_str());
    const bool brokerImage = std::filesystem::path(ownPath).filename() == L"PulseBroker.exe" &&
                             std::filesystem::path(parentPath).filename() == L"Pulse.exe";
    const auto currentSid = userSid(GetCurrentProcess());
    if ((!sameImage && !brokerImage) || windowProcess != parent || currentSid.empty() ||
        userSid(parentProcess.value) != currentSid)
        return ERROR_ACCESS_DENIED;
    Handle mapping{OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name(session, L".data").c_str())};
    Handle command{OpenEventW(SYNCHRONIZE, FALSE, name(session, L".command").c_str())};
    Handle ready{OpenEventW(EVENT_MODIFY_STATE, FALSE, name(session, L".ready").c_str())};
    if (!mapping.value || !command.value || !ready.value)
        return ERROR_INVALID_HANDLE;
    auto *shared = static_cast<Shared *>(
        MapViewOfFile(mapping.value, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared)));
    if (!shared)
        return static_cast<int>(GetLastError());
    struct View {
        void *p;
        ~View() { UnmapViewOfFile(p); }
    } view{shared};
    InterlockedExchange(&shared->pid, static_cast<LONG>(GetCurrentProcessId()));
    SetEvent(ready.value);
    Handle singleton{CreateMutexW(nullptr, FALSE, L"Global\\Pulse.Efficiency.Owner.v1")};
    if (!singleton.value)
        return static_cast<int>(GetLastError());
    DWORD lock = WaitForSingleObject(singleton.value, 0);
    if (lock != WAIT_OBJECT_0 && lock != WAIT_ABANDONED)
        return ERROR_BUSY;
    struct Unlock {
        HANDLE h;
        ~Unlock() { ReleaseMutex(h); }
    } unlock{singleton.value};
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr))
        return static_cast<int>(hr);
    struct Com {
        ~Com() { CoUninitialize(); }
    } com;
    hr = CoInitializeSecurity(nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_PKT_PRIVACY,
                              RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr);
    if (FAILED(hr))
        return static_cast<int>(hr);
    Handle updateEvent{CreateEventW(nullptr, FALSE, FALSE, nullptr)};
    Handle realtimeEvent{CreateEventW(nullptr, FALSE, FALSE, nullptr)};
    if (!updateEvent.value || !realtimeEvent.value)
        return static_cast<int>(GetLastError());
    Update update(updateEvent.value);
    Realtime realtime(realtimeEvent.value);
    std::array<Backend *, 2> backends{&update, &realtime};
    std::array<Slot, 2> slots;
    // Recovery precedes any new mutation, including after an elevated helper crash/reboot.
    for (unsigned i = 0; i < 2; ++i) {
        DWORD e = backends[i]->restore();
        if (e) {
            slots[i].owned = true;
            slots[i].state = State::RestoreFailed;
            slots[i].error = e;
            slots[i].next = GetTickCount64() + 30000;
        }
    }
    bool dirty[2]{true, true};
    bool commandChanged = true;
    auto publish = [&] {
        for (unsigned i = 0; i < 2; ++i) {
            InterlockedExchange(&shared->states[i], static_cast<LONG>(slots[i].state));
            InterlockedExchange(&shared->errors[i], static_cast<LONG>(slots[i].error));
            InterlockedExchange(&shared->writes[i], static_cast<LONG>(slots[i].writes));
        }
        PostMessageW(owner, ChangedMessage, 0, 0);
    };
    for (;;) {
        const bool stopping =
            read(shared->stop) || WaitForSingleObject(parentProcess.value, 0) != WAIT_TIMEOUT;
        const LONG requested = stopping ? 0 : read(shared->requested) & 3;
        uint64_t now = GetTickCount64();
        for (unsigned i = 0; i < 2; ++i) {
            bool wanted = (requested & (1L << i)) != 0;
            const bool retryRestore =
                (!wanted && commandChanged) ||
                (slots[i].state == State::RestoreFailed && now >= slots[i].next);
            const bool reconcile =
                wanted &&
                (dirty[i] || slots[i].state == State::Pending || slots[i].state == State::Retry) &&
                now >= slots[i].next;
            if (stopping || retryRestore || reconcile) {
                slots[i].step(wanted, now, *backends[i]);
                dirty[i] = false;
            }
            if (!wanted)
                dirty[i] = false;
        }
        commandChanged = false;
        publish();
        if (stopping)
            break;
        DWORD timeout = INFINITE;
        now = GetTickCount64();
        for (unsigned i = 0; i < 2; ++i) {
            const auto &slot = slots[i];
            if (slot.next && (slot.state == State::Pending || slot.state == State::Retry ||
                              slot.state == State::RestoreFailed || dirty[i]))
                timeout =
                    std::min(timeout, static_cast<DWORD>(slot.next > now ? slot.next - now : 0));
        }
        HANDLE waits[]{parentProcess.value, command.value, updateEvent.value, realtimeEvent.value};
        DWORD wait = WaitForMultipleObjects(4, waits, FALSE, timeout);
        if (wait == WAIT_OBJECT_0 + 1) {
            dirty[0] = dirty[1] = true;
            commandChanged = true;
        } else if (wait == WAIT_OBJECT_0 + 2)
            dirty[0] = true;
        else if (wait == WAIT_OBJECT_0 + 3)
            dirty[1] = true;
        else if (wait == WAIT_FAILED) {
            InterlockedExchange(&shared->stop, 1); // restore on wait failure
        }
    }
    return slots[0].owned || slots[1].owned ? ERROR_RECOVERY_FAILURE : 0;
}

int diagnose(const std::wstring &output) {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr))
        return static_cast<int>(hr);
    DWORD result = 0;
    {
        Update update(nullptr);
        Realtime realtime(nullptr);
        Snapshot snapshot;
        DWORD state = 0;
        DWORD ue = update.inspect(snapshot, state);
        bool preference = false, enabled = true, tamper = true;
        DWORD de = realtime.inspect(preference, enabled, tamper);
        std::ofstream file(std::filesystem::path(output), std::ios::binary);
        file << "Read-only: no policy, service or security writes.\n" << "update.error=" << ue;
        if (!ue)
            file << " start=" << snapshot.first << " state=" << state;
        file << "\ndefender.error=" << de;
        if (!de)
            file << " preferenceDisabled=" << preference << " realtimeEnabled=" << enabled
                 << " tamperProtected=" << tamper;
        file << "\nrecoveryPending=" << recoveryPending() << "\n";
        result = !file ? ERROR_WRITE_FAULT : ue ? ue : de;
    }
    CoUninitialize();
    return static_cast<int>(result);
}
int clientExercise(const std::wstring &output) {
    if (GetPrivateProfileIntW(L"Preferences", L"EfficiencyOptions", 0, preferencePath().c_str()) ||
        recoveryPending())
        return ERROR_INVALID_STATE;
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr))
        return static_cast<int>(hr);
    HWND owner = CreateWindowExW(0, L"STATIC", L"Pulse efficiency transport test", 0, 0, 0, 0, 0,
                                 HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!owner) {
        CoUninitialize();
        return static_cast<int>(GetLastError());
    }
    DWORD error = 0;
    bool applied = false, restored = false, resumed = false;
    auto settle = [&](Client &client, bool wanted) {
        for (unsigned i = 0; i < 40; ++i) {
            auto status = client.status(Option::Update);
            if (wanted ? status.find(L"Verified off") == 0 : status == L"Off")
                return true;
            MSG message;
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
                DispatchMessageW(&message);
            Sleep(250);
        }
        return false;
    };
    {
        Client client;
        error = client.toggle(owner, Option::Update);
        if (!error)
            applied = settle(client, true);
        client.stop();
        restored = !recoveryPending();
    }
    if (applied && restored) {
        Client client;
        error = client.resume(owner);
        if (!error)
            resumed = settle(client, true);
        if (client.selected(Option::Update))
            error = client.toggle(owner, Option::Update);
        restored = settle(client, false) && !recoveryPending();
        client.stop();
    }
    // Test preference is always reset, even after failure; system recovery remains journaled.
    WritePrivateProfileStringW(L"Preferences", L"EfficiencyOptions", L"0",
                               preferencePath().c_str());
    DestroyWindow(owner);
    CoUninitialize();
    std::ofstream file(std::filesystem::path(output), std::ios::binary);
    file << "Broker transport and saved-selection exercise; Update only\n"
         << "applied=" << applied << " resumed=" << resumed << " restored=" << restored
         << " error=" << error << "\n";
    return file && applied && resumed && restored && !error ? 0 : ERROR_INVALID_STATE;
}
int exercise(const std::wstring &output) {
    if (!elevated())
        return ERROR_ELEVATION_REQUIRED;
    Handle mutex{CreateMutexW(nullptr, FALSE, L"Global\\Pulse.Efficiency.Owner.v1")};
    if (!mutex.value)
        return static_cast<int>(GetLastError());
    DWORD wait = WaitForSingleObject(mutex.value, 0);
    if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED)
        return ERROR_BUSY;
    struct Unlock {
        HANDLE h;
        ~Unlock() { ReleaseMutex(h); }
    } unlock{mutex.value};
    if (recoveryPending())
        return ERROR_RECOVERY_FAILURE;
    Handle event{CreateEventW(nullptr, FALSE, FALSE, nullptr)};
    if (!event.value)
        return static_cast<int>(GetLastError());
    Update update(event.value);
    Slot slot;
    slot.step(true, GetTickCount64(), update);
    auto settle = [&] {
        for (unsigned i = 0; i < 5 && slot.state == State::Pending; ++i) {
            Sleep(2100);
            slot.step(true, GetTickCount64(), update);
        }
        return slot.state == State::Applied;
    };
    const bool applied = settle();
    bool notification = false, corrected = false;
    if (applied) {
        ResetEvent(event.value);
        SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
        SC_HANDLE service =
            manager ? OpenServiceW(manager, L"wuauserv", SERVICE_CHANGE_CONFIG) : nullptr;
        if (service && ChangeServiceConfigW(service, SERVICE_NO_CHANGE, SERVICE_DEMAND_START,
                                            SERVICE_NO_CHANGE, nullptr, nullptr, nullptr, nullptr,
                                            nullptr, nullptr, nullptr)) {
            notification = WaitForSingleObject(event.value, 5000) == WAIT_OBJECT_0;
            Sleep(2100);
            slot.step(true, GetTickCount64(), update);
            corrected = settle() && slot.writes >= 2;
        }
        if (service)
            CloseServiceHandle(service);
        if (manager)
            CloseServiceHandle(manager);
    }
    const DWORD operationError = slot.error;
    slot.step(false, GetTickCount64(), update);
    const bool restored = !slot.owned && !recoveryPending();
    std::ofstream file(std::filesystem::path(output), std::ios::binary);
    file << "Administrative Update-only exercise; Defender not modified\n"
         << "applied=" << applied << " notification=" << notification << " corrected=" << corrected
         << " restored=" << restored << " operationError=" << operationError
         << " restoreError=" << slot.error << " writes=" << slot.writes << "\n";
    return file && applied && notification && corrected && restored ? 0 : ERROR_INVALID_STATE;
}
} // namespace efficiency
