#pragma once
#include <array>
#include <cstdint>
#include <string>

namespace pulse::idle {
// The only native-AUX transactions allowed in this observer are four-byte
// reads at these protocol-defined locations. No arbitrary address/action API.
inline constexpr std::array<uint32_t, 4> panelAddresses{0, 0x70, 0x170, 0x2008};
inline constexpr bool panelAddressAllowed(uint32_t address) {
  for (const auto allowed : panelAddresses)
    if (allowed == address)
      return true;
  return false;
}
struct PanelState {
  uint8_t support = 0;
  bool enabled = false;
  uint8_t status = 0;
};
inline constexpr PanelState decodePanel(uint8_t capability, uint8_t config,
                                        uint8_t status) {
  return {capability, (config & 1) != 0, uint8_t(status & 7)};
}
inline constexpr const wchar_t *panelSupport(uint8_t support) {
  switch (support) {
  case 0:
    return L"not advertised";
  case 1:
    return L"PSR1";
  case 2:
    return L"PSR2";
  case 3:
    return L"PSR2 with Y-coordinate";
  case 4:
    return L"PSR2 with Y-coordinate / early transport";
  default:
    return L"unknown capability revision";
  }
}
inline constexpr const wchar_t *panelStatus(uint8_t status) {
  switch (status) {
  case 0:
    return L"inactive";
  case 1:
    return L"transition: source synced";
  case 2:
    return L"active: sink displaying remote frame buffer";
  case 3:
    return L"transition: sink synced";
  case 4:
    return L"transition: resynchronizing";
  case 7:
    return L"sink internal error";
  default:
    return L"reserved / unknown";
  }
}
// Explicit diagnostic only. Requires confirmed DC even when the rest of the
// inventory was requested on AC. Unsupported binaries are never called.
std::wstring observePanel();
} // namespace pulse::idle
