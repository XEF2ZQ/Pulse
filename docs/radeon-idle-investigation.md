# Radeon 890M idle-power investigation

## Decision — 19 October 2026

Ship diagnostics, not an unverified GPU actuator. On this HX 370 laptop, the installed AMD driver does not expose supported manual GPU clock, memory-clock or power tuning through the ADLX queries tested. Vari-Bright reports unsupported. AMD dynamic-refresh capability interrogation fails, so its availability remains unknown. These observations do not establish universal hardware limitations.

The requested battery-only scope is preserved in the supplied diagnostic launchers. No new GPU polling, driver, startup task or power-setting writer is added to Pulse. Its installed executable and the user's settings remain unchanged. No battery-runtime improvement is claimed.

## Evaluation of the supplied mechanisms

| Proposed mechanism | Evidence and disposition |
|---|---|
| GFXOFF | Driver/firmware-owned; no validated Windows user-mode setter. Low effective GPU clock does not prove residency. |
| Graphics clock gating | Driver-owned; no arbitrary register writes. |
| Graphics power gating | Same ownership boundary; no supported per-block control established. |
| Graphics deep sleep | No verified Windows actuator on this target. |
| GPU clock limits | ADLX manual graphics tuning: successful query, unsupported. |
| Display clock reduction | Display-driver-owned; changing a clock directly is not equivalent to choosing a supported display mode. |
| Panel self refresh | No supported panel-specific actuator or residency measurement established. |
| SoC clock reduction | No validated Windows interface; changing shared fabric limits could affect multiple devices. |
| FCLK / data-fabric sleep | Linux support does not establish a Windows control path or safe register recipe. |
| VCN video engine | Driver/runtime owns workload gating. Disabling it may replace efficient hardware decoding with CPU work. |
| JPEG engine | No validated independent idle control. |
| VPE / display hub | No validated independent idle control. |
| LCLK | No validated Windows control or safe target-specific limits. |
| Refresh rate | Observed 240 Hz on AC and 60 Hz on battery; G Helper already owns automatic switching. No duplicate writer added. |
| PCIe ASPM | Active scheme already requests maximum power savings on DC (index 2), off on AC (0). Policy readback does not prove each device's physical residency. |

Windows exposes GPU component power management to the display driver through [WDDM power-management interfaces](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/gpu-power-management-of-idle-and-active-power). Linux's [SMU 14 implementation](https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/amd/pm/swsmu/smu14/smu_v14_0_0_ppt.c) is useful architectural context, not a supported Windows mailbox contract. Reverse engineering alone would not satisfy the requested reliability standard; no undocumented writes were attempted.

## Capability checker

`PulseGpuProbe` is an independent, read-only, one-shot program. It enumerates active display modes, Windows display paths and active-scheme AC/DC ASPM policy. An optional ADLX build asks the installed driver for tuning and display capabilities. It loads the system ADLX library, releases its interfaces and terminates ADLX before unloading. It never calls a setting setter.

| Local driver query | Result |
|---|---|
| Manual graphics / memory / power tuning | `ADLX_OK`, unsupported for all three |
| Vari-Bright | `ADLX_OK`, unsupported |
| Dynamic-refresh interface | Obtained; support query returns `ADLX_FAIL` (3), availability unknown |
| ADLX termination | Success |
| Windows display | 2560 × 1600 at 60 Hz on battery; matching advertised rates 60, 75, 90, 120, 240 Hz |
| Windows DRR path boost flag | Not set in this snapshot; not proof of unsupported DRR |

The full report intentionally returns **2 (incomplete)** for the failed dynamic-refresh query. Successful unsupported responses are valid results. Exit codes: 0 requested queries completed, 1 invalid arguments, 2 incomplete evidence, 3 battery-only requirement not met or source changed. `--windows-only` skips ADLX; `--require-battery` checks source before and after discovery. This is a bounded snapshot, not a continuously enforced power-source lock.

Build with `-DPULSE_ADLX_SDK_ROOT=<official ADLX checkout>` to include ADLX queries; leave empty for Windows-only builds. Local ADLX headers came from revision `32b5a740d42295c5dfe9026b9f52683da0f3af91`; headers and AMD binaries are not vendored into Pulse. See [ADLX tuning services](https://gpuopen.com/manuals/adlx/adlx-sdk-references/adlx-interfaces/gpu-tuning/iadlxgputuningservices/), [Windows ASPM policy](https://learn.microsoft.com/en-us/windows-hardware/customize/power-settings/pci-express-settings-link-state-power-management) and [QueryDisplayConfig](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-querydisplayconfig).

## HWiNFO reader correction

The live producer uses version 2, revision 1: 392-byte sensor records, 460-byte reading records, 16 sensors and 310 readings. Its mapped reservation is 20,029,440 bytes, but the described payload is only 148,920 bytes. The previous reader rejected both the version and the large reservation.

The recorder now accepts the validated v2 layout, copies only the bounded described payload under the shared mutex, and retains its 16 MiB payload ceiling. Version/revision, strides, counts, arithmetic bounds, overlaps and sensor indices remain checked. Numeric fields and original legacy labels are read from the preserved prefix; appended localized UTF-8 fields are not interpreted. Unknown layouts fail closed. Allocation failure releases the producer mutex.

This changes only the opt-in recorder. It does not add hardware polling to Pulse or alter HWiNFO's sampling interval.

## Local measurements and validation

A 60-second battery capture produced 60 samples at one-second intervals and 30 distinct HWiNFO frames. All samples reported DC, with zero battery or HWiNFO read errors. There were 41 changes in the raw battery-rate value.

| Reading | Minimum | Mean | Maximum |
|---|---:|---:|---:|
| Battery-reported discharge | 8.867 W | 9.699 W | 11.925 W |
| CPU Package Power | 4.668 W | 5.134 W | 9.351 W |
| CPU SoC Power (SVI3 TFN) | 0.546 W | 0.573 W | 0.868 W |

These are baseline observations during the investigation, not a controlled A/B test or a workload-recovery experiment. Battery reporting may be filtered and sensor domains/sample windows differ. Subtracting package power from battery power would not isolate GPU power. HWiNFO's separate “Total System Power” label is not assumed to cover the whole laptop.

The recorder's main thread used 125 ms CPU during the capture (about 0.21% of one core), with 1.76 MiB private memory. That excludes HWiNFO and other observer costs. This overhead exists only while the diagnostic runs.

Validation completed: Release compilation with warnings treated as errors; all 10 native CTest suites; 1,562 checks including replay of the captured v2 mapping; Windows-only battery discovery; live v2 inventory; invalid/duplicate capability-checker arguments; full ADLX capability queries with explicit partial-failure reporting. Boundary tests include truncated frames, undersized strides, unknown versions, invalid indices and oversized arithmetic inputs. Raw machine captures remain local.

**Deferred at the user's request:** live AC-start refusal and DC-to-AC interruption tests, because the charger is unavailable. Do not describe those paths as live-validated. A successful build or local test is not a guarantee across all drivers and machines.

`PulseTailProbe --require-battery` refuses unconfirmed DC at startup and aborts at the next sample if AC appears or source becomes unknown. With `--require-hwinfo`, missing shared data also fails startup. No synthetic load runs unless explicitly requested. See [recorder usage](../tools/TAIL_PROBE.md).
