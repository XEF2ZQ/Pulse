#pragma once
#include <string>
#include <cstdint>

namespace pulse {
enum class PowerState { Efficiency, Burst, Compute };
inline bool knownCompute(const std::wstring& n) {
    // Exact executable names, never window titles or command-line contents.
    for (auto name : {L"cinebench.exe", L"cinebench windows 64 bit.exe", L"performancetest.exe", L"performancetest64.exe", L"performancetest32.exe",
        L"pt-cputest64.exe", L"pt-bulletphysics64.exe", L"pt-databasetest64.exe", L"pt-dbbenchmark64.exe",
        L"pt-opencv64.exe", L"pt-pdftest.exe",
        L"cl.exe", L"c1.exe", L"c1xx.exe", L"c2.exe", L"clang.exe", L"clang++.exe", L"clang-cl.exe", L"gcc.exe", L"g++.exe",
        L"cc1.exe", L"cc1plus.exe", L"rustc.exe", L"link.exe", L"lld.exe", L"lld-link.exe",
        L"msbuild.exe", L"vbc.exe", L"csc.exe", L"vbcsccompiler.exe", L"javac.exe",
        L"ninja.exe", L"cmake.exe", L"make.exe", L"mingw32-make.exe", L"cargo.exe"})
        if(n==name)return true;
    return false;
}
inline bool sustainedExcluded(const std::wstring& n) {
    for (auto name : {L"chrome.exe", L"msedge.exe", L"firefox.exe", L"thorium.exe", L"brave.exe",
        L"opera.exe", L"vivaldi.exe", L"codex.exe", L"chatgpt.exe", L"ms-teams.exe", L"teams.exe",
        L"zoom.exe", L"discord.exe", L"slack.exe", L"vlc.exe", L"mpv.exe", L"wmplayer.exe",
        L"spotify.exe", L"audiodg.exe", L"applicationframehost.exe", L"runtimebroker.exe",
        L"dwm.exe", L"explorer.exe", L"ghelper.exe", L"parkcontrol.exe", L"pulse.exe"})
        if(n==name)return true;
    return false;
}
// Inputs are CPU time / wall time in percent of one logical processor.
// A known tool must actually be busy. Unknown foreground applications require
// longer evidence; browser/media/voice hosts never use that fallback.
class ComputePolicy {
public:
    bool active=false;
    uint64_t candidate=0, quietSince=0;
    bool update(uint64_t now,double known,double foreground,bool blocked) {
        bool old=active;
        bool busy=known>=65.0 || foreground>=90.0;
        if(blocked)reset();
        else if(!active) {
            if(!busy)candidate=0;
            else {if(!candidate)candidate=now;
                if(now-candidate >= (known>=65.0?1500u:3500u)){active=true;quietSince=0;}}
        } else {
            // A lower exit threshold prevents oscillation between compiler phases.
            if(known>=20.0||foreground>=35.0)quietSince=0;
            else {if(!quietSince)quietSince=now;if(now-quietSince>=1200)reset();}
        }
        return active!=old;
    }
    void reset(){active=false;candidate=quietSince=0;}
};
}
