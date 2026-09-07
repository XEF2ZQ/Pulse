#include "power.h"
#include "policy.h"
#include "process_monitor.h"
#include "scheduler_policy.h"
#include <dbt.h>
#include <windowsx.h>
#include <objbase.h>
#include <shellapi.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <wtsapi32.h>
#include <psapi.h>
#include <powrprof.h>
#include <tlhelp32.h>
#include <string>
#include <vector>
#include <array>
#include <deque>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <algorithm>

using namespace pulse;
constexpr wchar_t ClassName[]=L"PulseAdaptivePowerWindow";
constexpr UINT TrayMsg=WM_APP+1, QuitMsg=WM_APP+2, ReportMsg=WM_APP+3, EventMsg=WM_APP+4;
constexpr UINT BenchmarkMsg=WM_APP+5, ModeMsg=WM_APP+6, SchedulerRefreshMsg=WM_APP+7;
constexpr UINT TickTimer=1, StopTimer=2, CheckTimer=3;
constexpr int AutoButton=101, EcoButton=102, PauseButton=103, BoostButton=104, ReportButton=105, HelperButton=106, ExitButton=107, AcButton=108, SchedulerButton=109;
static HWND window=nullptr;
static Power power;
static Policy policy;
static ComputePolicy compute;
static ProcessMonitor processMonitor;
static bool armed=false, manageAC=false;
static scheduler::SchedulerGuard schedulerGuard;
static bool schedulerTopologyDirty=true, schedulerRefreshQueued=false;
static std::wstring schedulerTestIni;
static std::wstring schedulerIni(){return schedulerTestIni.empty()?appFolder()+L"\\Pulse.ini":schedulerTestIni;}
static void schedulerEvent();
static void schedulerRefresh(){
    schedulerTopologyDirty=true;
    if(window&&schedulerGuard.enabled()&&!schedulerRefreshQueued){
        schedulerRefreshQueued=PostMessageW(window,SchedulerRefreshMsg,0,0)!=FALSE;
    }
}
static uint64_t lastComputeSample=0;
static PowerState state(){return policy.burst?PowerState::Burst:compute.active?PowerState::Compute:PowerState::Efficiency;}
static Sample sample;
static bool enabled=false, efficientOnly=false, dryRun=false, locked=false, displayOff=false, suspended=false;
static bool keyboardRegistered=false, shuttingDown=false, autoStart=false;
static bool appliedOnBattery=true;
static HWINEVENTHOOK foregroundHook=nullptr, titleHook=nullptr;
static HPOWERNOTIFY displayNotification=nullptr, sourceNotification=nullptr, schemeNotification=nullptr, saverNotification=nullptr;
static HANDLE foregroundProcess=nullptr, guardianProcess=nullptr;
static DWORD foregroundPid=0;
static HWND foregroundWindow=nullptr;
static bool browserForeground=false;
static std::wstring foregroundName=L"Unknown", reportOnExit, fault;
static uint64_t launchTime=0, previousSample=0, previousCpu=0, previousIdle=0, previousForeground=0;
static uint64_t bursts=0, burstMs=0, samples=0, lastNav=0, lastTitle=0, previousAccounting=0, cpuAtStart=0;
static uint64_t burstAppliedAt=0, paintCount=0, buttonPaintCount=0;
static uint64_t accessibilityRequests=0;
struct Trace { double begin=0,end=0; const wchar_t* source=L""; bool burst=false,accepted=false; DWORD error=0; };
static std::array<Trace,256> traces{};
static uint64_t traceCount=0;
static bool recording=false, benchmarkPreviousEfficient=false;
static HANDLE benchmarkProcess=nullptr;
static uint64_t benchmarkDeadline=0;
static uint64_t lastApplied=0, lastAudit=0, timerInterval=0;
static std::array<bool,256> keyDown{};
static std::deque<std::wstring> events;
static std::vector<HWND> labels;
static HFONT regular=nullptr, fontSmall=nullptr, heading=nullptr, hero=nullptr, bold=nullptr;
static HBRUSH backgroundBrush=nullptr, cardBrush=nullptr;
static NOTIFYICONDATAW tray{};
static UINT taskbarCreated=0;
static std::wstring lastTrayTip;
static int lastButtonMode=-1;
static double uiScale=1;
static const COLORREF Background=RGB(13,20,31), Card=RGB(23,33,46), Text=RGB(234,241,246), Muted=RGB(151,171,188), Mint=RGB(123,231,199), Amber=RGB(255,199,112);

