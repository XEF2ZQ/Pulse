# Platform idle: evidence before control

## Decision record — 26 October 2026

Scope: panel self refresh/replay, data-fabric idle, GFXOFF/gating, and PCIe/device idle. No refresh-rate, undervolt, security, or CPU-classifier changes. The supplied proposal incorrectly assumed that Pulse already has a validated SMU actuator, direct SMU telemetry, and its own kernel driver. It has none of these. Its optional recorder reads HWiNFO shared memory.

### 1. Feasibility matrix

`Unavailable` below means unavailable **to this Pulse build**, not absent from silicon. Configuration, capability, enabled state, and measured residency are different facts.

| Mechanism | Observe | Supported control | Own-driver write | Verdict / next verification |
|---|---|---|---|---|
| A — PSR / replay | CONFIRMED: PSR1 capability and enabled sink configuration on the tested panel, through version-guarded native AUX reads. Instantaneous status was inactive. Replay and residency remain unknown. | No validated public PSR setter in the inspected ADL/ADLX headers. Registry names exist but are not a supported runtime contract. | NO-GO: readback does not establish a safe AUX/DMCUB ownership handoff for writes. | Preserve AMD ownership; report the actual panel state. Do not equate enabling the sink with making the source enter self refresh. |
| B — DF C-states | CONFIRMED: OS idle/activity signals are available as proxies. UNVERIFIED: actual DF residency. | No validated runtime DF control. OS idle policy is not a DF switch. | NO-GO: driver dispatch, firmware version, mailbox arbitration and restoration are unverified. | Attribute activity first. Firmware/BIOS comparisons require a controlled reboot experiment; do not invent an MSR or SMU opcode. |
| C — GFXOFF / gating | CONFIRMED: driver configuration and per-engine activity can be observed. UNVERIFIED: actual GFXOFF residency. | No validated public setter. A private driver implementation was found; presence does not establish a callable contract. | NO-GO: direct writes race the graphics driver's power sequencing. | Keep driver ownership. Record engine activity without disabling hardware decoding or throttling foreground apps. |
| D — PCIe / device idle | CONFIRMED: AC/DC policy, topology, last-reported device D-state, FADT ASPM prohibition bit. UNVERIFIED: physical L1/L1.2 residency and D3cold. | CONFIRMED: existing Windows DC ASPM guard requests index 2. No second policy writer. | NO-GO for capability writes; Windows reserves them. Supported driver-mediated reads would need a signed, reviewed device-stack implementation. | Retain existing guard. Enumerate NVMe settings; do not increase exit-latency tolerance without storage latency measurements. |

On the tested machine: Windows 11 25H2, build 26200.9457; Radeon 890M driver 32.0.13022.1001. The current scheme requests ASPM AC=0/DC=2. NVMe DC primary/secondary idle timeout is 30/1000 ms; latency tolerance is 50/100 ms. These values are policy, not proof of transition timing.

### 2. Functional verification and persistence gates

The user's subsequent instruction removes power-savings measurement as an implementation prerequisite. The remaining gates are functional: verify actual state rather than only a registry write, disable/restore behavior, restart persistence, battery/AC transitions, conflict handling and hardware compatibility. Do not claim persistence across reboot without performing that test. The energy protocol below is retained as optional future work, not a blocker requested by the user.

First collect identity/configuration once, then stop inventory tools. A diagnostic run is explicitly requested, bounded, and separate from Pulse's resident classifier. GPU-engine activity is a blocker candidate, not proof that a process prevents PSR or GFXOFF. CPU C1/C2/C3 counters are Windows processor accounting, never DF residency. Do not sum GPU engine percentages into a purported whole-GPU percentage.

Use static desktop, blinking caret, scrolling, 1080p playback and full-screen playback, followed by a foreground burst. Hold brightness, refresh rate, network, power source, temperatures and background applications constant. HWiNFO must use the same polling interval in both arms; compare an observer-off run to expose instrumentation effects. Wait for thermal stabilization; use at least five randomized AB/BA pairs, three minutes per steady-state segment. Collect raw battery rate, CPU package/SoC readings, sensor frame timestamps, and engine activity in separate attribution runs. EC-filtered discharge cannot identify a component or establish subsecond settling.

Only create an optional B arm after a reversible, verified control exists. A supported policy readback is insufficient for A/B of PSR, DF or GFXOFF. A future efficiency study could require paired energy savings exceeding measurement noise and observer cost. Functional validation must reject display artifacts, dropped playback frames and increased errors. Storage needs separate first-I/O latency checks; do not repurpose CPU timing as storage evidence. No energy measurements are required by the latest request.

