#include "panel_observer.h"
#include <sstream>
#include <windows.h>
#ifdef PULSE_ADL_PANEL_PROBE
#include <adl_sdk.h>
#include <bcrypt.h>
#include <cstdlib>
#include <memory>
#include <vector>

namespace {
struct File {
  HANDLE value = INVALID_HANDLE_VALUE;
  ~File() {
    if (value != INVALID_HANDLE_VALUE)
      CloseHandle(value);
  }
};
struct Algorithm {
  BCRYPT_ALG_HANDLE value = nullptr;
  ~Algorithm() {
    if (value)
      BCryptCloseAlgorithmProvider(value, 0);
  }
};
struct Hash {
  BCRYPT_HASH_HANDLE value = nullptr;
  ~Hash() {
    if (value)
      BCryptDestroyHash(value);
  }
};
struct Module {
  HMODULE value = nullptr;
  ~Module() {
    if (value)
      FreeLibrary(value);
  }
};
using Destroy = int (*)(ADL_CONTEXT_HANDLE);
struct Context {
  ADL_CONTEXT_HANDLE value = nullptr;
  Destroy destroy = nullptr;
  ~Context() {
    if (value && destroy)
      destroy(value);
  }
};
bool battery() {
  SYSTEM_POWER_STATUS p{};
  return GetSystemPowerStatus(&p) && p.ACLineStatus == 0;
}
void *__stdcall allocate(int bytes) {
  return bytes > 0 ? std::malloc(static_cast<size_t>(bytes)) : nullptr;
}
bool verifiedDll(HANDLE file) {
  // Inspected ADL native-AUX implementation, RVA AA050 -> 1150A0. A driver
  // update requires renewed ABI review; never infer compatibility by export.
  constexpr std::array<unsigned char, 32> expected{
      0x0c, 0x77, 0xfa, 0xaa, 0xd7, 0x43, 0x81, 0xab, 0x81, 0x18, 0x46,
      0xea, 0x76, 0x60, 0x0a, 0x0e, 0x93, 0xe4, 0xbe, 0x64, 0x05, 0xe1,
      0xbf, 0x4c, 0xf1, 0xf0, 0x35, 0xe9, 0x35, 0xb5, 0xf8, 0xac};
  LARGE_INTEGER size{};
  if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 ||
      size.QuadPart > 64 * 1024 * 1024)
    return false;
  Algorithm algorithm;
  if (BCryptOpenAlgorithmProvider(&algorithm.value, BCRYPT_SHA256_ALGORITHM,
                                  nullptr, 0) < 0)
    return false;
  Hash hash;
  if (BCryptCreateHash(algorithm.value, &hash.value, nullptr, 0, nullptr, 0,
                       0) < 0)
    return false;
  std::array<unsigned char, 65536> buffer{};
  LONGLONG total = 0;
  for (;;) {
    DWORD read = 0;
    if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read,
                  nullptr))
      return false;
    if (!read)
      break;
    total += read;
    if (total > size.QuadPart ||
        BCryptHashData(hash.value, buffer.data(), read, 0) < 0)
      return false;
  }
  std::array<unsigned char, 32> actual{};
  return total == size.QuadPart &&
         BCryptFinishHash(hash.value, actual.data(),
                          static_cast<ULONG>(actual.size()), 0) >= 0 &&
         actual == expected;
}
struct AuxRead {
  uint32_t size = 36, status = 0, action = 0, address = 0, length = 4;
  unsigned char data[16]{};
};
static_assert(sizeof(AuxRead) == 36 && offsetof(AuxRead, data) == 20);
using Aux = int (*)(ADL_CONTEXT_HANDLE, int, int, AuxRead *);
bool readPanel(Aux read, ADL_CONTEXT_HANDLE context, int adapter, int display,
               uint32_t address, uint8_t &byte, std::wostream &out) {
  if (!pulse::idle::panelAddressAllowed(address) || !battery()) {
    out << L"Panel read refused: address or battery guard.\n";
    return false;
  }
  AuxRead request;
  request.address = address;
  const int result = read(context, adapter, display, &request);
  // The validated implementation reports transaction success as status 1.
  // Do not decode uninitialized, truncated or rejected data as 'disabled'.
  out << L"DPCD 0x" << std::hex << address << std::dec << L": result=" << result
      << L" status=" << request.status << L" length=" << request.length;
  if (result != ADL_OK || request.status != 1 || request.length != 4 ||
      request.size != 36 || request.action != 0 || request.address != address) {
    out << L"; unavailable\n";
    return false;
  }
  out << L" bytes=";
  for (unsigned i = 0; i < 4; ++i)
    out << std::hex << unsigned(request.data[i]) << L' ';
  out << std::dec << L'\n';
  byte = request.data[0];
  return true;
}
} // namespace
#endif

