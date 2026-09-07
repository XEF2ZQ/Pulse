# Architecture

## Responsibility and scope

Pulse is a per-user Win32 process with a tray UI and a second instance of its executable acting as a waiting recovery companion. It requests Windows policy changes; it does not implement a CPU scheduler, frequency governor or hardware power-limit driver. The current source is the installed 1.1.2 scheduler-preview application's source, with publication build/test infrastructure added.

```mermaid
flowchart TD
    Input[Foreground and navigation events] --> Controller[Win32 controller / main.cpp]
    Source[Power, display and session notifications] --> Controller
    Samples[Adaptive CPU-time sampling / process_monitor.h] --> Controller
    Controller --> Burst[Pure burst policy / policy.h]
    Controller --> Compute[Pure compute policy / workload.h]
    Burst --> Arbitration[Eligibility and state arbitration]
    Compute --> Arbitration
    Arbitration --> Power[Power ownership and native APIs / power.cpp]
    Power --> Windows[Windows power policy]
    Power --> Journal[Source-specific restore journal]
    Guardian[Waiting recovery companion] --> Journal
    Guardian --> Power
    Controller --> Guard[Optional scheduler diagnostics]
    Guard --> Topology[Read-only Windows topology]
```

## State arbitration

Eligibility requires an armed controller and either battery power or the persisted AC opt-in. Display-off, session-lock, suspend and Windows Energy Saver conditions suppress performance requests. Pause releases ownership. A foreground burst has priority over sustained Compute; otherwise the controller chooses Compute while its evidence remains active, and Efficiency when it settles. Keep efficient suppresses both automatic performance paths.

| Policy | Inputs | Entry | Exit / containment |
|---|---|---|---|
| Burst | Foreground/navigation intent; recent input; CPU rise | Accepted event or load edge | Quiet samples, deadline, blocked state or exhausted budget |
| Compute, known tools | Summed cached-process CPU time | At least 65% of one logical processor for 1.5 s of observed evidence | Below known 20% and foreground 35% thresholds for 1.2 s of observed quiet |
| Compute, unknown foreground | Foreground process CPU time, host exclusions | At least 90% of one logical processor for 3.5 s of observed evidence | Same quiet rule |

Evidence durations are evaluated at sample times. Discovery and timer cadence add latency; these thresholds are not end-to-end timing guarantees. Executable-name matching identifies candidates, not proof that a compiler is currently compiling. Known tools still need CPU evidence. Browser/media/voice host exclusions apply to the foreground fallback; they do not prevent a separately recognized compiler from contributing known-tool CPU.

The current monitor aggregates recognized processes, not arbitrary process subtrees. It does not measure retired instructions or classify all unknown background workloads. Very short jobs, mixed-purpose hosts, renamed tools and inaccessible processes can be missed.

## Timing and overhead budget

| Condition | Controller timer |
|---|---|
| Active burst | 16–125 ms, bounded by remaining deadline |
| Compute candidate / active compute / tracked tools | 1 s |
| Ordinary recent interaction | 1 s |
| No recent input for more than 15 s | 3 s |
| Blocked active controller | 10 s |
| Bypassed and hidden in tray | No periodic controller timer |
| Bypassed with visible UI | 1 s |

Non-burst timers allow 100 ms coalescing tolerance. Discovery runs no more frequently than every 3 s without tracked tools, or every 1 s with tracked tools. Process CPU collection is restricted to roughly once per second even during a burst. Active control verifies owned settings and guardian health at a 10 s audit interval. Scheduler guard adds no periodic timer.

Bursts have a 3,600 ms budget, consumed during boosting and replenished at 0.20 ms per elapsed non-burst millisecond. A request needs at least 450 ms available; ordinary successive bursts have an 850 ms cooldown. Deadlines are capped at 1,800 ms on DC and 2,400 ms on AC, subject to budget. Quiet detection can end a burst after 375 ms and two low-load samples. Timer delivery and native API latency remain outside these policy bounds.

## Power ownership transaction

1. Resolve required power-mode APIs at runtime and read the active scheme and source modes.
2. Capture supported, explicitly present managed settings for the owned source into a versioned, checksummed journal. Write and flush the temporary journal before replacing the durable journal path.
3. Start the recovery companion and configure the selected source. Write only changed values, commit active-plan changes and read settings back.
4. Apply state changes through boost indices and power-mode APIs. Do not launch `powercfg` for each transition.
5. On pause, source handoff or exit, compare current values with Pulse's recognized values before restoring the snapshot. Preserve distinguishable external choices. Retain the journal on restoration failure.

The compare-and-restore check cannot distinguish another writer choosing exactly the same numeric value as Pulse. This is conflict mitigation, not a multi-writer transaction protocol. The checksum detects accidental corruption; it is not cryptographic authentication. The guardian covers controller termination, not simultaneous system/storage/API failure. Next-launch recovery is best effort after an OS crash.

## Ownership boundaries

| Control | Owner while Pulse is active |
|---|---|
| Windows source power mode and CPU boost | Pulse |
| Minimum active logical cores / minimum processor state | Pulse baseline configuration |
| Standard parking and automatic long/short-thread policy | Pulse baseline configuration |
| Explicit EPP register/value tuning | No Pulse actuator; Windows/OEM policy remains authoritative |
| Fan curves, GPU mode, TDP, Curve Optimizer | G Helper / OEM |
| Per-thread affinity, ideal processor, CPU sets, priorities, QoS | Windows / applications; scheduler guard does not write these |

The baseline scheduling-policy values are distinct from the optional Scheduler guard switch. Turning that switch off does not remove the existing baseline power configuration.

## Source map

- `src/main.cpp`: window procedure, event registration, eligibility, arbitration, tray UI, diagnostics and guardian entry points.
- `src/policy.h`: deterministic responsiveness-burst policy.
- `src/workload.h`: sustained-compute policy and executable categories.
- `src/process_monitor.h`: cached process CPU-time sampling and bounded discovery.
- `src/power.cpp`, `src/power.h`: snapshot, recovery journal, native writes, readback and restore.
- `src/scheduler_topology.*`: variable-record parsing and read-only topology discovery.
- `src/scheduler_policy.h`: optional diagnostics, conservative cost predicate, preference persistence; no placement actuator.

## Privacy

The controller has no network or telemetry client. It observes process identity, CPU time, foreground transitions and selected navigation key signals. Its decision records do not include typed text, window titles, URLs or audio. Diagnostics may contain machine configuration and local context; users should review reports before sharing them. No personal runtime reports are included in this repository.
