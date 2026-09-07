#include "scheduler_policy.h"
#include <iostream>
#include <cstring>
#include <stdexcept>
#include <cstddef>
#include <set>

using namespace pulse::scheduler;
static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x))throw std::runtime_error(#x);}while(false)
using Bytes=std::vector<unsigned char>;
template<class T> static void put(Bytes& data,size_t at,const T& value){
    CHECK(at+sizeof(T)<=data.size());std::memcpy(data.data()+at,&value,sizeof(T));
}
static void relation(Bytes& data,LOGICAL_PROCESSOR_RELATIONSHIP type,WORD group,KAFFINITY mask,BYTE efficiency=0) {
    SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX item{};
    item.Relationship=type;
    item.Size=sizeof(item);
    if(type==RelationCache){item.Cache.Level=3;item.Cache.Type=CacheUnified;item.Cache.CacheSize=16*1024*1024;item.Cache.GroupCount=1;item.Cache.GroupMask={mask,group,{0,0,0}};}
    else{item.Processor.GroupCount=1;item.Processor.EfficiencyClass=efficiency;item.Processor.GroupMask[0]={mask,group,{0,0,0}};}
    const auto start=data.size();data.resize(start+sizeof(item));std::memcpy(data.data()+start,&item,sizeof(item));
}
static void cpuset(Bytes& data,DWORD id,WORD group,BYTE index,BYTE core,BYTE efficiency) {
    SYSTEM_CPU_SET_INFORMATION item{};item.Size=sizeof(item);item.Type=CpuSetInformation;
    item.CpuSet.Id=id;item.CpuSet.Group=group;item.CpuSet.LogicalProcessorIndex=index;item.CpuSet.CoreIndex=core;item.CpuSet.EfficiencyClass=efficiency;
    const auto start=data.size();data.resize(start+sizeof(item));std::memcpy(data.data()+start,&item,sizeof(item));
}
static void parserTests() {
    Bytes relations,sets;
    for(WORD group:{WORD(0),WORD(1)}){
        relation(relations,RelationProcessorPackage,group,3);
        relation(relations,RelationCache,group,3);
        relation(relations,RelationProcessorCore,group,3,static_cast<BYTE>(group));
        cpuset(sets,group*2,group,0,0,static_cast<BYTE>(group));cpuset(sets,group*2+1,group,1,0,static_cast<BYTE>(group));
    }
    const auto t=parseTopology(relations,sets,4);CHECK(t.valid());CHECK(t.cpus.size()==4);CHECK(t.capabilities.hasSmt);
    CHECK(t.capabilities.heterogeneous);CHECK(t.cpus[0].core!=t.cpus[2].core); // CoreIndex 0 in two groups is NOT one core.
    CHECK(t.cpus[0].cpuSetId==0u); // Zero-valued IDs are valid, not missing.
    CHECK(distance(t.cpus[0],t.cpus[2])==Distance::OtherPackage);
    CHECK(distance(t.cpus[0],t.cpus[1])==Distance::SameCore);
    CHECK(parseTopology(relations,{},4).valid()); // No CPU Set API: topology remains usable read-only.
    CHECK(!parseTopology(relations,sets,5).valid());
    for(size_t n=0;n<relations.size();++n)CHECK(!parseTopology(std::span(relations).first(n),sets,4).valid());
    for(size_t n=1;n<sets.size();++n)CHECK(!parseTopology(relations,std::span(sets).first(n),4).valid());
    Bytes bad=relations;put(bad,offsetof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX,Size),DWORD(0));CHECK(!parseTopology(bad,sets,4).valid());
    bad=relations;put(bad,offsetof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX,Size),DWORD(UINT_MAX));CHECK(!parseTopology(bad,sets,4).valid());
    bad=sets;put(bad,offsetof(SYSTEM_CPU_SET_INFORMATION,Size),DWORD(0));CHECK(!parseTopology(relations,bad,4).valid());
    bad=sets;put(bad,sizeof(SYSTEM_CPU_SET_INFORMATION)+offsetof(SYSTEM_CPU_SET_INFORMATION,CpuSet.Id),DWORD(0));CHECK(!parseTopology(relations,bad,4).valid());
    // Future record types must be skipped using their advertised size.
    bad=relations;SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX future{};future.Relationship=static_cast<LOGICAL_PROCESSOR_RELATIONSHIP>(999);future.Size=sizeof(future);
    const auto start=bad.size();bad.resize(start+sizeof(future));std::memcpy(bad.data()+start,&future,sizeof(future));CHECK(parseTopology(bad,sets,4).valid());
    // Legacy GroupCount=0 cache records still contain one GROUP_AFFINITY.
    bad=relations;put(bad,sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)+offsetof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX,Cache.GroupCount),WORD(0));CHECK(parseTopology(bad,sets,4).valid());
}
static void policyTests() {
    CHECK(classify({})==WorkloadClass::MixedUnknown);
    WorkloadEvidence e;e.productive=true;CHECK(classify(e)==WorkloadClass::MixedUnknown);
    e.lowPriority=true;e.deadlineSlackVerified=true;CHECK(classify(e)==WorkloadClass::SustainedLowPriority);
    e.playbackOrVoice=true;CHECK(classify(e)==WorkloadClass::MixedUnknown);
    e.playbackOrVoice=false;e.interactive=true;CHECK(classify(e)==WorkloadClass::Interactive);
    e.interactive=false;e.cacheSensitive=true;CHECK(classify(e)==WorkloadClass::CacheSensitive);
    e={};e.periodic=true;e.deadlineSlackVerified=true;CHECK(classify(e)==WorkloadClass::IdlePeriodic);
    CHECK(profile({})==Profile::PassThrough);
    CpuCapabilities c;c.topologyValid=true;CHECK(profile(c)==Profile::PassThrough);
    c.hasQoSApi=true;CHECK(profile(c)==Profile::QosOnly);
    c.hasCpuSets=true;c.heterogeneous=true;CHECK(profile(c)==Profile::GenericHeterogeneous);
    MigrationBudget budget;
    MeasuredCost old{100,105,0,0,0,0,500},candidate{65,70,2,2,2,2,550};
    BenefitEvidence proof{true,true,true,42,3,600};
    auto choose=[&](uint64_t now){return evaluateCost(old,candidate,proof,42,now,0,budget,WorkloadClass::SustainedLowPriority,Distance::SamePackage);};
    CHECK(!choose(1999).worthTrial);CHECK(choose(2000).worthTrial);
    proof.validated=false;CHECK(!choose(2000).worthTrial);proof.validated=true;
    proof.topologyFingerprint=41;CHECK(!choose(2000).worthTrial);proof.topologyFingerprint=42;
    proof.sameWorkUnits=false;CHECK(!choose(2000).worthTrial);proof.sameWorkUnits=true;
    proof.deadlineMs=500;CHECK(!choose(2000).worthTrial);proof.deadlineMs=600;
    candidate.cachePenaltyMj=80;CHECK(!choose(2000).worthTrial);candidate.cachePenaltyMj=2;
    candidate.energyUpperMj=NAN;CHECK(!choose(2000).worthTrial);candidate.energyUpperMj=70;
    CHECK(!evaluateCost(old,candidate,proof,42,2000,0,budget,WorkloadClass::Interactive,Distance::SameCore).worthTrial);
    CHECK(!evaluateCost(old,candidate,proof,42,2000,0,budget,WorkloadClass::IdlePeriodic,Distance::Unknown).worthTrial);
    for(int i=0;i<4;++i)CHECK(budget.committed(2000+i));
    for(uint64_t now=2004;now<62000;++now)CHECK(!budget.available(now));
    CHECK(!budget.committed(0));CHECK(!choose(61000).worthTrial);CHECK(budget.committed(62000));
    SchedulerGuard guard;CHECK(!guard.enabled());guard.enabled(false);guard.event(true,e);CHECK(guard.report().find(L"Scheduler events: 0")!=std::wstring::npos);
    guard.enabled(true);guard.event(true,e);CHECK(guard.report().find(L"Scheduler events: 1")!=std::wstring::npos);
    CHECK(guard.report().find(L"Scheduler policy writes: 0")!=std::wstring::npos);
}
static void liveChecks() {
    const auto t=discoverTopology();CHECK(t.valid());
    std::set<CpuKey> identities;
    for(const auto& cpu:t.cpus) {
        CHECK(identities.insert(cpu.key).second);
        for(auto sibling:cpu.siblings) {
            auto peer=std::find_if(t.cpus.begin(),t.cpus.end(),[&](const auto& other){return other.key==sibling;});
            CHECK(peer!=t.cpus.end());CHECK(peer->core==cpu.core);CHECK(std::find(peer->siblings.begin(),peer->siblings.end(),cpu.key)!=peer->siblings.end());
        }
        // Independent Windows NUMA API cross-check; not a second parser view.
        PROCESSOR_NUMBER processor{cpu.key.group,cpu.key.index,0};USHORT node=0;
        if(GetNumaProcessorNodeEx(&processor,&node)&&cpu.numa)CHECK(*cpu.numa==node);
    }
    CHECK(identities.size()==GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
}
int main(int argc, char** argv) {
    const bool unitOnly=argc==2&&std::string(argv[1])=="--unit-only";
    if(argc>1&&!unitOnly){std::cerr<<"Usage: scheduler_tests [--unit-only]\n";return 2;}
    try{parserTests();policyTests();if(!unitOnly)liveChecks();std::cout<<"PASS: "<<checks<<" scheduler checks; no scheduler or power writes.\n";return 0;}
    catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
