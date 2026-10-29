// Modeless owner window; all edits are committed on the existing GUI thread.
static HWND adaptiveWindow = nullptr;
static LRESULT CALLBACK adaptiveProcedure(HWND h, UINT message, WPARAM w,
                                          LPARAM l) {
  enum {
    Tiers = 1001,
    Placement = 1002,
    Background = 1003,
    Eco = 1004,
    Rules = 1005,
    Apply = 1006,
    Restore = 1007,
    Status = 1008
  };
  if (message == WM_CREATE) {
    auto control = [&](const wchar_t *type, const wchar_t *text, DWORD style,
                       int id, int x, int y, int width, int height) {
      HWND c = CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style,
                               px(x), px(y), px(width), px(height), h,
                               reinterpret_cast<HMENU>(INT_PTR(id)), nullptr,
                               nullptr);
      SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(regular), TRUE);
      return c;
    };
    control(L"STATIC", L"Experimental scheduling — battery only", 0, 0, 20, 15,
            650, 30);
    control(L"BUTTON", L"Adaptive Windows power tiers",
            BS_AUTOCHECKBOX | WS_TABSTOP, Tiers, 20, 52, 620, 28);
    SendDlgItemMessageW(
        h, Tiers, BM_SETCHECK,
        placementController.config.tiers ? BST_CHECKED : BST_UNCHECKED, 0);
    control(L"STATIC", L"Application placement", 0, 0, 20, 92, 240, 25);
    HWND placement =
        control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL,
                Placement, 280, 88, 350, 180);
    for (auto item : {L"Off — Windows scheduling", L"Adaptive efficient four",
                      L"Strict four request (soft; may be slower)"})
      SendMessageW(placement, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item));
    SendMessageW(placement, CB_SETCURSEL,
                 static_cast<unsigned>(placementController.config.profile), 0);
    control(L"STATIC", L"Background consolidation", 0, 0, 20, 136, 240, 25);
    HWND background =
        control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL,
                Background, 280, 132, 350, 180);
    for (auto item :
         {L"Off", L"Conservative", L"Experimental aggressive (shorter dwell)"})
      SendMessageW(background, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item));
    SendMessageW(background, CB_SETCURSEL,
                 static_cast<unsigned>(placementController.config.background),
                 0);
    control(L"BUTTON", L"EcoQoS for verified noncritical background candidates",
            BS_AUTOCHECKBOX | WS_TABSTOP, Eco, 20, 178, 620, 28);
    SendDlgItemMessageW(
        h, Eco, BM_SETCHECK,
        placementController.config.eco ? BST_CHECKED : BST_UNCHECKED, 0);
    control(
        L"STATIC",
        L"Application rules: full path|number, one per line\n0 = automatic, 1 "
        L"= all eligible, 2 = efficient four, 3 = untouched / exclude",
        0, 0, 20, 220, 630, 48);
    HWND rules = control(
        L"EDIT", adaptive::rulesText(placementController.config).c_str(),
        WS_BORDER | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN |
            WS_VSCROLL,
        Rules, 20, 276, 620, 150);
    SendMessageW(rules, EM_SETLIMITTEXT, 30000, 0);
    control(L"STATIC",
            L"Unknown and protected roles remain untouched. Compute rescues "
            L"override restrictions.\nStrict mode is a trade-off; no automatic "
            L"audio/deadline guarantee is claimed.",
            0, 0, 20, 440, 620, 48);
    control(L"BUTTON", L"Apply", BS_PUSHBUTTON | WS_TABSTOP, Apply, 20, 506,
            190, 36);
    control(L"BUTTON", L"Disable scheduling & restore",
            BS_PUSHBUTTON | WS_TABSTOP, Restore, 225, 506, 310, 36);
    control(L"STATIC", placementController.status().c_str(), 0, Status, 20, 555,
            620, 65);
    return 0;
  }
  if (message == WM_COMMAND && (LOWORD(w) == Apply || LOWORD(w) == Restore)) {
    auto next = placementController.config;
    next.tiers =
        SendDlgItemMessageW(h, Tiers, BM_GETCHECK, 0, 0) == BST_CHECKED;
    auto p = SendDlgItemMessageW(h, Placement, CB_GETCURSEL, 0, 0),
         b = SendDlgItemMessageW(h, Background, CB_GETCURSEL, 0, 0);
    if (p < 0 || p > 2 || b < 0 || b > 2)
      return 0;
    next.profile = static_cast<adaptive::Profile>(p);
    next.background = static_cast<adaptive::Background>(b);
    next.eco = SendDlgItemMessageW(h, Eco, BM_GETCHECK, 0, 0) == BST_CHECKED;
    std::wstring text(30001, L'\0');
    int length = GetDlgItemTextW(h, Rules, text.data(), int(text.size()));
    text.resize(length);
    if (!adaptive::parseRules(text, next.rules)) {
      SetDlgItemTextW(h, Status,
                      L"Invalid rule. Use an absolute path|0, 1, 2 or 3. No "
                      L"wildcards; maximum 64 paths.");
      return 0;
    }
    if (LOWORD(w) == Restore) {
      next.profile = adaptive::Profile::Off;
      next.background = adaptive::Background::Off;
      next.eco = false;
    }
    const bool wasArmed = armed;
    if (!dryRun)
      turnOff(false);
    if (!fault.empty()) {
      SetDlgItemTextW(h, Status, fault.c_str());
      return 0;
    }
    if (!adaptive::save(schedulerIni(), next)) {
      SetDlgItemTextW(
          h, Status,
          L"Settings could not be saved; previous preference retained.");
      if (wasArmed && !dryRun)
        turnOn();
      return 0;
    }
    placementController.config = next;
    placementController.enabled = next.profile != adaptive::Profile::Off ||
                                  next.background != adaptive::Background::Off;
    if (placementController.enabled && !dryRun)
      placementController.topology(scheduler::discoverTopology());
    if (wasArmed && !dryRun)
      turnOn();
    SendDlgItemMessageW(h, Placement, CB_SETCURSEL,
                        static_cast<unsigned>(next.profile), 0);
    SendDlgItemMessageW(h, Background, CB_SETCURSEL,
                        static_cast<unsigned>(next.background), 0);
    SetDlgItemTextW(h, Status, placementController.status().c_str());
    updateUi();
    return 0;
  }
  if (message == WM_CLOSE) {
    DestroyWindow(h);
    return 0;
  }
  if (message == WM_DESTROY) {
    adaptiveWindow = nullptr;
    return 0;
  }
  return DefWindowProcW(h, message, w, l);
}
static void showAdaptiveSettings() {
  if (adaptiveWindow) {
    SetForegroundWindow(adaptiveWindow);
    return;
  }
  WNDCLASSW c{};
  c.lpfnWndProc = adaptiveProcedure;
  c.hInstance = GetModuleHandleW(nullptr);
  c.lpszClassName = L"PulseExperimentalPolicySettings";
  c.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  c.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
  RegisterClassW(&c);
  adaptiveWindow = CreateWindowExW(
      WS_EX_CONTROLPARENT, c.lpszClassName,
      L"Pulse — Experimental policy settings",
      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, CW_USEDEFAULT,
      CW_USEDEFAULT, px(680), px(680), window, nullptr, c.hInstance, nullptr);
}
