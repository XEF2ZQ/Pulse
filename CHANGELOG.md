# Changelog

## 1.4.3.2-cadence-preview — local 1.4.3-line patch, 2026-10-27

- Commit the active power plan only after an actual policy-value write; include pending static configuration writes in that decision. Repeated settled requests perform no writes or plan activation.
- Keep existing event triggers and adaptive monitoring cadence. No new timer, shorter sampling interval or speculative fabric operation.
- Add diagnostic plan-commit counts and explicit live regression checks for repeated requests and static-only configuration. All 13 native suites, live restoration/source-isolation checks and parent-failure recovery passed.
- Leave the shorter Windows idle-policy interval OFF by default; no physical C-state or battery improvement is claimed.

## 1.4.3.1-idle-policy-preview — local 1.4.3-line patch, 2026-10-27

- Add an opt-in battery Efficiency-only Windows idle-policy interval editor (100–50,000 us); preserve the captured interval for Burst/Compute and exclude AC/weak chargers.
- Reuse journaled ownership with v5 recovery, readback and external-change preservation; no extra resident timer or hardware access.
- Validate 13 native suites, actual apply/readback/restoration, source exclusions and recovery after simulated parent failure. Four paired policy-call timing runs do not prove faster hardware sleep entry; keep the option OFF by default.
- Keep 1.4.4 platform/panel work separate and explicitly experimental. No fabric-clock or direct C-state force operation is added.

See [the idle-policy decision and results](docs/battery-idle-interval.md).

## 1.4.3-weak-charger-preview — local patch, 2026-10-23

- Recognize the tested ASUS USB-C charger even when Windows reports AC offline, using a read-only charger query and filtered connection events; use the Windows charging flag as a generic fallback.
- Temporarily map AC profiles into Windows' active DC policy slot with journaled restoration; preserve the existing plugged-in adaptive-control choice and leave actual AC values unchanged.
- Add a default-on Efficiency options selection, source diagnostics and explicit weak-charger UI status.
- Validate real unplug/reconnect notifications, both AC-profile mapping paths and restoration, plus twelve native suites. No charger-wattage, cross-device coverage or energy-saving claim.

## 1.4.2-pcie-guard-preview — local patch, 2026-10-19

- Default-on battery PCIe ASPM guard, selectable in Efficiency options; persistent preference, no AC writes.
- Observe active-plan and PCIe setting changes with bounded thread-pool notifications; correct drift, verify readback, avoid redundant writes and back off on failure.
- Keep permanent DC preference across CPU Pause/exit; resume the saved guard selection at normal Pulse startup.
- Validate eleven native suites and live correction/disable/restart behavior. Physical charger transitions and reboot remain untested; no measured energy gain is claimed.

## 1.4.0-controls-preview — local patch, 2026-09-18

- Add three native pages and editable AC/DC core-parking, minimum-processor-state and heterogeneous/thread scheduling policies matching the supplied ParkControl controls.
- Persist explicit baseline Apply with readback and rollback; record exact configurable targets in recovery journal v3 and retain v1/v2 recovery compatibility.
- Add default-OFF adaptive placement for conservatively eligible, below-normal-priority background threads, using measured CPU time and reversible Windows CPU Sets.
- Reuse the existing timer, bound discovery/admission and journal ownership before writes; release on demand/foreground events and preserve different external selections during recovery.
- Add CPU identity and undervolt capability UI. Separate P/E values remain drafts: Apply and periodic reapplication are disabled pending a verified backend, readback and ownership.
- Validate nine native suites, real power-state restoration, own-thread CPU-set recovery and isolated background-demand release. No battery gain or working undervolt actuator is claimed. Existing efficiency controls and G Helper hardware settings are unchanged by this patch.

## 1.3.0-efficiency-preview — 2026-09-17

- Add the independently checkable **Efficiency options** dropdown for Windows Update service control and Defender real-time preference control; both OFF by default with remembered selections.
- Isolate administrative operations in a native helper; keep the CPU controller unelevated and unchanged.
- Use SCM and Defender event notifications with coalescing, readback and automatic retry backoff; no healthy steady-state polling timer.
- Add one-time installation of a protected, on-demand Task Scheduler broker for normal operation without recurring UAC prompts; validate transport and saved-selection resume locally.
- Preserve per-option baselines in a protected recovery journal; restore on deselect or controller exit and surface recovery failures.
- Respect Tamper Protection and management policy; do not claim unbypassable suppression or battery-runtime gains.
- Add failure-injection tests, a read-only provider diagnostic and explicit Update-only integration exercises. Document live validation and remaining Defender setter/long-duration coverage gaps.

## 1.2 publication and PulseTailProbe 0.2 — 2026-09-10

- Publish the 1.2 productive-demand controller with its existing local validation scope.
- Add independent native battery/HWiNFO acquisition and offline component-recovery comparison; keep both outside the resident controller.
- Reject absent required sensors at acquisition startup and stale/gapped component evidence during analysis.
- Publish two sanitized battery-rate traces, reproducible summaries, figures and their limitations.
- Add engineering case study, failure-handling matrix, evidence index and decisions on observability and deferred hardware limits.
- Extend Windows CI with fifteen offline analysis tests and the standalone recorder artifact; hosted execution remains subject to the account restriction documented in build/validation.
- Retain All rights reserved terms and explicit AI-assisted provenance. No new SMU, scheduler or NPU actuator and no battery-runtime gain are claimed.

## 1.2.0-demand-preview — 2026-09-08

- Request Best performance + Efficient Aggressive for measured productive work, with priority over momentary UI bursts.
- Add unknown process-tree demand alongside faster known-tool evidence, cached identities/handles, bounded process-exit waits and adaptive hot-target reads.
- Add foreground edit-intent probes that require fresh CPU demand; avoid global high-rate mouse monitoring.
- Add lightweight native identity discovery with defensive parsing and Toolhelp fallback.
- Preserve battery-first ownership, source restoration, media exclusions and scheduler OFF default.
- Add demand/parser tests and a read-only probe; record MSVC, unlisted Python compilation and isolated Darktable slider observations.
- No battery-runtime improvement, universal classification or instantaneous hardware frequency transition is claimed.


## 1.1.2-scheduler-preview — 2026-09-07

- Set the optional Scheduler guard to OFF by default, including absent-configuration behavior and the GUI label.
- Retain the ON/OFF persisted toggle, read-only topology diagnostics and conservative cost-policy groundwork.
- No per-thread placement, QoS or priority actuator is enabled.
- Preserve the 1.1 power-controller policy and G Helper ownership boundary.

## 1.1.1-scheduler-preview — 2026-09-07

- Introduced topology parsing/discovery, optional scheduler diagnostics, cost predicates and native GUI toggle validation.
- Initial preview defaulted the guard ON; 1.1.2 changes that default to OFF.

## 1.1 — 2026-09-06

- Added sampled sustained-compute detection and Balanced/boost `4` Compute state.
- Added DC-first control, persisted AC opt-in and source-specific restoration.
- Exercised real MSVC compilation, PassMark CPU workloads and physical charger transitions locally.

## 1.0 — 2026-09-05

- Initial native Win32 tray application, event-driven responsiveness bursts, efficiency baseline and recovery companion.
- Local power-state, media/voice and short sleep/resume checks.

## Public source preparation — 2026-09-08

- Added architecture, design decisions, usage/recovery, validation, future measurement protocol and rights/provenance documentation.
- Added standalone build configuration and Windows CI; separated hardware-independent scheduler checks from live topology validation.
- Preserved the installed application's runtime source. This publication is a source snapshot, not a fabricated reconstruction of development Git history.
