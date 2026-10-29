// Native fourth page. No hardware sensor polling or register access.
static cpu::Capability cpuCapability;
static std::wstring cpuMessage = L"Options are off by default. Apply uses the "
                                 L"same battery / AC eligibility as Pulse.";
static void populateCpu(const cpu::Options &o) {
  SendDlgItemMessageW(window, CpuEpp, BM_SETCHECK,
                      o.epp ? BST_CHECKED : BST_UNCHECKED, 0);
  SendDlgItemMessageW(window, CpuAcBoost, BM_SETCHECK,
                      o.disableBoostAc ? BST_CHECKED : BST_UNCHECKED, 0);
  SendDlgItemMessageW(window, CpuDcBoost, BM_SETCHECK,
                      o.disableBoostDc ? BST_CHECKED : BST_UNCHECKED, 0);
  setControlText(CpuAcEpp, std::to_wstring(o.acEpp));
  setControlText(CpuDcEpp, std::to_wstring(o.dcEpp));
}
static void showCpuHardware() {
  const bool intel =
      SendDlgItemMessageW(window, CpuVendor, CB_GETCURSEL, 0, 0) == 0;
  for (int i = 0; i < 10; ++i)
    ShowWindow(GetDlgItem(window, 400 + i),
               currentPage == 3 && intel ? SW_SHOW : SW_HIDE);
  for (int i = 0; i < 8; ++i)
    ShowWindow(GetDlgItem(window, 420 + i),
               currentPage == 3 && !intel ? SW_SHOW : SW_HIDE);
  const bool matches = intel ? cpuCapability.intel : cpuCapability.amd;
  setControlText(CpuHardwareStatus,
                 matches ? L"No verified register backend. Enable / lock state "
                           L"is unknown; controls are unavailable."
                         : L"Different CPU vendor. These register controls do "
                           L"not apply to this processor.");
}
static void refreshCpu() {
  cpuCapability = cpu::detect();
  const auto yes = [](bool b) { return b ? L"reported" : L"not reported"; };
  std::wstring text =
      cpuCapability.intel
          ? std::wstring(L"Intel  |  HWP: ") + yes(cpuCapability.hwp) +
                L"  |  HWP EPP: " + yes(cpuCapability.hwpEpp)
      : cpuCapability.amd
          ? std::wstring(L"AMD  |  CPPC MSR: ") + yes(cpuCapability.cppcMsr) +
                L"  |  Core Boost: " + yes(cpuCapability.cpb)
          : L"Unknown CPU vendor. Windows CPU options are unavailable.";
  setControlText(CpuDetected, text);
  EnableWindow(GetDlgItem(window, CpuApply),
               cpuCapability.knownVendor() && power.available());
  GUID scheme{};
  DWORD ac = 0, dc = 0;
  if (!power.activeScheme(scheme) && !power.read(scheme, 8, true, ac) &&
      !power.read(scheme, 8, false, dc))
    cpuMessage =
        L"Plan EPP readback: AC " + std::to_wstring(ac) + L", battery " +
        std::to_wstring(dc) +
        L". This is Windows policy, not a hardware-register measurement.";
  else
    cpuMessage = L"Windows EPP readback unavailable for this plan. No hardware "
                 L"access was attempted.";
  setControlText(CpuStatus, cpuMessage);
  showCpuHardware();
}
static void makeCpuPage() {
  pageLabel(3, L"CPU options", 42, 138, 710, 43, hero);
  pageLabel(3, L"", 42, 190, 710, 38, regular, CpuDetected);
  SetWindowTheme(
      pageControl(3, CpuEpp, L"BUTTON",
                  L"Customize Windows EPP (0 = performance, 100 = efficiency)",
                  BS_AUTOCHECKBOX, 42, 245, 710, 28),
      L"", L"");
  for (bool ac : {true, false}) {
    const int x = ac ? 42 : 426;
    pageLabel(3, ac ? L"PLUGGED IN (AC)" : L"ON BATTERY (DC)", x, 287, 320, 25,
              bold);
    pageLabel(3, L"EPP policy (0–100)", x, 326, 224, 26);
    const int id = ac ? CpuAcEpp : CpuDcEpp;
    pageControl(3, id, L"EDIT", L"", WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL,
                x + 252, 323, 64, 28);
    SendDlgItemMessageW(window, id, EM_SETLIMITTEXT, 3, 0);
    SetWindowTheme(pageControl(3, ac ? CpuAcBoost : CpuDcBoost, L"BUTTON",
                               L"Disable CPU boost while controlled",
                               BS_AUTOCHECKBOX, x, 368, 324, 27),
                   L"", L"");
  }
  pageLabel(3,
            L"EPP changes existing plan / overlay values only; inherited "
            L"overlays may take priority.\nPause, AC bypass and exit restore "
            L"originals. Firmware decides actual clocks.",
            42, 407, 710, 44, fontSmall);
  pageButton(3, CpuApply, L"Apply options", 42, 466, 222, 38);
  pageButton(3, CpuRefresh, L"Refresh status", 280, 466, 220, 38);
  pageButton(3, CpuDefaults, L"Load defaults", 516, 466, 220, 38);
  pageLabel(3, cpuMessage.c_str(), 42, 515, 710, 37, fontSmall, CpuStatus);
  pageButton(3, IdleOpen, L"Idle states & thresholds...", 42, 553, 280, 29);
  pageButton(3, 370, L"Platform idle power...", 340, 553, 280, 29);
  HWND vendor = pageControl(3, CpuVendor, L"COMBOBOX", L"",
                            CBS_DROPDOWNLIST | WS_VSCROLL, 42, 590, 223, 100);
  for (const auto *name :
       {L"Intel hardware controls", L"AMD hardware controls"})
    SendMessageW(vendor, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
  pageLabel(3, L"", 281, 587, 463, 47, fontSmall, CpuHardwareStatus);
  const wchar_t *intel[] = {L"Speed Shift / EPP (direct)",
                            L"SpeedStep / EIST",
                            L"C1E promotion",
                            L"Package C-state: unverified",
                            L"C1 demotion",
                            L"C1 undemotion",
                            L"C3 demotion",
                            L"C3 undemotion",
                            L"Package demotion",
                            L"Package undemotion"};
  const wchar_t *amd[] = {
      L"CPPC / EPP (direct)",    L"Core Performance Boost (direct)",
      L"Core C6 (CC6)",          L"Package C6 (PC6)",
      L"Global C-State Control", L"Requested C-state: unverified",
      L"Hardware demotion",      L"Hardware undemotion"};
  for (int i = 0; i < 10; ++i) {
    const auto style =
        i == 3 ? DWORD(CBS_DROPDOWNLIST) : DWORD(BS_AUTOCHECKBOX);
    HWND c = pageControl(3, 400 + i, i == 3 ? L"COMBOBOX" : L"BUTTON",
                         i == 3 ? L"" : intel[i], style, 42 + (i % 2) * 384,
                         643 + (i / 2) * 29, 324, i == 3 ? 80 : 26);
    if (i == 3) {
      SendMessageW(c, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(intel[i]));
      SendMessageW(c, CB_SETCURSEL, 0, 0);
    } else
      SetWindowTheme(c, L"", L"");
    EnableWindow(c, FALSE);
  }
  for (int i = 0; i < 8; ++i) {
    HWND c = pageControl(
        3, 420 + i, i == 5 ? L"COMBOBOX" : L"BUTTON", i == 5 ? L"" : amd[i],
        i == 5 ? DWORD(CBS_DROPDOWNLIST) : DWORD(BS_AUTOCHECKBOX),
        42 + (i % 2) * 384, 643 + (i / 2) * 29, 324, i == 5 ? 80 : 26);
    if (i == 5) {
      SendMessageW(c, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(amd[i]));
      SendMessageW(c, CB_SETCURSEL, 0, 0);
    }
    SetWindowTheme(c, L"", L"");
    EnableWindow(c, FALSE);
  }
  pageLabel(3,
            L"Unavailable does not mean off. CC6 is deep core sleep, not an "
            L"AMD C1E toggle.\nOS idle thresholds are separate; hardware state "
            L"and residency remain unverified.",
            42, 795, 710, 38, fontSmall);
  cpuCapability = cpu::detect();
  SendMessageW(vendor, CB_SETCURSEL, cpuCapability.amd ? 1 : 0, 0);
  populateCpu(power.cpuOptions);
  refreshCpu();
}
static bool commitCpuOptions(const cpu::Options &proposed, int statusId) {
  if (!proposed.valid())
    return false;
  if (!cpuCapability.knownVendor() || !power.available())
    return false;
  for (unsigned index : {8u, 10u, 11u, 12u}) {
    if (proposed.manages(index) && !power.optionAvailable(index)) {
      setControlText(
          statusId,
          L"This plan lacks restorable originals for a selected option. "
          L"No settings or preferences changed.");
      return false;
    }
  }
  if (dryRun) {
    setControlText(statusId,
                   L"Preview: values validated; no settings were changed.");
    return true;
  }
  const auto old = power.cpuOptions;
  const bool wasArmed = armed;
  turnOff(false);
  if (!fault.empty()) {
    setControlText(statusId,
                   L"Apply stopped: original settings need recovery.");
    return false;
  }
  power.cpuOptions = proposed;
  if (!cpu::save(appFolder() + L"\\Pulse.ini", proposed)) {
    power.cpuOptions = old;
    if (wasArmed)
      turnOn();
    setControlText(statusId,
                   L"Preferences could not be saved. No new options applied.");
    return false;
  }
  if (wasArmed)
    turnOn();
  if (!fault.empty()) {
    const auto reason = fault;
    power.cpuOptions = old;
    if (!cpu::save(appFolder() + L"\\Pulse.ini", old))
      fault = L"CPU option preferences need recovery.";
    else if (wasArmed && !power.hasJournal())
      turnOn();
    setControlText(statusId, L"Options rejected: " + reason);
    updateUi();
    return false;
  }
  setControlText(statusId, enabled
                               ? L"Applied and read back. Original settings "
                                 L"restore on pause, bypass or exit."
                               : L"Saved. Options apply when Pulse next "
                                 L"controls this power source.");
  updateUi();
  return true;
}
static bool applyCpuUi() {
  auto proposed = power.cpuOptions;
  proposed.epp =
      SendDlgItemMessageW(window, CpuEpp, BM_GETCHECK, 0, 0) == BST_CHECKED;
  proposed.disableBoostAc =
      SendDlgItemMessageW(window, CpuAcBoost, BM_GETCHECK, 0, 0) == BST_CHECKED;
  proposed.disableBoostDc =
      SendDlgItemMessageW(window, CpuDcBoost, BM_GETCHECK, 0, 0) == BST_CHECKED;
  if (!parsePercent(controlText(CpuAcEpp), proposed.acEpp) ||
      !parsePercent(controlText(CpuDcEpp), proposed.dcEpp)) {
    setControlText(
        CpuStatus,
        L"Enter whole EPP values from 0 to 100. No changes applied.");
    return false;
  }
  return commitCpuOptions(proposed, CpuStatus);
}
