# Build and verification

## Toolchain

The application targets Windows 11 x64. The local publication build uses MSVC 19.51.36248.0, Windows SDK 10.0.26100.0, CMake 4.2.3 and Ninja. Install Visual Studio's Desktop development with C++ workload and a recent Windows 11 SDK. No external source dependencies are downloaded by CMake.

From an x64 Native Tools prompt:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build -L unit --output-on-failure
```

Alternatively, `python build.py` discovers the installed Visual Studio toolchain, builds with Ninja into `build-native`, and runs all local tests. CMake must be on PATH. The script never downloads a compiler or SDK.

MSVC builds use C++20 and a static C/C++ runtime. The application and topology implementation enable warning level 4, warnings as errors and strict conformance; the application enables Control Flow Guard, DEP and ASLR. This is build hardening, not a security audit. Debug symbols, local build directories and executables are excluded from Git.

## Test layers

| Test | Scope | Side effects |
|---|---|---|
| `tail_telemetry` | Battery units/sentinels and bounded HWiNFO wire parsing | Synthetic data only; no power writes |
| `productive_demand` | Current demand entry/exit, missing evidence, overlap, noise and native discovery parser | No power API writes |
| `power_policy` | Deterministic/randomized burst and legacy sustained-compute decisions | No power API writes |
| `scheduler_unit` | Synthetic topology buffers, parser boundaries, classification, cost guards, change budget | No live topology requirement or power writes |
| `live_topology` | Actual Windows topology discovery | Read-only |
| `scheduler_live` | Unit checks plus independent local OS topology cross-checks | Read-only |
| `scheduler_gui` | Real native button events, OFF default, persistence, text/layout, no-write check | Creates an isolated test INI/report; no power control |

Run all local checks with `ctest --test-dir build --output-on-failure`. The topology integration checks may reject incomplete virtual-machine topology; their purpose is to validate the actual target, not to assume every host exposes sufficient data.

The GitHub workflow is configured to build Release on `windows-2025`, run the `unit` label and upload the executable plus CTest logs as an unsigned engineering artifact. At publication, the initial hosted run was blocked before any job steps started; hosted verification and its artifact remain pending. Hosted CI does not run benchmarks, apply power settings or establish compatibility with a physical laptop. The action versions are pinned to reviewed commit identifiers. Workflow permissions are limited to reading repository contents.

## Historical 1.1.2 package provenance

The published application's `src/` snapshot is preserved from the locally installed scheduler-preview source. Publication adds documentation, a standalone CMake project and a `--unit-only` path in the scheduler test executable; it does not change runtime policy or the installed desktop application.

The local installed executable's SHA-256 at publication preparation was:

```text
074E950E3E8E7F5216912F91FD97C71CD63530B24A93458883E47B10FA432C51
```

Different toolchains or build paths can produce a different PE hash. That installed hash is provenance for the recorded local revision, not an expected hash for all rebuilds. The resource version is `1.1.2-scheduler-preview`; the power diagnostics header retains its `Pulse 1.1` component label.

The new patch resource version is `1.2.0-demand-preview`. Its hash is supplied with the local patch package; the historical hash above is not the new binary. `demand_probe` is a separate read-only, adaptive detector observer for opt-in live workload experiments. It does not apply power settings.

## Local power experiments

`PulseTailProbe.exe` is a separate recorder built by CMake. Its optional `--exercise` creates a bounded CPU workload but does not write power settings; the running Pulse controller can respond to that workload. See [recorder documentation](../tools/TAIL_PROBE.md) and [September 9 investigation](power-tail-investigation.md). It is not launched by Pulse.

Launching the normal controller, using AC opt-in, or calling `Pulse.exe --exercise` changes live system power policy. These are deliberately outside default CI. Use a controlled test machine, preserve the journal and integration backups, compare before/after settings, and verify restoration before interpreting results. [Validation](validation.md) records the existing local experiments; [benchmark plan](benchmark-plan.md) describes the future runtime harness.
