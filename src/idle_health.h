#pragma once
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <windows.h>

namespace pulse::idle {
// No control capability is inferred from names, low clocks or activity proxies.
enum class Capability { Unavailable, Observable, Supported, OwnDriver };
struct Fadt {
  bool valid = false;
  bool aspmForbidden = false;
};
inline Fadt parseFadt(std::span<const uint8_t> bytes) {
  if (bytes.size() < 116 || std::memcmp(bytes.data(), "FACP", 4) ||
      bytes[8] < 3)
    return {};
  uint32_t length = 0;
  std::memcpy(&length, bytes.data() + 4, sizeof(length));
  if (length < 116 || length > bytes.size())
    return {};
  uint8_t checksum = 0;
  for (uint32_t i = 0; i < length; ++i)
    checksum = static_cast<uint8_t>(checksum + bytes[i]);
  if (checksum)
    return {};
  return {true, (bytes[109] & 0x10) != 0};
}
inline bool confirmedBattery(bool valid, BYTE ac) { return valid && ac == 0; }
inline std::optional<DWORD> enginePid(std::wstring_view name) {
  if (!name.starts_with(L"pid_"))
    return {};
  DWORD pid = 0;
  size_t i = 4;
  for (; i < name.size() && name[i] >= L'0' && name[i] <= L'9'; ++i) {
    const DWORD digit = static_cast<DWORD>(name[i] - L'0');
    if (pid > (MAXDWORD - digit) / 10)
      return {};
    pid = pid * 10 + digit;
  }
  if (i == 4 || !name.substr(i).starts_with(L"_luid_"))
    return {};
  return pid;
}
// All collection is explicit, synchronous and read-only. Call in a separate
// diagnostic process, never in the classifier or the window's event loop.
std::wstring inventory();
std::wstring activity(unsigned seconds, bool requireBattery, DWORD &error);
} // namespace pulse::idle
