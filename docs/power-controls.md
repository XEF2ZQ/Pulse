# Power controls and adaptive placement — 1.4 preview

This patch adds three native pages: Overview, Power & scheduling, and CPU undervolt. The second page implements the AC/DC controls shown in the supplied ParkControl reference. It is not a reimplementation of every ParkControl or Process Lasso feature. The third page is a capability inspector and draft editor; it has **no undervolt actuator**.

## Independent AC and battery profiles

| Control | Windows policy | Default AC / DC |
|---|---|---|
| Allow core parking + percentage | Minimum active logical processors, including the class-1 setting when already present | 15% / 10% |
| Allow frequency scaling + percentage | Minimum processor state, including class-1 when present | 80% / 15% |
| Parking policy | Heterogeneous policy, indices 0–4; 0 is standard parking | 0 / 0 |
| Short threads | Short-thread scheduling policy, indices 0–5 | Automatic / Automatic |
| Long threads | Scheduling policy, indices 0–5 | Automatic / Automatic |

Turning either percentage control OFF requests 100% for that minimum. These are Windows policy values, not guaranteed physical core counts or clock percentages. Firmware, autonomous processor management and topology still matter. The nonzero heterogeneous-policy labels retain their numeric Windows indices because descriptive meanings should not be invented across processor platforms. Thread-policy indices are All, Performance, Prefer performance, Efficient, Prefer efficient, Automatic.

**Apply AC + battery** deliberately persists both source profiles in the current plan and existing applicable overlays, including while automatic AC boost control is bypassed. It does not turn on AC boost control. **Load Pulse defaults** only edits the form until Apply is pressed. Overview reports the configured profile, not live frequency or proof that an AC profile has been applied.

The apply transaction first releases temporary control, stores the requested preferences, journals original values, writes the static settings, activates the plan and reads every changed static value back. Failure invokes restoration and reports unresolved recovery. Success removes the temporary journal and makes these values the new baseline. Later Pause/exit restores temporary boost changes to that baseline; it does not undo an explicit baseline Apply. Avoid simultaneous baseline editing in another utility.

Profiles are stored in `[PowerAC]` and `[PowerDC]` sections of `Pulse.ini`: `Parking`, `Scaling`, `Cores`, `Minimum`, `Hetero`, `ShortThreads`, `LongThreads`. Saving preserves unrelated preferences and integration paths. Invalid source profiles fall back to defaults. Journal v3 records the exact applied AC/DC targets, so recovery does not assume hardcoded percentages. Existing v1/v2 journals remain readable with their original dynamic-state semantics.

## Optional adaptive scheduler

The old diagnostic Scheduler guard button becomes **Adaptive scheduler**. `Preferences/PlacementEnabled` defaults to zero and is independent of the old diagnostic `SchedulerEnabled` preference. ON enables an experimental, reversible thread-placement policy only while Pulse's normal source/session eligibility permits control. OFF releases Pulse-owned selections. There is no placement discovery or sampling while it is OFF or the controller is bypassed.

This is a conservative heuristic, not a proof that a thread saves energy on an efficient core. A candidate must be in the current user's session, outside the Windows directory, have no visible unminimized top-level window, not be foreground, and have a below-normal relative thread priority. Process priority must be normal or lower at admission. Known compute, media and voice processes and their identified descendants are excluded. Applications with an existing process CPU-set default, thread selection or restricted group affinity are excluded at admission; the constraints are rechecked before assignment. A normal-priority background worker is deliberately left alone.

| Mechanism | Condition | Action / timing |
|---|---|---|
| Candidate discovery | Enabled, eligible, supported heterogeneous topology | At most once per 5 s, using the existing native process-identity reader and per-candidate thread enumeration |
| CPU evidence | Candidate thread CPU time, normalized to one logical processor | At most once per 1 s; uses the existing controller timer |
| Efficient placement | Continuous observed 1–20% CPU evidence for 5 s | Select the available lowest-efficiency-class CPU sets |
| Demand release | Thread reaches at least 50% of one logical processor | Clear selection on the next sample |
| Foreground or Pulse Burst/Compute | Existing event or classifier transition | Release without waiting for the placement sample gate |
| Missing/stale evidence, priority/visibility exclusion | Invalid sample or eligibility loss | Release; visibility cache refresh is bounded by the 5 s scan interval |
| Re-entry after sampled demand release | 15 s cooldown plus fresh light-load evidence | Prevent immediate reassignment |
| Global bounds | At most 128 tracked threads; 4,096 enumerated per scan; 8 new placements/minute | Stop admission at the bound |

Discovery plus evidence collection means first placement is not immediate. These bounds are deliberate: moving a short-lived thread after it has already finished creates overhead without benefit. Foreground/global performance release clears tracking; fresh admission still needs a new observation interval. A CPU-time threshold measures occupancy, not IPC, queueing delay, energy or progress.

The actuator is `SetThreadSelectedCpuSets`, not hard affinity, priority mutation or EcoQoS. Windows documents CPU Sets as soft affinity: OS reservations and hard affinity can take precedence. Release returns the thread to its original unrestricted selection, allowing Windows to choose performance cores; Pulse does not promise P-only placement. Only single-group heterogeneous topologies with usable CPU-set identities are supported in this preview. The tested HX370 exposes 8 higher-class and 16 lower-class logical processors; those labels are OS scheduling classes, not Intel marketing categories.

