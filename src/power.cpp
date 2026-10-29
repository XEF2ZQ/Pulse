#include <initguid.h>
#include "power.h"
#include <powrprof.h>
#include <objbase.h>
#include <powersetting.h>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace pulse {
static constexpr GUID BalancedMode{};
std::wstring executablePath() { wchar_t p[32768]; DWORD n=GetModuleFileNameW(nullptr,p,32768); return std::wstring(p,n); }
std::wstring appFolder() { auto p=executablePath(); return p.substr(0,p.find_last_of(L"\\/")); }
std::wstring guidText(const GUID& g) { wchar_t p[40]; StringFromGUID2(g,p,40); return p; }
std::wstring errorText(DWORD code) {
    wchar_t* text=nullptr; FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,nullptr,code,0,reinterpret_cast<wchar_t*>(&text),0,nullptr);
    std::wstring s=text?text:L"Windows did not provide details."; if(text)LocalFree(text);
    return std::to_wstring(code)+L": "+s;
}
uint64_t fileTimeValue(const FILETIME& t) { return (static_cast<uint64_t>(t.dwHighDateTime)<<32)|t.dwLowDateTime; }
const std::vector<Setting>& settings() {
    static const std::vector<Setting> list={
        {GUID_PROCESSOR_PERF_BOOST_MODE,L"CPU boost",0,0,true},
        {GUID_PROCESSOR_CORE_PARKING_MIN_CORES,L"Minimum active logical cores",15,10,false},
        {GUID_PROCESSOR_CORE_PARKING_MIN_CORES_1,L"Class 1 minimum active cores",15,10,false},
        {GUID_PROCESSOR_THROTTLE_MINIMUM,L"Minimum processor state",80,15,false},
        {GUID_PROCESSOR_THROTTLE_MINIMUM_1,L"Class 1 minimum processor state",80,15,false},
        {GUID_PROCESSOR_HETEROGENEOUS_POLICY,L"Standard core parking",0,0,false},
        {GUID_PROCESSOR_THREAD_SCHEDULING_POLICY,L"Long threads (Automatic)",5,5,false},
        {GUID_PROCESSOR_SHORT_THREAD_SCHEDULING_POLICY,L"Short threads (Automatic)",5,5,false},
        {GUID_PROCESSOR_PERF_ENERGY_PERFORMANCE_PREFERENCE,L"Windows EPP",50,80,false},
        {GUID_PROCESSOR_PERF_ENERGY_PERFORMANCE_PREFERENCE_1,L"Windows EPP class 1",50,80,false},
        {GUID_PROCESSOR_IDLE_DEMOTE_THRESHOLD,L"Windows idle demotion threshold",12,40,false},
        {GUID_PROCESSOR_IDLE_PROMOTE_THRESHOLD,L"Windows idle promotion threshold",100,60,false},
        {GUID_PROCESSOR_IDLE_STATE_MAXIMUM,L"Windows idle state maximum",0,0,false}
    }; return list;
}
static std::wstring journalPath() { return appFolder()+L"\\state\\restore.bin"; }
static DWORD explicitMask(const GUID& scheme,DWORD index) {
    auto bare=[](const GUID& g){auto text=guidText(g);return text.substr(1,text.size()-2);};
    const std::wstring path=L"SYSTEM\\CurrentControlSet\\Control\\Power\\User\\PowerSchemes\\"+bare(scheme)+L"\\"+bare(GUID_PROCESSOR_SETTINGS_SUBGROUP)+L"\\"+bare(settings()[index].id);
    DWORD data=0,size=sizeof(data),mask=0;
    if(RegGetValueW(HKEY_LOCAL_MACHINE,path.c_str(),L"ACSettingIndex",RRF_RT_REG_DWORD,nullptr,&data,&size)==ERROR_SUCCESS)mask|=1;
    size=sizeof(data);
    if(RegGetValueW(HKEY_LOCAL_MACHINE,path.c_str(),L"DCSettingIndex",RRF_RT_REG_DWORD,nullptr,&data,&size)==ERROR_SUCCESS)mask|=2;
    return mask;
}
template<class T> static DWORD checksum(const T& s) {
    const auto* data=reinterpret_cast<const unsigned char*>(&s); DWORD h=2166136261u;
    for(size_t i=0;i<offsetof(T,checksum);++i)h=(h^data[i])*16777619u; return h;
}
Power::Power() {
    HMODULE m=GetModuleHandleW(L"powrprof.dll");
    if(!m)return;
    getAc=reinterpret_cast<GetMode>(GetProcAddress(m,"PowerGetUserConfiguredACPowerMode"));
    getDc=reinterpret_cast<GetMode>(GetProcAddress(m,"PowerGetUserConfiguredDCPowerMode"));
    setAc=reinterpret_cast<SetMode>(GetProcAddress(m,"PowerSetUserConfiguredACPowerMode"));
    setDc=reinterpret_cast<SetMode>(GetProcAddress(m,"PowerSetUserConfiguredDCPowerMode"));
    getEffective=reinterpret_cast<GetMode>(GetProcAddress(m,"PowerGetEffectiveOverlayScheme"));
}
bool Power::available()const { return getAc&&getDc&&setAc&&setDc&&getEffective; }
DWORD Power::mode(bool ac,GUID& out)const { auto f=ac?getAc:getDc; return f?f(&out):ERROR_CALL_NOT_IMPLEMENTED; }
DWORD Power::setMode(bool ac,const GUID& v)const { auto f=ac?setAc:setDc; return f?f(&v):ERROR_CALL_NOT_IMPLEMENTED; }
DWORD Power::effectiveMode(GUID& out)const { return getEffective?getEffective(&out):ERROR_CALL_NOT_IMPLEMENTED; }
DWORD Power::activeScheme(GUID& out)const { GUID* p=nullptr; DWORD e=PowerGetActiveScheme(nullptr,&p); if(!e&&p){out=*p;LocalFree(p);}return e; }
DWORD Power::read(const GUID& scheme,DWORD index,bool ac,DWORD& value)const {
    if(index>=settings().size())return ERROR_INVALID_DATA;
    return ac?PowerReadACValueIndex(nullptr,&scheme,&GUID_PROCESSOR_SETTINGS_SUBGROUP,&settings()[index].id,&value):
        PowerReadDCValueIndex(nullptr,&scheme,&GUID_PROCESSOR_SETTINGS_SUBGROUP,&settings()[index].id,&value);
}
DWORD Power::write(const GUID& scheme,DWORD index,bool ac,DWORD value) {
    DWORD old=0; DWORD e=read(scheme,index,ac,old); if(e)return e; if(old==value)return ERROR_SUCCESS;
    e=ac?PowerWriteACValueIndex(nullptr,&scheme,&GUID_PROCESSOR_SETTINGS_SUBGROUP,&settings()[index].id,value):
        PowerWriteDCValueIndex(nullptr,&scheme,&GUID_PROCESSOR_SETTINGS_SUBGROUP,&settings()[index].id,value);
    if(e)return e; ++writes; commitPending=true; DWORD actual=0; e=read(scheme,index,ac,actual); return e?e:actual==value?ERROR_SUCCESS:ERROR_WRITE_FAULT;
}
DWORD Power::commit(const GUID& scheme) { const DWORD error=PowerSetActiveScheme(nullptr,&scheme); if(!error){++planCommits;commitPending=false;} return error; }
bool Power::hasJournal()const { return GetFileAttributesW(journalPath().c_str())!=INVALID_FILE_ATTRIBUTES; }
DWORD Power::saveJournal() {
    CreateDirectoryW((appFolder()+L"\\state").c_str(),nullptr);
    saved.checksum=checksum(saved); auto path=journalPath()+L".tmp";
    HANDLE f=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH,nullptr);
    if(f==INVALID_HANDLE_VALUE)return GetLastError();
    DWORD wrote=0; bool ok=WriteFile(f,&saved,sizeof(saved),&wrote,nullptr)&&wrote==sizeof(saved)&&FlushFileBuffers(f);
    DWORD e=ok?ERROR_SUCCESS:GetLastError(); CloseHandle(f); if(e)return e;
    if(!MoveFileExW(path.c_str(),journalPath().c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))return GetLastError();
    return 0;
}
DWORD Power::loadJournal(Snapshot& out)const {
    HANDLE f=CreateFileW(journalPath().c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    if(f==INVALID_HANDLE_VALUE)return GetLastError(); DWORD n=0;
    struct LegacyRow { GUID scheme; DWORD setting, ac, dc, mask; };
    struct LegacySnapshot { DWORD magic,version,owner,count; GUID scheme,acMode,dcMode; LegacyRow rows[48]; DWORD checksum; };
    const DWORD size=GetFileSize(f,nullptr);
    bool ok=false;
    if(size==sizeof(out)) {
        ok=ReadFile(f,&out,sizeof(out),&n,nullptr)&&n==sizeof(out)&&(out.version==3||out.version==4)&&out.checksum==checksum(out);
    } else if(size==sizeof(LegacySnapshot)) {
        LegacySnapshot old{};
        ok=ReadFile(f,&old,sizeof(old),&n,nullptr)&&n==sizeof(old)&&old.magic==0x50554C53&&
            (old.version==1||old.version==2)&&old.count<=48&&old.checksum==checksum(old);
        if(ok) {
            out=Snapshot{};out.version=old.version;out.owner=old.owner;out.count=old.count;out.scheme=old.scheme;out.acMode=old.acMode;out.dcMode=old.dcMode;
            for(DWORD i=0;i<old.count;++i) {
                const auto& r=old.rows[i];if(r.setting>=settings().size()){ok=false;break;}
                const auto& s=settings()[r.setting];out.rows[i]={r.scheme,r.setting,r.ac,r.dc,r.mask,s.ac,s.dc};
            }
        }
    }
    CloseHandle(f);
    if(!ok||out.magic!=0x50554C53||out.count>48||out.count<6)return ERROR_INVALID_DATA;
    for(DWORD i=0;i<out.count;++i) {
        const auto& r=out.rows[i]; if(r.setting>=settings().size()||(out.version<4&&r.setting>=8)||r.ac>100||r.dc>100||r.targetAc>100||r.targetDc>100||r.mask<1||r.mask>3)return ERROR_INVALID_DATA;
        if(r.scheme!=out.scheme&&r.scheme!=GUID_POWER_MODE_BEST_EFFICIENCY&&r.scheme!=GUID_POWER_MODE_BEST_PERFORMANCE)return ERROR_INVALID_DATA;
    }
    return 0;
}
bool Power::optionAvailable(DWORD index, DWORD sourceMask) const {
    if(index<8||index>=settings().size()||sourceMask<1||sourceMask>3)return false;
    GUID scheme{};if(activeScheme(scheme))return false;
    if((explicitMask(scheme,index)&sourceMask)!=sourceMask)return false;
    for(bool ac:{true,false})if(sourceMask&(ac?1:2)) {
        DWORD value=0;if(read(scheme,index,ac,value)||value>100)return false;
    }
    return true;
}
DWORD Power::capture(DWORD sourceMask, bool includeCpuOptions) {
    captureIssue.clear();
    if(sourceMask<1||sourceMask>3)return ERROR_INVALID_PARAMETER;
    if((dcUsesAcProfile&&sourceMask!=2)||(acPassthrough&&!dcUsesAcProfile))return ERROR_INVALID_PARAMETER;
    if(hasJournal())return ERROR_ALREADY_EXISTS;
    if(!available())return ERROR_CALL_NOT_IMPLEMENTED;
    if(!cpuOptions.valid())return ERROR_INVALID_PARAMETER;
    commitPending=false; saved=Snapshot{}; saved.owner=GetCurrentProcessId(); DWORD e=activeScheme(saved.scheme); if(e)return e;
    e=mode(true,saved.acMode); if(e)return e; e=mode(false,saved.dcMode); if(e)return e;
    for(const GUID& scheme:{saved.scheme,GUID_POWER_MODE_BEST_EFFICIENCY,GUID_POWER_MODE_BEST_PERFORMANCE}) {
        for(DWORD i=0;i<settings().size();++i) {
            if(i>=8&&(!includeCpuOptions||!cpuOptions.manages(i)))continue;
            if(i>=10&&scheme!=saved.scheme)continue;
            Row r{scheme,i,0,0,explicitMask(scheme,i),profiles.value(i,true),profiles.value(i,false)};
            if(i>=8) {
                r.targetAc=cpuOptions.value(i,true);r.targetDc=cpuOptions.value(i,false);
                // Only take ownership of existing overrides. Never create an
                // inherited value that the recovery journal cannot remove.
                if(scheme==saved.scheme&&i!=9&&(r.mask&sourceMask)!=sourceMask) {
                    captureIssue=std::wstring(settings()[i].name)+L" needs explicit plan values. Disable this custom option to resume.";
                    return ERROR_NOT_SUPPORTED;
                }
                if(!(r.mask&sourceMask))continue;
            }
            if(!r.mask&&!(scheme==saved.scheme&&i!=2&&i!=4&&i<8))continue;
            if(scheme==saved.scheme&&i<8&&r.mask!=3) {
                // Inherited values cannot be restored exactly by the v3 journal.
                // Do not materialize overrides or silently replace the user's plan.
                captureIssue=L"This power plan has inherited settings Pulse cannot safely restore. Select Balanced in Windows, then click Automatic.";
                return ERROR_NOT_SUPPORTED;
            }
            auto a=read(scheme,i,true,r.ac),d=read(scheme,i,false,r.dc);
            if(a||d) { if(scheme==saved.scheme)return a?a:d; if(a==ERROR_FILE_NOT_FOUND&&d==ERROR_FILE_NOT_FOUND)continue; return a?a:d; }
            if(dcUsesAcProfile) {
                r.targetDc=r.targetAc;
                if(i==0&&acPassthrough)r.targetDc=cpuOptions.disableBoostAc?0:r.ac;
            }
            r.mask &= sourceMask; if(!r.mask)continue;
            if(saved.count>=48)return ERROR_BUFFER_OVERFLOW;
            saved.rows[saved.count++]=r;
        }
    }
    e=saveJournal(); if(!e)owned=true; return e;
}
DWORD Power::configure() {
    if(!owned)return ERROR_INVALID_STATE;
    for(DWORD i=0;i<saved.count;++i) {
        auto& r=saved.rows[i];
        DWORD e=0;
        if(r.mask&1){e=write(r.scheme,r.setting,true,r.targetAc); if(e)return e;}
        if(r.mask&2){e=write(r.scheme,r.setting,false,r.targetDc); if(e)return e;}
    }
    return apply(false);
}
DWORD Power::persistProfiles() {
    if(owned||!profiles.valid())return ERROR_INVALID_STATE;
    DWORD e=capture(3,false);if(e)return e;
    for(DWORD i=0;i<saved.count&&!e;++i) {
        const auto& r=saved.rows[i];if(settings()[r.setting].dynamic)continue;
        for(bool ac:{true,false})if(r.mask&(ac?1:2)) {
            e=write(r.scheme,r.setting,ac,ac?r.targetAc:r.targetDc);if(e)break;
        }
    }
    if(!e)e=commit(saved.scheme);
    if(!e){GUID current{};e=activeScheme(current);if(!e&&current!=saved.scheme)e=ERROR_INVALID_STATE;}
    for(DWORD i=0;i<saved.count&&!e;++i){
        const auto& r=saved.rows[i];if(settings()[r.setting].dynamic)continue;
        for(bool ac:{true,false})if(r.mask&(ac?1:2)){
            DWORD value=0;e=read(r.scheme,r.setting,ac,value);
            if(!e&&value!=(ac?r.targetAc:r.targetDc))e=ERROR_INVALID_STATE;if(e)break;
        }
    }
    if(e){const DWORD recovery=restore();return recovery?recovery:e;}
    if(!DeleteFileW(journalPath().c_str())){e=GetLastError();restore();return e;}
    owned=false;return ERROR_SUCCESS;
}
static GUID modeFor(PowerState state){return state==PowerState::Efficiency?GUID_POWER_MODE_BEST_EFFICIENCY:GUID_POWER_MODE_BEST_PERFORMANCE;}
DWORD Power::apply(PowerState state) {
    if(!owned)return ERROR_INVALID_STATE;
    GUID current{}; DWORD e=activeScheme(current); if(e)return e; if(current!=saved.scheme)return ERROR_INVALID_STATE;
    LARGE_INTEGER begin,end,freq; QueryPerformanceCounter(&begin); QueryPerformanceFrequency(&freq);

    for(DWORD i=0;i<saved.count;++i) {
        const auto& r=saved.rows[i]; if(!settings()[r.setting].dynamic)continue;
        for(bool ac:{true,false}) { if(!(r.mask&(ac?1:2)))continue; DWORD old=0; e=read(r.scheme,r.setting,ac,old); if(e)return e;
            const DWORD boost=acPassthrough?(ac?r.targetAc:r.targetDc):cpuOptions.boost(state,ac||dcUsesAcProfile);
            if(old!=boost){e=write(r.scheme,r.setting,ac,boost); if(e)return e;}
        }
    }
    // Writes to the active plan need one explicit commit (never spawn powercfg).
    if(commitPending) { e=commit(saved.scheme); if(e)return e; }
    SYSTEM_POWER_STATUS status{}; if(!GetSystemPowerStatus(&status))return GetLastError();
    DWORD mask=0;for(DWORD i=0;i<saved.count;++i)mask|=saved.rows[i].mask;
    if(!(mask&(status.ACLineStatus==1?1:2)))return ERROR_RETRY;
    GUID target=acPassthrough?saved.acMode:modeFor(state);
    GUID old{}; e=mode(status.ACLineStatus==1,old); if(e)return e;
    if(old!=target) { e=setMode(status.ACLineStatus==1,target); if(e)return e; ++writes; }
    QueryPerformanceCounter(&end); lastApplyMs=1000.0*static_cast<double>(end.QuadPart-begin.QuadPart)/freq.QuadPart;
    return verify(state);
}
DWORD Power::verify(PowerState state,bool full)const {
    if(!owned)return 0;
    GUID current{}; DWORD e=activeScheme(current); if(e)return e; if(current!=saved.scheme)return ERROR_INVALID_STATE;
    for(DWORD i=0;i<saved.count;++i) {
        const auto& r=saved.rows[i]; const auto& s=settings()[r.setting]; if(!full&&!s.dynamic)continue;
        for(bool ac:{true,false}) { if(!(r.mask&(ac?1:2)))continue; DWORD value=0; e=read(r.scheme,r.setting,ac,value); if(e)return e;
            DWORD expected=s.dynamic&&!acPassthrough?cpuOptions.boost(state,ac||dcUsesAcProfile):(ac?r.targetAc:r.targetDc); if(value!=expected)return ERROR_INVALID_STATE;
        }
    }
    SYSTEM_POWER_STATUS status{}; if(!GetSystemPowerStatus(&status))return GetLastError();
    GUID requested{}; e=mode(status.ACLineStatus==1,requested); if(e)return e;
    return requested==(acPassthrough?saved.acMode:modeFor(state))?0:ERROR_INVALID_STATE;
}
DWORD Power::restore(DWORD expectedOwner) {
    if(!hasJournal()){owned=false;return 0;}
    Snapshot snap{}; DWORD e=loadJournal(snap); if(e)return e;
    if(expectedOwner&&snap.owner!=expectedOwner)return 0;
    DWORD firstError=0;
    for(DWORD i=0;i<snap.count;++i) {
        const auto& r=snap.rows[i]; const auto& s=settings()[r.setting];
        for(bool ac:{true,false}) {
            if(!(r.mask&(ac?1:2)))continue;
            DWORD value=0; e=read(r.scheme,r.setting,ac,value); if(e){if(!firstError)firstError=e;continue;}
            const DWORD original=ac?r.ac:r.dc, target=ac?r.targetAc:r.targetDc;
            // Preserve a third party's newer value rather than silently undoing it.
            if(value==target||(s.dynamic&&(value==0||value==2||(snap.version>=2&&value==4)))) { e=write(r.scheme,r.setting,ac,original); if(e&&!firstError)firstError=e; }
        }
    }
    GUID active{}; e=activeScheme(active); if(e&&!firstError)firstError=e;
    if(!e&&active==snap.scheme) { e=commit(active); if(e&&!firstError)firstError=e; }
    DWORD mask=0;for(DWORD i=0;i<snap.count;++i)mask|=snap.rows[i].mask;
    for(bool ac:{true,false}) {
        if(!(mask&(ac?1:2)))continue;
        GUID current{}; e=mode(ac,current); if(e){if(!firstError)firstError=e;continue;}
        if(current==GUID_POWER_MODE_BEST_EFFICIENCY||current==GUID_POWER_MODE_BEST_PERFORMANCE||(snap.version>=2&&current==BalancedMode)||(!ac&&current==snap.acMode)) {
            e=setMode(ac,ac?snap.acMode:snap.dcMode); if(e&&!firstError)firstError=e;
        }
    }
    if(!firstError) { if(!DeleteFileW(journalPath().c_str()))return GetLastError(); owned=false; }
    return firstError;
}
std::wstring Power::report()const {
    std::wostringstream out; GUID plan{},ac{},dc{},effective{};
    auto p=activeScheme(plan),a=mode(true,ac),d=mode(false,dc),f=effectiveMode(effective);
    SYSTEM_POWER_STATUS status{}; GetSystemPowerStatus(&status);
    out<<L"Pulse 1.5.1 experimental - Windows power diagnostics\nNative power-mode API: "<<(available()?L"available":L"unavailable")
       <<L"\nPower source: "<<(status.ACLineStatus==1?L"AC":status.ACLineStatus==0?L"Battery":L"Unknown")
       <<L"\nBattery: "<<(unsigned)status.BatteryLifePercent<<L"%\nEnergy Saver: "<<(unsigned)status.SystemStatusFlag
       <<L"\nPlan: "<<guidText(plan)<<L" ("<<p<<L")\nAC mode: "<<guidText(ac)<<L" ("<<a<<L")\nDC mode: "<<guidText(dc)<<L" ("<<d<<L")"
       <<L"\nEffective overlay: "<<guidText(effective)<<L" ("<<f<<L")\n"
       <<L"Custom EPP: "<<cpuOptions.epp<<L"; AC="<<cpuOptions.acEpp<<L"; DC="<<cpuOptions.dcEpp
       <<L"\nDisable boost: AC="<<cpuOptions.disableBoostAc<<L"; DC="<<cpuOptions.disableBoostDc
       <<L"\nCustom idle thresholds: "<<cpuOptions.idleThresholds<<L"; custom idle ceiling: "<<cpuOptions.idleLimit
       <<L"\nEPP scope: existing plan/overlay overrides only; OS/firmware may override effective EPP.\n";
    for(const GUID& scheme:{plan,GUID_POWER_MODE_BEST_EFFICIENCY,GUID_POWER_MODE_BEST_PERFORMANCE}) {
        out<<L"\n"<<guidText(scheme)<<L"\n";
        for(DWORD i=0;i<settings().size();++i) { DWORD av=0,dv=0; auto ae=read(scheme,i,true,av),de=read(scheme,i,false,dv);
            out<<settings()[i].name<<L": AC="; if(ae)out<<L"inherited/error "<<ae; else out<<av;
            if(!(explicitMask(scheme,i)&1))out<<L" (not explicitly overridden)";
            out<<L", DC="; if(de)out<<L"inherited/error "<<de; else out<<dv;
            if(!(explicitMask(scheme,i)&2))out<<L" (not explicitly overridden)";
            out<<L"\n";
        }
    }
    return out.str();
}
}
