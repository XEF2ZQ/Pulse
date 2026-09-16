<p align="center"><img src="src/pulse.svg" width="96" alt="Pulse icon"></p>
<h1 align="center">Pulse</h1>
<p align="center"><strong>Responsive Windows power policy with bounded overhead and recoverable settings.</strong></p>
<p align="center">C++20 · Native Win32 · Windows 11 x64 · Battery-first · G Helper companion</p>

Pulse temporarily gives interactive requests and productive CPU work more performance headroom, then returns to an efficient baseline. It addresses a practical tradeoff: permanently disabling boost can make a laptop sluggish; treating every busy interval as performance-critical can waste energy during routine media or voice use.

The implementation combines foreground intent, measured process-tree CPU demand, bounded process-exit notifications and a source-specific recovery journal. It uses Windows policy APIs and leaves hardware tuning to G Helper/OEM firmware.

**Current publication: Pulse 1.3.0-efficiency-preview + PulseTailProbe 0.2.** Locally built and tested on one ASUS Ryzen AI 9 HX 370 laptop. Eight native suites and fifteen offline analysis tests passed. Hosted CI is configured; earlier execution was blocked by a GitHub account restriction. Local verification is documented independently.

**New: [Efficiency options](docs/efficiency-options.md).** An opt-in dropdown provides remembered Windows Update service and Defender real-time preference controls, with an optional installed administrator broker, event notifications, automatic correction/backoff, verified status and recovery records. Both default OFF. Update correction, restoration and no-prompt broker transport passed local tests. Defender's live setter is unverified and respects Tamper Protection; these controls are not unbypassable. No battery benefit from them is claimed.

**No battery-runtime gain is claimed yet.** Policy transitions, observer overhead and battery-reported recovery have been measured; paired runtime/energy-per-completed-work validation is still planned. The optional Scheduler guard is diagnostic and OFF by default. No SMU power-limit, undervolting, NPU or per-thread placement actuator is shipped.

## Review the project

| Reading path | Purpose |
|---|---|
| [Engineering case study](docs/engineering-case-study.md) | Problem, key decisions, measured outcomes and unresolved questions |
| [Architecture](docs/architecture.md) | Detection, arbitration, timing, ownership and source map |
| [Decision records](docs/design-decisions.md) | Why this design; alternatives and consequences |
| [Failure handling](docs/failure-handling.md) | Missing evidence, external changes and controller failure |
| [Evidence index](docs/evidence/README.md) | Claim-to-test mapping and reproducible sanitized traces |
| [Usage and recovery](docs/usage.md) | Controls, G Helper handoff, Pause and restoration |
| [Build and verification](docs/build.md) | Toolchain, native/Python tests and hosted-check limits |
| [Roadmap](docs/roadmap.md) | Remaining work and acceptance gates |

## Runtime behavior

| Observed situation | Requested Windows mode | Requested CPU boost |
|---|---|---|
| Light or settled work | Best power efficiency | Disabled (`0`) |
| Accepted foreground/navigation burst | Best performance | Aggressive (`2`) |
| Measured productive compute or edit demand | Best performance | Efficient Aggressive (`4`) |
| AC connected, default configuration | Release ownership; bypass | Restore owned values |
| Pause or normal exit | Restore owned values | Restore owned values |

Productive work takes priority over momentary UI bursts. Known tools have a faster candidate path; unfamiliar process trees can qualify through CPU-time evidence. Editing intent opens a short observation window and still needs measured demand. Detection is sampled and cannot recognize every workload or guarantee instantaneous transitions. See [productive-demand validation](docs/productive-demand-patch.md).

The plugged-in button enables the controller on AC. These are policy requests, not direct clock commands. Firmware, thermals and OEM configuration determine physical frequency and power. The label Efficient Aggressive does not establish an efficiency advantage on a particular CPU.

## Measurement highlights

| Observation | Scope and limit |
|---|---|
| MSVC compilation: 0.422–0.750 s observed release after process exit | Individual local runs, not a latency distribution |
| Unlisted Python compilation and Darktable slider edits detected | Functional evidence; broad compiler/editor coverage remains unverified |
| Settled controller: 0.117% of one logical CPU, 2.67 MiB private memory | One 40.067 s interval; not a battery-energy result |
| Synthetic work: boost Disabled in about 1 s; reported battery rate below 8 W after 16–28.5 s | Two CPU-load captures; no package/GPU sensor frames in those runs |

The final observation motivated an investigation, not an unverified hardware fix. A delayed battery-rate curve does not prove continued CPU consumption. A proposed 12/35 W SMU actuator remains deferred pending component evidence and coordinated hardware ownership. Read the [battery-tail investigation](docs/power-tail-investigation.md), [SMU evaluation](docs/smu-cap-evaluation.md) and [sanitized evidence](docs/evidence/README.md).

PulseTailProbe is a separate, opt-in recorder. It reads Windows battery telemetry and optional existing HWiNFO shared data, without adding sensor polling to the resident controller. Its offline component comparison rejects stale or insufficient evidence. [Recorder documentation](tools/TAIL_PROBE.md).

## Build and run

Requires MSVC C++ tools, CMake 3.22+ and a recent Windows 11 SDK. From an x64 Native Tools prompt:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
python -m unittest discover -s tools -p "test_*.py"
```

Python 3.10+ is required only for offline analysis/tests; optional plots use Matplotlib. Native binaries use the static MSVC runtime and Windows system libraries. `python build.py` can discover the locally installed toolchain and run native checks.

Start `build/Pulse.exe --paused` for an inactive UI, or read [usage](docs/usage.md) before enabling power control. Review [preview releases](https://github.com/XEF2ZQ/Pulse/releases) for unsigned local build artifacts, hashes and source provenance. A successful build is not a compatibility certification.

## Ownership and provenance

Created by **Peter Kosanyi**, with AI-assisted implementation, research, documentation and testing. Published for inspection and portfolio review. **Copyright © 2026 Peter Kosanyi. All rights reserved.** This is not an open-source licence; build instructions do not grant reuse permission. See [LICENSE](LICENSE), [NOTICE](NOTICE.md) and [contribution policy](CONTRIBUTING.md).
