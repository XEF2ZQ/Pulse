#include "scheduler_topology.h"
#include <intrin.h>
#include <algorithm>
#include <cstring>
#include <map>
#include <set>
#include <sstream>
#include <iomanip>
#include <cstddef>

namespace pulse::scheduler {
namespace {
using Bytes = std::vector<unsigned char>;
constexpr size_t payload = offsetof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX, Processor);
template<class T> bool read(std::span<const unsigned char> data, size_t at, T& value) {
    if (at > data.size() || sizeof(T) > data.size() - at) return false;
    std::memcpy(&value, data.data() + at, sizeof(T)); return true;
}
bool masks(std::span<const unsigned char> record, size_t at, size_t count, std::vector<CpuKey>& cpus) {
    if (!count || at > record.size() || count > (record.size()-at)/sizeof(GROUP_AFFINITY)) return false;
    std::set<CpuKey> unique;
    for (size_t i = 0; i < count; ++i) {
        GROUP_AFFINITY group{}; if (!read(record, at + i*sizeof(group), group)) return false;
        for (unsigned bit = 0; bit < sizeof(KAFFINITY)*8; ++bit)
            if (group.Mask & (KAFFINITY(1) << bit))
                if (!unique.insert({group.Group, static_cast<BYTE>(bit)}).second) return false;
    }
    cpus.assign(unique.begin(), unique.end()); return !cpus.empty();
}
bool contains(const Domain& domain, CpuKey key) {
    return std::find(domain.cpus.begin(), domain.cpus.end(), key) != domain.cpus.end();
}
void identity(Topology& topology) {
    struct Identity { std::string vendor; unsigned family=0, model=0; };
    static const Identity cached = [] {
        Identity id; int r[4]{}; char vendor[13]{};
        __cpuid(r, 0); const int maxLeaf = r[0];
        std::memcpy(vendor, &r[1], 4); std::memcpy(vendor+4, &r[3], 4); std::memcpy(vendor+8, &r[2], 4);
        id.vendor = vendor;
        if (maxLeaf >= 1) {
            __cpuid(r, 1); const unsigned a=static_cast<unsigned>(r[0]);
            const unsigned base=(a>>8)&15; id.family=base+(base==15?((a>>20)&255):0);
            id.model=((a>>4)&15)|((base==6||base==15)?((a>>16)&15)<<4:0);
        }
        return id;
    }();
    topology.vendor=cached.vendor; topology.family=cached.family; topology.model=cached.model;
}
std::string quote(const std::string& value) {
    std::ostringstream out; out << '"';
    for (unsigned char c : value) {
        if (c=='"'||c=='\\') out << '\\' << c;
        else if (c<32) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(c) << std::dec;
        else out << c;
    }
    out << '"'; return out.str();
}
const char* kind(LOGICAL_PROCESSOR_RELATIONSHIP relation) {
    switch(relation) {
    case RelationProcessorCore:return "core"; case RelationCache:return "cache";
    case RelationProcessorPackage:return "package"; case RelationProcessorDie:return "die";
    case RelationProcessorModule:return "module"; case RelationNumaNode:case RelationNumaNodeEx:return "numa";
    default:return "unknown";
    }
}
}
Topology parseTopology(std::span<const unsigned char> relations,
                       std::span<const unsigned char> cpuSets, unsigned activeCpuCount) {
    Topology t; t.activeCpuCount=activeCpuCount;
    size_t at=0;
    while(at<relations.size()) {
        DWORD size=0; LOGICAL_PROCESSOR_RELATIONSHIP relation{};
        if(!read(relations,at,relation)||!read(relations,at+sizeof(relation),size)||size<payload||size>relations.size()-at) {
            t.errors.push_back("Malformed logical-processor record"); break;
        }
        const auto record=relations.subspan(at,size); at+=size;
        Domain d; d.id=static_cast<unsigned>(t.domains.size()); d.relationship=relation;
        bool recognized=true, ok=true;
        if(relation==RelationProcessorCore||relation==RelationProcessorPackage||relation==RelationProcessorDie||relation==RelationProcessorModule) {
            WORD count=0; BYTE efficiency=0, flags=0;
            ok=read(record,payload+offsetof(PROCESSOR_RELATIONSHIP,GroupCount),count)&&
                read(record,payload+offsetof(PROCESSOR_RELATIONSHIP,EfficiencyClass),efficiency)&&
                read(record,payload+offsetof(PROCESSOR_RELATIONSHIP,Flags),flags)&&
                masks(record,payload+offsetof(PROCESSOR_RELATIONSHIP,GroupMask),count,d.cpus);
            d.efficiency=efficiency; d.flags=flags;
        } else if(relation==RelationCache) {
            CACHE_RELATIONSHIP cache{};
            // Read the fixed prefix; variable group-mask arrays follow it.
            constexpr size_t prefix=offsetof(CACHE_RELATIONSHIP,GroupMask);
            if(record.size()<payload+prefix) ok=false;
            else {
                std::memcpy(&cache,record.data()+payload,prefix);
                d.level=cache.Level;d.type=cache.Type;d.bytes=cache.CacheSize;d.lineBytes=cache.LineSize;
                ok=masks(record,payload+prefix,std::max<unsigned>(1,cache.GroupCount),d.cpus);
            }
        } else if(relation==RelationNumaNode||relation==RelationNumaNodeEx) {
            DWORD node=0; WORD count=0;
            ok=read(record,payload+offsetof(NUMA_NODE_RELATIONSHIP,NodeNumber),node)&&
                read(record,payload+offsetof(NUMA_NODE_RELATIONSHIP,GroupCount),count)&&
                masks(record,payload+offsetof(NUMA_NODE_RELATIONSHIP,GroupMask),std::max<unsigned>(1,count),d.cpus);
            d.flags=node;
        } else recognized=false;
        if(!ok) t.errors.push_back("Malformed domain record");
        else if(recognized) t.domains.push_back(std::move(d));
    }
    std::map<CpuKey,size_t> index;
    std::set<unsigned> coreEfficiencies;
    for(const auto& d:t.domains) if(d.relationship==RelationProcessorCore) {
        coreEfficiencies.insert(d.efficiency);
        for(auto key:d.cpus) {
            if(index.contains(key)){t.errors.push_back("Logical CPU belongs to multiple cores");continue;}
            LogicalCpu cpu; cpu.key=key;cpu.core=d.id;
            for(auto sibling:d.cpus)if(sibling!=key)cpu.siblings.push_back(sibling);
            t.capabilities.hasSmt|=!cpu.siblings.empty();
            index[key]=t.cpus.size();t.cpus.push_back(cpu);
        }
    }
    if(t.cpus.empty()||t.cpus.size()!=activeCpuCount)t.errors.push_back("Core map disagrees with active CPU count");
    for(auto& cpu:t.cpus) {
        unsigned llcLevel=0;
        for(const auto& d:t.domains) if(contains(d,cpu.key)) {
            auto setDomain=[&](std::optional<unsigned>& field,unsigned value) {
                if(field&&*field!=value)t.errors.push_back("Conflicting domain membership");
                field=value;
            };
            switch(d.relationship) {
            case RelationProcessorPackage:setDomain(cpu.package,d.id);break;
            case RelationProcessorDie:setDomain(cpu.die,d.id);break;
            case RelationProcessorModule:setDomain(cpu.module,d.id);break;
            case RelationNumaNode:case RelationNumaNodeEx:setDomain(cpu.numa,d.flags);break;
            case RelationCache:
                cpu.caches.push_back(d.id);
                if((d.type==CacheUnified||d.type==CacheData)&&d.level>llcLevel){llcLevel=d.level;cpu.llc=d.id;}
                break;
            default:break;
            }
        }
        if(!cpu.package||!cpu.llc)t.errors.push_back("Missing package or data/unified cache membership");
    }
    at=0;std::set<unsigned> ids, efficiencies, scheduling;
    while(at<cpuSets.size()) {
        DWORD size=0;CPU_SET_INFORMATION_TYPE type{};
        if(!read(cpuSets,at,size)||!read(cpuSets,at+sizeof(size),type)||size<8||size>cpuSets.size()-at) {
            t.errors.push_back("Malformed CPU Set record");break;
        }
        auto record=cpuSets.subspan(at,size);at+=size;
        if(type!=CpuSetInformation)continue;
        SYSTEM_CPU_SET_INFORMATION set{};
        constexpr size_t prefix=offsetof(SYSTEM_CPU_SET_INFORMATION,CpuSet.AllocationTag);
        if(size<prefix){t.errors.push_back("Truncated CPU Set fields");continue;}
        std::memcpy(&set,record.data(),std::min<size_t>(size,sizeof(set)));
        const auto& c=set.CpuSet;CpuKey key{c.Group,c.LogicalProcessorIndex};
        if(!index.contains(key)){t.errors.push_back("CPU Set has no active core mapping");continue;}
        auto& cpu=t.cpus[index[key]];
        if(cpu.cpuSetId||!ids.insert(c.Id).second){t.errors.push_back("Duplicate CPU Set identity");continue;}
        cpu.cpuSetId=c.Id;cpu.setCore=c.CoreIndex;cpu.setLlc=c.LastLevelCacheIndex;cpu.setNuma=c.NumaNodeIndex;
        cpu.efficiency=c.EfficiencyClass;cpu.schedulingClass=c.SchedulingClass;cpu.cpuSetFlags=c.AllFlags;
        if(t.domains[cpu.core].efficiency!=c.EfficiencyClass)t.errors.push_back("Core and CPU Set efficiency classes disagree");
        efficiencies.insert(c.EfficiencyClass);scheduling.insert(c.SchedulingClass);
    }
    t.capabilities.hasCpuSets=!t.cpus.empty()&&ids.size()==t.cpus.size();
    if(!cpuSets.empty()&&!t.capabilities.hasCpuSets)t.errors.push_back("CPU Set coverage is incomplete");
    if(t.capabilities.hasCpuSets) for(size_t i=0;i<t.cpus.size();++i)for(size_t j=i+1;j<t.cpus.size();++j) {
        const auto& a=t.cpus[i];const auto& b=t.cpus[j];if(a.key.group!=b.key.group)continue;
        if((a.core==b.core)!=(a.setCore==b.setCore))t.errors.push_back("Core and CPU Set SMT maps disagree");
        if(a.core==b.core&&a.efficiency!=b.efficiency)t.errors.push_back("SMT sibling efficiency classes disagree");
        if(a.llc&&b.llc&&((*a.llc==*b.llc)!=(a.setLlc==b.setLlc)))t.errors.push_back("LLC and CPU Set sharing maps disagree");
        if(a.numa&&b.numa&&((*a.numa==*b.numa)!=(a.setNuma==b.setNuma)))t.errors.push_back("NUMA sharing maps disagree");
    }
    if(!t.capabilities.hasCpuSets)efficiencies=coreEfficiencies;
    t.capabilities.efficiencyClassCount=static_cast<unsigned>(efficiencies.size());
    t.capabilities.schedulingClassCount=static_cast<unsigned>(scheduling.size());
    t.capabilities.heterogeneous=efficiencies.size()>1;
    std::set<unsigned> llcs;for(const auto& cpu:t.cpus)if(cpu.llc)llcs.insert(*cpu.llc);
    t.capabilities.cacheDomainCount=static_cast<unsigned>(llcs.size());
    t.capabilities.topologyValid=t.errors.empty();
    if(!t.capabilities.hasCpuSets)t.notes.push_back("CPU Sets unavailable; no placement capability");
    if(std::none_of(t.cpus.begin(),t.cpus.end(),[](const auto& cpu){return cpu.die.has_value();}))t.notes.push_back("Die membership not exposed by Windows; not inferred");
    if(std::none_of(t.cpus.begin(),t.cpus.end(),[](const auto& cpu){return cpu.module.has_value();}))t.notes.push_back("Module membership not exposed by Windows; not inferred");
    t.notes.push_back("SchedulingClass is raw OS data, not a measured energy or throughput ratio");
    // Stable within an unchanged OS enumeration; deliberately invalidates cached
    // profiles on topology changes. No ephemeral parked/allocation flags included.
    uint64_t hash=14695981039346656037ull;
    auto mix=[&](uint64_t value){for(int b=0;b<8;++b){hash^=(value>>(b*8))&255;hash*=1099511628211ull;}};
    for(const auto& d:t.domains){mix(d.relationship);mix(d.level);mix(d.type);mix(d.bytes);mix(d.efficiency);for(auto k:d.cpus){mix(k.group);mix(k.index);}}
    for(const auto& cpu:t.cpus){mix(cpu.cpuSetId.value_or(UINT_MAX));mix(cpu.efficiency.value_or(UINT_MAX));}
    t.fingerprint=hash;return t;
}
Topology discoverTopology() {
    LARGE_INTEGER start{},end{},frequency{};QueryPerformanceCounter(&start);QueryPerformanceFrequency(&frequency);
    Bytes relations,sets;DWORD error=0;
    for(int attempt=0;attempt<4;++attempt) {
        DWORD bytes=static_cast<DWORD>(relations.size());
        if(GetLogicalProcessorInformationEx(RelationAll,relations.empty()?nullptr:reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(relations.data()),&bytes)) {relations.resize(bytes);error=0;break;}
        error=GetLastError();if(error!=ERROR_INSUFFICIENT_BUFFER||!bytes||bytes>16*1024*1024)break;
        relations.resize(bytes);
    }
    if(error)relations.clear();
    using GetSets=BOOL(WINAPI*)(PSYSTEM_CPU_SET_INFORMATION,ULONG,PULONG,HANDLE,ULONG);
    auto kernel=GetModuleHandleW(L"kernel32.dll");
    auto getSets=reinterpret_cast<GetSets>(GetProcAddress(kernel,"GetSystemCpuSetInformation"));
    DWORD setError=ERROR_NOT_SUPPORTED;
    if(getSets)for(int attempt=0;attempt<4;++attempt) {
        ULONG bytes=0;
        if(getSets(sets.empty()?nullptr:reinterpret_cast<PSYSTEM_CPU_SET_INFORMATION>(sets.data()),static_cast<ULONG>(sets.size()),&bytes,GetCurrentProcess(),0)){sets.resize(bytes);setError=0;break;}
        setError=GetLastError();if(setError!=ERROR_INSUFFICIENT_BUFFER||!bytes||bytes>16*1024*1024)break;
        sets.resize(bytes);
    }
    if(setError)sets.clear();
    auto t=parseTopology(relations,sets,GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
    if(error)t.errors.push_back("Topology query failed: "+std::to_string(error));
    if(setError)t.notes.push_back("CPU Set query unavailable: "+std::to_string(setError));
    identity(t);
    PROCESS_POWER_THROTTLING_STATE qos{};qos.Version=PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    t.capabilities.hasQoSApi=GetProcessInformation(GetCurrentProcess(),ProcessPowerThrottling,&qos,sizeof(qos))!=FALSE;
    t.capabilities.topologyValid=t.errors.empty();
    QueryPerformanceCounter(&end);t.discoveryMs=1000.0*double(end.QuadPart-start.QuadPart)/double(frequency.QuadPart);
    return t;
}
std::string Topology::json() const {
    std::ostringstream o;o<<std::boolalpha;
    auto opt=[&](std::optional<unsigned> v){if(v)o<<*v;else o<<"null";};
    auto keys=[&](const std::vector<CpuKey>& list){o<<'[';bool first=true;for(auto k:list){if(!first)o<<',';first=false;o<<"["<<k.group<<','<<unsigned(k.index)<<']';}o<<']';};
    o<<"{\n  \"schemaVersion\": 1,\n  \"vendor\": "<<quote(vendor)<<", \"family\": "<<family<<", \"model\": "<<model
     <<",\n  \"valid\": "<<valid()<<", \"activeLogicalCpus\": "<<activeCpuCount<<", \"discoveryMs\": "<<discoveryMs
     <<",\n  \"fingerprint\": \""<<std::hex<<fingerprint<<std::dec<<"\",\n  \"capabilities\": {\"cpuSets\": "<<capabilities.hasCpuSets<<", \"smt\": "<<capabilities.hasSmt
     <<", \"qosApi\": "<<capabilities.hasQoSApi<<", \"heterogeneous\": "<<capabilities.heterogeneous<<", \"efficiencyClasses\": "<<capabilities.efficiencyClassCount
     <<", \"rawSchedulingClasses\": "<<capabilities.schedulingClassCount<<", \"llcDomains\": "<<capabilities.cacheDomainCount<<"},\n  \"logicalCpus\": [\n";
    bool first=true;for(const auto& c:cpus){if(!first)o<<",\n";first=false;
        o<<"    {\"group\": "<<c.key.group<<", \"index\": "<<unsigned(c.key.index)<<", \"core\": "<<c.core<<", \"siblings\": ";keys(c.siblings);
        o<<", \"package\": ";opt(c.package);o<<", \"die\": ";opt(c.die);o<<", \"module\": ";opt(c.module);o<<", \"numa\": ";opt(c.numa);o<<", \"llc\": ";opt(c.llc);
        o<<", \"cpuSetId\": ";opt(c.cpuSetId);o<<", \"setCore\": ";opt(c.setCore);o<<", \"setLlc\": ";opt(c.setLlc);o<<", \"setNuma\": ";opt(c.setNuma);
        o<<", \"efficiencyClass\": ";opt(c.efficiency);o<<", \"schedulingClassRaw\": ";opt(c.schedulingClass);o<<", \"flagsSnapshot\": "<<c.cpuSetFlags<<", \"caches\": [";
        for(size_t i=0;i<c.caches.size();++i){if(i)o<<',';o<<c.caches[i];}o<<"]}";
    }
    o<<"\n  ],\n  \"domains\": [\n";first=true;for(const auto& d:domains){if(!first)o<<",\n";first=false;
        o<<"    {\"id\": "<<d.id<<", \"kind\": "<<quote(kind(d.relationship))<<", \"level\": "<<d.level<<", \"type\": "<<d.type<<", \"bytes\": "<<d.bytes
         <<", \"lineBytes\": "<<d.lineBytes<<", \"coreEfficiencyClass\": "<<d.efficiency<<", \"flagsOrNode\": "<<d.flags<<", \"cpus\": ";keys(d.cpus);o<<'}';}
    o<<"\n  ],\n  \"errors\": [";for(size_t i=0;i<errors.size();++i){if(i)o<<',';o<<quote(errors[i]);}
    o<<"],\n  \"notes\": [";for(size_t i=0;i<notes.size();++i){if(i)o<<',';o<<quote(notes[i]);}o<<"]\n}\n";return o.str();
}
}
