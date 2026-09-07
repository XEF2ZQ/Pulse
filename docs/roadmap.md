# Roadmap and acceptance gates

These items are future work, not promises about the current executable.

| Milestone | Proposed work | Evidence needed before claiming completion |
|---|---|---|
| Repeatable everyday + development experiment | Versioned mixed-workload harness, paired Automatic/Paused battery runs | Reproducible inputs, equal completed work, latency/quality measures and full result distribution |
| Faster productive-work lifecycle detection | Process-start/exit assistance and bounded job leases alongside adaptive sampling | Measure onset/exit percentiles, child-process races, persistent hosts, cancellation, access denial and total observer overhead |
| Open-set productive-work recognition | Behavioral evidence for unknown build/render/export/simulation work; preserve specific fast paths | Held-out unseen applications, background process trees, mixed media + build cases and false-positive/false-negative analysis |
| Optional scheduling actuator | Only if calibrated energy-per-work evidence supports a reversible placement/QoS trial | Cache/SMT/NUMA costs, deadline preservation, topology invalidation, restore tests and net energy benefit after monitoring cost |
| Wider compatibility | Additional AMD/Intel machines, OEM configurations, sleep and power-source transitions | Per-device validation, unsupported-capability fallback and recovery results |
| Distribution maturity | Signed release packaging and reviewed installation/upgrade/uninstall flows | Clean-machine tests, provenance, migration/rollback checks and redistribution review |

## Open-set detector direction

Known executable candidates remain a fast path, but the desired general detector should reason about productive CPU work independently of names. Candidate evidence includes attributable CPU-time demand over a process tree, runnable pressure where observable, persistence, foreground intent and application-provided job boundaries. No single signal proves intent; steady video or voice must not be treated as productive compute merely because CPU usage is high.

Keep classification per candidate workload rather than imposing a global media veto. An unrelated video must not suppress a concurrent compiler. Unknown or ambiguous real-time hosts should remain conservative until evidence justifies adaptation. Hardware instruction counters and ETW collection add privileges/overhead/compatibility constraints and are not assumed to be free.

## Completion latency direction

Registered process waits can shorten completion handling for short-lived tools. They must use valid process identity/handles, bounded registration, cancellation-safe teardown and child-work tracking. Long-lived IDEs/render hosts need another completion signal or quiet evidence. End-to-end measurements must include Windows policy application time; a millisecond notification cannot guarantee an instantaneous physical clock reduction.

## Hardware tuning boundary

Potential future G Helper coordination may expose existing Curve Optimizer settings accurately in **steps**, not millivolts, and distinguish requested settings from hardware readback. No UV, watt-limit or NPU actuator is in the current release. Hardware tuning should have a single owner and a separate validation/recovery plan; publishing this roadmap does not authorize or implement new hardware changes.
