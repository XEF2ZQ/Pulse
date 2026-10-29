#include "power_source.h"
#include <atomic>
#include <new>
#include <sstream>
#include <wbemidl.h>

namespace pulse::supply {
class Monitor::Sink final : public IWbemObjectSink {
  std::atomic<ULONG> references_{1};

public:
  std::atomic<HWND> owner{nullptr};
  ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
  ULONG STDMETHODCALLTYPE Release() override {
    const ULONG count = --references_;
    if (!count)
      delete this;
    return count;
  }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) override {
    if (!out)
      return E_POINTER;
    *out = nullptr;
    if (id != IID_IUnknown && id != IID_IWbemObjectSink)
      return E_NOINTERFACE;
    *out = static_cast<IWbemObjectSink *>(this);
    AddRef();
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE Indicate(LONG, IWbemClassObject **) override {
    if (const HWND h = owner.load())
      PostMessageW(h, ChangedMessage, 0, 0);
    return WBEM_S_NO_ERROR;
  }
  HRESULT STDMETHODCALLTYPE SetStatus(LONG flags, HRESULT result, BSTR,
                                      IWbemClassObject *) override {
    if (flags == WBEM_STATUS_COMPLETE && FAILED(result))
      if (const HWND h = owner.load())
        PostMessageW(h, ChangedMessage, 1, static_cast<LPARAM>(result));
    return WBEM_S_NO_ERROR;
  }
};
void Monitor::subscribe() {
  if (services_ || !evidence_.asusValid)
    return;
  IWbemLocator *locator = nullptr;
  eventError_ =
      CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
                       IID_IWbemLocator, reinterpret_cast<void **>(&locator));
  if (FAILED(eventError_))
    return;
  BSTR space = SysAllocString(L"ROOT\\WMI");
  if (!space) {
    locator->Release();
    eventError_ = E_OUTOFMEMORY;
    return;
  }
  eventError_ = locator->ConnectServer(space, nullptr, nullptr, nullptr,
                                       WBEM_FLAG_CONNECT_USE_MAX_WAIT, nullptr,
                                       nullptr, &services_);
  SysFreeString(space);
  locator->Release();
  if (SUCCEEDED(eventError_))
    eventError_ =
        CoSetProxyBlanket(services_, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE,
                          nullptr, RPC_C_AUTHN_LEVEL_PKT_PRIVACY,
                          RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
  if (SUCCEEDED(eventError_)) {
    sink_ = new (std::nothrow) Sink;
    if (!sink_)
      eventError_ = E_OUTOFMEMORY;
    else {
      sink_->owner = owner_;
      BSTR language = SysAllocString(L"WQL");
      BSTR query =
          SysAllocString(L"SELECT * FROM AsusAtkWmiEvent WHERE EventID = 123");
      eventError_ =
          language && query
              ? services_->ExecNotificationQueryAsync(
                    language, query, WBEM_FLAG_SEND_STATUS, nullptr, sink_)
              : E_OUTOFMEMORY;
      SysFreeString(query);
      SysFreeString(language);
    }
  }
  if (FAILED(eventError_)) {
    if (sink_) {
      sink_->owner = nullptr;
      sink_->Release();
      sink_ = nullptr;
    }
    if (services_) {
      services_->Release();
      services_ = nullptr;
    }
  }
}
void Monitor::start(HWND owner) {
  stop();
  owner_ = owner;
  refresh();
  subscribe();
}
void Monitor::stop() {
  if (owner_) {
    KillTimer(owner_, RefreshTimer);
    KillTimer(owner_, SettleTimer);
  }
  scheduled_ = false;
  if (sink_)
    sink_->owner = nullptr;
  if (services_ && sink_)
    services_->CancelAsyncCall(sink_);
  if (sink_) {
    sink_->Release();
    sink_ = nullptr;
  }
  if (services_) {
    services_->Release();
    services_ = nullptr;
  }
  if (asus_ != INVALID_HANDLE_VALUE)
    CloseHandle(asus_);
  asus_ = INVALID_HANDLE_VALUE;
  owner_ = nullptr;
  evidence_ = {};
}
void Monitor::refresh() {
  evidence_ = {};
  if (asus_ == INVALID_HANDLE_VALUE)
    asus_ =
        CreateFileW(L"\\\\.\\ATKACPI", 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    nullptr, OPEN_EXISTING, 0, nullptr);
  if (asus_ == INVALID_HANDLE_VALUE) {
    readError_ = GetLastError();
    return;
  }
  // Protocol constants describe the ASUS read-only charger query. No firmware
  // setter exists here.
  DWORD request[4]{0x53545344, 8, 0x0012006c, 0}, response[4]{}, bytes = 0;
  ++reads_;
  if (!DeviceIoControl(asus_, 0x0022240c, request, sizeof(request), response,
                       sizeof(response), &bytes, nullptr)) {
    readError_ = GetLastError();
    CloseHandle(asus_);
    asus_ = INVALID_HANDLE_VALUE;
    return;
  }
  evidence_ = parseAsus(response[0], bytes);
  readError_ = evidence_.asusValid ? ERROR_SUCCESS : ERROR_NOT_SUPPORTED;
}
void Monitor::notify(bool settle) {
  if (!owner_)
    return;
  ++events_;
  if (!scheduled_)
    scheduled_ =
        SetCoalescableTimer(owner_, RefreshTimer, 150, nullptr, 100) != 0;
  if (settle)
    SetCoalescableTimer(owner_, SettleTimer, 2000, nullptr, 100);
}
void Monitor::timer(UINT_PTR id) {
  KillTimer(owner_, id);
  if (id == RefreshTimer)
    scheduled_ = false;
  refresh();
  if (!services_)
    subscribe();
}
Decision Monitor::decide(bool valid, const SYSTEM_POWER_STATUS &windows) const {
  // Do not retain an ASUS override without a live disconnect notification
  // subscription.
  return classify(valid, windows,
                  SUCCEEDED(eventError_) ? evidence_ : Evidence{});
}
std::wstring Monitor::report() const {
  std::wostringstream out;
  out << L"\nCharger monitor: ASUS read-only / event driven\nASUS charger "
         L"valid: "
      << evidence_.asusValid << L"; mode: " << evidence_.asusMode
      << L"; error: " << readError_ << L"\nASUS notification HRESULT: "
      << static_cast<unsigned long>(eventError_) << L"\nCharger reads: "
      << reads_ << L"; event requests: " << events_ << L'\n';
  return out.str();
}
} // namespace pulse::supply
