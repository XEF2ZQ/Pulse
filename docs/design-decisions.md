# Design decisions

## 001 — Native Windows policy, bounded authority

**Decision:** use Windows power-mode and power-setting APIs, verify writes, and leave hardware tuning to the existing OEM/G Helper stack.

**Why:** a driver or direct SMU/MSR writer would add privileged compatibility and recovery responsibilities. A background companion should have a narrow ownership boundary. Pulse currently changes boost, overlays and a small baseline setting set. It does not explicitly write EPP, undervolting offsets or package watt limits.

**Consequence:** a policy request cannot guarantee an exact frequency, power draw or transition deadline. Microsoft's [boost-mode table](https://learn.microsoft.com/en-us/windows-hardware/customize/power-settings/options-for-perf-state-engine-perfboostmode) maps index 4 to the behavior of index 2 on its listed paths. The label Efficient Aggressive is not evidence of additional savings.

## 002 — Events for intent, sampling for sustained work

**Decision:** retain foreground and browser-navigation signals for bounded bursts, with a separate sampled CPU-evidence policy for productive work.

**Why:** foreground changes arrive cheaply but do not cover background compilation. CPU load alone also cannot distinguish a build from real-time media. Known executable candidates plus actual CPU consumption avoid boosting an idle benchmark menu; foreground exclusions protect common media/voice hosts.

**Consequence:** the implementation is neither non-polling nor universal. The 1.2 patch adds per-tree CPU evidence, coalesced process-exit waits and foreground edit probes. Discovery still introduces onset latency; quiet evidence is necessary for long-lived hosts. The [patch decision record](productive-demand-patch.md) documents the selected native discovery method, rejected sensor paths and measured limits.

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

## 007 — Separate diagnosis from resident control

**Decision:** keep battery/shared-sensor acquisition in a standalone opt-in executable and analyze captures offline.

**Why:** the battery recovery question needs more observability than the runtime classifier, but continuous sensor acquisition could change idle behavior and add firmware/driver overhead. Existing HWiNFO shared readings avoid a second hardware scanner.

**Consequence:** no constant recorder overhead is added to Pulse. Data remains explicitly missing when the producer is unavailable. Valid units and successful parsing do not establish sensor meaning or freshness. [Acquisition boundaries](../tools/TAIL_PROBE.md).

## 008 — Do not infer physical power from a delayed reported rate

**Decision:** report the measured battery curve as received; do not deconvolve or relabel it as instantaneous power.

**Why:** two traces showed prompt policy release and a much slower reported-rate decay, but lacked component watts. Similar exponential fits support a hypothesis without identifying the gauge, SMU or device responsible.

**Consequence:** the investigation remains open at the component level. Sanitized numerical evidence and reproducible analysis are published so this uncertainty can be independently assessed. [Evidence index](evidence/README.md).

## 009 — Dynamic hardware ceilings need an owner and an observed benefit

**Decision:** defer automatic 12/35 W SMU writes; implement component-evidence validation first.

**Why:** a 12 W ceiling does not directly constrain an already 5 W component. The reviewed G Helper source does not expose the external lease needed to coordinate temporary limits. A second independent writer risks competing settings; acknowledgement alone also does not prove enforcement.

**Consequence:** no hardware-limit button is presented as an active feature. A future actuator must serialize state changes, verify capabilities and effect, recover partial writes, and restore/relinquish ownership on faults and source transitions. [Detailed evaluation](smu-cap-evaluation.md).
