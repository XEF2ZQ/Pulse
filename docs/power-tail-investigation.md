# Battery discharge recovery: diagnostic patch

**September 9, 2026 · Pulse 1.2.0-demand-preview · PulseTailProbe 0.1**

The slow decline is reproducible in the rate reported by the Windows battery interface. In two local experiments, CPU activity fell promptly and the boost setting returned to Disabled about one second after the work ended, while reported discharge took 16–28.5 seconds to remain below 8 W. This establishes a separation between policy release and reported battery recovery. It does **not** establish how much of that tail is actual energy consumption.

The patch adds an independent recorder, conversion/parser tests and an offline analyzer. It makes no changes to Pulse's resident controller, startup behavior, settings or scheduling. There is no evidence yet that another CPU policy change would remove this reported tail.

## Experiment and results

The installed Pulse controller remained active on battery on the ASUS GA605WI / Ryzen AI 9 HX 370. Each capture lasted 100 seconds: 30 seconds before a bounded eight-second integer workload, followed by recovery. Four worker threads were used in the first run and eight in the second. The workload's completed thread join supplies the end marker. Sampling was every 500 ms; timings below are observations at that resolution, not millisecond-accurate transition guarantees.

| Observation | Four workers | Eight workers |
|---|---:|---:|
| Samples / battery read errors | 200 / 0 | 200 / 0 |
| Source changes | 0 | 0 |
| Baseline reported discharge, median | 6.178 W | 6.100 W |
| Late recovery reported discharge, median | 5.897 W | 6.974 W |
| Highest observed reported discharge | 11.297 W | 13.753 W |
| Reported peak after workload completion | 1.502 s | 1.509 s |
| First total CPU activity below 5% after completion | 0.497 s | 0.505 s |
| First boost Disabled readback after completion | 0.997 s | 0.995 s |
| Reported discharge remaining below 10 W | 6.492 s | 12.507 s |
| Reported discharge remaining below 8 W | 15.999 s | 28.505 s |
| Median interval between distinct raw rate values | 1.485 s | 1.009 s |
| Descriptive exponential decay time constant | 13.852 s | 13.292 s |
| Log-domain fit R-squared | 0.996 | 0.987 |
| Median combined telemetry query time | 1.733 ms | 1.793 ms |
| Logger thread CPU time across capture | 15.625 ms | 109.375 ms |
| Recorder private memory at capture end | 1.461 MiB | 1.492 MiB |

“Remaining below” requires approximately three seconds of subsequent valid samples. Baseline is the last 15 seconds before load; the late-recovery reference is the last 30 valid samples (about 15 seconds here). The exponential fit describes the portion above that reference plus 0.6 W, from the post-work peak through 45 seconds after completion. It is not a calibrated battery-controller model.

The raw battery rate changed every approximately 1–1.5 seconds. It was not simply frozen for the full recovery interval. Both runs had a similar smooth decay despite different workload sizes. Battery voltage also recovered much sooner: in the four-worker capture it had rebounded to 16.138 V about 1.5 seconds after completion, while the rate was at its reported peak. Together these observations support an averaging/filtering hypothesis. Voltage is not an independent power measurement, and device activity, fan power, thermal effects and background work remain possible contributors.

Raw captures and figures are retained with the local diagnostic package, outside the source repository. No battery-life improvement, saved joules or macOS comparison is claimed. The recorded logger CPU time excludes the intentionally busy worker threads and does not count all kernel/firmware or HWiNFO overhead. Sampling twice per second is an opt-in diagnostic cost, not a new resident Pulse behavior.

## Missing evidence

HWiNFO's shared mapping returned Windows error 2 (not found), including a final inventory check. No shared sensor frames were collected, so **these captures contain no CPU-package watts, GPU watts, fan speeds or residency measurements**. The user's earlier package-power observation remains an observation, not a measurement performed by this recorder.

The user enabled shared support, but the live Sensors window could not be verified because the native app-access request timed out while the user was away. The recorder must be restarted once HWiNFO is publishing. Synthetic parser tests do not replace this live check.

These were short CPU-only experiments with the normal desktop background workload present. Screen brightness, panel power state, device states and fan speed were not instrumented. Darktable rotation/perspective recovery has not been remeasured with component telemetry. No independent instantaneous battery current meter was available. An application-only test cannot resolve every whole-platform energy source.

## Windows, AMD and Intel findings