`NtGetNextThread` is resolved at runtime for read-only enumeration of candidate processes. This native API is not a stable documented Win32 contract. If unavailable, placement is unsupported; no expensive global thread-snapshot fallback runs. Calls use access checks, bounded iteration and closed handles, without a driver, debug privilege or embedded syscall numbers. This replaced a global thread snapshot after its overhead was measured. The setter and selected-set readback remain documented Win32 APIs.

Before selecting CPU sets, Pulse writes a bounded, checksummed, atomic journal containing thread/process identities, creation time and target sets. Only initially unrestricted threads are admitted. Restore checks identity and current selection: it clears a still-owned selection and preserves a different external assignment. The existing waiting guardian recovers after controller death. Queries/writes that fail stop new placements and retain recovery evidence; the error remains visible until restart. This protects against ordinary faults, not malicious same-user journal modification or all races with another scheduler.

## Why the undervolt controls cannot apply yet

The follow-up [firmware investigation](undervolt-investigation.md) records the approved twelve-core probe, actual SMU response, conflicting upstream assumptions, and the remaining requirements. Administrator access succeeded; verified offset readback did not.

CPU identification reads CPUID vendor, brand, family, model and stepping. The capability check also reports whether a PawnIO service exists. Neither proves that firmware is unlocked, that separate P/E domains are writable, or that actual hardware offsets can be read back. This build reports the lock as **unverified**, not incorrectly locked or unlocked based on a model-name list.

The two fields/sliders are drafts. AMD labels use **Curve Optimizer steps**, not millivolts; Intel labels use mV. The draft range is not a recommended or validated hardware range. Apply and periodic reapplication remain disabled on every CPU in this build. No undervolting driver, mailbox/MSR writer or background verification timer is installed. G Helper keeps its existing Curve Optimizer configuration.

A future backend must demonstrate CPU/firmware capability, verified logical-to-physical core mapping, independent domain control, hardware readback rather than a last-written software cache, exclusive ownership with G Helper, and rollback/stability behavior. Silently issuing guessed writes or endlessly reapplying against a competing owner would not satisfy those requirements. The attached desktop AM5 tuning notes do not establish mobile HX370 support; EcoQoS in the scheduling notes is a power/QoS hint, not an E-core pinning API.

## Validation and limits

Local Windows 11 / HX370 validation, September 18, 2026:

- Nine native CTest suites passed, including control ranges, profile mappings, placement thresholds/cooldown, capability gating, and real GUI button/navigation validation with no power writes.
- A power integration exercise used nondefault AC 23%/37% and DC 19%/21% core/minimum targets, exercised Efficiency/Burst/Compute, and restored the complete before/after power report exactly. Installed preferences were unchanged.
- After installation, the real Apply button saved and read back AC 15%/80% and DC 10%/15%, standard parking and automatic short/long-thread policies. No recovery journal remained. Integration paths, AC-control preference and existing efficiency-option selections were preserved. The baseline Apply is a deliberate persistent change; it is distinct from the earlier restore-only exercise.
- A diagnostic changed only its own thread's CPU sets. Set/readback/restore passed; journal recovery passed; a different external assignment was preserved.
- An isolated low-priority child fixture qualified for efficient CPU sets and was released when it became busy. The native-enumeration run took 18,156 ms, with 4 scans, 18 thread reads and 2 placement writes. `GetProcessTimes` reported zero additional controller CPU ticks over that interval. This is **below the measurement resolution**, not zero cost. Earlier global-snapshot runs consumed 109–141 ms over approximately 18 s, motivating its removal.
- A repeat using 12 ms microbursts did not qualify: Windows reported multiple zero-CPU samples, resetting the required continuous evidence. The final fixture uses 120 ms work / 880 ms sleep and records each sample; placement occurred at 6,047 ms and release at 11,094 ms, on the first high-demand sample. This exposes an accounting-resolution limit rather than treating every background wakeup as worthwhile migration.
- All three pages were visually inspected, including readable checkbox text and disabled unsupported undervolt actions. Drafts survive page changes and DPI recreation.

The isolated fixture proves admission/release behavior, not better battery life or universal workload classification. There is no paired energy-per-work result, broad application latency distribution, cross-vendor certification or completed undervolt backend. Leave adaptive placement OFF unless intentionally evaluating it. The existing productive-work booster remains independent of this conservative background placement policy.

## Primary references

- [Microsoft CPU Sets](https://learn.microsoft.com/en-us/windows/win32/procthread/cpu-sets), [SetThreadSelectedCpuSets](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-setthreadselectedcpusets), [GetThreadSelectedCpuSets](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getthreadselectedcpusets).
- [Microsoft process power throttling / EcoQoS](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-setprocessinformation) and [scheduling-policy values](https://learn.microsoft.com/en-us/windows-hardware/customize/power-settings/configuration-for-hetero-power-scheduling-schedulingpolicy).
- [Native thread enumeration reference](https://ntdoc.m417z.com/ntgetnextthread), reverse-engineered native API documentation, not a Microsoft support contract.
- [Intel Undervolt Protection](https://www.intel.com/content/www/us/en/support/articles/000094219/processors.html), [AMD HX370 product capabilities](https://www.amd.com/en/products/processors/laptop/ryzen/ai-300-series/amd-ryzen-ai-9-hx-370.html), [AMD Curve Optimizer guide](https://docs.amd.com/r/en-US/68886-ryzen-master-user-guide/Curve-Optimizer).
