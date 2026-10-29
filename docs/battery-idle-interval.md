# Battery idle policy and request cadence — Pulse 1.4.3.2

## Decision

Add a reversible Windows idle-policy interval control on the **1.4.3 feature
baseline**. Do not carry the experimental 1.4.4 platform page or private AMD
panel observer into this line. Keep the new control **OFF by default**: its
policy transactions work, but faster physical C-state entry, improved energy
use and fabric downclocks have not been established.

On 27 October the attached HX370 already requested a 1,000 us DC idle-policy
interval, allowed idle states (`IDLEDISABLE=0`) and imposed no Windows idle-state
ceiling (`IDLESTATEMAX=0`). AC interval was 50,000 us. A long discharge-reporting
tail therefore cannot be attributed to a default 50 ms battery idle-check
interval on this machine. This is a policy observation, not an actual residency
measurement.

## Research: what can be influenced

The installed Windows SDK 10.0.26100.0, `winnt.h`, defines
`GUID_PROCESSOR_IDLE_TIME_CHECK` (`c4581c31-89ab-4597-8e2b-9c9cab440e6b`) as the
minimum elapsed time after an idle-state promotion/demotion before another
adjustment is allowed, in microseconds. Microsoft's
[idle configuration interface](https://learn.microsoft.com/en-us/dotnet/api/microsoft.windows.eventtracing.power.iidleconfiguration?view=trace-processor-dotnet-1.0)
also describes a minimum duration between checks. This does not command a
processor to sleep, change hardware wake latency, or create a Pulse timer.

Deeper states need an idle opportunity and must satisfy platform constraints.
Microsoft's [CPU analysis guide](https://learn.microsoft.com/en-us/windows-hardware/test/wpt/cpu-analysis)
distinguishes selected target states from actual states when hardware exposes
them. Actual entry time must be measured separately; reading back the power-plan
index is insufficient. The [platform idle-state contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/pep_x/ns-pep_x-_pep_platform_idle_state)
includes wake latency, break-even duration and processor dependencies. A user
process cannot replace this coordination by setting a policy number.

An aggressive promotion/demotion preset was rejected for this patch:
[IdlePromoteThreshold](https://learn.microsoft.com/en-us/windows-hardware/customize/power-settings/options-for-perf-state-engine-idlepromotethreshold)
and [IdleDemoteThreshold](https://learn.microsoft.com/en-us/windows-hardware/customize/power-settings/options-for-perf-state-engine-idledemotethreshold)
use idleness wording, while the SDK and trace-processing definitions describe
busy thresholds. The existing manual editors remain available, but this patch
does not guess an extreme preset from conflicting descriptions.

## Fabric downclocking: not implemented

The [AMD Linux SMU 14 driver](https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/amd/pm/swsmu/smu14/smu_v14_0_0_ppt.c)
has distinct fabric/SoC clock domains, DPM tables and driver-owned clock
constraints. Its [firmware header](https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/amd/pm/swsmu/inc/pmfw_if/smu_v14_0_0_ppsmc.h)
names fabric clock minimum/maximum requests. These are evidence of a Linux
driver protocol, not authorization to send the same numbers through a different
Windows mailbox. Reducing a minimum removes a constraint; setting a low maximum
can throttle useful memory/display traffic. Neither operation guarantees a
fabric C-state.

[AMD E-SMI](https://github.com/amd/esmi_ib_library) remains a Linux EPYC interface,
not a native Windows HX370 control path. Prior local research is documented in
[the AMD idle API review](amd-idle-api-review.md). No validated Windows fabric
writer, matching firmware contract, readback/restoration or driver arbitration
has been established. This patch sends no SMU messages and writes no MSRs,
PCI configuration fields, undocumented driver settings or firmware tables.

## User controls and state matrix

Open **CPU options → Idle states & thresholds**. Check **Shorter idle-policy
interval on battery (Efficiency only)**, enter 100–50,000 microseconds, and use
**Apply idle options**. The initial field is 1,000 us; 100 us is the more
aggressive value used in functional tests. It is not a recommendation or a
promise of 100 us sleep entry. Smaller values may add OS policy work without
improving residency. Load defaults clears the option; Apply saves that choice.

| Situation | Interval request |
|---|---|
| Option OFF | No ownership or interval writes |
| True battery, Efficiency | Smaller of captured original and requested value |
| Burst or Compute | Captured original interval |
| AC, including Force AC | No interval ownership |
| Detected weak USB-C charger / AC-profile bridge | No interval ownership |
| Pause, disable or normal exit | Restore the captured original if still owned |
| Main process failure | Existing recovery companion restores the journal |
| Inherited/missing DC original | Control unavailable; no override invented |
| New distinguishable external interval | Do not overwrite it during restoration |

Actual AC detection still uses the existing event flow; no physical charger
transition was performed in this test. The source-exclusion transaction was
tested directly. Saved preferences and the existing logon shortcut provide
normal restart persistence; a full reboot is not claimed as tested.

## Implementation and recovery

### Cadence decision in 1.4.3.2

Keep the established 1.4.3 detector cadence; remove unnecessary commits instead
of raising the sampling rate. This is a conservative engineering choice, not a
claim that a globally optimal interval has been measured.

| Mechanism | Existing cadence retained | Policy writes |
|---|---|---|
| Foreground/input/process-exit signals | Event-driven, bounded by existing classifier | Only when the selected state changes |
| Burst timer | Remaining deadline clamped to 16–125 ms | State transition only; 16 ms is the near-deadline floor |
| Editor interaction sampling | 125 ms during a 1,250 ms interaction lease | Classified state transition only |
| Active/pending productive demand | 250 ms | Classified state transition only |
| Ordinary active observation | 1,000 ms | Classified state transition only |
| No input for over 15 seconds, no fast-demand lease | 3,000 ms | Classified state transition only |
| Blocked state | 10,000 ms | Existing blocked-state handling |
| Hidden paused/AC-bypass governor | Main sample timer stopped | Event-driven source/control changes |
| Process discovery / broad CPU reads | 1,000 ms when active/pending; otherwise 3,000 ms | Read-only |
| Full ownership audit while controlling | 10,000 ms | Read-only; conflict pauses control |

The intervals are requested timer periods, not hard real-time guarantees. The
existing non-burst timer allows 100 ms coalescing; burst timers do not. Enabled
adaptive placement caps the main interval at 1,000 ms. The hidden bypass row
describes the governor timer, not a promise of zero callbacks from separately
enabled guards or charger notifications. Existing power-change notifications
also schedule conflict verification. This patch adds no resident activity.

`Power::write` marks a pending commit only after a successful actual value
change. `Power::apply` commits that work once and clears the flag only on
successful activation. This includes static values written by `configure`, even
if boost already has the requested value. Unchanged values avoid the redundant
second read inside `write`; the initial read remains to detect external drift.
Mode setters also retain their equality check. Restoration and explicit profile
Apply retain their deliberate commit path.

This removes the previous unconditional Efficiency-state plan activation.
It does not defer a real performance request, batch it behind a new debounce,
or shorten any classifier interval. Real changed-plan transitions can still
cost roughly 230–250 ms in the local policy APIs; the patch does not make that
cost disappear. The OS idle-policy interval below is a separate parameter,
not Pulse's own sampling period.

### Ownership

`idle_policy.h` contains parsing, ranges and source/state selection. The native
idle editor persists `CpuOptions/EarlyIdle` and `IdleIntervalUs` through the
existing temporary-file replacement. It preserves unrelated preferences.
`power.cpp` remains the only CPU-policy writer. Stable setting index 13 is the
idle interval; it is captured only in the base plan's existing DC slot and is
excluded from overlays, AC and charger bridges. State transitions reuse the
existing commit and verification path. There is no additional background timer,
sensor scan, driver or tracing session. The feature does add bounded API work
to policy transitions when enabled.

Recovery journal v5 retains the existing row layout, accepts v1–v4 originals,
and validates the new time unit separately from percentages. Its idle row must
be DC-only and in the captured base plan. Restore recognizes the interval's
own saved target; boost's 0/2/4 ownership rule is not reused for this setting.
As with existing recovery, equal-value external writes cannot be distinguished.

**Downgrade rule:** use Restore & exit before replacing this binary with the
original 1.4.3. The older binary cannot read a v5 journal. Keep the new recovery
binary available if a journal remains unresolved.

Source provenance: this folder was copied from the local source snapshot,
retaining the earlier 1.4.3 features and excluding the 1.4.4-specific sources,
targets and GUI wiring. `baseline-source-manifest.json` records input hashes;
`../prepare-idle-143.py` records the extraction. This is not a claim that the
old executable is byte-reproducible or that a new GitHub release was published.

## Validation — 27 October 2026

1.4.3.2 cadence regression:

- All 13 native suites passed after the change.
- Four state checks, each followed by three identical requests: all twelve
  repeated requests produced **zero writes and zero plan commits**. Group mean
  policy-call time was 0.65–1.08 ms. This is a small local functional sample,
  not a general throughput, energy or latency benchmark.
- Static configuration on DC changed 14 static values and one dynamic value,
  with one commit. The inactive AC transaction changed four static values and
  no dynamic values, also with exactly one commit. Both restored all captured
  values and removed their journals.
- The parent-exit recovery exercise again restored DC 1,000 us and removed the
  journal; installed preferences remained byte-identical. AC/weak-charger
  exclusion tests passed. No physical plug transition or reboot was performed.

1.4.3.1 interval validation (before the cadence change):

- MSVC Release, C++20, `/W4 /WX`; 13 native suites passed. New pure idle-policy
  suite: 596 checks, including interval bounds, source/state combinations and
  preservation of an already shorter interval. Preference round-trip and
  malformed-input tests passed; GUI construction/draft-validation passed.
- Live transaction: DC 1,000 → 100 in Efficiency → 1,000 for Burst and Compute
  → 100 in Efficiency → 1,000 on restore. AC stayed 50,000. All captured values
  restored and journal removed. Direct AC and weak-charger exclusion checks
  passed.
- Simulated parent exit with code 77 while the 100 us policy was owned: the
  normal recovery companion restored 1,000 us and removed the v5 journal.
  Installed preferences were unchanged.
- Four alternating AB/BA pairs compared the feature off/on, using identical
  policy paths. Arithmetic mean policy-apply times:

| Transition | OFF, ms | ON, ms | Difference, ms |
|---|---:|---:|---:|
| Burst | 244.73 | 247.87 | +3.14 |
| Compute | 240.30 | 241.48 | +1.18 |
| Efficiency | 241.96 | 252.22 | +10.25 |

These are elapsed Windows policy-call times reported by `Power::lastApplyMs`,
not physical sleep-entry, hardware wake, total event-to-response latency, or
energy measurements. Four pairs do not establish significance. The major
transition cost was already present with the feature off; a small extra cost
is plausible. No faster hardware idle result has been established, so the
installed feature remains opt-in.

Local raw results are in `validation-20261027/`. The manually invoked
`--idle-interval-exercise`, `--idle-interval-comparison`, and
`--idle-interval-crash` are excluded from CTest and normal startup; run only
with Pulse cleanly stopped and confirmed battery power. The crash variant
intentionally exits its own test process after arming recovery.