The Windows battery status interface returns the battery driver's reported rate. Negative rate denotes discharge; absolute rates use mW, whereas relative-unit batteries and unknown sentinels must not be labelled as watts. The ACPI battery stack obtains present-rate information from platform firmware; asking Windows more often does not itself improve the gauge's temporal response. See [BATTERY_STATUS](https://learn.microsoft.com/en-us/windows/win32/power/battery-status-str), [status IOCTL](https://learn.microsoft.com/en-us/windows/win32/power/ioctl-battery-query-status) and [ACPI battery firmware requirements](https://learn.microsoft.com/en-us/windows-hardware/design/component-guidelines/acpi-battery-and-power-subsystem-firmware-implementation).

HWiNFO's author explains that firmware can limit battery refresh independently of the application's polling interval. That statement concerns another ASUS model; it supports checking the reporting path but does not prove this laptop's filtering algorithm. [HWiNFO author response](https://www.hwinfo.com/forum/threads/battery-sensor-only-updates-every-10s.7819/).

Read-only power-plan inspection found `PERFAUTONOMOUS=1`, `PERFAUTONOMOUSWINDOW=0x7530` (30,000 microseconds, or 30 ms), and `PERFDECTIME=1` on AC and DC. Microsoft documents that the autonomous window influences sensitivity to brief utilization changes; it explicitly excludes autonomous-enabled systems from `PerfDecreaseTime`. The configured 30 ms window is not evidence of a 15-second processor policy hold. Plan readback is not proof of a physical register's effective value. [Autonomous mode](https://learn.microsoft.com/en-us/windows-hardware/customize/power-settings/options-for-perf-state-engine-perfautonomousmode), [autonomous window](https://learn.microsoft.com/en-us/windows-hardware/customize/power-settings/options-for-perf-state-engine-perfautonomouswindow), [performance decrease timer](https://learn.microsoft.com/en-us/windows-hardware/customize/power-settings/options-for-perf-state-engine-perfdecreasetime).

AMD's CPPC description distinguishes software performance requests from hardware performance selection. It provides no basis for treating a processor request as a battery discharge command. Direct SMU/MSR writes would also introduce ownership and platform-compatibility issues beyond Pulse's documented Windows policy interface. [AMD64 Architecture Programmer's Manual, Volume 2, section 17.6](https://docs.amd.com/api/khub/documents/sD1_QL~h4Afq2_tvzxqqSQ/content).

Intel's power-management documentation describes dependencies and latency constraints around deeper package idle states. This is architectural context, not a register recipe for this AMD laptop. CPU activity and package power alone cannot establish the state of the full platform. [12th Generation Intel Core processor datasheet, power management](https://cdrdv2-public.intel.com/655258/655258-011.pdf).

For devices, Windows PoFx uses driver-supplied component states, wake latency and shared power-domain dependencies. A desktop utility cannot safely substitute an unconditional “power everything down now” operation for those driver decisions. A verified device-specific tail would require addressing the responsible workload, driver or supported device policy. [Microsoft PoFx overview](https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/overview-of-the-power-management-framework).

## Decision and next experiment

Keep the installed controller unchanged. Do not repeatedly write the same efficiency settings, shorten an inapplicable legacy timer, force devices off, change fan policy, or present a mathematically deconvolved rate as measured watts.

Once live shared telemetry is available:

1. Inventory CPU package power, available GPU power/clock sensors, fan speeds and idle residency by their actual sensor identities. Check units and producer timestamps.
2. Repeat the same CPU workload with unchanged HWiNFO polling. Compare component recovery against the workload end, CPU activity, boost readback and raw battery rate.
3. Repeat bounded Darktable edits on a disposable fixture, with settled time before each run. Separate CPU work from GPU work and thermal/fan recovery.
4. If a component remains active, investigate its supported ownership and idle controls. Test one change at a time and verify responsiveness plus restoration.
5. If component power settles quickly while the rate alone decays, retain raw reporting and explain its response time. Establish energy differences through repeated longer controlled runs or independent electrical measurement before changing the governor.

## Validation

Release compilation with MSVC warnings-as-errors passed. All seven native CTest suites passed, including battery conversion and malformed/truncated shared-memory parsing. Both live battery captures completed without read errors. The optional HWiNFO path passed synthetic parsing tests only and remains pending live validation.

The diagnostic suite executed 644 conversion/parser checks. Five additional Python tests passed for continuous recovery windows, invalid/source-changing samples, gaps, insufficient duration and strict thresholds. CLI smoke checks rejected malformed numeric input and existing capture directories. A final three-second capture completed with six battery samples; its shared sensor mapping remained unavailable. Reanalysis with the gap/error checks preserved both experiments' reported results.

See [recorder usage and boundaries](../tools/TAIL_PROBE.md). Copyright © 2026 Peter Kosanyi. All rights reserved under the repository's existing terms.
