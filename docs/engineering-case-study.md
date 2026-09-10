# Engineering case study

## Problem and constraints

Pulse explores whether a small Windows companion can preserve interactive responsiveness while spending more time in an efficient CPU policy. The target machine already uses G Helper for platform tuning, so replacing the OEM control stack would duplicate responsibility. The controller must also cost little enough to avoid defeating its own purpose.

Three workload classes drive the design: short UI requests, sustained productive CPU work, and steady media/voice activity. A focus hook helps with the first class but misses background compilation. CPU usage helps with the second class but cannot reveal semantic intent. The design combines incomplete signals rather than presenting one metric as a universal classifier.

The intended outcome is better useful work per battery charge without perceptible responsiveness or media-quality loss. That outcome remains to be established by paired end-to-end experiments. Existing measurements establish narrower functional properties.

## Decisions that carry the design

### Events for prompt intent; bounded sampling for work

Window/navigation events admit short bursts cheaply. A cached process-tree monitor supplies CPU-time evidence for known and unfamiliar producers. Exit waits accelerate short-lived process completion, but a persistent editor or benchmark needs quiet evidence because process lifetime and work lifetime differ.

This avoids continuous scheduler tracing and driver-level counters in the resident path. The cost is discovery latency and incomplete classification. Exact timer targets and exclusions are documented rather than an “instant” or “infinite-set” guarantee. [Architecture](architecture.md), [productive-demand contract](productive-demand-patch.md).

### One state decision and recoverable ownership

Eligibility gates source, Pause, display/session state and Energy Saver. Measured productive demand outranks momentary UI bursts. The resulting state requests Windows mode/boost settings, with changed-value writes, readback and restoration metadata.

A separate waiting companion can restore owned settings when the controller terminates. A durable journal supports best-effort next-launch recovery. These mechanisms reduce the consequences of failure, but do not create an atomic transaction across Windows, firmware and other applications. [Failure handling](failure-handling.md).

### Demand after editing intent

Moving a slider does not always require extra CPU performance. The editor path opens a bounded probe and requires fresh CPU evidence before entering Compute. This preserves responsiveness opportunities without turning all mouse movement into boost requests. Darktable supplied functional evidence; GPU-only editing and other control implementations remain separate validation cases.

### Observability as a separate subsystem

The resident controller has no hardware sensor scanner. PulseTailProbe runs only when requested, records its observer cost and reads existing HWiNFO shared data if available. The offline analyzer checks freshness, identity and coverage before interpreting component recovery.

Adding sensor wakeups to diagnose idle consumption can change the behavior under investigation. Separating acquisition from runtime control also exposes measurement limitations instead of silently substituting a stale sample or zero for absent telemetry.

## The discharge-tail investigation

Two 100-second experiments reproduced a slow battery-reported decay after eight seconds of CPU work. CPU activity fell promptly and boost readback became Disabled in approximately one second. Reported rate took approximately 16 and 28.5 seconds to remain below 8 W. Similar descriptive decay constants and faster voltage recovery made reporting/filtering plausible.

The data did not include CPU-package or GPU watts. It could not establish that real platform consumption had already settled, or that firmware filtering was the sole cause. The decision was to add component comparison and defer an automatic SMU change—not manufacture a corrected battery value. [Trace evidence](evidence/README.md).

A proposed 12 W efficiency / 35 W performance limit is a potential experiment, not a demonstrated discharge fix. A ceiling cannot directly constrain a component already below it; the linked enforcement report did not measure workload-end response. G Helper must also participate in hardware ownership before Pulse can add coordinated writes. [Evaluation and prerequisites](smu-cap-evaluation.md).

## Evidence and limits

Local tests cover policy transitions, malformed native/shared-memory records, topology, GUI preference behavior and offline evidence validity. Live observations cover compilation, PassMark, isolated Darktable editing and battery-rate recovery on one machine. Each result retains its date, version and scope in the [evidence index](evidence/README.md).

The record does not establish battery-runtime improvement, broad OEM compatibility, universal productive-work recognition, optimal heterogeneous-core scheduling or a vendor-equivalent NPU governor. Assertion counts are not independent workload counts. Controller CPU time is not whole-laptop energy.

## Next experiments and acceptance

1. Obtain fresh component power alongside the CPU-work/battery trace, then isolate any actual remaining device activity.
2. Measure repeated onset/release latency across unfamiliar compilers and persistent creator hosts, including concurrent playback and failed/cancelled jobs.
3. Execute counterbalanced Automatic/Paused runs with equal completed work, matched thermal/source conditions, responsiveness and media quality recorded.
4. Add hardware or scheduling control only after a measured limitation and reversible ownership protocol justify it.

The [measurement protocol](benchmark-plan.md) defines run-order controls, reporting and acceptance gates. This repository exposes the decisions for review, including cases where a plausible optimization was not supported by the evidence.
