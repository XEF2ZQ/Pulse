#include "idle_health.h"
#include "panel_observer.h"
#include <array>
#include <iostream>
#include <vector>
using namespace pulse::idle;
int main() {
  unsigned checks = 0, errors = 0;
  auto check = [&](bool ok) {
    ++checks;
    if (!ok) {
      ++errors;
      std::cerr << "Failed " << checks << '\n';
    }
  };
  std::vector<uint8_t> bytes(276, 0);
  std::memcpy(bytes.data(), "FACP", 4);
  const uint32_t length = static_cast<uint32_t>(bytes.size());
  std::memcpy(bytes.data() + 4, &length, 4);
  bytes[8] = 6;
  auto checksum = [&] {
    bytes[9] = 0;
    uint8_t sum = 0;
    for (auto byte : bytes)
      sum = static_cast<uint8_t>(sum + byte);
    bytes[9] = static_cast<uint8_t>(0 - sum);
  };
  checksum();
  check(parseFadt(bytes).valid && !parseFadt(bytes).aspmForbidden);
  bytes[109] = 0x10;
  checksum();
  check(parseFadt(bytes).valid && parseFadt(bytes).aspmForbidden);
  for (size_t i = 0; i < bytes.size(); ++i)
    check(!parseFadt(std::span(bytes).first(i)).valid);
  bytes[9] ^= 1;
  check(!parseFadt(bytes).valid);
  checksum();
  bytes[8] = 1;
  checksum();
  check(!parseFadt(bytes).valid);
  bytes[8] = 6;
  bytes[0] = 'X';
  checksum();
  check(!parseFadt(bytes).valid);
  bytes[0] = 'F';
  bytes[7] = 0xff;
  checksum();
  check(!parseFadt(bytes).valid);
  check(!confirmedBattery(false, 0));
  check(confirmedBattery(true, 0));
  check(!confirmedBattery(true, 1));
  check(!confirmedBattery(true, 255));
  check(enginePid(L"pid_42_luid_0x00000000_0x123_phys_0_eng_0_engtype_3D") ==
        DWORD{42});
  check(enginePid(L"pid_4294967295_luid_x") == MAXDWORD);
  for (auto name :
       {L"pid_4294967296_luid_x", L"pid_-1_luid_x", L"pid_1x_luid_x",
        L"pid__luid_x", L"pid_4", L"pid_4_eng_x", L"", L"luid_1_pid_42"})
    check(!enginePid(name));
  for (uint32_t address = 0; address <= 0x2100; ++address)
    check(panelAddressAllowed(address) ==
          (address == 0 || address == 0x70 || address == 0x170 ||
           address == 0x2008));
  check(!panelAddressAllowed(UINT32_MAX));
  for (unsigned value = 0; value <= 255; ++value) {
    const auto p =
        decodePanel(static_cast<uint8_t>(value), static_cast<uint8_t>(value),
                    static_cast<uint8_t>(value));
    check(p.support == value && p.enabled == ((value & 1) != 0) &&
          p.status == (value & 7));
  }
  const auto observed = decodePanel(1, 5, 0);
  check(observed.support == 1 && observed.enabled && observed.status == 0);
  check(std::wstring_view(panelSupport(1)) == L"PSR1");
  check(std::wstring_view(panelSupport(255)) == L"unknown capability revision");
  check(std::wstring_view(panelStatus(2)) ==
        L"active: sink displaying remote frame buffer");
  check(std::wstring_view(panelStatus(5)) == L"reserved / unknown");
  std::cout << checks << " checks, " << errors << " failures\n";
  return errors ? 1 : 0;
}