static int px(int value) { return static_cast<int>(value*uiScale+0.5); }
static double preciseMs() { LARGE_INTEGER t,f;QueryPerformanceCounter(&t);QueryPerformanceFrequency(&f);return 1000.0*static_cast<double>(t.QuadPart)/f.QuadPart; }
static void trace(const wchar_t* source,double begin,bool accepted,DWORD error=0) {
    if(recording)traces[traceCount++%traces.size()]={begin,preciseMs(),source,policy.burst,accepted,error};
}
static std::wstring fixed(double value,int places=2) { std::wostringstream o;o<<std::fixed<<std::setprecision(places)<<value;return o.str(); }
static void logEvent(const std::wstring& value) {
    auto elapsed=(GetTickCount64()-launchTime)/1000;
    events.push_back(std::to_wstring(elapsed)+L"s  "+value); if(events.size()>80)events.pop_front();
}
static DWORD inputAge() { LASTINPUTINFO i{sizeof(i)}; return GetLastInputInfo(&i)?GetTickCount()-i.dwTime:UINT_MAX; }
static void environment() {
    sample.now=GetTickCount64();sample.inputAge=inputAge();
    SYSTEM_POWER_STATUS s{};bool valid=GetSystemPowerStatus(&s)!=FALSE;
    sample.battery=!valid||s.ACLineStatus!=1;
    sample.blocked=locked||displayOff||suspended||!valid||s.SystemStatusFlag==1;
}
static bool browserName(const std::wstring& n) {
    return n==L"chrome.exe"||n==L"msedge.exe"||n==L"firefox.exe"||n==L"thorium.exe"||n==L"brave.exe"||n==L"opera.exe"||n==L"vivaldi.exe";
}
static std::wstring processName(HANDLE process) {
    wchar_t p[32768];DWORD n=32768;if(!QueryFullProcessImageNameW(process,0,p,&n))return L"Unknown";
    std::wstring name(p,n);name=name.substr(name.find_last_of(L"\\/")+1);
    std::transform(name.begin(),name.end(),name.begin(),[](wchar_t c){return static_cast<wchar_t>(towlower(c));});return name;
}
static void schedulerEvent() {
    scheduler::WorkloadEvidence evidence;
    evidence.interactive=policy.burst;
    evidence.productive=compute.active;
    evidence.playbackOrVoice=sustainedExcluded(foregroundName);
    // Existing signals do not establish per-thread deadline slack, cache hotness,
    // or an energy gain. Preserve unknown values instead of inventing evidence.
    schedulerGuard.event(enabled&&!sample.blocked,evidence);
}
static void updateUi();
static void setTimer();
static void turnOff(bool fromFailure=false);
static void finishBenchmark();
static void powerEvent();
static void fail(DWORD e,const std::wstring& context) {
    fault=context+L" (Windows "+std::to_wstring(e)+L")";logEvent(fault);turnOff(true);updateUi();
}
static bool applyState(const wchar_t* source=L"Policy",double began=0) {
    schedulerEvent();
    if(recording&&!began)began=preciseMs();
    if(!dryRun) { DWORD e=power.apply(state()); if(e==ERROR_RETRY){powerEvent();return false;} if(e){trace(source,began,false,e);fail(e,L"Power control paused after an unsuccessful change");return false;} }
    trace(source,began,true);
    lastApplied=GetTickCount64();
    appliedOnBattery=sample.battery;
    if(policy.burst)burstAppliedAt=lastApplied;
    else if(burstAppliedAt){burstMs+=lastApplied-burstAppliedAt;burstAppliedAt=0;}
    logEvent(std::wstring(policy.burst?L"Burst: ":compute.active?L"Compute: ":L"Efficiency: ")+(compute.active&&!policy.burst?L"Sustained CPU work; Balanced / Efficient Aggressive":policy.reason));
    if(policy.burst)++bursts;
    setTimer();updateUi();return true;
}
static void requestBurst(Signal signal,const wchar_t* source=L"Foreground") {
    double began=recording?preciseMs():0;
    if(!enabled||efficientOnly){trace(source,began,false);return;}
    environment();
    if(policy.request(signal,sample))applyState(source,began);else trace(source,began,false);
}
static void CALLBACK foregroundEvent(HWINEVENTHOOK,DWORD,HWND hwnd,LONG,LONG,DWORD,DWORD) {
    if(window&&hwnd)PostMessageW(window,EventMsg,0,reinterpret_cast<LPARAM>(hwnd));
}
static void CALLBACK titleEvent(HWINEVENTHOOK,DWORD,HWND hwnd,LONG object,LONG child,DWORD,DWORD) {
    if(hwnd==foregroundWindow&&object==OBJID_WINDOW&&child==CHILDID_SELF&&GetTickCount64()-lastTitle>500) {
        lastTitle=GetTickCount64();PostMessageW(window,EventMsg,1,reinterpret_cast<LPARAM>(hwnd));
    }
}
static void setForeground(HWND hwnd,bool allowBurst) {
    if(!hwnd||hwnd!=GetForegroundWindow())return;
    DWORD pid=0;GetWindowThreadProcessId(hwnd,&pid);if(!pid)return;
    bool changed=hwnd!=foregroundWindow;
    foregroundWindow=hwnd;
    if(pid!=foregroundPid) {
        if(foregroundProcess)CloseHandle(foregroundProcess);foregroundProcess=nullptr;
        if(titleHook)UnhookWinEvent(titleHook);titleHook=nullptr;
        foregroundPid=pid;previousForeground=0;sample.foreground=0;processMonitor.refreshSoon();
        foregroundProcess=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
        foregroundName=foregroundProcess?processName(foregroundProcess):L"Protected application";
        browserForeground=browserName(foregroundName);
        if(browserForeground&&enabled&&!sample.blocked)
            titleHook=SetWinEventHook(EVENT_OBJECT_NAMECHANGE,EVENT_OBJECT_NAMECHANGE,nullptr,titleEvent,pid,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);
    }
    bool excluded=pid==GetCurrentProcessId()||foregroundName==L"ghelper.exe"||foregroundName==L"parkcontrol.exe"||foregroundName==L"shellexperiencehost.exe";
    if(changed)schedulerEvent();
    if(allowBurst&&changed&&!excluded)requestBurst(Signal::Foreground);
}
static void registerInput(bool on) {
    if(on==keyboardRegistered)return;
    RAWINPUTDEVICE keyboard{0x01,0x06,static_cast<DWORD>(on?RIDEV_INPUTSINK:RIDEV_REMOVE),on?window:nullptr};
    if(RegisterRawInputDevices(&keyboard,1,sizeof(keyboard))) { keyboardRegistered=on;keyDown.fill(false); }
    else logEvent(L"Keyboard navigation signals unavailable; foreground and load detection remain active");
}
static void rawKeyboard(LPARAM lparam) {
    RAWINPUT input{};UINT size=sizeof(input);
    if(GetRawInputData(reinterpret_cast<HRAWINPUT>(lparam),RID_INPUT,&input,&size,sizeof(RAWINPUTHEADER))==UINT(-1)||input.header.dwType!=RIM_TYPEKEYBOARD)return;
    auto& key=input.data.keyboard;UINT v=key.VKey;if(v>=256)return;
    bool released=(key.Flags&RI_KEY_BREAK)!=0; bool repeat=keyDown[v];keyDown[v]=!released;
    if(released||repeat||!browserForeground||!enabled||efficientOnly)return;
    bool control=(GetAsyncKeyState(VK_CONTROL)&0x8000)!=0;
    bool alt=(GetAsyncKeyState(VK_MENU)&0x8000)!=0;
    if((control&&(v=='T'||v=='L'||v=='R'||v==VK_TAB))||v==VK_RETURN||v==VK_F5||(alt&&(v==VK_LEFT||v==VK_RIGHT))) {
        lastNav=GetTickCount64();requestBurst(Signal::Navigation,L"Keyboard navigation");
    }
    // Nothing is translated into text, recorded, or transmitted.
}
static void collect() {
    environment(); ++samples;
    FILETIME idle{},kernel{},user{};
    if(GetSystemTimes(&idle,&kernel,&user)) {
        uint64_t total=fileTimeValue(kernel)+fileTimeValue(user), id=fileTimeValue(idle);
        if(previousCpu&&total>previousCpu&&id>=previousIdle)
            sample.system=std::clamp(100.0*(1.0-static_cast<double>(id-previousIdle)/(total-previousCpu)),0.0,100.0);
        previousCpu=total;previousIdle=id;
    }
    if(foregroundProcess) {
        FILETIME c{},x{},k{},u{};
        if(GetProcessTimes(foregroundProcess,&c,&x,&k,&u)) {
            auto cpu=fileTimeValue(k)+fileTimeValue(u);
            if(previousForeground&&sample.now>previousSample&&cpu>=previousForeground)
                sample.foreground=100.0*static_cast<double>(cpu-previousForeground)/(10000.0*(sample.now-previousSample));
            previousForeground=cpu;
        } else sample.foreground=0;
    }
    previousAccounting=sample.now;previousSample=sample.now;
}
static DWORD guardian() {
    if(guardianProcess) { if(WaitForSingleObject(guardianProcess,0)==WAIT_TIMEOUT)return 0;CloseHandle(guardianProcess);guardianProcess=nullptr; }
    std::wstring readyName=L"Local\\PulseGuardianReady-"+std::to_wstring(GetCurrentProcessId());
    HANDLE ready=CreateEventW(nullptr,TRUE,FALSE,readyName.c_str());if(!ready)return GetLastError();
    std::wstring exe=executablePath(),cmd=L"\""+exe+L"\" --guard "+std::to_wstring(GetCurrentProcessId());
    STARTUPINFOW si{sizeof(si)};si.dwFlags=STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE;PROCESS_INFORMATION pi{};
    if(!CreateProcessW(exe.c_str(),cmd.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,appFolder().c_str(),&si,&pi)){DWORD e=GetLastError();CloseHandle(ready);return e;}
    CloseHandle(pi.hThread);guardianProcess=pi.hProcess;
    DWORD result=WaitForSingleObject(ready,4000);CloseHandle(ready);
    return result==WAIT_OBJECT_0?0:ERROR_TIMEOUT;
}
static void turnOn() {
    armed=true;environment();
    if(enabled)return;fault.clear();
    if(!sample.battery&&!manageAC){setTimer();updateUi();return;}
    if(!dryRun) {
        DWORD e=guardian();if(e){fail(e,L"Recovery companion could not start");return;}
        e=power.restore();if(e){fail(e,L"Previous settings could not be recovered");return;}
        e=power.capture(sample.battery?2:1);if(e){fail(e,L"Original settings could not be saved");return;}
        e=power.configure();if(e==ERROR_RETRY){DWORD restored=power.restore();if(restored){fail(restored,L"Source change recovery failed");return;}PostMessageW(window,WM_POWERBROADCAST,PBT_APMPOWERSTATUSCHANGE,0);return;}if(e){fail(e,L"Windows did not accept the requested settings");return;}
    }
    enabled=true;environment();appliedOnBattery=sample.battery;policy.reset(sample.now);compute.reset();processMonitor.clear();lastComputeSample=0;previousAccounting=sample.now;
    foregroundHook=SetWinEventHook(EVENT_SYSTEM_FOREGROUND,EVENT_SYSTEM_FOREGROUND,nullptr,foregroundEvent,0,0,WINEVENT_OUTOFCONTEXT);
    if(!foregroundHook)logEvent(L"Foreground notifications unavailable; periodic fallback remains active");
    foregroundPid=0;setForeground(GetForegroundWindow(),false);registerInput(!sample.blocked);
    lastApplied=sample.now;lastAudit=sample.now;
    schedulerEvent();
    logEvent(dryRun?L"Observation mode: no power settings changed":L"Automatic control started; original settings saved");
    setTimer();updateUi();
}
static void turnOff(bool fromFailure) {
    finishBenchmark();
    if(burstAppliedAt){burstMs+=GetTickCount64()-burstAppliedAt;burstAppliedAt=0;}
    enabled=false;armed=false;policy.burst=false;compute.reset();processMonitor.clear();schedulerEvent();
    if(foregroundHook)UnhookWinEvent(foregroundHook);foregroundHook=nullptr;
    if(titleHook)UnhookWinEvent(titleHook);titleHook=nullptr;
    registerInput(false);
    if(!dryRun) {
        DWORD e=power.restore();if(e){fault=L"Restoration needs attention: Windows "+std::to_wstring(e)+L". Recovery file retained.";logEvent(fault);}
    }
    if(foregroundProcess)CloseHandle(foregroundProcess);foregroundProcess=nullptr;foregroundPid=0;previousForeground=previousCpu=0;
    if(!fromFailure)logEvent(L"Paused; owned settings restored, newer external choices preserved");
    setTimer();updateUi();
}
static void setTimer() {
    if(!window)return;
    uint64_t burstInterval=policy.deadline>GetTickCount64()?std::clamp<uint64_t>(policy.deadline-GetTickCount64(),16,125):16;
    uint64_t next=enabled?(sample.blocked?10000:policy.burst?burstInterval:(compute.active||compute.candidate||processMonitor.hasTools())?1000:sample.inputAge>15000?3000:1000):IsWindowVisible(window)?1000:0;
    if(timerInterval==next)return;KillTimer(window,TickTimer);timerInterval=next;
    if(next)SetCoalescableTimer(window,TickTimer,static_cast<UINT>(next),nullptr,policy.burst?0:100);
}
static void finishBenchmark() {
    if(!benchmarkProcess)return;
    recording=false;CloseHandle(benchmarkProcess);benchmarkProcess=nullptr;
    efficientOnly=benchmarkPreviousEfficient;
    if(enabled&&efficientOnly&&(policy.burst||compute.active)){policy.reset(GetTickCount64());compute.reset();applyState();}
    logEvent(L"Benchmark finished; previous control policy restored");updateUi();
}
static void powerEvent();
static void tick() {
    bool oldBlocked=sample.blocked;environment();
    if(!enabled){if(armed&&(sample.battery||manageAC))turnOn();setTimer();return;}
    if(appliedOnBattery!=sample.battery){powerEvent();return;}
    collect();
    if(benchmarkProcess&&(WaitForSingleObject(benchmarkProcess,0)!=WAIT_TIMEOUT||sample.now>=benchmarkDeadline))finishBenchmark();
    if(enabled) {
        // Compare with the applied source, not the latest sensor sample: a timer
        // may observe a charger change before its Windows notification arrives.
        if(appliedOnBattery!=sample.battery){powerEvent();return;}
        if(oldBlocked!=sample.blocked)registerInput(!sample.blocked);
        if(!sample.blocked)setForeground(GetForegroundWindow(),false);
        PowerState before=state();
        if(sample.blocked){compute.reset();processMonitor.clear();lastComputeSample=0;}
        else if(!efficientOnly&&(!lastComputeSample||sample.now-lastComputeSample>=900)){
            processMonitor.collect(sample.now);lastComputeSample=sample.now;
            compute.update(sample.now,processMonitor.cpu,sustainedExcluded(foregroundName)?0:sample.foreground,false);
        }
        if(efficientOnly){compute.reset();if(policy.burst)policy.reset(sample.now);}
        else policy.tick(sample);
        if(before!=state())applyState();
        if(enabled&&!dryRun&&sample.now-lastAudit>=10000) {
            lastAudit=sample.now;DWORD e=power.verify(state(),true);
            if(!e&&(!guardianProcess||WaitForSingleObject(guardianProcess,0)!=WAIT_TIMEOUT))e=ERROR_PROCESS_ABORTED;
            if(e)fail(e,L"Another power utility changed a managed setting; control paused");
        }
    }
    // Refresh the panel on state changes; detailed measurements are on demand.
    setTimer();
}
static bool saveText(const std::wstring& path,const std::wstring& text) {
    int size=WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);
    std::string bytes(size,'\0');WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),bytes.data(),size,nullptr,nullptr);
    std::ofstream f(std::filesystem::path(path),std::ios::binary|std::ios::trunc);f.write(bytes.data(),bytes.size());return f.good();
}
static uint64_t ownCpu() {FILETIME c{},e{},k{},u{};GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u);return fileTimeValue(k)+fileTimeValue(u);}
static std::wstring runtimeReport() {
    PROCESS_MEMORY_COUNTERS_EX memory{};GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory));
    uint64_t elapsed=GetTickCount64()-launchTime;double cpuMs=(ownCpu()-cpuAtStart)/10000.0;
    std::wostringstream out;out<<power.report()<<L"\nRuntime\nMode: "<<(dryRun?L"Observation (no writes)":enabled?L"Controlling":L"Paused")
        <<L"\nArmed: "<<(armed?L"yes":L"no")<<L"\nPlugged-in control: "<<(manageAC?L"on":L"off")
        <<L"\nPower state: "<<(state()==PowerState::Burst?L"Burst":state()==PowerState::Compute?L"Compute":enabled?L"Efficiency":armed?L"Waiting for battery":L"Restored")
        <<L"\nCompute CPU percent of one core: "<<fixed(processMonitor.cpu)
        <<L"\nCompute process scans: "<<processMonitor.scans<<L"\nCompute process reads: "<<processMonitor.reads
        <<L"\nControl policy: "<<(efficientOnly?L"Keep efficient":L"Automatic")
        <<L"\nBenchmark lease: "<<(benchmarkProcess?L"active":L"off")
        <<L"\nElapsed ms: "<<elapsed<<L"\nController CPU time ms: "<<fixed(cpuMs)
        <<L"\nMean CPU percent of one logical core: "<<fixed(elapsed?100.0*cpuMs/elapsed:0,4)
        <<L"\nPrivate memory MiB: "<<fixed(memory.PrivateUsage/1048576.0)
        <<L"\nWorking set MiB: "<<fixed(memory.WorkingSetSize/1048576.0)
        <<L"\nSamples: "<<samples<<L"\nBurst count: "<<bursts<<L"\nBurst time ms: "<<(burstMs+(burstAppliedAt?GetTickCount64()-burstAppliedAt:0))
        <<L"\nWindow paints: "<<paintCount<<L"\nButton paints: "<<buttonPaintCount
        <<L"\nWindows accessibility queries: "<<accessibilityRequests
        <<L"\nPower value writes: "<<power.writes<<L"\nLast power transition ms: "<<fixed(power.lastApplyMs)
        <<L"\nForeground notification: "<<(foregroundHook?L"active":L"off")<<L"\nKeyboard navigation: "<<(keyboardRegistered?L"active":L"off")
        <<L"\nRecovery companion: "<<(guardianProcess&&WaitForSingleObject(guardianProcess,0)==WAIT_TIMEOUT?L"waiting":L"off")
        <<L"\nFault: "<<fault<<L"\n\nRecent decisions (no keys, titles, URLs or audio recorded)\n";
    out<<schedulerGuard.report()<<L"\n";
    for(const auto& line:events)out<<line<<L"\n";
    if(traceCount){out<<L"\nBenchmark traces: sequence,begin_qpc_ms,end_qpc_ms,source,burst,accepted,error\n";
        for(uint64_t i=traceCount>traces.size()?traceCount-traces.size():0;i<traceCount;++i){const auto& t=traces[i%traces.size()];
            out<<i<<L","<<fixed(t.begin,4)<<L","<<fixed(t.end,4)<<L","<<t.source<<L","<<t.burst<<L","<<t.accepted<<L","<<t.error<<L"\n";}
    }
    return out.str();
}
static void setLabel(size_t i,const std::wstring& text) {
    wchar_t old[1024];GetWindowTextW(labels[i],old,1024);if(text!=old)SetWindowTextW(labels[i],text.c_str());
}
static void updateTray() {
    std::wstring tip=L"Pulse - ";tip+=!fault.empty()?L"Needs attention":!enabled?(armed?L"Waiting for battery":L"Paused"):compute.active&&!policy.burst?L"Sustained compute / Balanced":policy.burst?L"Short performance burst":L"Best efficiency / boost disabled";
    if(tip==lastTrayTip)return;lastTrayTip=tip;
    wcsncpy_s(tray.szTip,tip.c_str(),_TRUNCATE);Shell_NotifyIconW(NIM_MODIFY,&tray);
}
static void updateUi() {
    if(labels.size()<15)return;
    setLabel(2,dryRun?L"OBSERVATION MODE":!fault.empty()?L"NEEDS ATTENTION":enabled?L"ADAPTIVE POWER COMPANION":armed?L"BATTERY CONTROL READY":L"CONTROL PAUSED");
    setLabel(3,!fault.empty()?L"Control paused":!enabled?(armed?L"Ready for battery.":L"Your settings restored"):compute.active&&!policy.burst?L"Room to finish the job.":policy.burst?L"A little more momentum.":L"Quietly efficient.");
    setLabel(4,!fault.empty()?fault:!enabled?(armed?L"Plugged-in control is off. Windows keeps your restored settings.":L"Resume when you want Pulse to manage responsiveness."):efficientOnly?L"Efficiency stays on until you choose Automatic.":compute.active&&!policy.burst?L"Sustained computation detected. Efficiency returns when CPU work settles.":policy.reason);
    setLabel(5,!enabled?L"WINDOWS  /  restored":policy.burst?L"WINDOWS  /  best performance":compute.active?L"WINDOWS  /  balanced":L"WINDOWS  /  best power efficiency");
    setLabel(6,!enabled?L"CPU BOOST  /  restored":policy.burst?L"CPU BOOST  /  aggressive":compute.active?L"CPU BOOST  /  efficient aggressive":L"CPU BOOST  /  disabled");
    SYSTEM_POWER_STATUS s{};GetSystemPowerStatus(&s);
    setLabel(7,std::wstring(s.ACLineStatus==1?L"PLUGGED IN":L"ON BATTERY")+(s.BatteryLifePercent<=100?L"  ·  "+std::to_wstring(s.BatteryLifePercent)+L"%":L""));
    setLabel(12,L"Short boosts  "+std::to_wstring(bursts)+L"     ·     Burst limit  "+(sample.battery?L"1.8 seconds":L"2.4 seconds"));
    setLabel(13,schedulerGuard.status());
    setLabel(14,L"G Helper keeps fans, GPU, charge limit and hardware power limits.\nClose this window to keep Pulse in the system tray.");
    setLabel(11,manageAC?L"15%  active cores\n80%  minimum processor state":L"Off by default\nYour plugged-in settings stay in control");
    const wchar_t* acTitle=manageAC?L"Plugged-in control: ON":L"Plugged-in control: OFF";
    wchar_t acOld[64];GetWindowTextW(GetDlgItem(window,AcButton),acOld,64);
    if(wcscmp(acOld,acTitle)){SetWindowTextW(GetDlgItem(window,AcButton),acTitle);InvalidateRect(GetDlgItem(window,AcButton),nullptr,FALSE);}
    const wchar_t* schedulerTitle=schedulerGuard.enabled()?L"Scheduler guard: ON":L"Scheduler guard: OFF";
    wchar_t schedulerOld[64]{};GetWindowTextW(GetDlgItem(window,SchedulerButton),schedulerOld,64);
    if(wcscmp(schedulerOld,schedulerTitle)){SetWindowTextW(GetDlgItem(window,SchedulerButton),schedulerTitle);InvalidateRect(GetDlgItem(window,SchedulerButton),nullptr,FALSE);}
    const wchar_t* pauseTitle=armed?L"Pause & restore":L"Resume control";
    wchar_t previousTitle[64];GetWindowTextW(GetDlgItem(window,PauseButton),previousTitle,64);
    if(wcscmp(previousTitle,pauseTitle))SetWindowTextW(GetDlgItem(window,PauseButton),pauseTitle);
    EnableWindow(GetDlgItem(window,BoostButton),enabled&&!sample.blocked);
    int buttonMode=armed?(efficientOnly?2:1):0;
    if(buttonMode!=lastButtonMode){lastButtonMode=buttonMode;InvalidateRect(GetDlgItem(window,AutoButton),nullptr,FALSE);InvalidateRect(GetDlgItem(window,EcoButton),nullptr,FALSE);}
    updateTray();
}
static HFONT font(int height,int weight) {return CreateFontW(-px(height),0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");}
static HWND label(const wchar_t* text,int x,int y,int w,int h,HFONT f) {
    HWND c=CreateWindowExW(0,L"STATIC",text,WS_CHILD|WS_VISIBLE|SS_LEFT,px(x),px(y),px(w),px(h),window,nullptr,nullptr,nullptr);
    SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(f),FALSE);labels.push_back(c);return c;
}
static void button(int id,const wchar_t* text,int x,int y,int w,int h) {
    HWND c=CreateWindowExW(0,L"BUTTON",text,WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,px(x),px(y),px(w),px(h),window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),nullptr,nullptr);
    SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(regular),FALSE);
}
static void makeUi() {
    regular=font(15,FW_NORMAL);fontSmall=font(13,FW_NORMAL);heading=font(32,FW_SEMIBOLD);hero=font(34,FW_SEMIBOLD);bold=font(15,FW_SEMIBOLD);
    label(L"Pulse",92,27,200,46,heading); // 0
    label(L"Power when it matters.",94,75,450,24,regular); // 1
    label(L"ADAPTIVE POWER COMPANION",42,129,700,24,fontSmall); // 2
    label(L"Quietly efficient.",42,160,700,49,hero); // 3
    label(L"",42,218,700,44,regular); // 4
    label(L"",42,282,360,25,fontSmall); // 5
    label(L"",428,282,320,25,fontSmall); // 6
    label(L"",565,49,220,28,bold); // 7
    label(L"ON BATTERY",42,400,300,24,fontSmall); // 8
    label(L"10%  active cores\n15%  minimum processor state",42,435,310,60,bold); // 9
    label(L"PLUGGED IN",428,400,300,24,fontSmall); // 10
    label(L"15%  active cores\n80%  minimum processor state",428,435,310,60,bold); // 11
    label(L"",42,587,690,24,regular); // 12
    label(L"",42,616,710,24,fontSmall); // 13
    label(L"",32,716,740,43,fontSmall); // 14
    label(L"Standard parking · Automatic long and short threads",42,552,690,26,fontSmall); // 15
    button(SchedulerButton,L"Scheduler guard: OFF",32,510,362,36);
    button(AcButton,L"Plugged-in control: OFF",428,510,344,36);
    button(AutoButton,L"Automatic",32,334,220,42);button(EcoButton,L"Keep efficient",269,334,220,42);button(PauseButton,L"Pause & restore",506,334,266,42);
    button(BoostButton,L"Boost now",32,654,167,42);button(ReportButton,L"Diagnostics",213,654,167,42);button(HelperButton,L"Open G Helper",394,654,180,42);button(ExitButton,L"Restore & exit",588,654,184,42);
}
static void rectangle(HDC dc,int x,int y,int w,int h,COLORREF color,int radius=14) {
    HBRUSH b=CreateSolidBrush(color);auto old=SelectObject(dc,b);auto pen=SelectObject(dc,GetStockObject(NULL_PEN));
    RoundRect(dc,px(x),px(y),px(x+w),px(y+h),px(radius),px(radius));SelectObject(dc,pen);SelectObject(dc,old);DeleteObject(b);
}
static void paint() {
    ++paintCount;
    PAINTSTRUCT ps;HDC dc=BeginPaint(window,&ps);RECT r;GetClientRect(window,&r);FillRect(dc,&r,backgroundBrush);
    rectangle(dc,24,116,756,204,Card);rectangle(dc,24,390,370,116,Card);rectangle(dc,410,390,370,116,Card);
    rectangle(dc,24,548,756,90,Card);
    DrawIconEx(dc,px(30),px(36),LoadIconW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(1)),px(48),px(48),0,nullptr,DI_NORMAL);
    EndPaint(window,&ps);
}
static void drawButton(DRAWITEMSTRUCT* item) {
    ++buttonPaintCount;
    FillRect(item->hDC,&item->rcItem,backgroundBrush);
    bool selected=(item->CtlID==AutoButton&&armed&&!efficientOnly)||(item->CtlID==EcoButton&&armed&&efficientOnly)||(item->CtlID==AcButton&&manageAC)||(item->CtlID==SchedulerButton&&schedulerGuard.enabled());
    COLORREF color=selected?Mint:Card;if(item->itemState&ODS_SELECTED)color=RGB(71,143,130);
    HBRUSH b=CreateSolidBrush(color);auto old=SelectObject(item->hDC,b);auto pen=SelectObject(item->hDC,GetStockObject(NULL_PEN));
    RoundRect(item->hDC,item->rcItem.left,item->rcItem.top,item->rcItem.right,item->rcItem.bottom,px(10),px(10));SelectObject(item->hDC,pen);SelectObject(item->hDC,old);DeleteObject(b);
    SetBkMode(item->hDC,TRANSPARENT);SetTextColor(item->hDC,(item->itemState&ODS_DISABLED)?Muted:selected?Background:Text);auto f=SelectObject(item->hDC,bold);
    wchar_t text[128];GetWindowTextW(item->hwndItem,text,128);DrawTextW(item->hDC,text,-1,&item->rcItem,DT_SINGLELINE|DT_CENTER|DT_VCENTER|DT_NOPREFIX);SelectObject(item->hDC,f);
    if(item->itemState&ODS_FOCUS){RECT focus=item->rcItem;InflateRect(&focus,-px(4),-px(4));DrawFocusRect(item->hDC,&focus);}
}
static void showWindow() {ShowWindow(window,SW_SHOW);ShowWindow(window,SW_RESTORE);SetForegroundWindow(window);setTimer();updateUi();}
static void trayMenu() {
    HMENU m=CreatePopupMenu();AppendMenuW(m,MF_STRING,1,L"Open Pulse");AppendMenuW(m,MF_STRING,2,L"Automatic");AppendMenuW(m,MF_STRING,3,L"Keep efficient");
    AppendMenuW(m,MF_STRING,4,armed?L"Pause & restore":L"Resume control");AppendMenuW(m,MF_SEPARATOR,0,nullptr);AppendMenuW(m,MF_STRING,5,L"Restore & exit");
    POINT pt;GetCursorPos(&pt);SetForegroundWindow(window);int id=TrackPopupMenu(m,TPM_RETURNCMD|TPM_NONOTIFY,pt.x,pt.y,0,window,nullptr);DestroyMenu(m);PostMessageW(window,WM_NULL,0,0);
    if(id==1)showWindow();else if(id==2)SendMessageW(window,WM_COMMAND,AutoButton,0);else if(id==3)SendMessageW(window,WM_COMMAND,EcoButton,0);else if(id==4)SendMessageW(window,WM_COMMAND,PauseButton,0);else if(id==5)SendMessageW(window,QuitMsg,0,0);
}
static void cleanup() {
    if(shuttingDown)return;shuttingDown=true;
    if(!reportOnExit.empty())saveText(reportOnExit,runtimeReport());
    turnOff(false);Shell_NotifyIconW(NIM_DELETE,&tray);
    if(displayNotification)UnregisterPowerSettingNotification(displayNotification);
    if(sourceNotification)UnregisterPowerSettingNotification(sourceNotification);
    if(schemeNotification)UnregisterPowerSettingNotification(schemeNotification);
    if(saverNotification)UnregisterPowerSettingNotification(saverNotification);
    WTSUnRegisterSessionNotification(window);if(foregroundProcess)CloseHandle(foregroundProcess);
    if(benchmarkProcess){CloseHandle(benchmarkProcess);benchmarkProcess=nullptr;}
    DeleteObject(regular);DeleteObject(fontSmall);DeleteObject(heading);DeleteObject(hero);DeleteObject(bold);DeleteObject(backgroundBrush);DeleteObject(cardBrush);
}
static void powerEvent() {
    environment();schedulerEvent();if(!armed)return;
    bool shouldControl=sample.battery||manageAC;
    if(enabled&&(!shouldControl||appliedOnBattery!=sample.battery)){
        turnOff(false);if(!fault.empty())return;armed=true;
    }
    if(shouldControl&&!enabled)turnOn();
    if(enabled){registerInput(!sample.blocked);
        if(sample.blocked&&(policy.burst||compute.active)){policy.reset(sample.now);compute.reset();applyState();}}
    setTimer();updateUi();
}
static LRESULT CALLBACK procedure(HWND h,UINT m,WPARAM w,LPARAM l) {
    if(m==taskbarCreated&&taskbarCreated) {Shell_NotifyIconW(NIM_ADD,&tray);return 0;}
    switch(m) {
    case WM_CREATE:window=h;return 0;
    case WM_GETOBJECT:++accessibilityRequests;break;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:paint();return 0;
    case WM_DRAWITEM:drawButton(reinterpret_cast<DRAWITEMSTRUCT*>(l));return TRUE;
    case WM_CTLCOLORSTATIC:{HDC dc=reinterpret_cast<HDC>(w);HWND c=reinterpret_cast<HWND>(l);
        bool outside=labels.size()>14&&(c==labels[0]||c==labels[1]||c==labels[7]||c==labels[14]);
        SetTextColor(dc,Text);SetBkColor(dc,outside?Background:Card);SetBkMode(dc,OPAQUE);
        return reinterpret_cast<LRESULT>(outside?backgroundBrush:cardBrush);}
    case SchedulerRefreshMsg:
        schedulerRefreshQueued=false;
        if(schedulerGuard.enabled()&&schedulerTopologyDirty){schedulerGuard.refresh();schedulerTopologyDirty=false;}
        schedulerEvent();updateUi();return 0;
    case WM_DEVICECHANGE:if(w==DBT_DEVNODES_CHANGED)schedulerRefresh();break;
    case WM_INPUT:rawKeyboard(l);return DefWindowProcW(h,m,w,l);
    case WM_TIMER:
        if(w==TickTimer)tick();else if(w==StopTimer)SendMessageW(h,QuitMsg,0,0);
        else if(w==CheckTimer){KillTimer(h,CheckTimer);if(enabled&&!dryRun&&GetTickCount64()-lastApplied>700){DWORD e=power.verify(state(),true);if(e)fail(e,L"External power setting changed; Pulse paused to avoid a conflict");}}
        return 0;
    case EventMsg:
        if(w==0&&enabled)setForeground(reinterpret_cast<HWND>(l),true);
        else if(enabled&&browserForeground&&GetTickCount64()-lastNav>3000&&inputAge()<1200) {lastNav=GetTickCount64();requestBurst(Signal::Navigation,L"Title navigation");}return 0;
    case WM_POWERBROADCAST:
        if(w==PBT_APMSUSPEND){suspended=true;powerEvent();}
        else if(w==PBT_APMRESUMEAUTOMATIC||w==PBT_APMRESUMESUSPEND){suspended=false;previousCpu=previousForeground=0;schedulerRefresh();powerEvent();}
        else if(w==PBT_APMPOWERSTATUSCHANGE)powerEvent();
        else if(w==PBT_POWERSETTINGCHANGE){auto* p=reinterpret_cast<POWERBROADCAST_SETTING*>(l);
            if(p->PowerSetting==GUID_CONSOLE_DISPLAY_STATE&&p->DataLength==sizeof(DWORD)){DWORD value;memcpy(&value,p->Data,sizeof(value));displayOff=value==0;powerEvent();}
            else if(p->PowerSetting==GUID_ACDC_POWER_SOURCE||p->PowerSetting==GUID_POWER_SAVING_STATUS)powerEvent();
            else if(p->PowerSetting==GUID_ACTIVE_POWERSCHEME)SetTimer(h,CheckTimer,1000,nullptr);
        }return TRUE;
    case WM_WTSSESSION_CHANGE:if(w==WTS_SESSION_LOCK)locked=true;else if(w==WTS_SESSION_UNLOCK)locked=false;powerEvent();return 0;
    case WM_QUERYENDSESSION:return TRUE;
    case WM_ENDSESSION:if(w){cleanup();DestroyWindow(h);}return 0;
    case WM_CLOSE:ShowWindow(h,SW_HIDE);setTimer();return 0;
    case QuitMsg:cleanup();DestroyWindow(h);return 0;
    case ReportMsg:saveText(appFolder()+L"\\diagnostics.txt",runtimeReport());return 0;
    case ModeMsg:
        if(w>=2){if(manageAC!=(w==3))SendMessageW(h,WM_COMMAND,AcButton,0);return manageAC==(w==3);}
        if(!armed)return 0;SendMessageW(h,WM_COMMAND,w?EcoButton:AutoButton,0);return 1;
    case BenchmarkMsg:
        if(!w){finishBenchmark();return 1;}
        if(!enabled||dryRun||benchmarkProcess)return 0;
        benchmarkProcess=OpenProcess(SYNCHRONIZE,FALSE,static_cast<DWORD>(l));if(!benchmarkProcess)return 0;
        benchmarkPreviousEfficient=efficientOnly;benchmarkDeadline=GetTickCount64()+15*60*1000;traceCount=0;recording=true;
        logEvent(L"Benchmark started; timing trace enabled temporarily");return 1;
    case WM_DESTROY:PostQuitMessage(0);return 0;
    case WM_SIZE:if(w==SIZE_MINIMIZED){ShowWindow(h,SW_HIDE);setTimer();}return 0;
    case WM_DPICHANGED:{uiScale=HIWORD(w)/96.0;auto r=reinterpret_cast<RECT*>(l);SetWindowPos(h,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);
        for(auto c:labels)DestroyWindow(c);labels.clear();for(int id=101;id<=109;++id)DestroyWindow(GetDlgItem(h,id));
        DeleteObject(regular);DeleteObject(fontSmall);DeleteObject(heading);DeleteObject(hero);DeleteObject(bold);makeUi();updateUi();return 0;}
    case TrayMsg:if(l==WM_LBUTTONUP||l==WM_LBUTTONDBLCLK)showWindow();else if(l==WM_RBUTTONUP)trayMenu();return 0;
    case WM_COMMAND:
        switch(LOWORD(w)) {
        case AutoButton:efficientOnly=false;turnOn();updateUi();break;
        case EcoButton:efficientOnly=true;turnOn();if(enabled&&(policy.burst||compute.active)){policy.reset(GetTickCount64());compute.reset();applyState();}updateUi();break;
        case PauseButton:if(armed)turnOff(false);else turnOn();break;
        case SchedulerButton:{
            if(HIWORD(w)!=BN_CLICKED)break;
            const bool next=!schedulerGuard.enabled();
            if(!scheduler::saveSchedulerPreference(schedulerIni(),next)){logEvent(L"Could not save scheduler preference");break;}
            schedulerGuard.enabled(next);
            if(next&&schedulerTopologyDirty)schedulerRefresh();
            schedulerEvent();updateUi();break;}
        case AcButton:{
            bool next=!manageAC;
            if(!WritePrivateProfileStringW(L"Preferences",L"ManageAC",next?L"1":L"0",(appFolder()+L"\\Pulse.ini").c_str())){logEvent(L"Could not save plugged-in preference");break;}
            manageAC=next;powerEvent();updateUi();break;}
        case BoostButton:efficientOnly=false;requestBurst(Signal::Manual,L"Manual");break;
        case ReportButton:{auto path=appFolder()+L"\\diagnostics.txt";if(saveText(path,runtimeReport()))ShellExecuteW(h,L"open",path.c_str(),nullptr,nullptr,SW_SHOWNORMAL);break;}
        case HelperButton:{wchar_t exe[32768];GetPrivateProfileStringW(L"Integration",L"GHelperPath",L"",exe,32768,(appFolder()+L"\\Pulse.ini").c_str());if(*exe)ShellExecuteW(h,L"open",exe,nullptr,nullptr,SW_SHOWNORMAL);else MessageBoxW(h,L"G Helper remains available from its existing system tray icon.",L"Pulse",MB_OK);break;}
        case ExitButton:SendMessageW(h,QuitMsg,0,0);break;
        }return 0;
    }return DefWindowProcW(h,m,w,l);
}
static int guardMain(DWORD parent) {
    HANDLE process=OpenProcess(SYNCHRONIZE,FALSE,parent);
    if(!process)return 2;
    HANDLE ready=OpenEventW(EVENT_MODIFY_STATE,FALSE,(L"Local\\PulseGuardianReady-"+std::to_wstring(parent)).c_str());
    if(ready){SetEvent(ready);CloseHandle(ready);}
    WaitForSingleObject(process,INFINITE);CloseHandle(process);
    HANDLE mutex=CreateMutexW(nullptr,FALSE,L"Local\\Pulse.Power.Controller.v1");if(!mutex)return 3;
    DWORD wait=WaitForSingleObject(mutex,10000);if(wait!=WAIT_OBJECT_0&&wait!=WAIT_ABANDONED){CloseHandle(mutex);return 0;}
    DWORD e=power.restore(parent);ReleaseMutex(mutex);CloseHandle(mutex);return static_cast<int>(e);
}
static int exercise(const std::wstring& output) {
    std::wostringstream report;report<<L"Power integration exercise\nBEFORE\n"<<power.report();
    DWORD e=guardian();if(!e)e=power.restore();if(!e)e=power.capture();if(!e)e=power.configure();
    report<<L"\nConfigure result: "<<e<<L"\n";
    if(!e) {report<<L"EFFICIENCY\n"<<power.report();e=power.verify(false,true);report<<L"Verify efficiency: "<<e<<L"\n";}
    if(!e) {
        e=power.apply(true);report<<L"Burst apply: "<<e<<L", transition ms: "<<fixed(power.lastApplyMs)<<L"\n";
        if(!e){report<<L"BURST\n"<<power.report();Sleep(375);e=power.apply(false);report<<L"Return to efficiency: "<<e<<L", transition ms: "<<fixed(power.lastApplyMs)<<L"\n";}
    }
    if(!e){e=power.apply(PowerState::Compute);report<<L"COMPUTE\n"<<power.report()<<L"Compute apply: "<<e<<L"\n";if(!e)e=power.verify(PowerState::Compute,true);report<<L"Verify compute: "<<e<<L"\n";}
    DWORD restore=power.restore();report<<L"Restore result: "<<restore<<L"\nAFTER\n"<<power.report();
    if(!saveText(output,report.str()))return ERROR_WRITE_FAULT;return static_cast<int>(e?e:restore);
}
static int schedulerGuiTest(HINSTANCE instance,const std::wstring& output) {
    dryRun=true;autoStart=false;armed=enabled=false;
    schedulerTestIni=appFolder()+L"\\scheduler-ui-test-"+std::to_wstring(GetCurrentProcessId())+L".ini";
    schedulerGuard.enabled(scheduler::loadSchedulerPreference(schedulerIni()));
    const bool defaultOff=!schedulerGuard.enabled();
    schedulerGuard.refresh();schedulerTopologyDirty=false;
    backgroundBrush=CreateSolidBrush(Background);cardBrush=CreateSolidBrush(Card);
    WNDCLASSEXW wc{sizeof(wc)};wc.lpfnWndProc=procedure;wc.hInstance=instance;wc.lpszClassName=L"PulseSchedulerTestOnly";
    if(!RegisterClassExW(&wc))return 2;
    window=CreateWindowExW(0,wc.lpszClassName,L"Pulse scheduler UI test",WS_OVERLAPPED,0,0,804,780,nullptr,nullptr,instance,nullptr);
    if(!window)return 3;
    makeUi();updateUi();
    HWND button=GetDlgItem(window,SchedulerButton);
    SendMessageW(window,WM_COMMAND,MAKEWPARAM(SchedulerButton,BN_CLICKED),reinterpret_cast<LPARAM>(button));
    const bool on=schedulerGuard.enabled()&&scheduler::loadSchedulerPreference(schedulerIni());
    wchar_t title[64]{};GetWindowTextW(button,title,64);const bool onText=wcscmp(title,L"Scheduler guard: ON")==0;
    SendMessageW(window,WM_COMMAND,MAKEWPARAM(SchedulerButton,BN_CLICKED),reinterpret_cast<LPARAM>(button));
    const bool off=!schedulerGuard.enabled()&&!scheduler::loadSchedulerPreference(schedulerIni());
    GetWindowTextW(button,title,64);const bool offText=wcscmp(title,L"Scheduler guard: OFF")==0;
    RECT left{},right{};GetWindowRect(button,&left);GetWindowRect(GetDlgItem(window,AcButton),&right);
    const bool layout=left.right<right.left&&left.bottom==right.bottom;
    const bool noPowerWrites=power.writes==0&&!enabled&&!armed;
    std::wostringstream report;report<<L"defaultOff="<<defaultOff<<L" offPersisted="<<off<<L" offText="<<offText
      <<L" onPersisted="<<on<<L" onText="<<onText<<L" layout="<<layout<<L" noPowerWrites="<<noPowerWrites<<L"\n"<<schedulerGuard.report();
    const bool saved=saveText(output,report.str());
    for(auto f:{regular,fontSmall,heading,hero,bold})DeleteObject(f);
    DeleteObject(backgroundBrush);DeleteObject(cardBrush);DestroyWindow(window);window=nullptr;
    return defaultOff&&off&&offText&&on&&onText&&layout&&noPowerWrites&&saved?0:4;
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int) {
    launchTime=GetTickCount64();cpuAtStart=ownCpu();
    int argc=0;LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);if(!argv)return 2;
    std::vector<std::wstring> args;for(int i=1;i<argc;++i)args.emplace_back(argv[i]);LocalFree(argv);
    auto has=[&](const wchar_t* a){return std::find(args.begin(),args.end(),a)!=args.end();};
    auto value=[&](const wchar_t* a)->std::wstring{auto p=std::find(args.begin(),args.end(),a);return p!=args.end()&&++p!=args.end()?*p:L"";};
    if(has(L"--scheduler-gui-test"))return schedulerGuiTest(instance,value(L"--scheduler-gui-test"));
    if(has(L"--topology")){
        auto topology=scheduler::discoverTopology();
        std::ofstream out(std::filesystem::path(value(L"--topology")),std::ios::binary);
        out<<topology.json();return !out?ERROR_WRITE_FAULT:topology.valid()?0:ERROR_INVALID_DATA;
    }
    if(has(L"--guard"))return guardMain(wcstoul(value(L"--guard").c_str(),nullptr,10));
    if(has(L"--diagnose"))return saveText(value(L"--diagnose"),power.report())?0:ERROR_WRITE_FAULT;
    if(has(L"--automatic")||has(L"--efficient")||has(L"--ac-on")||has(L"--ac-off")||has(L"--bench-begin")||has(L"--bench-end")){
        HWND h=FindWindowW(ClassName,nullptr);if(!h)return ERROR_NOT_FOUND;
        bool bench=has(L"--bench-begin")||has(L"--bench-end");DWORD_PTR result=0;
        LRESULT sent=SendMessageTimeoutW(h,bench?BenchmarkMsg:ModeMsg,bench?has(L"--bench-begin"):has(L"--ac-on")?3:has(L"--ac-off")?2:has(L"--efficient"),
            bench?wcstoul(value(L"--bench-begin").c_str(),nullptr,10):0,SMTO_ABORTIFHUNG,10000,&result);
        return sent&&result?0:ERROR_INVALID_STATE;
    }
    if(has(L"--quit")||has(L"--status")){HWND h=FindWindowW(ClassName,nullptr);if(h)SendMessageTimeoutW(h,has(L"--quit")?QuitMsg:ReportMsg,0,0,SMTO_ABORTIFHUNG,5000,nullptr);return h?0:1;}
    HANDLE singleton=CreateMutexW(nullptr,FALSE,L"Local\\Pulse.Power.Controller.v1");if(!singleton)return 2;
    DWORD wait=WaitForSingleObject(singleton,0);
    if(wait!=WAIT_OBJECT_0&&wait!=WAIT_ABANDONED) {HWND h=FindWindowW(ClassName,nullptr);if(h){ShowWindow(h,SW_SHOW);SetForegroundWindow(h);}CloseHandle(singleton);return 0;}
    if(has(L"--restore")){int e=static_cast<int>(power.restore());ReleaseMutex(singleton);CloseHandle(singleton);return e;}
    if(has(L"--exercise")){int e=exercise(value(L"--exercise"));ReleaseMutex(singleton);CloseHandle(singleton);return e;}
    schedulerGuard.enabled(scheduler::loadSchedulerPreference(schedulerIni()));
    if(schedulerGuard.enabled()){schedulerGuard.refresh();schedulerTopologyDirty=false;}
    manageAC=GetPrivateProfileIntW(L"Preferences",L"ManageAC",0,(appFolder()+L"\\Pulse.ini").c_str())!=0;
    if(has(L"--manage-ac"))manageAC=true; // One-run override for the local verification tool.
    wchar_t computeApps[4096];GetPrivateProfileStringW(L"Preferences",L"ComputeApps",L"",computeApps,4096,(appFolder()+L"\\Pulse.ini").c_str());processMonitor.configure(computeApps);
    dryRun=has(L"--dry-run");reportOnExit=value(L"--report");autoStart=!has(L"--paused");
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
    // Ask Windows to ignore this process for timer-resolution requests made elsewhere.
    PROCESS_POWER_THROTTLING_STATE throttling{PROCESS_POWER_THROTTLING_CURRENT_VERSION,PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION,PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION};
    SetProcessInformation(GetCurrentProcess(),ProcessPowerThrottling,&throttling,sizeof(throttling));
    uiScale=GetDpiForSystem()/96.0;
    backgroundBrush=CreateSolidBrush(Background);cardBrush=CreateSolidBrush(Card);
    WNDCLASSEXW wc{sizeof(wc)};wc.lpfnWndProc=procedure;wc.hInstance=instance;wc.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(1));wc.hIconSm=wc.hIcon;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.lpszClassName=ClassName;wc.hbrBackground=backgroundBrush;RegisterClassExW(&wc);
    RECT size{0,0,px(804),px(780)};DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX|WS_CLIPCHILDREN;
    AdjustWindowRectExForDpi(&size,style,FALSE,0,static_cast<UINT>(96*uiScale));
    CreateWindowExW(0,ClassName,L"Pulse — Adaptive Power",style,CW_USEDEFAULT,CW_USEDEFAULT,size.right-size.left,size.bottom-size.top,nullptr,nullptr,instance,nullptr);
    if(!window){ReleaseMutex(singleton);CloseHandle(singleton);return 3;}
    BOOL dark=TRUE;DwmSetWindowAttribute(window,20,&dark,sizeof(dark));makeUi();
    tray.cbSize=sizeof(tray);tray.hWnd=window;tray.uID=1;tray.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP;tray.uCallbackMessage=TrayMsg;tray.hIcon=wc.hIcon;wcscpy_s(tray.szTip,L"Pulse");Shell_NotifyIconW(NIM_ADD,&tray);
    taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");
    displayNotification=RegisterPowerSettingNotification(window,&GUID_CONSOLE_DISPLAY_STATE,DEVICE_NOTIFY_WINDOW_HANDLE);
    sourceNotification=RegisterPowerSettingNotification(window,&GUID_ACDC_POWER_SOURCE,DEVICE_NOTIFY_WINDOW_HANDLE);
    schemeNotification=RegisterPowerSettingNotification(window,&GUID_ACTIVE_POWERSCHEME,DEVICE_NOTIFY_WINDOW_HANDLE);
    saverNotification=RegisterPowerSettingNotification(window,&GUID_POWER_SAVING_STATUS,DEVICE_NOTIFY_WINDOW_HANDLE);
    WTSRegisterSessionNotification(window,NOTIFY_FOR_THIS_SESSION);
    if(!has(L"--hidden"))ShowWindow(window,SW_SHOW);
    environment();if(!dryRun){DWORD e=power.restore();if(e){fault=L"Previous settings need recovery: "+std::to_wstring(e);autoStart=false;}}if(autoStart)turnOn();else updateUi();setTimer();
    auto seconds=value(L"--seconds");if(!seconds.empty()){unsigned long duration=wcstoul(seconds.c_str(),nullptr,10);if(duration>0&&duration<=3600)SetTimer(window,StopTimer,duration*1000,nullptr);}
    MSG message;while(GetMessageW(&message,nullptr,0,0)>0){if(!IsDialogMessageW(window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}}
    cleanup();CoUninitialize();ReleaseMutex(singleton);CloseHandle(singleton);if(guardianProcess)CloseHandle(guardianProcess);return fault.empty()?0:1;
}
