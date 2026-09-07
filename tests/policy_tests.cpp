#include "policy.h"
#include "workload.h"
#include <iostream>
#include <random>
#include <stdexcept>
using namespace pulse;
int count = 0;
void check(bool v, const char* message) { ++count; if (!v) throw std::runtime_error(message); }
int main() {
    try {
        Policy p; Sample s{1000, 3, 5, 50, true, false}; p.tick(s);
        check(p.request(Signal::Foreground, s), "launch starts immediately");
        s.now += 125; p.tick(s); check(p.burst, "minimum hold prevents flapping");
        s.now += 125; p.tick(s); s.now += 125; p.tick(s);
        check(!p.burst, "completed low-load task returns to efficiency in 375ms");
        s.now += 1000; s.system = 35; s.foreground = 90;
        check(p.request(Signal::Navigation, s), "navigation starts a burst");
        auto start = s.now;
        for (int i=0;i<14;++i) { s.now+=125; p.request(Signal::Navigation,s); p.tick(s); }
        s.now=start+1800;p.tick(s);
        check(!p.burst, "event flood cannot extend burst forever");
        check(p.lastStop-start <= 1800, "battery hard deadline");
        Policy voice; s={1000,25,120,0,true,false}; voice.tick(s);
        for(int i=0;i<180;++i) { s.now+=1000; s.system=25+(i%3); voice.tick(s); check(!voice.burst,"steady voice with typing never boosts"); }
        Policy video; s={1000,8,40,20000,true,false}; video.tick(s);
        for(int i=0;i<100;++i) { s.now+=1000; s.system=(i%7)*10; video.tick(s); check(!video.burst,"unattended fluctuating playback never boosts"); }
        Policy scroll; s={1000,3,10,10,true,false}; scroll.tick(s);
        for(int i=0;i<100;++i) { s.now+=1000; s.system=3+i%5; scroll.tick(s); check(!scroll.burst,"light scrolling remains efficient"); }
        Policy rise; s={1000,2,0,20,true,false}; rise.tick(s);
        s.now+=1000; s.system=25; s.foreground=95; rise.tick(s); check(rise.burst,"correlated sharp load edge triggers");
        s.now+=100; s.blocked=true; rise.tick(s); check(!rise.burst,"lock or saver cuts burst immediately");
        check(!rise.request(Signal::Manual,s),"blocked conditions reject manual boost");
        Policy background; s={1000,2,0,0,true,false}; background.tick(s);
        s.now+=1000; s.system=80; background.tick(s); check(!background.burst,"background CPU rise alone is insufficient");
        Policy flood; s={1000,80,150,0,true,false}; flood.tick(s); double high=0;
        for(int i=0;i<2400;++i) { s.now+=125; flood.request(Signal::Navigation,s); flood.tick(s); if(flood.burst)high+=125; check(flood.budget>=0 && flood.budget<=3600,"budget invariant"); }
        check(high/300000 < .20,"navigation flood long-run burst duty is bounded");
        std::mt19937 random(1729); Policy fuzz; s={1000,0,0,0,true,false};
        for(int i=0;i<100000;++i) {
            s.now+=1+random()%1500; s.system=random()%101; s.foreground=random()%401;
            s.inputAge=random()%25000; s.blocked=random()%20==0; s.battery=random()%2==0;
            fuzz.tick(s); if(random()%5==0)fuzz.request(static_cast<Signal>(random()%4),s);
            check(!s.blocked || !fuzz.burst,"blocked invariant across randomized sequences");
            check(fuzz.deadline >= fuzz.started,"deadline invariant");
        }
        check(knownCompute(L"cinebench.exe"),"Cinebench recognized");
        check(knownCompute(L"performancetest.exe"),"PassMark recognized");
        check(knownCompute(L"cl.exe")&&knownCompute(L"rustc.exe"),"compiler tools recognized");
        check(!knownCompute(L"not-cinebench.exe"),"compute names match exactly");
        check(sustainedExcluded(L"chrome.exe")&&sustainedExcluded(L"codex.exe"),"playback and voice hosts excluded");
        ComputePolicy compute;
        for(uint64_t t=1000;t<10000;t+=1000)compute.update(t,0,0,false);
        check(!compute.active,"idle benchmark menus stay efficient");
        compute.update(10000,100,0,false);compute.update(11000,100,0,false);
        check(!compute.active,"one noisy CPU sample cannot enter compute");
        compute.update(12000,100,0,false);check(compute.active,"sustained single-thread build enters compute");
        for(uint64_t t=13000;t<73000;t+=1000)compute.update(t,2400,0,false);
        check(compute.active,"multicore render stays in compute beyond burst deadline");
        compute.update(73000,0,0,false);compute.update(74000,0,0,false);
        check(compute.active,"brief compiler gap does not flap");
        compute.update(75000,0,0,false);check(!compute.active,"completed work releases compute");
        for(uint64_t t=76000;t<83000;t+=1000)compute.update(t,0,100,false);
        check(compute.active,"unknown CPU-bound foreground work gets sustained support");
        compute.update(84000,2400,2400,true);check(!compute.active,"energy saver blocks compute");
        for(uint64_t t=85000;t<95000;t+=1000)compute.update(t,2400,2400,true);
        check(!compute.active,"blocked compute remains off");
        compute.reset();compute.update(96000,100,0,false);compute.update(97000,0,0,false);
        compute.update(98000,100,0,false);check(!compute.active,"separated spikes do not accumulate");
        std::cout << "PASS: " << count << " assertions; idle, voice, video, navigation, load edges, deadlines, lock, budget, randomized sequences.\n";
        return 0;
    } catch(const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
