# Planned mixed-workload battery experiment

**Status: protocol only. No runtime results are available.** The project owner plans to build an automated everyday-plus-development workload harness. This document defines what it should measure; no such end-to-end harness is claimed to ship in this release.

## Question and comparisons

Does Pulse improve useful battery runtime while maintaining acceptable interactive responsiveness and productive throughput on the same laptop?

Primary comparison: **Pulse Automatic** versus **Pulse Paused with original settings restored**. Name and record that restored Windows/OEM baseline explicitly. Keep G Helper integration ownership identical between arms so its global mode handoff does not become a hidden variable. An optional Keep efficient arm can isolate the responsiveness cost of always disabling boost, but is not a substitute for the primary comparison.

## Workload contract

The harness should repeat a versioned sequence of reading/typing/scrolling, controlled page navigation, locally reproducible video playback, idle intervals and a representative project compilation or creator export. Define input data, application versions, build cache state and successful completion criteria. Track each work unit separately so a policy cannot appear efficient merely by completing less work.

Use local media or record network/cache conditions for online content. Capture decoded video resolution and frame drops when measuring playback. For browser tasks, record navigation start and a defined readiness criterion; window focus alone is not page-load completion. Do not record private typed content, account pages or voice audio.

## Controls and run order

- Fix brightness, refresh rate, volume, radios, peripherals, GPU mode, fan profile, undervolt, Windows build and background services across paired runs.
- Start at matched battery charge and thermal conditions. Record battery health/capacity, charge/discharge thresholds and ambient temperature.
- Define cache warmup and cool-down procedures. Keep the observer's sampling overhead equal in both arms.
- Randomize or counterbalance order (AB/BA), and perform at least three complete pairs before interpreting direction. More runs may be necessary if differences are small relative to variation.
- Predefine exclusions such as Windows Update, accidental charger connection or workload failure; retain excluded-run reasons instead of silently dropping inconvenient results.

## Outcomes

| Primary measure | Supporting measurements |
|---|---|
| Elapsed runtime over the same charge interval | Battery energy/capacity readings and sampling resolution |
| Completed work over that interval | Builds/exports completed, failures, task durations |
| Interactive responsiveness | Median and tail task latency, with an agreed acceptable regression bound |
| Real-time quality | Playback dropped frames and audio discontinuities where instrumentable |
| Controller cost | Controller + guardian CPU time, memory and wakeup observations |
| Policy correctness | Accepted state transitions, onset-to-policy and completion-to-policy distributions, restoration checks |

Measure completed-work energy separately when reliable energy readings are available. Report uncertainties and distinguish the battery's coarse capacity estimates from calibrated external measurements. Report API transition timing separately from actual frequency/energy observations.

## Reporting and acceptance

Publish the workload version, source commit, configuration, run order, sanitized raw metrics and analysis code. Report every completed paired run, mean/median paired differences and variation or confidence intervals; do not rely on a single best run. If savings are indistinguishable from noise, state that.

Accept an improvement only if runtime/energy gains coexist with the predeclared responsiveness, media quality and completed-work requirements. Treat instability, restoration errors or throughput loss as findings, even if battery duration increases. No target percentage is assumed in advance.
