// No discovery or monitoring on page construction or ordinary UI refresh.
static constexpr UINT PlatformProbeDone = WM_APP + 13;
static constexpr int PlatformOpen = 370, PlatformBack = 371,
                     PlatformProbe = 372, PlatformStatus = 373,
                     PlatformPcie = 374;
static HANDLE platformProcess = nullptr, platformWait = nullptr;
static std::wstring platformReport;
static HWND platformOwner = nullptr;
static void CALLBACK platformFinished(void *, BOOLEAN timedOut) {
  PostMessageW(platformOwner, PlatformProbeDone, timedOut ? 1 : 0, 0);
}
static void stopPlatformProbe() {
  if (platformWait) {
    UnregisterWaitEx(platformWait, INVALID_HANDLE_VALUE);
    platformWait = nullptr;
  }
  if (platformProcess) {
    if (WaitForSingleObject(platformProcess, 0) == WAIT_TIMEOUT)
      TerminateProcess(platformProcess,
                       ERROR_CANCELLED); // Our read-only child only.
    CloseHandle(platformProcess);
    platformProcess = nullptr;
  }
  platformOwner = nullptr;
}
static void completePlatformProbe(bool timeout) {
  DWORD code = ERROR_PROCESS_ABORTED;
  if (!platformProcess)
    return;
  if (!GetExitCodeProcess(platformProcess, &code))
    code = GetLastError();
  stopPlatformProbe();
  EnableWindow(GetDlgItem(window, PlatformProbe), TRUE);
  setControlText(
      PlatformStatus,
      timeout ? L"Diagnostic timed out and was stopped. No settings changed."
      : code == 0
          ? L"Read-only report ready. Unknown hardware states remain unknown."
      : code == 3 ? L"Battery-only check stopped: Windows did not confirm "
                    L"battery power."
                  : L"Diagnostic incomplete (Windows / exit code " +
                        std::to_wstring(code) + L").");
  if (!timeout && (code == 0 || code == 2 || code == 3) &&
      GetFileAttributesW(platformReport.c_str()) != INVALID_FILE_ATTRIBUTES)
    ShellExecuteW(window, L"open", platformReport.c_str(), nullptr, nullptr,
                  SW_SHOWNORMAL);
}
static void startPlatformProbe() {
  if (dryRun || platformProcess)
    return;
  wchar_t path[32768]{};
  const DWORD length = GetModuleFileNameW(nullptr, path, 32768);
  if (!length || length >= 32768) {
    setControlText(PlatformStatus, L"Cannot locate the diagnostic program.");
    return;
  }
  const auto executable =
      (std::filesystem::path(path).parent_path() / L"PulseIdleProbe.exe")
          .wstring();
  platformReport = appFolder() + L"\\platform-idle-" +
                   std::to_wstring(GetTickCount64()) + L".txt";
  std::wstring command =
      L"\"" + executable +
      L"\" --require-battery --sample-seconds 3 --report \"" + platformReport +
      L"\"";
  STARTUPINFOW startup{sizeof(startup)};
  PROCESS_INFORMATION child{};
  if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr,
                      FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                      &child)) {
    setControlText(PlatformStatus,
                   L"Cannot start PulseIdleProbe.exe (Windows " +
                       std::to_wstring(GetLastError()) + L").");
    return;
  }
  CloseHandle(child.hThread);
  platformProcess = child.hProcess;
  platformOwner = window;
  if (!RegisterWaitForSingleObject(&platformWait, platformProcess,
                                   platformFinished, nullptr, 45000,
                                   WT_EXECUTEONLYONCE)) {
    stopPlatformProbe();
    setControlText(
        PlatformStatus,
        L"Could not observe the diagnostic process; it was stopped.");
    return;
  }
  EnableWindow(GetDlgItem(window, PlatformProbe), FALSE);
  setControlText(PlatformStatus, L"Checking configuration and three seconds of "
                                 L"GPU activity. No settings change.");
}
static void updatePlatformPage() {
  setControlText(PlatformPcie, pcieGuard.enabled()
                                   ? L"PCIe battery guard: ON"
                                   : L"PCIe battery guard: OFF");
  EnableWindow(GetDlgItem(window, PlatformProbe), platformProcess == nullptr);
}
static void makePlatformPage() {
  pageLabel(5, L"Platform idle power", 42, 138, 710, 43, hero);
  pageLabel(5,
            L"Battery-first controls. Hardware state is never inferred from a "
            L"switch.",
            42, 190, 710, 36);
  const wchar_t *titles[] = {L"Panel self refresh / replay",
                             L"Data Fabric C-states",
                             L"GFXOFF / graphics gating"};
  const wchar_t *details[] = {
      L"Panel state can be read on the validated AMD driver. Residency and "
      L"a safe control remain unverified; inspection does not enable PSR.",
      L"No validated runtime control or residency counter. CPU C-state "
      L"readings do not establish fabric sleep.",
      L"Driver configuration and GPU activity can be inspected. A private "
      L"driver path is not a verified control."};
  for (int i = 0; i < 3; ++i) {
    const int y = 242 + i * 102;
    HWND c = pageControl(5, 380 + i, L"BUTTON", titles[i], BS_AUTOCHECKBOX, 42,
                         y, 710, 27);
    SetWindowTheme(c, L"", L"");
    EnableWindow(c, FALSE);
    pageLabel(5, details[i], 42, y + 34, 710, 48, fontSmall);
  }
  pageButton(5, PlatformPcie, L"PCIe battery guard", 42, 552, 330, 36);
  pageLabel(5,
            L"Uses the existing maximum-savings policy watcher on Windows "
            L"battery power.\nOff stops enforcement and retains your saved "
            L"policy. L1.2 / D3cold are not guaranteed.",
            42, 600, 710, 49, fontSmall);
  pageButton(5, PlatformProbe, L"Inspect idle health...", 42, 667, 330, 38);
  pageButton(5, PlatformBack, L"Back to CPU options", 408, 667, 328, 38);
  pageLabel(5,
            platformProcess ? L"Diagnostic running..."
                            : L"On-demand snapshot only; no new background "
                              L"sampling.\nUnavailable controls are not "
                              L"evidence that the hardware feature is off.",
            42, 724, 710, 75, fontSmall, PlatformStatus);
  updatePlatformPage();
}
