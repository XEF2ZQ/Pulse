// Research diagnostic only: fixed voltage-curve queries, no offset setter.
// Firmware 0x0B5D0B00 disassembly shows 0xAF is NOT an offset getter.
#include "scheduler_topology.h"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <intrin.h>
#include <iomanip>
#include <map>
#include <string>
#include <vector>
#include <wincrypt.h>
#include <windows.h>

namespace {
struct CryptoResources {
  HCRYPTPROV provider = 0;
  HCRYPTHASH hash = 0;
  ~CryptoResources() {
    if (hash)
      CryptDestroyHash(hash);
    if (provider)
      CryptReleaseContext(provider, 0);
  }
};
struct Library {
  HMODULE value = nullptr;
  ~Library() {
    if (value)
      FreeLibrary(value);
  }
};
using Close = HRESULT(WINAPI *)(HANDLE);
struct Device {
  HANDLE value = nullptr;
  Close close = nullptr;
  ~Device() {
    if (value && close)
      close(value);
  }
};
struct MailboxLock {
  HANDLE value = nullptr;
  bool owned = false;
  ~MailboxLock() {
    if (owned)
      ReleaseMutex(value);
    if (value)
      CloseHandle(value);
  }
};
} // namespace

int wmain(int argc, wchar_t **argv) {
  if (argc != 3)
    return 2; // signed module path, output report
  std::ofstream out{std::filesystem::path(argv[2])};
  if (!out)
    return 3;
  out << "Pulse Strix voltage-curve diagnostic; no offset setter\n";
  int r[4]{};
  __cpuid(r, 0);
  char vendor[13]{};
  memcpy(vendor, r + 1, 4);
  memcpy(vendor + 4, r + 3, 4);
  memcpy(vendor + 8, r + 2, 4);
  __cpuid(r, 1);
  const unsigned a = static_cast<unsigned>(r[0]);
  const unsigned family = ((a >> 8) & 15) + ((a >> 20) & 255),
                 model = ((a >> 4) & 15) | ((a >> 12) & 240);
  out << "vendor=" << vendor << " family=" << family << " model=" << model
      << "\n";
  if (strcmp(vendor, "AuthenticAMD") || family != 26 || model != 36) {
    out << "Unsupported CPU; no hardware access\n";
    return 4;
  }
  __cpuid(r, static_cast<int>(0x80000000u));
  if (static_cast<unsigned>(r[0]) < 0x8000001Eu)
    return 4;
  auto topology = pulse::scheduler::discoverTopology();
  if (!topology.valid())
    return 5;
  std::map<unsigned, unsigned> physical;
  for (const auto &cpu : topology.cpus) {
    if (cpu.key.group || cpu.key.index >= 64)
      return 6;
    GROUP_AFFINITY next{}, old{};
    next.Mask = KAFFINITY(1) << cpu.key.index;
    if (!SetThreadGroupAffinity(GetCurrentThread(), &next, &old))
      return 7;
    __cpuid(r, static_cast<int>(0x8000001Eu));
    const unsigned core = static_cast<unsigned>(r[1]) & 255;
    out << "logical=" << unsigned(cpu.key.index)
        << " apic=" << static_cast<unsigned>(r[0]) << " core=" << core
        << " class=" << unsigned(cpu.efficiency.value_or(255)) << "\n";
    if (!SetThreadGroupAffinity(GetCurrentThread(), &old, nullptr))
      return 8;
    if (core >= 16)
      return 9;
    const unsigned cls = cpu.efficiency.value_or(255);
    if (physical.contains(core) && physical[core] != cls)
      return 10;
    physical[core] = cls;
  }
  std::ifstream file{std::filesystem::path(argv[1]),
                     std::ios::binary | std::ios::ate};
  if (!file)
    return 11;
  const auto size = file.tellg();
  if (size <= 0 || size > 1024 * 1024)
    return 11;
  std::vector<unsigned char> blob(static_cast<size_t>(size));
  file.seekg(0);
  if (!file.read(reinterpret_cast<char *>(blob.data()),
                 static_cast<std::streamsize>(blob.size())))
    return 11;
  CryptoResources crypto;
  BYTE digest[32]{};
  DWORD length = 32;
  if (!CryptAcquireContextW(&crypto.provider, nullptr, nullptr, PROV_RSA_AES,
                            CRYPT_VERIFYCONTEXT) ||
      !CryptCreateHash(crypto.provider, CALG_SHA_256, 0, 0, &crypto.hash) ||
      !CryptHashData(crypto.hash, blob.data(), static_cast<DWORD>(blob.size()),
                     0) ||
      !CryptGetHashParam(crypto.hash, HP_HASHVAL, digest, &length, 0) ||
      length != 32)
    return 12;
  const BYTE expected[32] = {0xB8, 0x4E, 0xCA, 0x7F, 0x32, 0xC6, 0x3B, 0x3D,
                             0x8C, 0x14, 0xB2, 0xC6, 0xD4, 0x54, 0x82, 0x70,
                             0x6D, 0xF8, 0x68, 0x3A, 0xA6, 0xF4, 0x3E, 0xB8,
                             0xBE, 0xAD, 0x9D, 0xC6, 0x21, 0x81, 0xD3, 0x8F};
  if (memcmp(digest, expected, 32)) {
    out << "Module hash mismatch\n";
    return 13;
  }
  Library library{LoadLibraryExW(
      L"C:\\Program Files\\PawnIO\\PawnIOLib.dll", nullptr,
      LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32)};
  auto dll = library.value;
  if (!dll) {
    out << "DLL error=" << GetLastError() << "\n";
    return 14;
  }
  using Open = HRESULT(WINAPI *)(PHANDLE);
  using Load = HRESULT(WINAPI *)(HANDLE, const unsigned char *, SIZE_T);
  using Execute = HRESULT(WINAPI *)(HANDLE, PCSTR, const ULONG64 *, SIZE_T,
                                    PULONG64, SIZE_T, PSIZE_T);
  auto open = reinterpret_cast<Open>(GetProcAddress(dll, "pawnio_open"));
  auto load = reinterpret_cast<Load>(GetProcAddress(dll, "pawnio_load"));
  auto execute =
      reinterpret_cast<Execute>(GetProcAddress(dll, "pawnio_execute"));
  auto close = reinterpret_cast<Close>(GetProcAddress(dll, "pawnio_close"));
  if (!open || !load || !execute || !close)
    return 15;
  Device owner{nullptr, close};
  HRESULT hr = open(&owner.value);
  auto device = owner.value;
  out << "open=0x" << std::hex << static_cast<unsigned>(hr) << std::dec << "\n";
  out.flush();
  if (FAILED(hr))
    return 16;
  hr = load(device, blob.data(), blob.size());
  out << "load=0x" << std::hex << static_cast<unsigned>(hr) << std::dec << "\n";
  if (FAILED(hr))
    return 17;
  MailboxLock lock{CreateMutexW(nullptr, FALSE, L"Global\\Access_PCI"), false};
  auto mutex = lock.value;
  if (!mutex)
    return 18;
  const DWORD wait = WaitForSingleObject(mutex, 2000);
  if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED)
    return 19;
  lock.owned = true;
  ULONG64 result[6]{};
  SIZE_T count = 0;
  hr = execute(device, "ioctl_get_code_name", nullptr, 0, result, 1, &count);
  out << "codename=" << result[0] << " hr=0x" << std::hex
      << static_cast<unsigned>(hr) << std::dec << "\n";
  bool okay = SUCCEEDED(hr) && count == 1 && result[0] == 31;
  if (okay) {
    count = 0;
    hr =
        execute(device, "ioctl_get_smu_version", nullptr, 0, result, 1, &count);
    out << "smu_version=0x" << std::hex << result[0] << " hr=0x"
        << static_cast<unsigned>(hr) << std::dec << "\n";
    okay = SUCCEEDED(hr) && count == 1 && result[0] == 0x0B5D0B00;
  }
  if (!okay) {
    out << "Firmware identity mismatch; no curve queries\n";
    return 21;
  }
  // Use the reverse-engineered upstream Strix RSMU mailbox explicitly.
  // Different signed module revisions may route their generic command helper
  // differently.
  auto readRegister = [&](ULONG64 address, ULONG64 &value) {
    SIZE_T n = 0;
    return SUCCEEDED(execute(device, "ioctl_read_smu_register", &address, 1,
                             &value, 1, &n)) &&
           n == 1;
  };
  auto writeRegister = [&](ULONG64 address, ULONG64 value) {
    const ULONG64 input[2] = {address, value};
    SIZE_T n = 0;
    return SUCCEEDED(
        execute(device, "ioctl_write_smu_register", input, 2, nullptr, 0, &n));
  };
  auto readCurve = [&](unsigned message, unsigned argument,
                       ULONG64(&values)[6]) {
    if (message != 0xAF && message != 0xC3)
      return false;
    constexpr ULONG64 command = 0x03B10A20, response = 0x03B10A80,
                      arguments = 0x03B10A88;
    ULONG64 state = 0;
    auto start = GetTickCount64();
    do {
      if (!readRegister(response, state))
        return false;
      if (state)
        break;
      Sleep(1);
    } while (GetTickCount64() - start < 100);
    if (!state || !writeRegister(response, 0))
      return false;
    for (unsigned i = 0; i < 6; ++i)
      if (!writeRegister(arguments + 4 * i, i ? 0 : argument))
        return false;
    // Verify request arguments before issuing a query, catching transport
    // mixups.
    for (unsigned i = 0; i < 6; ++i) {
      ULONG64 actual = 0;
      if (!readRegister(arguments + 4 * i, actual) ||
          actual != (i ? 0 : argument))
        return false;
    }
    if (!writeRegister(command, message))
      return false;
    start = GetTickCount64();
    state = 0;
    do {
      if (!readRegister(response, state))
        return false;
      if (state)
        break;
      Sleep(1);
    } while (GetTickCount64() - start < 100);
    out << "raw_mailbox_status=" << state << " ";
    if (state != 1)
      return false;
    ULONG64 actualCommand = 0;
    if (!readRegister(command, actualCommand) || actualCommand != message)
      return false;
    for (unsigned i = 0; i < 6; ++i)
      if (!readRegister(arguments + 4 * i, values[i]))
        return false;
    return true;
  };
  for (const auto &[core, cls] : physical) {
    if (!okay)
      break;
    // These query selectors differ from the setter: complex in [31:28],
    // slot in [27:24], temperature in [23:16], curve input in [15:0].
    // CPUID-to-complex correspondence is still under investigation.
    const unsigned selector = ((core / 8) << 28) | ((core % 8) << 24);
    for (const auto &query : {std::pair{0xAFu, 0u},
                              {0xAFu, 1000u},
                              {0xAFu, 3000u},
                              {0xC3u, 800u},
                              {0xC3u, 1000u}}) {
      if (!okay)
        break;
      count = 0;
      memset(result, 0, sizeof(result));
      const unsigned argument = selector | query.second;
      hr = readCurve(query.first, argument, result) ? S_OK : E_FAIL;
      count = SUCCEEDED(hr) ? 6 : 0;
      out << "core=" << core << " class=" << cls << " hr=0x" << std::hex
          << static_cast<unsigned>(hr) << " command=0x" << query.first
          << " argument=0x" << argument << " raw=0x" << result[0] << std::dec
          << " count=" << count << " curve_result=" << result[0] << "\n";
      out.flush();
      out << "all_arguments=";
      for (auto word : result)
        out << " 0x" << std::hex << word;
      out << std::dec << "\n";
      okay = SUCCEEDED(hr) && count == 6;
    }
  }
  out << "transport_complete=" << okay << "\nsemantic_readback_verified=0\n";
  // This research tool cannot establish selector identity or offset semantics.
  // Never let plausible-looking data qualify a production undervolt backend.
  return 20;
}
