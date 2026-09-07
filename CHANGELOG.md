# Changelog

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
