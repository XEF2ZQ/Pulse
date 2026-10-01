// Included after the main window's native control/font helpers.
static std::vector<std::pair<HWND, int>> pageControls;
static undervolt::Capability uvCapability;
static std::wstring profileMessage =
    L"Apply saves both AC and battery settings to the current Windows plan.";
static constexpr int PageHome = 201, PagePower = 202, PageUv = 203, PageCpu = 204, ProfileApply = 210,
                     ProfileReset = 211, PlacementButton = 212;
static constexpr int AcBase = 240, DcBase = 260, UvP = 301, UvE = 302, UvPSlider = 303,
                     UvESlider = 304, UvApply = 305, UvMonitor = 306, UvRefresh = 307,
                     ProfileStatus = 308, UvStatus = 309;
static constexpr int CpuEpp = 330, CpuAcEpp = 331, CpuDcEpp = 332, CpuAcBoost = 333,
                     CpuDcBoost = 334, CpuApply = 335, CpuRefresh = 336, CpuDefaults = 337,
                     CpuStatus = 338, CpuVendor = 339, CpuHardwareStatus = 340, CpuDetected = 341;
static constexpr int IdleOpen = 350, IdleBack = 351, IdleThresholds = 352, IdleLimit = 353,
                     IdleAcDemote = 354, IdleDcDemote = 355, IdleAcPromote = 356,
                     IdleDcPromote = 357, IdleAcLimit = 358, IdleDcLimit = 359,
                     IdleApply = 360, IdleDefaults = 361, IdleStatus = 362, IdleSupport = 363;
