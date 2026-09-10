<p align="center"><img src="src/pulse.svg" width="104" alt="Pulse icon"></p>
<h1 align="center">Pulse</h1>
<p align="center"><strong>Adaptive Windows power policy, with bounded bursts and recoverable settings.</strong></p>
<p align="center">C++20 · Native Win32 · Windows 11 x64 · Battery-first · G Helper companion</p>
<p align="center"><a href="docs/validation.md">Local validation: 7/7 suites passed</a> · <a href="https://github.com/XEF2ZQ/Pulse/actions/workflows/windows.yml">Windows CI workflow</a></p>

Pulse explores a practical systems problem: keep light work efficient while temporarily giving interactive requests and sustained productive work more CPU headroom. It changes supported Windows power settings through native APIs, then restores the settings it owned when control ends.

**Current patch: 1.2.0-demand-preview.** The adaptive power controller is implemented and has been exercised on one ASUS Ryzen AI 9 HX 370 laptop. The optional scheduler guard is diagnostic groundwork, **off by default**; it does not place threads or alter their QoS. This is an experimental desktop utility, with measured limitations rather than a universal compatibility claim.

**Build verification:** the local Release build and all seven CTest suites passed, including the independent battery diagnostic parser. Hosted CI has not executed yet: the initial run was blocked before any job steps started. A hosted build artifact is therefore not yet available.

**Diagnostic patch:** [battery discharge recovery investigation](docs/power-tail-investigation.md) adds an opt-in recorder and measured recovery results. It makes no resident governor changes. Component-power comparison remains pending live HWiNFO data.

The [12/35 W SMU proposal evaluation](docs/smu-cap-evaluation.md) adds component-recovery comparison and a required-sensor capture option in PulseTailProbe 0.2. Automatic hardware limit writes remain deferred pending measurements and coordinated ownership with G Helper.

## Behavior at a glance

| Situation | Windows power mode | CPU boost policy |
|---|---|---|
| Light work / settled workload | Best power efficiency | Disabled (`0`) |
| Accepted foreground/navigation burst | Best performance | Aggressive (`2`) |
| Detected sustained compute / measured edit demand | Best performance | Efficient Aggressive (`4`) |
| AC connected, default configuration | Release Pulse's ownership; bypass | Restore owned values |
| Pause / normal exit | Restore owned settings | Restore owned values |

The plugged-in control button enables the same controller on AC. These are policy requests, not direct clock commands. Firmware, thermals, OEM configuration and Windows determine the resulting frequency. Efficient Aggressive is not guaranteed to be more efficient than Aggressive on a given platform.

## Engineering highlights

- **Two detection paths:** foreground/navigation events for short responsiveness bursts; CPU-time evidence for sustained compute. Known tools receive a faster decision path, with a behavioral fallback for unknown process trees in the current user session.
- **Bounded work:** coalescable timers adapt to controller state; process handles are cached; no continuous thread enumeration, WMI subscription, ETW stream, NPU inference or sensor polling.
- **Explicit ownership:** source-specific snapshots, native API readback, conflict detection and a separate recovery companion. Pulse does not repeatedly overwrite another utility's changed settings.
- **Independent policies:** pure burst/compute decisions and synthetic topology/cost tests can run without changing power settings.
- **Hardware boundaries:** G Helper retains fan, GPU and hardware tuning control. Pulse contains no driver, direct SMU/MSR access or undervolting actuator.

## Evidence, with scope

Historical baseline: the September 6 power-controller tests measured a **0.0937% average of one logical core** over a 50.047-second active tray interval, and **2.23 MiB controller private memory** at its end. A separate AC-bypass interval had no workload samples, scans or power writes. These are short process-level observations, not battery-energy measurements.

The 1.2 patch was exercised with real MSVC and previously unlisted Python compilation, plus Darktable local-contrast slider edits. A 40-second settled preview interval measured 0.117% of one logical core and 2.67 MiB private memory. See the [patch validation and limits](docs/productive-demand-patch.md) for timing and test scope. **The current sustained-work detector is sampled; it is not instantaneous.** Earlier user-assisted video/voice checks remained efficient and felt smooth to the user, without instrumented media quality measurements.

**No measured battery-runtime improvement is claimed.** A repeatable everyday-plus-development workload and paired Pulse-on/Pulse-paused discharge experiment are planned. See the [validation report](docs/validation.md) and [measurement protocol](docs/benchmark-plan.md) for dates, limitations and acceptance criteria.

## Review the design

| Start here | What it covers |
|---|---|
| [Architecture](docs/architecture.md) | Components, state arbitration, timing, power ownership and failure paths |
| [Design decisions](docs/design-decisions.md) | Tradeoffs, vendor research, sampled detection and scheduler boundaries |
| [Build and test](docs/build.md) | Toolchain, reproducible commands, CI scope and local integration checks |
| [Usage and recovery](docs/usage.md) | Controls, G Helper handoff, configuration and safe rollback |
| [Validation](docs/validation.md) | Observed results and unverified scenarios |
| [Roadmap](docs/roadmap.md) | Broader detector validation, completion signals and evidence-gated scheduling |
| [Research](RESEARCH.md) | Primary documentation and related projects |

## Build

Requires MSVC C++ tools, CMake 3.22+ and a recent Windows 11 SDK (locally tested with 10.0.26100.0). From an x64 Native Tools prompt:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build -L unit --output-on-failure
```

The output is `build/Pulse.exe`. Launching normally can change Windows power settings. Start with `Pulse.exe --paused` for an inactive UI or `Pulse.exe --dry-run` for observation, and read [usage](docs/usage.md) before enabling control. No installer, G Helper copy, personal configuration or machine log is included in the repository. Local compilation and use require the rights holder's permission; the build commands document the engineering workflow and do not themselves grant a licence.

## Project status and rights

Created by **Peter Kosanyi**, using an AI-assisted development workflow. The public source and engineering record are available for portfolio review. Copyright © 2026 Peter Kosanyi. **All rights reserved**; this is not an open-source licence. See [LICENSE](LICENSE), [NOTICE](NOTICE.md) and [contribution policy](CONTRIBUTING.md).
