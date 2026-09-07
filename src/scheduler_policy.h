#pragma once
#include "scheduler_topology.h"
#include <array>
#include <cmath>
#include <algorithm>

namespace pulse::scheduler {
enum class WorkloadClass { IdlePeriodic, SustainedLowPriority, CacheSensitive, Interactive, MixedUnknown };
struct WorkloadEvidence {
    bool interactive=false, mixed=false, cacheSensitive=false;
    bool productive=false, lowPriority=false, deadlineSlackVerified=false, periodic=false;
    bool playbackOrVoice=false;
};
inline WorkloadClass classify(const WorkloadEvidence& e) {
    // Retain the existing media/voice treatment. High utilization alone does not
    // establish productive intent or permission to change another process's QoS.
    if(e.playbackOrVoice||e.mixed)return WorkloadClass::MixedUnknown;
    if(e.interactive)return WorkloadClass::Interactive;
    if(e.cacheSensitive)return WorkloadClass::CacheSensitive;
    if(e.productive&&e.lowPriority&&e.deadlineSlackVerified)return WorkloadClass::SustainedLowPriority;
    if(e.periodic&&e.deadlineSlackVerified)return WorkloadClass::IdlePeriodic;
    return WorkloadClass::MixedUnknown;
}
enum class Profile { GenericHeterogeneous, GenericHomogeneous, QosOnly, PassThrough };
inline Profile profile(const CpuCapabilities& c) {
    if(!c.topologyValid)return Profile::PassThrough;
    if(c.hasCpuSets&&c.heterogeneous)return Profile::GenericHeterogeneous;
    if(c.hasQoSApi)return Profile::QosOnly;
    return Profile::PassThrough;
}
// Topological relation is an ordinal, NOT a measured energy penalty. An unknown
// relation can never be converted to zero migration cost.
enum class Distance { SameCore, SameLlc, SameDie, SamePackage, OtherPackage, Unknown };
inline Distance distance(const LogicalCpu& a,const LogicalCpu& b) {
    if(a.core==b.core)return Distance::SameCore;
    if(a.llc&&b.llc&&a.llc==b.llc)return Distance::SameLlc;
    if(a.die&&b.die&&a.die==b.die)return Distance::SameDie;
    if(a.package&&b.package)return a.package==b.package?Distance::SamePackage:Distance::OtherPackage;
    return Distance::Unknown;
}
class MigrationBudget {
    // Limits Pulse-issued policy changes, not migrations performed by Windows.
    std::array<uint64_t,4> stamps_{};
    size_t count_=0;
    uint64_t last_=0;
public:
    bool available(uint64_t now) const {
        if(now<last_)return false;
        size_t recent=0;for(size_t i=0;i<count_;++i)if(now-stamps_[i]<60000)++recent;
        return recent<stamps_.size();
    }
    bool committed(uint64_t now) {
        if(!available(now))return false;
        size_t live=0;for(size_t i=0;i<count_;++i)if(now-stamps_[i]<60000)stamps_[live++]=stamps_[i];
        stamps_[live++]=now;count_=live;last_=now;return true;
    }
};
struct MeasuredCost {
    // All penalties are calibrated millijoules for comparable completed work.
    double energyLowerMj=0, energyUpperMj=0, cachePenaltyMj=0;
    double latencyPenaltyMj=0, contentionPenaltyMj=0, migrationPenaltyMj=0;
    double completionUpperMs=0;
};
struct BenefitEvidence {
    bool validated=false, sameWorkUnits=false, samePowerConditions=false;
    uint64_t topologyFingerprint=0;
    unsigned repeatedTrials=0;
    double deadlineMs=0;
};
struct CostDecision { bool worthTrial=false; const char* reason="unvalidated"; };
inline CostDecision evaluateCost(const MeasuredCost& oldCost,const MeasuredCost& newCost,
    const BenefitEvidence& evidence,uint64_t fingerprint,uint64_t now,uint64_t residentSince,
    const MigrationBudget& budget,WorkloadClass workload,Distance relation) {
    if(workload==WorkloadClass::MixedUnknown||workload==WorkloadClass::Interactive||workload==WorkloadClass::CacheSensitive)
        return {false,"workload remains with Windows"};
    if(!evidence.validated||!evidence.sameWorkUnits||!evidence.samePowerConditions||
       evidence.repeatedTrials<3||evidence.topologyFingerprint!=fingerprint||!fingerprint)
        return {false,"no comparable calibrated evidence"};
    auto valid=[](const MeasuredCost& c){
        for(double v:{c.energyLowerMj,c.energyUpperMj,c.cachePenaltyMj,c.latencyPenaltyMj,c.contentionPenaltyMj,c.migrationPenaltyMj,c.completionUpperMs})
            if(!std::isfinite(v)||v<0)return false;
        return c.energyLowerMj>0&&c.energyUpperMj>=c.energyLowerMj&&c.completionUpperMs>0;
    };
    if(!valid(oldCost)||!valid(newCost)||!std::isfinite(evidence.deadlineMs)||evidence.deadlineMs<=0)
        return {false,"invalid measurement"};
    if(relation==Distance::Unknown)return {false,"unknown locality cost"};
    if(now<residentSince||now-residentSince<2000)return {false,"minimum residency"};
    if(!budget.available(now))return {false,"global policy-change budget exhausted"};
    if(newCost.completionUpperMs>evidence.deadlineMs)return {false,"completion deadline"};
    const double upper=newCost.energyUpperMj+newCost.cachePenaltyMj+newCost.latencyPenaltyMj+newCost.contentionPenaltyMj+newCost.migrationPenaltyMj;
    // Conservative lower-vs-upper comparison and an additional 10% margin. This
    // guard margin is not an invented Zen5/Zen5c performance ratio.
    if(!std::isfinite(upper)||upper>=oldCost.energyLowerMj*0.9)return {false,"benefit does not exceed uncertainty and margin"};
    return {true,"measured candidate qualifies for an explicit trial"};
}
class SchedulerGuard {
    bool enabled_=false, eligible_=false;
    Topology topology_;
    uint64_t events_=0, discoveries_=0;
    WorkloadClass classification_=WorkloadClass::MixedUnknown;
public:
    bool enabled() const {return enabled_;}
    void enabled(bool value){enabled_=value;}
    void refresh(){topology_=discoverTopology();++discoveries_;}
    void event(bool eligible,const WorkloadEvidence& evidence) {
        if(!enabled_)return;
        eligible_=eligible;++events_;classification_=classify(evidence);
        // No actuator: this patch has no measured per-workload energy profile.
        // Generic topology is insufficient authority for external QoS/placement.
    }
    const Topology& topology() const{return topology_;}
    const wchar_t* status() const {
        if(!enabled_)return L"Scheduler guard off";
        if(!eligible_)return L"Scheduler guard ready; power control inactive";
        if(!topology_.valid())return L"Scheduler guard: Windows default (topology unavailable)";
        return L"Scheduler guard: Windows default (benefit not measured)";
    }
    std::wstring report() const {
        return std::wstring(L"SchedulerEnabled: ")+(enabled_?L"1":L"0")+L"\nScheduler status: "+status()+
            L"\nScheduler events: "+std::to_wstring(events_)+L"\nScheduler topology queries: "+std::to_wstring(discoveries_)+
            L"\nScheduler topology valid: "+(topology_.valid()?L"yes":L"no")+
            L"\nScheduler policy writes: 0\nScheduler periodic timers: 0\nScheduler calibration: unavailable; no placement actuator enabled\n";
    }
};
inline bool loadSchedulerPreference(const std::wstring& path) {
    wchar_t value[16]{};GetPrivateProfileStringW(L"Preferences",L"SchedulerEnabled",L"0",value,16,path.c_str());
    return wcscmp(value,L"1")==0;
}
inline bool saveSchedulerPreference(const std::wstring& path,bool value) {
    return WritePrivateProfileStringW(L"Preferences",L"SchedulerEnabled",value?L"1":L"0",path.c_str())!=FALSE;
}
}