static void makeCpuPage();
static void showCpuHardware();
static void makeIdlePage();
static HWND pageControl(int page, int id, const wchar_t *klass, const wchar_t *text, DWORD style,
                        int x, int y, int w, int h, HFONT f = nullptr) {
    HWND c = CreateWindowExW(0, klass, text, WS_CHILD | WS_TABSTOP | style, px(x), px(y), px(w),
                             px(h), window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                             nullptr, nullptr);
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(f ? f : regular), FALSE);
    pageControls.emplace_back(c, page);
    return c;
}
static void pageLabel(int page, const wchar_t *text, int x, int y, int w, int h, HFONT f = nullptr,
                      int id = 0) {
    pageControl(page, id, L"STATIC", text, SS_LEFT | SS_NOPREFIX, x, y, w, h, f);
}
static void pageButton(int page, int id, const wchar_t *text, int x, int y, int w, int h) {
    pageControl(page, id, L"BUTTON", text, BS_OWNERDRAW, x, y, w, h);
}
static void setControlText(int id, const std::wstring &text) {
    HWND h = GetDlgItem(window, id);
    wchar_t old[2048]{};
    GetWindowTextW(h, old, 2048);
    if (text != old)
        SetWindowTextW(h, text.c_str());
}
static std::wstring controlText(int id) {
    wchar_t value[128]{};
    GetWindowTextW(GetDlgItem(window, id), value, 128);
    return value;
}
struct PageDrafts {
    std::vector<std::pair<int, std::wstring>> text;
    std::vector<std::pair<int, LRESULT>> checks, combos;
};
static PageDrafts capturePageDrafts() {
    PageDrafts result;
    for (int base : {AcBase, DcBase}) {
        for (int offset : {1, 3})
            result.text.emplace_back(base + offset, controlText(base + offset));
        for (int offset : {0, 2})
            result.checks.emplace_back(
                base + offset, SendDlgItemMessageW(window, base + offset, BM_GETCHECK, 0, 0));
        for (int offset : {4, 5, 6})
            result.combos.emplace_back(
                base + offset, SendDlgItemMessageW(window, base + offset, CB_GETCURSEL, 0, 0));
    }
    for (int id : {UvP, UvE})
        result.text.emplace_back(id, controlText(id));
    for (int id : {CpuAcEpp, CpuDcEpp,IdleAcDemote,IdleDcDemote,IdleAcPromote,IdleDcPromote}) result.text.emplace_back(id, controlText(id));
    for (int id : {CpuEpp, CpuAcBoost, CpuDcBoost,IdleThresholds,IdleLimit})
        result.checks.emplace_back(id, SendDlgItemMessageW(window,id,BM_GETCHECK,0,0));
    result.combos.emplace_back(CpuVendor,SendDlgItemMessageW(window,CpuVendor,CB_GETCURSEL,0,0));
    for(int id:{IdleAcLimit,IdleDcLimit})result.combos.emplace_back(id,SendDlgItemMessageW(window,id,CB_GETCURSEL,0,0));
    return result;
}
static void restorePageDrafts(const PageDrafts &drafts) {
    for (const auto &[id, text] : drafts.text)
        setControlText(id, text);
    for (const auto &[id, value] : drafts.checks)
        SendDlgItemMessageW(window, id, BM_SETCHECK, value, 0);
    for (const auto &[id, value] : drafts.combos)
        SendDlgItemMessageW(window, id, CB_SETCURSEL, value, 0);
    showCpuHardware();
}
static void populateProfiles() {
    for (bool ac : {true, false}) {
        const auto &p = ac ? power.profiles.ac : power.profiles.dc;
        int base = ac ? AcBase : DcBase;
        SendDlgItemMessageW(window, base, BM_SETCHECK, p.parking ? BST_CHECKED : BST_UNCHECKED, 0);
        SendDlgItemMessageW(window, base + 2, BM_SETCHECK, p.scaling ? BST_CHECKED : BST_UNCHECKED,
                            0);
        setControlText(base + 1, std::to_wstring(p.cores));
        setControlText(base + 3, std::to_wstring(p.minimum));
        SendDlgItemMessageW(window, base + 4, CB_SETCURSEL, p.hetero, 0);
        SendDlgItemMessageW(window, base + 5, CB_SETCURSEL, p.shortThreads, 0);
        SendDlgItemMessageW(window, base + 6, CB_SETCURSEL, p.longThreads, 0);
    }
}
static void refreshUv() {
    uvCapability = undervolt::detect();
    std::wstring status = uvCapability.name + L"\n" + uvCapability.vendor + L"  |  family " +
                          std::to_wstring(uvCapability.family) + L", model " +
                          std::to_wstring(uvCapability.model) + L"\n\n";
    status += L"Firmware lock: unverified\nIndependent P / E control and hardware readback: "
              L"unavailable\n";
    status += uvCapability.driverInstalled
                  ? L"PawnIO driver is installed; that alone does not verify undervolt access."
                  : L"No PawnIO service detected. No driver will be installed automatically.";
    setControlText(UvStatus, status);
    EnableWindow(GetDlgItem(window, UvApply), uvCapability.canApply());
    EnableWindow(GetDlgItem(window, UvMonitor), uvCapability.canMonitor());
}
static void makePages() {
    for (int i = 0; i < 4; ++i)
        pageButton(-1, PageHome + i,
                   i == 0   ? L"Overview"
                   : i == 1 ? L"Power & cores"
                   : i == 2 ? L"CPU undervolt" : L"CPU options",
                   32 + i * 130, 80, 122, 30);
    pageLabel(1, L"Power & core parking", 42, 138, 710, 43, hero);
    pageLabel(
        1, L"Independent AC / battery controls. Apply also works while boost control is bypassed.",
        42, 189, 715, 40);
    for (bool ac : {true, false}) {
        const int x = ac ? 42 : 426, base = ac ? AcBase : DcBase;
        pageLabel(1, ac ? L"PLUGGED IN (AC)" : L"ON BATTERY (DC)", x, 245, 320, 25, bold);
        SetWindowTheme(pageControl(1, base, L"BUTTON", L"Allow core parking", BS_AUTOCHECKBOX, x,
                                   290, 234, 26),
                       L"", L"");
        pageControl(1, base + 1, L"EDIT", L"", WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL, x + 252, 290,
                    64, 26);
        SendDlgItemMessageW(window, base + 1, EM_SETLIMITTEXT, 3, 0);
        pageLabel(1, L"Minimum active logical processors (%)", x, 322, 320, 25, fontSmall);
        SetWindowTheme(pageControl(1, base + 2, L"BUTTON", L"Allow frequency scaling",
                                   BS_AUTOCHECKBOX, x, 364, 244, 26),
                       L"", L"");
        pageControl(1, base + 3, L"EDIT", L"", WS_BORDER | ES_NUMBER | ES_AUTOHSCROLL, x + 252, 364,
                    64, 26);
        SendDlgItemMessageW(window, base + 3, EM_SETLIMITTEXT, 3, 0);
        pageLabel(1, L"Minimum processor state (%)", x, 396, 320, 25, fontSmall);
        const wchar_t *names[] = {L"Parking policy", L"Short threads", L"Long threads"};
        for (int row = 0; row < 3; ++row) {
            pageLabel(1, names[row], x, 439 + row * 47, 119, 28, fontSmall);
            HWND combo =
                pageControl(1, base + 4 + row, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL,
                            x + 122, 433 + row * 47, 202, 220);
            if (row == 0) {
                for (const auto *text : {L"0: Standard parking", L"1: Heterogeneous policy 1",
                                         L"2: Heterogeneous policy 2", L"3: Heterogeneous policy 3",
                                         L"4: Heterogeneous policy 4"})
                    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
            } else {
                for (const auto *text :
                     {L"0: All processors", L"1: Performance processors", L"2: Prefer performance",
                      L"3: Efficient processors", L"4: Prefer efficient", L"5: Automatic"})
                    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
            }
        }
    }
    pageLabel(1,
              L"Off sets the corresponding minimum to 100%. Percentages are OS policy requests, "
              L"not fixed clocks.",
              42, 574, 710, 39, fontSmall);
    pageButton(1, ProfileApply, L"Apply AC + battery", 42, 618, 228, 38);
    pageButton(1, ProfileReset, L"Load Pulse defaults", 286, 618, 218, 38);
    pageButton(1, PlacementButton, L"Adaptive scheduler: OFF", 42, 676, 340, 38);
    pageLabel(1,
              L"Moves eligible low-priority background threads to efficient CPU sets.\nReleases "
              L"them for foreground work, CPU pressure or performance demand.",
              42, 725, 710, 42, fontSmall);
    pageLabel(1, profileMessage.c_str(), 42, 780, 710, 49, fontSmall, ProfileStatus);
    pageLabel(2, L"CPU undervolt", 42, 138, 710, 43, hero);
    pageLabel(2, L"Hardware capability is verified separately from the CPU model name.", 42, 190,
              710, 32);
    pageLabel(2, L"", 42, 245, 710, 157, regular, UvStatus);
    pageLabel(2, L"Performance cores / P class", 42, 433, 320, 30, bold);
    pageLabel(2, L"Efficient cores / E class", 426, 433, 320, 30, bold);
    for (int i = 0; i < 2; ++i) {
        const int x = i ? 426 : 42;
        pageControl(2, i ? UvE : UvP, L"EDIT", L"0", WS_BORDER | ES_AUTOHSCROLL, x, 476, 90, 30);
        SendDlgItemMessageW(window, i ? UvE : UvP, EM_SETLIMITTEXT, 5, 0);
        HWND slider = pageControl(2, i ? UvESlider : UvPSlider, TRACKBAR_CLASSW, L"",
                                  TBS_HORZ | TBS_NOTICKS, x + 108, 475, 217, 34);
        SendMessageW(slider, TBM_SETRANGEMIN, FALSE, -250);
        SendMessageW(slider, TBM_SETRANGEMAX, TRUE, 0);
        SendMessageW(slider, TBM_SETPOS, TRUE, 0);
    }
    uvCapability = undervolt::detect();
    pageLabel(2, uvCapability.units.c_str(), 42, 530, 710, 25);
    pageLabel(
        2,
        L"Values are drafts. This build has no verified per-class undervolt writer/readback.\nG "
        L"Helper retains its existing Curve Optimizer settings; Pulse does not overwrite them.",
        42, 571, 710, 57);
    pageButton(2, UvApply, L"Apply unavailable", 42, 654, 222, 40);
    pageButton(2, UvRefresh, L"Check capabilities", 280, 654, 220, 40);
    pageControl(2, UvMonitor, L"BUTTON",
                L"Check every 60 seconds; reapply only a verified mismatch", BS_AUTOCHECKBOX, 42,
                723, 710, 30);
    pageLabel(2,
              L"Monitoring stays off without reliable readback and exclusive ownership.\nCurve "
              L"Optimizer steps and voltage offsets are different controls.",
              42, 775, 710, 49, fontSmall);
    populateProfiles();
    refreshUv();
    makeCpuPage();
    makeIdlePage();
}
static void showPage(int page) {
    currentPage = page;
    for (size_t i = 0; i < labels.size(); ++i)
        ShowWindow(labels[i], (i == 0 || i == 7 || (page == 0 && i != 1)) ? SW_SHOW : SW_HIDE);
    for (int id = 101; id <= 110; ++id)
        ShowWindow(GetDlgItem(window, id),
                   (id == EfficiencyButton || page == 0) ? SW_SHOW : SW_HIDE);
    for (const auto &[h, p] : pageControls)
        ShowWindow(h, p == -1 || p == page ? SW_SHOW : SW_HIDE);
    showCpuHardware();
    for (int id = PageHome; id <= PageCpu; ++id)
        InvalidateRect(GetDlgItem(window, id), nullptr, FALSE);
    InvalidateRect(window, nullptr, FALSE);
}
#include "cpu_ui.inl"
#include "idle_ui.inl"
static void destroyPages() {
    for (const auto &[h, p] : pageControls)
        DestroyWindow(h);
    pageControls.clear();
}
static void updatePages() {
    setControlText(PlacementButton, placementController.enabled ? L"Adaptive scheduler: ON"
                                                                : L"Adaptive scheduler: OFF");
    setControlText(ProfileStatus, profileMessage);
    if (currentPage == 0) {
        auto summary = [](const SourceProfile &p) {
            return std::to_wstring(p.coreValue()) + L"%  active cores\n" +
                   std::to_wstring(p.minimumValue()) + L"%  minimum processor state";
        };
        setLabel(9, summary(power.profiles.dc));
        setLabel(11, summary(power.profiles.ac));
        setLabel(15, L"Configured profiles above; edit and apply on Power & scheduling");
        setLabel(13, placementController.status());
    }
}
static bool applyProfilesUi() {
    Profiles proposed = power.profiles;
    for (bool ac : {true, false}) {
        auto &p = ac ? proposed.ac : proposed.dc;
        int base = ac ? AcBase : DcBase;
        p.parking = SendDlgItemMessageW(window, base, BM_GETCHECK, 0, 0) == BST_CHECKED;
        p.scaling = SendDlgItemMessageW(window, base + 2, BM_GETCHECK, 0, 0) == BST_CHECKED;
        if (!parsePercent(controlText(base + 1), p.cores) ||
            !parsePercent(controlText(base + 3), p.minimum)) {
            profileMessage = L"Enter whole percentages from 0 to 100.";
            updatePages();
            return false;
        }
        p.hetero = static_cast<unsigned>(SendDlgItemMessageW(window, base + 4, CB_GETCURSEL, 0, 0));
        p.shortThreads =
            static_cast<unsigned>(SendDlgItemMessageW(window, base + 5, CB_GETCURSEL, 0, 0));
        p.longThreads =
            static_cast<unsigned>(SendDlgItemMessageW(window, base + 6, CB_GETCURSEL, 0, 0));
    }
    if (!proposed.valid())
        return false;
    if (dryRun) {
        profileMessage = L"Preview only: validation passed; no system settings were changed.";
        updatePages();
        return true;
    }
    const auto old = power.profiles;
    const bool wasArmed = armed;
    turnOff(false);
    if (!fault.empty()) {
        profileMessage = L"Apply stopped: restoration needs attention.";
        updatePages();
        return false;
    }
    DWORD error = guardian();
    power.profiles = proposed;
    bool preferenceSaved = false;
    if (!error) {
        preferenceSaved = saveProfiles(appFolder() + L"\\Pulse.ini", proposed);
        if (!preferenceSaved)
            error = ERROR_WRITE_FAULT;
    }
    if (!error)
        error = power.persistProfiles();
    profileMessage = error
                         ? L"Apply failed (Windows " + std::to_wstring(error) +
                               L"); previous settings retained or recovery pending."
                         : L"Saved and read back: AC + battery. Settings persist when Pulse exits.";
    if (error) {
        power.profiles = old;
        if (preferenceSaved && !saveProfiles(appFolder() + L"\\Pulse.ini", old))
            fault = L"Previous profile preferences could not be restored.";
        if (power.hasJournal())
            fault = L"Profile restoration needs attention; recovery journal retained.";
    }
    if (wasArmed && fault.empty())
        turnOn();
    updateUi();
    return error == 0;
}
