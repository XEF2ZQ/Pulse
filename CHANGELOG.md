# Changelog

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
