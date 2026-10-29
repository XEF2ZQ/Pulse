#include "idle_health.h"
#include "panel_observer.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

static uint64_t filetime(FILETIME t) {
  return (uint64_t(t.dwHighDateTime) << 32) | t.dwLowDateTime;
}
static bool battery() {
  SYSTEM_POWER_STATUS p{};
  const bool valid = GetSystemPowerStatus(&p) != FALSE;
  return pulse::idle::confirmedBattery(valid, p.ACLineStatus);
}
int wmain(int argc, wchar_t **argv) {
  unsigned seconds = 0;
  bool requireBattery = false, seenSeconds = false;
  std::filesystem::path output;
  for (int i = 1; i < argc; ++i) {
    const std::wstring arg = argv[i];
    if (arg == L"--require-battery" && !requireBattery)
      requireBattery = true;
    else if (arg == L"--report" && output.empty() && i + 1 < argc)
      output = argv[++i];
    else if (arg == L"--sample-seconds" && !seenSeconds && i + 1 < argc) {
      seenSeconds = true;
      const std::wstring text = argv[++i];
      if (text.empty() || text.size() > 2 ||
          text.find_first_not_of(L"0123456789") != std::wstring::npos)
        return 1;
      seconds = static_cast<unsigned>(std::stoul(text));
      if (seconds < 1 || seconds > 30)
        return 1;
    } else {
      std::wcerr << L"Usage: PulseIdleProbe [--require-battery] "
                    L"[--sample-seconds 1..30] [--report file]\n";
      return 1;
    }
  }
  try {
    FILETIME created{}, exited{}, kernelBefore{}, userBefore{}, kernelAfter{},
        userAfter{};
    const bool before = GetProcessTimes(GetCurrentProcess(), &created, &exited,
                                        &kernelBefore, &userBefore) != FALSE;
    const auto started = GetTickCount64();
    std::wostringstream report;
    DWORD error = ERROR_SUCCESS;
    if (requireBattery && !battery()) {
      report << L"Not run: battery source is not confirmed. No settings "
                L"changed.\n";
      error = ERROR_NOT_READY;
    } else {
      report << pulse::idle::inventory();
      report << pulse::idle::observePanel();
      if (seconds)
        report << pulse::idle::activity(seconds, requireBattery, error);
      if (requireBattery && !battery()) {
        report << L"Battery-only capture invalid: source changed or became "
                  L"unknown.\n";
        error = ERROR_NOT_READY;
      }
    }
    const bool after = GetProcessTimes(GetCurrentProcess(), &created, &exited,
                                       &kernelAfter, &userAfter) != FALSE;
    if (before && after)
      report << L"\nObserver process CPU microseconds="
             << (filetime(kernelAfter) + filetime(userAfter) -
                 filetime(kernelBefore) - filetime(userBefore)) /
                    10;
    else
      report << L"\nObserver CPU time unavailable";
    report << L"\nWall milliseconds=" << GetTickCount64() - started
           << L"\nNo resident sampling remains after this process exits.\n"
           << L"Completed read-only inventory; unknown mechanisms remain "
              L"unknown. Error="
           << error << L'\n';
    if (!output.empty()) {
      // UTF-8, so reports remain usable outside the local Windows code page.
      const auto text = report.str();
      const int size = WideCharToMultiByte(
          CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
          static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
      if (!size)
        return 2;
      std::string utf8(static_cast<size_t>(size), '\0');
      if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                               static_cast<int>(text.size()), utf8.data(), size,
                               nullptr, nullptr))
        return 2;
      std::ofstream stream(output, std::ios::binary);
      stream.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
      stream.close();
      if (!stream)
        return 2;
    } else
      std::wcout << report.str();
    return error == ERROR_NOT_READY ? 3 : error ? 2 : 0;
  } catch (const std::exception &) {
    std::wcerr << L"Diagnostic could not complete; no settings changed.\n";
    return 2;
  }
}

