// Windows idle-policy editor. OS indices must never be labelled as CC6/PC6.
static void populateIdle(const cpu::Options &o) {
  SendDlgItemMessageW(window, IdleThresholds, BM_SETCHECK,
                      o.idleThresholds ? BST_CHECKED : BST_UNCHECKED, 0);
  SendDlgItemMessageW(window, IdleLimit, BM_SETCHECK,
                      o.idleLimit ? BST_CHECKED : BST_UNCHECKED, 0);
  setControlText(IdleAcDemote, std::to_wstring(o.acDemote));
  setControlText(IdleDcDemote, std::to_wstring(o.dcDemote));
  setControlText(IdleAcPromote, std::to_wstring(o.acPromote));
  setControlText(IdleDcPromote, std::to_wstring(o.dcPromote));
  SendDlgItemMessageW(window, IdleAcLimit, CB_SETCURSEL, o.acLimit, 0);
  SendDlgItemMessageW(window, IdleDcLimit, CB_SETCURSEL, o.dcLimit, 0);
}
static void refreshIdleSupport() {
  const bool thresholds =
      power.optionAvailable(10) && power.optionAvailable(11);
  const bool limit = power.optionAvailable(12);
  // An already-saved option must remain uncheckable even if the plan changed.
  EnableWindow(GetDlgItem(window, IdleThresholds),
               thresholds || power.cpuOptions.idleThresholds);
  EnableWindow(GetDlgItem(window, IdleLimit),
               limit || power.cpuOptions.idleLimit);
  for (int id : {IdleAcLimit, IdleDcLimit})
    EnableWindow(GetDlgItem(window, id), limit);
  std::wstring status =
      thresholds ? L"Threshold values can be saved and restored in this plan."
                 : L"Threshold control unavailable: this plan lacks explicit "
                   L"originals.";
  status += limit ? L"\nOS idle ceiling is editable; physical C-state mapping "
                    L"is not verified."
                  : L"\nOS idle ceiling unavailable: inherited original cannot "
                    L"be restored exactly.";
  setControlText(IdleSupport, status);
}
static void makeIdlePage() {
  pageLabel(4, L"Idle states & thresholds", 42, 138, 710, 43, hero);
  pageLabel(4,
            L"Windows policies for Intel and AMD. These are not hardware "
            L"demotion bits.",
            42, 190, 710, 36);
  SetWindowTheme(pageControl(4, IdleThresholds, L"BUTTON",
                             L"Customize Windows idle promotion / demotion",
                             BS_AUTOCHECKBOX, 42, 245, 710, 28),
                 L"", L"");
  for (bool ac : {true, false}) {
    const int x = ac ? 42 : 426;
    pageLabel(4, ac ? L"PLUGGED IN (AC)" : L"ON BATTERY (DC)", x, 291, 320, 25,
              bold);
    for (bool promote : {false, true}) {
      const int y = promote ? 376 : 330;
      const int id = promote ? (ac ? IdleAcPromote : IdleDcPromote)
                             : (ac ? IdleAcDemote : IdleDcDemote);
      pageLabel(
          4, promote ? L"Promotion threshold (%)" : L"Demotion threshold (%)",
          x, y + 3, 244, 26);
      pageControl(4, id, L"EDIT", L"", WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL,
                  x + 252, y, 64, 28);
      SendDlgItemMessageW(window, id, EM_SETLIMITTEXT, 3, 0);
    }
  }
  pageLabel(4,
            L"Windows decides when to move between idle states. Values are OS "
            L"policy percentages;\nthey do not select CC6 directly or "
            L"guarantee a particular wake latency.",
            42, 424, 710, 45, fontSmall);
  SetWindowTheme(pageControl(4, IdleLimit, L"BUTTON",
                             L"Request a Windows idle-state ceiling",
                             BS_AUTOCHECKBOX, 42, 495, 710, 28),
                 L"", L"");
  for (bool ac : {true, false}) {
    HWND c = pageControl(4, ac ? IdleAcLimit : IdleDcLimit, L"COMBOBOX", L"",
                         CBS_DROPDOWNLIST | WS_VSCROLL, ac ? 42 : 426, 541, 324,
                         160);
    for (const auto *text : {L"0: No OS ceiling", L"1: OS idle state 1",
                             L"2: OS idle state 2", L"3: OS idle state 3"})
      SendMessageW(c, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
  }
  pageLabel(4,
            L"An OS state index is not an Intel package C-state or AMD CC6/PC6 "
            L"number.\nA shallower ceiling can increase idle power; new "
            L"options remain off by default.",
            42, 586, 710, 45, fontSmall);
  pageLabel(4, L"", 42, 648, 710, 48, fontSmall, IdleSupport);
  pageButton(4, IdleApply, L"Apply idle options", 42, 712, 222, 38);
  pageButton(4, IdleDefaults, L"Load defaults", 280, 712, 220, 38);
  pageButton(4, IdleBack, L"Back to CPU options", 516, 712, 220, 38);
  pageLabel(4,
            L"Pause, AC bypass and exit restore Pulse-owned values. No new "
            L"monitoring timer.",
            42, 778, 710, 51, fontSmall, IdleStatus);
  populateIdle(power.cpuOptions);
  refreshIdleSupport();
}
static bool applyIdleUi() {
  auto proposed = power.cpuOptions;
  proposed.idleThresholds =
      SendDlgItemMessageW(window, IdleThresholds, BM_GETCHECK, 0, 0) ==
      BST_CHECKED;
  proposed.idleLimit =
      SendDlgItemMessageW(window, IdleLimit, BM_GETCHECK, 0, 0) == BST_CHECKED;
  if (!parsePercent(controlText(IdleAcDemote), proposed.acDemote) ||
      !parsePercent(controlText(IdleDcDemote), proposed.dcDemote) ||
      !parsePercent(controlText(IdleAcPromote), proposed.acPromote) ||
      !parsePercent(controlText(IdleDcPromote), proposed.dcPromote)) {
    setControlText(
        IdleStatus,
        L"Enter whole thresholds from 0 to 100. No changes applied.");
    return false;
  }
  proposed.acLimit = static_cast<unsigned>(
      SendDlgItemMessageW(window, IdleAcLimit, CB_GETCURSEL, 0, 0));
  proposed.dcLimit = static_cast<unsigned>(
      SendDlgItemMessageW(window, IdleDcLimit, CB_GETCURSEL, 0, 0));
  if (!proposed.valid())
    return false;
  if ((proposed.idleThresholds &&
       (!power.optionAvailable(10) || !power.optionAvailable(11))) ||
      (proposed.idleLimit && !power.optionAvailable(12))) {
    setControlText(IdleStatus, L"This plan cannot safely restore the selected "
                               L"idle option. No changes applied.");
    return false;
  }
  return commitCpuOptions(proposed, IdleStatus);
}
