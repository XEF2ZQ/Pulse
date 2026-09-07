#pragma once
#include "workload.h"
#include <windows.h>
#include <tlhelp32.h>
#include <vector>
#include <algorithm>
#include <cwctype>

namespace pulse {
// Cache only compute-tool handles. No WMI, thread walking, sensor reads or ETW.
// Discovery runs every 3 seconds when idle, once per second with tools present.
class ProcessMonitor {
    struct Entry { DWORD pid; HANDLE handle; uint64_t cpu, at; };
    std::vector<Entry> entries;
    std::vector<std::wstring> extra;
    uint64_t discovery=0;
    static uint64_t value(FILETIME t){return (uint64_t(t.dwHighDateTime)<<32)|t.dwLowDateTime;}
public:
    uint64_t scans=0, reads=0;
    double cpu=0;
    ~ProcessMonitor(){clear();}
    void clear(){for(auto& e:entries)CloseHandle(e.handle);entries.clear();discovery=0;cpu=0;}
    void refreshSoon(){discovery=0;}
    bool hasTools() const{return !entries.empty();}
    void configure(const std::wstring& list){
        size_t start=0;while(start<list.size()){size_t end=list.find(L';',start);
            auto n=list.substr(start,end==std::wstring::npos?end:end-start);
            std::transform(n.begin(),n.end(),n.begin(),[](wchar_t c){return wchar_t(towlower(c));});
            if(!n.empty()&&n.find_first_of(L"\\/: ") == std::wstring::npos)extra.push_back(n);
            if(end==std::wstring::npos)break;start=end+1;
        }
    }
    void collect(uint64_t now){
        cpu=0;
        // Read exited processes once too, retaining the CPU work of completed jobs.
        for(auto it=entries.begin();it!=entries.end();){
            FILETIME c{},x{},k{},u{};++reads;
            if(GetProcessTimes(it->handle,&c,&x,&k,&u)){
                auto total=value(k)+value(u);
                if(now>it->at&&total>=it->cpu)cpu+=100.0*double(total-it->cpu)/(10000.0*(now-it->at));
                it->cpu=total;it->at=now;
                if(!value(x)){++it;continue;}
            }
            CloseHandle(it->handle);it=entries.erase(it);
        }
        if(discovery&&now-discovery<(entries.empty()?3000u:1000u))return;
        discovery=now;++scans;
        HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(snapshot==INVALID_HANDLE_VALUE)return;
        PROCESSENTRY32W p{};p.dwSize=sizeof(p);
        if(Process32FirstW(snapshot,&p))do{
            std::wstring name=p.szExeFile;
            std::transform(name.begin(),name.end(),name.begin(),[](wchar_t c){return wchar_t(towlower(c));});
            if(!knownCompute(name)&&std::find(extra.begin(),extra.end(),name)==extra.end())continue;
            if(std::any_of(entries.begin(),entries.end(),[&](const Entry& e){return e.pid==p.th32ProcessID;}))continue;
            HANDLE h=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,p.th32ProcessID);if(!h)continue;
            FILETIME c{},x{},k{},u{};
            if(GetProcessTimes(h,&c,&x,&k,&u)&&!value(x))entries.push_back({p.th32ProcessID,h,value(k)+value(u),now});
            else CloseHandle(h);
        }while(Process32NextW(snapshot,&p));
        CloseHandle(snapshot);
    }
};
}
