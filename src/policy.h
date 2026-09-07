#pragma once
#include <algorithm>
#include <cstdint>

namespace pulse {
enum class Signal { Foreground, Navigation, LoadEdge, Manual };
struct Sample {
    uint64_t now = 0;
    double system = 0;       // Percent of whole CPU.
    double foreground = 0;   // Percent of ONE logical processor.
    uint32_t inputAge = 0;
    bool battery = true;
    bool blocked = false;    // Screen off, session locked, suspended or Energy Saver.
};
// Pure policy: no timers, power APIs, allocations, process names, or I/O.
class Policy {
public:
    bool burst = false;
    double baseline = 0;
    double budget = 3600;
    uint64_t started = 0, deadline = 0, lastStop = 0, lastTick = 0;
    uint64_t sustainedUntil = 0;
    int quiet = 0;
    bool initialized = false;
    const wchar_t* reason = L"Waiting for a request";

    bool request(Signal signal, const Sample& s) {
        advance(s.now);
        if (s.blocked || burst) return false;
        const bool explicitIntent = signal != Signal::LoadEdge;
        if (!explicitIntent && (s.now < sustainedUntil || s.inputAge > 1200)) return false;
        if (signal != Signal::Manual && s.inputAge > 1800) return false;
        if (lastStop && s.now - lastStop < 850) return false;
        if (budget < 450) { reason = L"Protecting the battery between bursts"; return false; }
        burst = true; started = s.now; quiet = 0;
        deadline = s.now + static_cast<uint64_t>(std::min(budget, s.battery ? 1800.0 : 2400.0));
        reason = signal == Signal::Foreground ? L"An application came to the foreground" :
            signal == Signal::Navigation ? L"A browser navigation request" :
            signal == Signal::Manual ? L"A short boost requested by you" : L"A sharp load rise after interaction";
        return true;
    }
    bool tick(const Sample& s) {
        const bool old = burst;
        advance(s.now);
        if (!initialized) { baseline = s.system; initialized = true; }
        if (s.blocked) stop(s.now, L"Display off, locked, or Windows Energy Saver");
        else if (burst) {
            const bool low = s.system < std::max(8.0, baseline + 3.0) && s.foreground < 22.0;
            quiet = low ? quiet + 1 : 0;
            if (s.now >= deadline || budget < 1) {
                sustainedUntil = s.now + 8000;
                stop(s.now, L"Burst complete; sustained work stays efficient");
            } else if (s.now - started >= 375 && quiet >= 2) {
                stop(s.now, L"The short task has settled");
            }
        } else {
            // A rising edge, recent interaction AND foreground pressure are required.
            // High steady load, playback, typing and mouse movement alone cannot boost.
            if (s.system - baseline >= 9.0 && s.foreground >= 45.0 && s.inputAge < 1200)
                request(Signal::LoadEdge, s);
            if (!burst) reason = s.inputAge > 15000 ? L"No recent interaction" :
                s.system > 12.0 ? L"Sustained work, without a new responsiveness request" :
                L"Light work, reading, scrolling or playback";
        }
        if (!burst) baseline += 0.15 * (s.system - baseline);
        return old != burst;
    }
    void reset(uint64_t now) {
        burst = false; quiet = 0; initialized = false; lastTick = now;
        lastStop = now; sustainedUntil = now + 1000;
        reason = L"Efficiency is ready";
    }
private:
    void advance(uint64_t now) {
        if (lastTick && now >= lastTick) {
            const double elapsed = static_cast<double>(now - lastTick);
            budget = std::clamp(budget + elapsed * (burst ? -1.0 : 0.20), 0.0, 3600.0);
        }
        lastTick = now;
    }
    void stop(uint64_t now, const wchar_t* why) {
        if (burst) lastStop = now;
        burst = false; quiet = 0; reason = why;
    }
};
}
