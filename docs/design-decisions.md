# Design decisions

## 001 — Native Windows policy, bounded authority

**Decision:** use Windows power-mode and power-setting APIs, verify writes, and leave hardware tuning to the existing OEM/G Helper stack.

**Why:** a driver or direct SMU/MSR writer would add privileged compatibility and recovery responsibilities. A background companion should have a narrow ownership boundary. Pulse currently changes boost, overlays and a small baseline setting set. It does not explicitly write EPP, undervolting offsets or package watt limits.

**Consequence:** a policy request cannot guarantee an exact frequency, power draw or transition deadline. Microsoft's [boost-mode table](https://learn.microsoft.com/en-us/windows-hardware/customize/power-settings/options-for-perf-state-engine-perfboostmode) maps index 4 to the behavior of index 2 on its listed paths. The label Efficient Aggressive is not evidence of additional savings.

## 002 — Events for intent, sampling for sustained work

**Decision:** retain foreground and browser-navigation signals for bounded bursts, with a separate sampled CPU-evidence policy for productive work.

**Why:** foreground changes arrive cheaply but do not cover background compilation. CPU load alone also cannot distinguish a build from real-time media. Known executable candidates plus actual CPU consumption avoid boosting an idle benchmark menu; foreground exclusions protect common media/voice hosts.

**Consequence:** the implementation is neither non-polling nor universal. Discovery and hysteresis introduce seconds of latency. Process-exit waits can improve known-job completion, but a process exit does not mean all child work has completed, and long-lived hosts can finish a job without exiting. Future event-assisted detection must account for both cases.

## 003 — Battery-first ownership

**Decision:** default to DC control; an explicit persisted button enables AC control.

**Why:** the intended product is a battery companion. Hidden bypass removes the ordinary sampling timer and releases owned settings. Windows notifications can wake the controller when eligibility changes.

**Consequence:** G Helper's existing `skip_powermode` handoff is global. Pulse releasing AC ownership does not automatically re-enable G Helper's Windows-mode automation. This is documented instead of hiding repeated configuration edits/restarts behind charger events.

## 004 — Scheduling requires measured benefit

**Decision:** the optional Scheduler guard remains OFF by default and diagnostic only.

**Why:** topology describes processor relationships, not the energy cost of moving a particular workload. Class numbers are platform data, not portable P/E labels. Cache, SMT, memory locality and deadlines can invalidate a simplistic preference for efficient cores.

**Current implementation:** defensive parsing, OS topology cross-checks, workload categories and a pure cost predicate. The predicate requires comparable repeated measurements, a matching topology fingerprint, minimum residency, a change budget, a deadline and a conservative benefit margin.

**Consequence:** there is no measured live cost input, calibration loop or placement actuator. Turning the guard ON enables topology/event diagnostics; it cannot improve P/E placement by itself. Any future actuator needs its own tests, rollback and energy-per-completed-work evidence.

## 005 — NPU inference is deferred

Microsoft documents the Surface Laptop Studio 2's Movidius VPU and Studio Effects capabilities. This is not evidence that it controls Windows boost policy. The available research did not establish the hypothesized Surface VPU governor. Pulse does not claim to reproduce a vendor's proprietary algorithm.

**Decision:** keep the small native policy on the CPU. An NPU model would add inference-runtime, wakeup, data-collection and training costs before demonstrating that classification benefits outweigh those costs. No XDNA/NPU model is shipped. See [research sources](../RESEARCH.md).

## 006 — Evidence before performance marketing

**Decision:** separate policy tests, observed machine behavior, controller overhead, user impressions and future battery-runtime claims.

**Consequence:** assertion counts are not scenario counts; short CPU-time samples are not energy measurements; successful Windows readback is not a measured hardware clock change. The next milestone is a repeatable mixed-workload experiment, with completed work and responsiveness recorded alongside discharge.
