#include <initguid.h>
#include "tail_telemetry.h"
#include <objbase.h>
#include <psapi.h>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <thread>

using namespace pulse::tail;
static std::atomic<bool> stop{false};
static BOOL WINAPI interrupt(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT) {
        stop = true;
        return TRUE;
    }
    return FALSE;
}
static std::string quoted(const std::string &s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '\"')
            out += '\"';
        out += c;
    }
    return out + '\"';
}
static std::string guidText(const GUID &g) {
    wchar_t w[40]{};
    StringFromGUID2(g, w, 40);
    std::string s;
    for (wchar_t c : w) {
        if (!c)
            break;
        s += char(c);
    }
    return s;
}
static double seconds() {
    LARGE_INTEGER t{}, f{};
    QueryPerformanceCounter(&t);
    QueryPerformanceFrequency(&f);
    return double(t.QuadPart) / double(f.QuadPart);
}
static uint64_t threadCpu() {
    FILETIME c{}, e{}, k{}, u{};
    if (!GetThreadTimes(GetCurrentThread(), &c, &e, &k, &u))
        return 0;
    return ticks(k) + ticks(u);
}
static double wallUnix() {
    FILETIME t{};
    GetSystemTimePreciseAsFileTime(&t);
    return double(ticks(t) - 116444736000000000ull) / 1e7;
}
static unsigned number(const std::wstring &s) {
    if (s.empty() || s.find_first_not_of(L"0123456789") != std::wstring::npos)
        throw std::runtime_error("Expected an unsigned integer option");
    size_t consumed = 0;
    const auto value = std::stoul(s, &consumed);
    if (consumed != s.size() || value > std::numeric_limits<unsigned>::max())
        throw std::runtime_error("Numeric option too large");
    return static_cast<unsigned>(value);
}
static bool selected(const SensorValue &v) {
    return v.unit == "W" || v.unit == "RPM" || v.label.find("C-state") != std::string::npos ||
           v.label.find("C6 Residency") != std::string::npos ||
           v.label.find("C0 Residency") != std::string::npos ||
           v.label.find("GPU Clock") != std::string::npos ||
           v.label.find("Memory Clock") != std::string::npos ||
           v.label.find("GPU D3D Usage") != std::string::npos || v.label == "Total CPU Usage";
}
int wmain(int argc, wchar_t **argv) {
    try {
        wchar_t executable[32768]{};
        DWORD length = GetModuleFileNameW(nullptr, executable, 32768);
        if (!length || length >= 32768)
            throw std::runtime_error("Cannot resolve recorder directory");
        SYSTEMTIME stamp{};
        GetLocalTime(&stamp);
        wchar_t runName[64]{};
        swprintf_s(runName, L"%04u%02u%02u-%02u%02u%02u-%03u", stamp.wYear, stamp.wMonth,
                   stamp.wDay, stamp.wHour, stamp.wMinute, stamp.wSecond, stamp.wMilliseconds);
        std::filesystem::path output =
            std::filesystem::path(executable).parent_path() / L"TailCaptures" / runName;
        unsigned duration = 90, period = 500, workers = 0;
        bool inventory = false, requireHwinfo = false;
        for (int i = 1; i < argc; ++i) {
            std::wstring arg = argv[i];
            auto next = [&]() -> std::wstring {
                if (i + 1 >= argc)
                    throw std::runtime_error("Missing option value");
                return argv[++i];
            };
            if (arg == L"--out")
                output = next();
            else if (arg == L"--seconds")
                duration = number(next());
            else if (arg == L"--period-ms")
                period = number(next());
            else if (arg == L"--exercise")
                workers = number(next());
            else if (arg == L"--inventory")
                inventory = true;
            else if (arg == L"--require-hwinfo")
                requireHwinfo = true;
            else if (arg == L"--help") {
                std::cout
                    << "PulseTailProbe --out folder [--seconds 90] [--period-ms 500] [--exercise "
                       "1..8] [--inventory] [--require-hwinfo]\nRead-only telemetry. Optional exercise: 8 seconds of "
                       "CPU work after 30 seconds idle. No power writes.\n";
                return 0;
            } else
                throw std::runtime_error("Unknown option");
        }
        if (duration < 1 || duration > 1800 || period < 250 || period > 5000 || workers > 8 ||
            (workers && duration < 60))
            throw std::runtime_error("Option outside bounded range");
        std::filesystem::create_directories(output);
        // Exclusive new run directory: avoid overwriting previous evidence.
        for (const auto *name :
             {L"metadata.txt", L"sensor-catalog.csv", L"samples.csv", L"hwinfo.csv", L"events.csv"})
            if (std::filesystem::exists(output / name))
                throw std::runtime_error("Output already contains capture files");
        Battery battery;
        Hwinfo hwinfo;
        SensorSnapshot sensors;
        bool shared = hwinfo.read(sensors);
        const auto first = battery.read();
        std::ofstream meta(output / L"metadata.txt");
        meta.exceptions(std::ios::failbit | std::ios::badbit);
        meta << "PulseTailProbe 0.2\ninterval_ms=" << period << "\nseconds=" << duration
             << "\nexercise_workers=" << workers << "\nbattery_open_error=" << battery.openError
             << "\nbattery_read_error=" << first.error << "\nhwinfo_error=" << hwinfo.error
             << "\nhwinfo_period_ms=" << sensors.periodMs << "\n";
        std::cout << "Battery error=" << first.error << " discharge_W=" << first.dischargeW
                  << "; HWiNFO error=" << hwinfo.error << " readings=" << sensors.values.size()
                  << " period_ms=" << sensors.periodMs << '\n';
        std::ofstream catalog(output / L"sensor-catalog.csv");
        catalog.exceptions(std::ios::failbit | std::ios::badbit);
        catalog << "sensor_id,instance,reading_id,sensor,label,unit,current\n";
        for (const auto &v : sensors.values)
            catalog << v.sensorId << ',' << v.instance << ',' << v.readingId << ','
                    << quoted(v.sensor) << ',' << quoted(v.label) << ',' << quoted(v.unit) << ','
                    << v.value << '\n';
        catalog.close();
        if (requireHwinfo && (!shared || sensors.values.empty()))
            throw std::runtime_error("Required HWiNFO readings unavailable; no workload started");
        if (inventory) {
            meta.close();
            return first.error ? 2 : 0;
        }
        if (first.error)
            throw std::runtime_error("Battery interface unavailable; capture aborted");
        if (workers && (first.state & BATTERY_POWER_ON_LINE))
            throw std::runtime_error("Load/idle discharge experiment requires battery power");
        if (!shared)
            std::cout << "No HWiNFO shared readings; CPU package watts will remain unavailable.\n";
        std::ofstream rows(output / L"samples.csv"), hw(output / L"hwinfo.csv"),
            events(output / L"events.csv");
        for (auto *stream : {&rows, &hw, &events})
            stream->exceptions(std::ios::failbit | std::ios::badbit);
        rows << "elapsed_s,unix_s,phase,ac_status,battery_error,battery_tag,battery_state,battery_"
                "capabilities,raw_rate,discharge_w,capacity_wh,voltage_v,rate_unchanged_s,cpu_busy_"
                "percent,input_age_ms,boost_error,boost,mode_error,mode,hwinfo_error,hwinfo_poll_"
                "time,hwinfo_poll_age_s,query_ms\n";
        hw << "elapsed_s,poll_time,sensor_id,instance,reading_id,sensor,label,unit,value\n";
        events << "elapsed_s,event\n";
        rows << std::fixed << std::setprecision(6);
        hw << std::fixed << std::setprecision(6);
        events << std::fixed << std::setprecision(6);
        if (!rows || !hw || !events || !catalog || !meta)
            throw std::runtime_error("Cannot write capture files");
        SetConsoleCtrlHandler(interrupt, TRUE);
        using GetMode = DWORD(WINAPI *)(GUID *);
        HMODULE module = GetModuleHandleW(L"powrprof.dll");
        auto acMode =
            reinterpret_cast<GetMode>(GetProcAddress(module, "PowerGetUserConfiguredACPowerMode"));
        auto dcMode =
            reinterpret_cast<GetMode>(GetProcAddress(module, "PowerGetUserConfiguredDCPowerMode"));
        FILETIME idle{}, kernel{}, user{};
        bool previousTimesValid = GetSystemTimes(&idle, &kernel, &user) != FALSE;
        uint64_t previousTotal = ticks(kernel) + ticks(user), previousIdle = ticks(idle);
        std::atomic<bool> loadStop = false;
        std::atomic<uint64_t> checksum = 0;
        // Workers must join before either referenced atomic is destroyed on exceptions.
        std::vector<std::jthread> jobs;
        const double start = seconds();
        const uint64_t cpuStart = threadCpu();
        double next = start, lastRateAt = start;
        LONG lastRate = first.rawRate;
        bool started = false, ended = false;
        int64_t lastPoll = 0;
        uint64_t count = 0, changed = 0, hwFrames = 0;
        events << 0 << ",capture_start\n";
        while (!stop && seconds() - start < duration) {
            const double now = seconds(), elapsed = now - start;
            if (workers && !started && elapsed >= 30) {
                SYSTEM_POWER_STATUS p{};
                if (!GetSystemPowerStatus(&p) || p.ACLineStatus != 0 || p.SystemStatusFlag == 1 ||
                    p.BatteryLifePercent == 255 || p.BatteryLifePercent < 15)
                    throw std::runtime_error("Load cancelled: source/saver/charge changed");
                started = true;
                events << elapsed << ",load_start\n";
                for (unsigned i = 0; i < workers; ++i)
                    jobs.emplace_back([&, i](std::stop_token token) {
                        uint64_t x = uint64_t(i) + 1;
                        while (!loadStop && !token.stop_requested() && !stop) {
                            for (unsigned n = 0; n < 16384; ++n)
                                x = (x * 6364136223846793005ull + 1442695040888963407ull) ^
                                    (x >> 13);
                        }
                        checksum.fetch_xor(x);
                    });
            }
            if (started && !ended && elapsed >= 38) {
                loadStop = true;
                jobs.clear();
                ended = true;
                events << seconds() - start << ",load_joined\n";
            }
            const double began = seconds();
            auto b = battery.read();
            const double unixTime = wallUnix();
            if (!b.error && b.rawRate != lastRate) {
                lastRate = b.rawRate;
                lastRateAt = now;
                ++changed;
            }
            SYSTEM_POWER_STATUS power{};
            const bool sourceKnown = GetSystemPowerStatus(&power) != FALSE;
            const bool policySourceKnown = sourceKnown && power.ACLineStatus <= 1;
            if (started && !ended &&
                (!policySourceKnown || power.ACLineStatus != 0 || power.SystemStatusFlag == 1 ||
                 power.BatteryLifePercent == 255 || power.BatteryLifePercent < 15)) {
                loadStop = true;
                jobs.clear();
                ended = true;
                events << seconds() - start << ",load_cancelled_source_or_charge\n";
            }
            GUID *scheme = nullptr;
            GUID mode{};
            DWORD boost = 0;
            DWORD boostError =
                policySourceKnown ? PowerGetActiveScheme(nullptr, &scheme) : ERROR_NOT_READY;
            if (!boostError && !scheme)
                boostError = ERROR_INVALID_DATA;
            if (!boostError && scheme) {
                boostError =
                    power.ACLineStatus == 1
                        ? PowerReadACValueIndex(nullptr, scheme, &GUID_PROCESSOR_SETTINGS_SUBGROUP,
                                                &GUID_PROCESSOR_PERF_BOOST_MODE, &boost)
                        : PowerReadDCValueIndex(nullptr, scheme, &GUID_PROCESSOR_SETTINGS_SUBGROUP,
                                                &GUID_PROCESSOR_PERF_BOOST_MODE, &boost);
            }
            if (scheme)
                LocalFree(scheme);
            auto getMode = power.ACLineStatus == 1 ? acMode : dcMode;
            DWORD modeError = !policySourceKnown ? ERROR_NOT_READY
                              : getMode          ? getMode(&mode)
                                                 : ERROR_CALL_NOT_IMPLEMENTED;
            double busy = missing;
            if (GetSystemTimes(&idle, &kernel, &user)) {
                auto total = ticks(kernel) + ticks(user), id = ticks(idle);
                if (previousTimesValid && total > previousTotal && id >= previousIdle &&
                    id - previousIdle <= total - previousTotal)
                    busy = 100.0 * double(total - previousTotal - (id - previousIdle)) /
                           double(total - previousTotal);
                previousTotal = total;
                previousIdle = id;
                previousTimesValid = true;
            } else
                previousTimesValid = false;
            LASTINPUTINFO input{sizeof(input)};
            const bool inputKnown = GetLastInputInfo(&input) != FALSE;
            shared = hwinfo.read(sensors);
            if (shared && sensors.pollTime != lastPoll) {
                ++hwFrames;
                lastPoll = sensors.pollTime;
                for (const auto &v : sensors.values)
                    if (selected(v))
                        hw << elapsed << ',' << sensors.pollTime << ',' << v.sensorId << ','
                           << v.instance << ',' << v.readingId << ',' << quoted(v.sensor) << ','
                           << quoted(v.label) << ',' << quoted(v.unit) << ',' << v.value << '\n';
            }
            rows << elapsed << ',' << unixTime << ','
                 << (started && !ended ? "load"
                     : ended           ? "recovery"
                                       : "baseline")
                 << ',' << (sourceKnown ? unsigned(power.ACLineStatus) : 255) << ',' << b.error
                 << ',' << b.tag << ',' << b.state << ',' << b.capabilities << ',' << b.rawRate
                 << ',' << b.dischargeW << ',' << b.capacityWh << ',' << b.voltageV << ','
                 << now - lastRateAt << ',' << busy << ','
                 << (inputKnown ? GetTickCount() - input.dwTime : MAXDWORD) << ',' << boostError
                 << ',' << boost << ',' << modeError << ',' << guidText(mode) << ',' << hwinfo.error
                 << ',' << (shared ? sensors.pollTime : 0) << ','
                 << (shared ? unixTime - double(sensors.pollTime) : missing) << ','
                 << (seconds() - began) * 1000 << '\n';
            ++count;
            if (count % 10 == 0) {
                rows.flush();
                hw.flush();
                events.flush();
            }
            next += double(period) / 1000.0;
            const double remaining = next - seconds();
            if (remaining > 0)
                Sleep(static_cast<DWORD>(remaining * 1000));
            else
                next = seconds();
        }
        loadStop = true;
        jobs.clear();
        events << seconds() - start << ",capture_end\n";
        PROCESS_MEMORY_COUNTERS_EX memory{};
        GetProcessMemoryInfo(GetCurrentProcess(),
                             reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory), sizeof(memory));
        meta << "samples=" << count << "\nraw_rate_changes=" << changed
             << "\nhwinfo_frames=" << hwFrames
             << "\nlogger_thread_cpu_ms=" << double(threadCpu() - cpuStart) / 10000
             << "\nprivate_mib=" << double(memory.PrivateUsage) / 1048576
             << "\nwork_checksum=" << checksum.load() << "\n";
        for (auto *stream : {&rows, &hw, &events, &meta})
            stream->close();
        std::cout << "Capture complete: " << count << " samples, " << changed
                  << " raw rate changes, " << hwFrames << " shared sensor frames.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