Resident observer budget: zero periodic wakeups and zero background sensor sampling. Discovery runs only on explicit request in an independent process. Report measured process CPU time in microseconds; target <250,000 us for inventory and <500,000 us for a short activity sample. Measure rather than guarantee these budgets. Query-provider work may run in other processes and is not included in this CPU figure.

### 3. Reverse engineering performed and next gates

Downloaded portable Rizin 0.9.1 from its official [release](https://github.com/rizinorg/rizin/releases/tag/v0.9.1), checked the GitHub asset SHA-256 (`45ffa004e26653eaee8d262b8e6eeae09402a7d319c1f20a2ba794bd6af1cc28`), and used it locally without installing a driver. Inspected the installed `amdkmdag.sys` and `atiadlxx.dll` with Rizin strings/reference searches and LLVM exports/disassembly. No binary patching, injected hooks, kernel attachment, driver restart, signing changes or direct register writes were performed. A separate version-matched PSR returned-state query was executed as described below. Tool, experiment and proprietary binary material stay outside the repository.

Findings:

- Private `PEM_CWDDEPM_GfxOffControl` code validates input/output pointers, passes a command to an internal routine and returns a status. This is a useful lead, **not** evidence that the command is query-only or that an application can safely submit it. The external dispatch, input sizes, authorization and command semantics remain unresolved.
- References to `PP_GfxOffControl` enter driver configuration logic with ASIC-specific conditionals. The installed registry value is 1. It does not prove GFXOFF residency or universal semantics across ASICs.
- The driver contains DF-control support checks and DMCUB PSR state/residency trace strings. A string is not an exported ETW event, usable counter or verified hardware path for this device.
- Installed registry values `DalFeatureEnablePsrSU`, `DalPSRAllowSMUOptimizations`, and `DalReplayAllowSMUOptimizations` are 0; `DisableDynamicGfxMGPowerGating` is 1. Corresponding assignments exist in the installed INF. These may reflect OEM validation/workarounds. Do not flip them based on their names. A zero PSR-SU flag does not prove all PSR is disabled.
- Registered providers include `Microsoft-Windows-DxgKrnl`, DWM providers and `Microsoft-Windows-Kernel-Processor-Power`. The proposed `Microsoft-Windows-DxgKrnl-Display` name was not found. Presence alone does not prove a PSR/GFXOFF event schema.

**PSR private-interface experiment:** the installed ADL library exports `ADL2_Display_PsrControl` and `ADL2_Display_NativeAUXChannel_Access`, despite no declarations for them in the inspected public headers. Static analysis of the former established a returned-state branch distinct from its state-changing branch. A local probe called only the returned-state branch on connected displays. At normal user privilege it returned `ADL_ERR_NO_ADMINISTRATOR_PRIVILEGES` (-23); after normal UAC elevation it returned success and raw Boolean 1 for five logical adapter mappings of the connected display. This establishes a responding query, **not** the meaning of the Boolean, a residency measurement, or a safe setter. The five mappings are not five physical panels. No force/enable branch was invoked and the private probe is not shipped in Pulse.

The inspected ADL DLL SHA-256 is `0c77faaad74381ab811846ea76600a0e93e4be6405e1bf4cf1f035e935b5f8ac`; KMD SHA-256 is `7e0bbe1b99970f262b2e1d4a43c6d3dc11a387926a40f70480887f515730e988`. The launcher checked these before the first query. The public ADL checkout was `84c881cb2136f04287eb3af12de00a9e2867aa9e`. Its panel-firmware-update example describes pausing/resuming eDP low-power features for firmware servicing; that is not authority to repurpose firmware-update mode as everyday PSR control. That route was not invoked.

**Validated AUX readback:** static analysis of the native-AUX entry at RVA `0xAA050`, forwarding to `0x1150A0`, established a 36-byte request: size, status, action, address, length, and 16 data bytes. Action 0 reads returned bytes; state-changing action branches are never exposed. Four fixed four-byte reads succeeded without administrator privileges:

| DPCD address | Bytes (hex) | Interpretation |
|---|---|---|
| `0x000` | `14 14 c4 c1` | DPCD revision 1.4 |
| `0x070` | `01 4a 00 00` | PSR1 advertised; PSR2 not advertised |
| `0x170` | `05 00 00 00` | PSR enable bit set; CRC verification bit also set |
| `0x2008` | `00 01 00 00` | PSR sink inactive at this instant |

The delivered observer repeats those reads only for the first connected eDP target, checks success/status/length, and decodes no failed transaction. It checks Windows DC before each query and after the snapshot. The private call is permitted only when the System32 ADL file matches the inspected SHA-256; the read handle denies replacement while it is loaded. A changed driver fails closed until its ABI is reviewed. This is an empirical, version-specific interface, not a vendor-supported API or a compatibility certification. No panel writes, PSR force calls, firmware-service modes, arbitrary addresses or arbitrary command arguments are exposed. The optional observer adds no resident API calls.

This proves the panel is already configured to allow PSR. It does not prove time spent in self refresh, a saving, or persistence across a reboot. AUX observation itself can disturb idle. The enabled-but-inactive result is not grounds to repeatedly force the panel state: graphics-driver sequencing still owns entry and exit. Panel Replay capability was not queried.

Next RE steps: follow the private dispatch from the real user-mode client, establish a read-only request ABI and exact device/driver capability check; inspect provider metadata for actual fields; correlate output with controlled static/animated content. Diff only relevant adapter values around a vendor UI operation if such an operation exists. A BIOS DF comparison needs cold boots with one change at a time; runtime tracing cannot reconstruct mailbox traffic that happened before Windows started. Do not invoke arbitrary ACPI methods to replay firmware initialization.

Tier-3 go criteria: documented or independently validated device/version ABI; bounded readback; single-owner arbitration; a tested reverse operation; battery/source transition handling; an external recovery machine and crash evidence capture. If a display/fabric write can hard-hang, a rollback thread cannot save it: restoration may require a power cycle and offline removal of the experiment. These criteria are not met, so no Tier-3 writer ships.

### 4. Implementation boundary

The patch adds a read-only platform-idle report and a GUI entry point, not fake hardware switches. Unavailable A/B/C setters remain disabled with an explanation. The existing D guard remains the sole writer. Its established semantics are preserved: disabling it stops enforcement and retains the user's persistent DC preference. This differs from the proposal's temporary-control rollback request; silently changing that existing feature would be a regression. Any future temporary A/B/C control must journal originals and restore on disable, AC transition, shutdown and crash recovery.

### 5. Risk register

| Risk | Detection / response |
|---|---|
| Mislabeling a proxy as residency | Reports explicitly say configuration/activity/last-reported D-state; unknown residency stays unknown. |
| Observers keep GPU/fabric awake | On-demand bounded capture, observer-off baseline, report CPU time and sample count. |
| Driver reset or panel corruption | No private writes in this patch. Future experiment needs separate recovery validation. |
| Unsupported PCI config access crashes Windows | Use SetupAPI for current report; no physical-memory/config-space probing. |
| New settings fight G Helper | No new clock/refresh/power-limit owner. Existing ASPM guard only. |
| Device/provider/source changes during capture | Bound buffers/retries, retain errors and abort battery-only activity capture on unconfirmed DC. |
| Saved report mistaken for current state | Timestamp report; GUI identifies it as a snapshot. No automatic intervention from stale observations. |

## References

- [AMD Linux SMU 14 implementation](https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/amd/pm/swsmu/smu14/smu_v14_0_0_ppt.c): driver-owned protocol reference; not a Windows contract.
- [AMD eDP panel control](https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/amd/display/dc/link/protocols/link_edp_panel_control.c): capability and sequencing belong together.
- [DisplayPort DPCD definitions](https://github.com/torvalds/linux/blob/master/include/drm/display/drm_dp.h): PSR capability, enable configuration and sink-state encodings.
- [Windows GPU component power management](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/gpu-power-management-of-idle-and-active-power).
- [PCI configuration access restrictions](https://learn.microsoft.com/en-us/windows-hardware/drivers/pci/accessing-pci-device-configuration-space): Windows owns capability registers; unsupported access can bugcheck on protected systems.
- [Windows NVMe power management](https://learn.microsoft.com/en-us/windows-hardware/design/component-guidelines/power-management-for-storage-hardware-devices-nvme): StorNVMe chooses idle states by timeouts and transition-latency tolerance; APST is not a universal Windows shortcut.
- [Device D-states](https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/device-sleeping-states): D3hot and D3cold are distinct.
- [Windows ASPM policy](https://learn.microsoft.com/en-us/windows-hardware/customize/power-settings/pci-express-settings-link-state-power-management): maximum requests greater link savings with increased exit latency, not a universal L1.2/D3cold override.
- [AMD ADL public API source](https://github.com/GPUOpen-LibrariesAndSDKs/display-library) and [ADLX](https://github.com/GPUOpen-LibrariesAndSDKs/ADLX).
- [AMD panel firmware-update example description](https://github.com/GPUOpen-LibrariesAndSDKs/display-library/blob/master/Public-Documents/html/PanelFirmwareUpdateModeControl-example.html).
- [ACPI FADT definition](https://uefi.org/htmlspecs/ACPI_Spec_6_4_html/05_ACPI_Software_Programming_Model/ACPI_Software_Programming_Model.html): IAPC_BOOT_ARCH bit 4 prohibits OS ASPM control when set. Clearing it is not proof of an `_OSC` grant.

## Running the delivered observer

`PulseIdleProbe --report inventory.txt` reads one inventory on either power source. `PulseIdleProbe --require-battery --sample-seconds 3 --report activity.txt` requires Windows DC before discovery, before each PDH sample and after capture. AC or unknown source returns exit 3. Exit 1 means invalid arguments; exit 2 means output/activity failure; exit 0 means the requested diagnostic ran, **not** that all hardware states are known. Individual unavailable inventory fields remain explicit in the report.

The optional panel reader requires the official external ADL SDK checkout: set `PULSE_ADL_SDK_ROOT` to that checkout before running `build.py`, or pass the equivalent CMake cache path. Headers are not vendored. An empty value builds a diagnostic with the explicit message that panel readback was not built. `PULSE_ADLX_SDK_ROOT` independently selects the older GPU-capability observer. The delivered local build uses both optional SDKs; AMD's notices remain in [NOTICE.md](../NOTICE.md). The panel reader itself always requires battery power, even without `--require-battery`.

CPU options → Platform idle power → Inspect idle health launches the battery-only three-second variant. It runs without blocking the classifier. Closing Pulse cancels its own diagnostic child; a one-shot 45-second wait stops a hung child. Reports are UTF-8 snapshots beside Pulse, excluded from Git by filename. There is no startup GPU scan or periodic background observer. The disabled A/B/C controls disclose the missing actuators. D delegates to the existing guard and preserves its saved preference.

## Validation on 26 October 2026

- MSVC Release with `/W4 /WX`; all 13 native suites passed with and without optional SDKs. The final parser/source/DPCD suite contains 9,007 checks; GUI availability/no-write assertions also pass.
- Delivered optional panel observer returned the four successful DPCD reads above on battery: 140,625 us process CPU / 172 ms wall for the entire inventory plus panel query. This was a single explicit run, not periodic monitoring.
- Live AC refusal returned 3 before collecting inventory; live DC capture returned 0. Three valid GPU/CPU intervals, zero invalid GPU items; FADT checksum accepted and ASPM prohibition clear; ASPM AC=0/DC=2 unchanged.
- Three-second user-session capture: 531,250 us process CPU, 3,593 ms elapsed. Five-second earlier capture: 687,500 us CPU. These exceed the proposed 500,000 us short-capture target; PDH capture stays opt-in, never resident. An inventory-only sandbox run used 46,875 us CPU / 141 ms wall; execution contexts differ, so these are not paired comparisons.
- HWiNFO feed unavailable (error 2). No package-power or energy result claimed, and no such result is required by the updated request.
- Repeated the isolated live PCIe guard exercise with the resident app cleanly stopped: three resets corrected in 140/156/156 ms (20 ms test observation interval); disable, saved-off restart, re-enable and saved-on restart passed. AC stayed 0, original DC=2 restored, and a quiet five-second interval produced no extra checks/writes.
- Installed 1.4.4 locally after backing up the previous executable/preferences, verified both executable hashes, and resumed the user's current Automatic mode. Preferences were byte-identical; the running app reported no fault and the PCIe guard ON. The existing logon startup shortcut remains present. This is not a full reboot test.
- Manual GUI inspection could not run: the Windows automation helper twice returned `GetCursorPos failed: Access is denied (0x80070005)` while attempting to open Pulse. Automated GUI construction assertions passed; visual layout and clicking the installed diagnostic button remain unverified.
- Unverified: private PSR setter semantics/restoration, GFXOFF/DF actuation, actual link residency, reboot/resume persistence of new hardware controls. No such controls have been deployed or described as working.
