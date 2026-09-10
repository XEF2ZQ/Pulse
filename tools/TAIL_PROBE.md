# PulseTailProbe 0.2

An independent Windows x64 diagnostic executable for comparing workload completion, CPU-policy release and battery-reported discharge. It is not a resident service and does not change power settings. Pulse does not launch it or link its telemetry code.

## Run

Double-click `PulseTailProbe.exe` for a 90-second read-only capture. It writes a timestamped directory under `TailCaptures` beside the executable and exits. To include HWiNFO's existing values, the live Sensors window must be running with Shared Memory Support enabled before starting the recorder. Keep its usual polling interval. HWiNFO is optional; missing shared data is explicitly reported.

From PowerShell in the executable's folder:

```powershell
.\PulseTailProbe.exe --inventory --out .\sensor-inventory
.\PulseTailProbe.exe --out .\manual-edit --seconds 100
.\PulseTailProbe.exe --out .\cpu-comparison --seconds 100 --exercise 4
```

Use a new output directory for each command. `--inventory` only records availability and sensor identities. The exercise intentionally consumes CPU: 30 seconds of baseline followed by eight seconds on 1–8 workers, then recovery. It starts only on battery with known charge of at least 15% and Energy Saver off; source/charge changes end the synthetic load at the next sample. Other applications are not stopped. Without `--exercise`, no synthetic load runs. Ctrl+C stops a capture.

Options: `--seconds` 1–1800 (at least 60 with exercise); `--period-ms` 250–5000, default 500; `--exercise` 1–8; `--inventory`; `--help`. Short polling intervals have an observer cost and do not make firmware readings instantaneous. Keep 500 ms for comparable measurements. No high-resolution timer request is made.

Use `--require-hwinfo` to fail at startup if shared sensor readings are absent; it prevents the synthetic load from starting in that case. Continued freshness and coverage must still be checked after capture. [Component comparison and power-limit evaluation](../docs/smu-cap-evaluation.md) documents the new offline `component_tail.py` analyzer. Its ten tests run with `python test_component_tail.py`.

## Files

| File | Contents |
|---|---|
| `metadata.txt` | Interval, availability/error codes, completed samples, logger-thread CPU time and private memory |
| `samples.csv` | Elapsed/wall time, raw battery rate, voltage, CPU activity, input age, boost/mode readback, shared-data age and query duration |
| `sensor-catalog.csv` | HWiNFO sensor IDs, instances, reading IDs, original labels, units and startup values |
| `hwinfo.csv` | Selected shared power/fan/residency/activity/clock readings when producer timestamp changes |
| `events.csv` | Capture/workload markers; `load_joined` marks completed work rather than the request to stop |

Missing data is `nan` or an explicit error code. Raw signed battery rate is preserved; `discharge_w` reverses its sign so discharge is positive and charging negative. Relative-unit batteries cannot supply comparable watts. The first present non-UPS system battery is used; multiple packs are not aggregated.

Captures may identify installed sensor hardware and record wall-clock times. They do not collect typed text, URLs, audio, screenshots or battery serial numbers. Review captures before publishing. Default `TailCaptures` directories are excluded from Git.

## Analysis

With Python 3.10+:

```powershell
python .\analyze_tail.py .\cpu-comparison
python .\analyze_tail.py .\cpu-comparison --plot
```

The first command uses only the standard library and writes `analysis.json`. Plotting additionally requires Matplotlib and writes `tail.png`. Analysis requires completed synthetic workload markers; manual-edit captures remain raw data for time-aligned review. The decay fit describes reported rate; it cannot prove firmware filtering or actual energy loss. Examine `hwinfo.csv` separately for component power; the analyzer does not guess which sensor is CPU package power.

The sustained-recovery calculation rejects windows containing invalid data, AC samples or gaps over 1.1 seconds. Use an interval of 1000 ms or less for that metric. Run `python test_analyze_tail.py` for five offline analysis-validity tests; they are separate from the native CTest suites.

## Implementation boundaries

- SetupAPI and read-only battery query IOCTLs; cached handles/tags with stale-tag refresh. Status timeout is zero, but synchronous driver calls can still take time. No hard real-time acquisition bound is promised.
- HWiNFO uses `Global\HWiNFO_SENS_SM2` and its shared mutex. The reader skips a busy writer, copies a bounded mapping under the mutex, then validates version, offsets, counts, strides and indices. No hardware driver is loaded and HWiNFO polling is unchanged.
- The mapping is opened at startup. Restart after enabling shared support or restarting Sensors. Error 2 means no mapping was found in that process's context. Permission and licensing restrictions are not bypassed.
- Producer timestamps have whole-second precision. Subsecond updates cannot all be uniquely identified; equal timestamps do not prove an individual sensor is frozen. Reported age also depends on wall-clock adjustments.
- Power scheme/mode reads are separate calls and may straddle transitions. They read policy, not measured frequency. CPU busy time covers all logical CPUs, not instruction throughput or package watts.
- Display brightness/on-off state, device states and independent battery current are not acquired. These captures cannot completely attribute platform power.
- Run only on demand. Do not add the recorder to startup or its polling loop to Pulse.

## Build and verification

CMake builds `PulseTailProbe.exe` alongside `Pulse.exe`, using C++20, the static MSVC runtime and system libraries only. `ctest --test-dir build -L unit --output-on-failure` includes `tail_telemetry`: rate conversions and shared-memory boundaries. Live HWiNFO validation is separately required.

The parser is an original implementation of the public wire layout. [HWiNFO interface discussion](https://www.hwinfo.com/forum/threads/introducing-remote-sensor-monitor-a-restful-web-server.1025/page-4). HWiNFO itself is not included.

Copyright © 2026 Peter Kosanyi. All rights reserved. See the repository LICENSE.
