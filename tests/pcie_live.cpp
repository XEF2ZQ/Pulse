// Explicit manual integration exercise. Not registered in CTest: changes DC ASPM.
#include <initguid.h>
#include "pcie_guard.h"
#include <powrprof.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

static pulse::pcie::Guard guard;
static LRESULT CALLBACK procedure(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == pulse::pcie::ChangedMessage) { guard.notify(); return 0; }
    if (m == WM_TIMER && w == pulse::pcie::GuardTimer) { guard.timer(); return 0; }
    return DefWindowProcW(h,m,w,l);
}
static void pump(DWORD milliseconds) {
    const auto end = GetTickCount64() + milliseconds;
    while (GetTickCount64() < end) {
        MSG message{};
        while (PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
            TranslateMessage(&message); DispatchMessageW(&message);
        }
        const auto now=GetTickCount64();
        if(now<end)MsgWaitForMultipleObjectsEx(0,nullptr,static_cast<DWORD>(end-now),QS_ALLINPUT,MWMO_INPUTAVAILABLE);
    }
}
static void require(DWORD error) { if(error)throw std::runtime_error("Windows "+std::to_string(error)); }
static GUID active() {
    GUID* allocated=nullptr;require(PowerGetActiveScheme(nullptr,&allocated));
    if(!allocated)throw std::runtime_error("No active scheme");
    GUID value=*allocated;LocalFree(allocated);return value;
}
static DWORD read(const GUID& scheme,bool ac) {
    DWORD value=0;
    require((ac?PowerReadACValueIndex:PowerReadDCValueIndex)(nullptr,&scheme,
        &GUID_PCIEXPRESS_SETTINGS_SUBGROUP,&GUID_PCIEXPRESS_ASPM_POLICY,&value));return value;
}
static void write(const GUID& scheme,DWORD value) {
    require(PowerWriteDCValueIndex(nullptr,&scheme,&GUID_PCIEXPRESS_SETTINGS_SUBGROUP,&GUID_PCIEXPRESS_ASPM_POLICY,value));
    if(active()==scheme)require(PowerSetActiveScheme(nullptr,&scheme));
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=2){std::cerr<<"Usage: pcie_live NEW_OUTPUT_DIRECTORY\nChanges DC ASPM briefly, then restores it.\n";return 1;}
    SYSTEM_POWER_STATUS source{};
    if(!GetSystemPowerStatus(&source)||source.ACLineStatus!=0){std::cerr<<"Requires confirmed battery power\n";return 3;}
    GUID scheme{};DWORD beforeDc=0,beforeAc=0;bool captured=false;HWND window=nullptr;
    std::ofstream log;
    int result=0;
    try {
        std::filesystem::path folder(argv[1]);
        if(std::filesystem::exists(folder))throw std::runtime_error("Output already exists");
        std::filesystem::create_directories(folder);
        log.open(folder/L"result.txt");log.exceptions(std::ios::failbit|std::ios::badbit);
        scheme=active();beforeDc=read(scheme,false);beforeAc=read(scheme,true);captured=true;
        log<<"Initial DC="<<beforeDc<<" AC="<<beforeAc<<"\n";
        WNDCLASSW cls{};cls.lpfnWndProc=procedure;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"PulsePcieLiveTest";
        if(!RegisterClassW(&cls))throw std::runtime_error("Window class failed");
        window=CreateWindowW(cls.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,cls.hInstance,nullptr);
        if(!window)throw std::runtime_error("Window creation failed");
        const auto ini=(folder/L"test.ini").wstring();
        guard.start(window,ini);pump(1500);
        if(!guard.enabled()||read(scheme,false)!=2)throw std::runtime_error("Default-on failed");
        log<<"Default-on: PASS\n";
        for(int i=0;i<3;++i) {
            if(active()!=scheme||!GetSystemPowerStatus(&source)||source.ACLineStatus!=0)throw std::runtime_error("Source/plan changed; stopping test");
            write(scheme,i%2);
            const auto start=GetTickCount64();
            while(read(scheme,false)!=2&&GetTickCount64()-start<4000)pump(20);
            if(read(scheme,false)!=2)throw std::runtime_error("Notification correction failed");
            log<<"Correction "<<i+1<<": "<<GetTickCount64()-start<<" ms (20 ms observation interval)\n";
            pump(1300);
        }
        require(guard.toggle());write(scheme,1);pump(1300);
        if(guard.enabled()||read(scheme,false)!=1)throw std::runtime_error("Disable failed");
        guard.stop();guard.start(window,ini);pump(1300);
        if(guard.enabled()||read(scheme,false)!=1)throw std::runtime_error("Off preference did not persist");
        log<<"Disable and saved-off restart: PASS\n";
        require(guard.toggle());pump(1500);
        if(read(scheme,false)!=2)throw std::runtime_error("Re-enable failed");
        guard.stop();guard.start(window,ini);pump(1500);
        if(!guard.enabled()||read(scheme,false)!=2)throw std::runtime_error("On preference did not persist");
        log<<"Re-enable and saved-on restart: PASS\n";
        if(read(scheme,true)!=beforeAc)throw std::runtime_error("AC value changed");
        log<<"AC value unchanged: PASS\n";
        const auto before=guard.report();pump(5000);
        const auto after=guard.report();
        log<<"Quiet 5-second report unchanged: "<<(before==after?"yes":"no (external events may occur)")<<"\n";
        const int bytes=WideCharToMultiByte(CP_UTF8,0,after.data(),static_cast<int>(after.size()),nullptr,0,nullptr,nullptr);
        std::string utf8(bytes,'\0');
        WideCharToMultiByte(CP_UTF8,0,after.data(),static_cast<int>(after.size()),utf8.data(),bytes,nullptr,nullptr);
        log<<utf8;
        if(after.find(L"Needs attention")!=std::wstring::npos)throw std::runtime_error("Watcher reports failure");
    } catch(const std::exception& e) {
        result=1;std::cerr<<e.what()<<'\n';if(log.is_open())log<<"FAIL: "<<e.what()<<'\n';
    }
    guard.stop();
    if(captured) {
        try { write(scheme,beforeDc);if(read(scheme,false)!=beforeDc||read(scheme,true)!=beforeAc)throw std::runtime_error("Restore mismatch");if(log.is_open())log<<"Original DC restored; AC unchanged: PASS\n"; }
        catch(const std::exception& e){result=2;std::cerr<<"RESTORE FAILED "<<e.what()<<'\n';}
    }
    if(window)DestroyWindow(window);
    return result;
}