namespace pulse::idle {
std::wstring observePanel() {
  std::wostringstream out;
  out << L"\nA: Panel protocol readback (explicit, battery-only diagnostic)\n";
#ifndef PULSE_ADL_PANEL_PROBE
  out << L"Not built: optional official ADL SDK headers were not supplied.\n";
#else
  if (!battery()) {
    out << L"Skipped: Windows battery source not confirmed.\n";
    return out.str();
  }
  wchar_t directory[MAX_PATH]{};
  const UINT length = GetSystemDirectoryW(directory, MAX_PATH);
  if (!length || length >= MAX_PATH) {
    out << L"System library directory unavailable.\n";
    return out.str();
  }
  const std::wstring path = std::wstring(directory) + L"\\atiadlxx.dll";
  // Retain the handle without write/delete sharing through unloading, so the
  // DLL checked here cannot be replaced between hashing and loading.
  File file{CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
  if (file.value == INVALID_HANDLE_VALUE || !verifiedDll(file.value)) {
    out << L"Unavailable: ADL binary not in the reviewed SHA-256 allowlist. "
           L"No private function called.\n";
    return out.str();
  }
  Module module{
      LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)};
  if (!module.value) {
    out << L"ADL load failed: " << GetLastError() << L'\n';
    return out.str();
  }
  const auto create = reinterpret_cast<int (*)(ADL_MAIN_MALLOC_CALLBACK, int,
                                               ADL_CONTEXT_HANDLE *)>(
      GetProcAddress(module.value, "ADL2_Main_Control_Create"));
  const auto destroy = reinterpret_cast<Destroy>(
      GetProcAddress(module.value, "ADL2_Main_Control_Destroy"));
  const auto count = reinterpret_cast<int (*)(ADL_CONTEXT_HANDLE, int *)>(
      GetProcAddress(module.value, "ADL2_Adapter_NumberOfAdapters_Get"));
  const auto info =
      reinterpret_cast<int (*)(ADL_CONTEXT_HANDLE, LPAdapterInfo, int)>(
          GetProcAddress(module.value, "ADL2_Adapter_AdapterInfo_Get"));
  const auto displays = reinterpret_cast<int (*)(ADL_CONTEXT_HANDLE, int, int *,
                                                 ADLDisplayInfo **, int)>(
      GetProcAddress(module.value, "ADL2_Display_DisplayInfo_Get"));
  const auto aux = reinterpret_cast<Aux>(
      GetProcAddress(module.value, "ADL2_Display_NativeAUXChannel_Access"));
  if (!create || !destroy || !count || !info || !displays || !aux ||
      !battery()) {
    out << L"Required exports or confirmed battery source unavailable.\n";
    return out.str();
  }
  Context context{nullptr, destroy};
  if (create(allocate, 1, &context.value) != ADL_OK || !context.value) {
    out << L"ADL context unavailable.\n";
    return out.str();
  }
  int n = 0;
  if (!battery() || count(context.value, &n) != ADL_OK || n < 1 || n > 64) {
    out << L"Adapter count or battery source unavailable.\n";
    return out.str();
  }
  std::vector<AdapterInfo> adapters(static_cast<size_t>(n));
  for (auto &a : adapters)
    a.iSize = sizeof(a);
  if (!battery() ||
      info(context.value, adapters.data(),
           static_cast<int>(adapters.size() * sizeof(AdapterInfo))) != ADL_OK) {
    out << L"Adapter enumeration or battery source unavailable.\n";
    return out.str();
  }
  for (const auto &a : adapters) {
    if (!battery()) {
      out << L"Stopped: battery source no longer confirmed.\n";
      return out.str();
    }
    int number = 0;
    ADLDisplayInfo *raw = nullptr;
    const int result =
        displays(context.value, a.iAdapterIndex, &number, &raw, 0);
    std::unique_ptr<ADLDisplayInfo, decltype(&std::free)> list(raw, &std::free);
    if (result != ADL_OK || !list || number < 1 || number > 64)
      continue;
    for (int i = 0; i < number; ++i) {
      const auto &d = raw[i];
      if (!(d.iDisplayInfoValue & ADL_DISPLAY_DISPLAYINFO_DISPLAYCONNECTED) ||
          d.iDisplayConnector != ADL_DISPLAY_CONTYPE_EDP)
        continue;
      out << L"Connected eDP: adapter=" << a.iAdapterIndex << L" display="
          << d.displayID.iDisplayLogicalIndex << L'\n';
      std::array<uint8_t, 4> data{};
      for (size_t j = 0; j < panelAddresses.size(); ++j)
        if (!readPanel(aux, context.value, a.iAdapterIndex,
                       d.displayID.iDisplayLogicalIndex, panelAddresses[j],
                       data[j], out))
          return out.str();
      if (!battery()) {
        out << L"Snapshot invalid: power source changed during readback.\n";
        return out.str();
      }
      const auto state = decodePanel(data[1], data[2], data[3]);
      out << L"DPCD revision: 0x" << std::hex << unsigned(data[0]) << std::dec
          << L"\nPSR capability: " << panelSupport(state.support)
          << L"\nPSR enabled in sink configuration: "
          << (state.enabled ? L"yes" : L"no")
          << L"\nPSR instantaneous sink status: " << panelStatus(state.status)
          << L"\nNot a residency measurement. AUX access may disturb idle. "
             L"Panel Replay remains unknown. No panel writes or state "
             L"forcing.\n";
      return out
          .str(); // One physical-panel snapshot, not duplicate ADL mappings.
    }
  }
  out << L"No connected eDP reported by ADL; no private query called.\n";
#endif
  return out.str();
}
} // namespace pulse::idle
