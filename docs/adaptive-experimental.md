# Experimental adaptive scheduling — 1.5.0

**Status:** implemented, locally built, acceptance-gated. Battery only. New process
placement, background consolidation and EcoQoS default **off**. Existing Windows
power tiers default on while eligible. No energy saving or exact four-core
execution guarantee is claimed.

The tested platform is one ASUS GA605WI, Ryzen AI 9 HX 370, Windows 11 x64.
Standard 1.4.3.2 remained the installed controller during child-process tests.
The [release lines](../BRANCHES.md) separate this work from everyday deployment.

## Ownership and integration

The existing GUI/message thread remains the coordinator. Foreground/title/input
events supply intent; `ProductiveMonitor` supplies measured process-tree demand
and coalesced process-exit waits. `Power` remains the sole adaptive Windows power
writer, with its existing source-specific recovery journal. Its active-plan
commit is now skipped when no plan value changed; a real pending change still
commits before requesting the overlay. No SMU/PPT/voltage writes were added.
G Helper and firmware retain their externally configured power ceilings; the
reported 15/17/38 W values are not reinterpreted or programmed by this patch.

The new process controller replaces the legacy per-thread placement controller
in the runtime. The legacy implementation remains linked for old-journal recovery
and its existing validation tools. The scheduler guard remains an observer.
PCIe policy, explicit CPU-options controls and the separate efficiency broker
retain their existing ownership and persistence semantics.

| Module | Responsibility |
|---|---|
| `src/main.cpp` | Source gating, shared events, T0/T1/T2 arbitration, startup/guardian/shutdown, GUI integration |
| `src/adaptive_policy.h` | Pure topology selection, role arbitration, elapsed-time compute evidence, battery predicate |
| `src/adaptive_config.h` | Validated `[Adaptive]` preferences and full-path overrides, atomic INI replacement |
| `src/process_placement.cpp` | Bounded process handles, lifecycle waits, CPU Sets/EcoQoS ownership, recovery and diagnostics |
| `src/adaptive_ui.inl` | Modeless experimental settings, independent tiers/placement/background controls, restore action |
| `src/productive_monitor.h` | Existing shared discovery snapshot; clear stale catalog on reset |
| `src/power.cpp`, `src/power.h` | Idempotent plan commits; existing journal and power ownership retained |
| `tools/adaptive_probe.cpp` | Disposable-worker API, actual-execution, timing, A/B and crash-recovery tests |
| `tools/replay_adaptive.py` | Offline threshold/EWMA/CUSUM comparison; no resident filter overhead |
| `tests/adaptive_tests.cpp`, `tests/test_adaptive_replay.py` | Topology, arbitration, source matrix, configuration and deterministic replay tests |

## Battery-only contract

New adaptive tiers and process/background scheduling require a known battery
source and no recognized weak charger. Recognized AC/USB-C external power or
unknown source releases owned settings. Lock, display-off, suspend and Energy
Saver suppress scheduling restrictions and interactive promotion through the
existing blocked-state logic. Battery efficiency policy can remain owned while
blocked. Resume/topology events rediscover before making new assignments.

This experimental branch disables the plugged-in-control button and refuses
`--ac-on`. Legacy `ManageAC`, `RecognizeWeakCharger=0` and `--manage-ac` cannot
override the new battery-only rule. Weak-charger recognition stays enabled here;
the standard line retains its reversible AC-profile mapping. A queued policy
event checks source eligibility before writing, even if it precedes the power
notification. No new high-frequency source poll is introduced.

Physical charger transitions have not been re-tested with the complete 1.5.0
application. All 16 combinations of the pure known/battery/weak/blocked gate are
tested. The source-gate owned-worker trial reported AC offline, 30% battery and Energy
Saver off. An injected weak-charger decision released an actual assignment. Unknown unsupported chargers remain a limitation of the existing
source detector, not evidence of battery-only eligibility.

## Topology and placement

Reuse verified `GetLogicalProcessorInformationEx` physical core/cache/group data
and `GetSystemCpuSetInformation` IDs; never infer core type from a hard-coded LP
range. Select four usable physical cores in the lowest OS efficiency class,
including all SMT siblings, in one verified LLC and processor group. Selection
is deterministic under enumeration-order changes. On the target, four cores
produced eight IDs, 264–271, in one LLC. These IDs are observed, not constants.

Incomplete, homogeneous, inconsistent or insufficient topology fails open.
Multi-group runtime management is intentionally unsupported. Intel LP-E identity
is not established by efficiency class alone; no LP-E preference is claimed.
For example, a two-core lowest-class island cannot satisfy this four-core policy.
Topology tests cover SMT variation, shuffled records, reservations, multiple
groups, split caches and contradictory/missing metadata.

Use **process default CPU Sets**, not hard affinity. Existing default sets,
restrictive affinity, job-managed processes, protected/critical processes,
Windows paths, other users/sessions and inaccessible processes are skipped.
Explicit thread CPU Sets and affinity remain authoritative. Unknown roles remain
unrestricted unless a validated full-path override nominates them. Everyday
window-owning Chromium/Office/Notepad roots are candidates; browser helper,
media, voice, service, security and uncertain dependency roles are not inferred
safe from their executable name. Known compiler/tool names are an unrestricted
fast path. Unknown accessible productive work also qualifies behaviorally via
the existing process-tree detector and the bounded new per-process observer.

Global T2 does not constrain unrelated processes or automatically make every
process unrestricted. Per-process compute evidence and bounded ancestor-chain
dependencies release that process's restrictions. Foreground bursts immediately
release adaptive placement. Strict-four suppresses that foreground expansion,
but compute and safety rules still win. Strict means a persistent **request**,
not a kernel-enforced confinement guarantee; no hard-affinity workaround ships.

Background candidates are deliberately narrow: same-user, low-priority
OneDrive/Dropbox without a visible window, observed read/write I/O or active
burst. This is not universal Windows-maintenance classification. Aggressive
mode shortens background re-entry dwell; it does not expand unsafe coverage.
Optional EcoQoS changes only the execution-speed bit for those candidates.
Existing execution-speed ownership is skipped and unrelated QoS bits preserved.

## Cadence and cost controls

| Mechanism | Bound / behavior |
|---|---|
| Existing interactive/compute monitor | Existing adaptive cadence retained; no global timer-resolution request |
| New process CPU samples | At most once/second for at most 64 retained candidates; event-driven foreground release is independent |
| Candidate reconciliation | At most once/5 seconds, using the shared snapshot where available; standalone fallback otherwise |
| Process completion | One thread-pool wait per candidate, coalesced GUI wake; cancel/drain before handle destruction |
| Unknown compute evidence | At least 80% of one LP over 500 ms; actual one-second sampling adds latency |
| Known-hint evidence | 65% / 250 ms baseline; known compute identities already unrestricted |
| Restriction re-entry | Two-second compute quiet hold below 20%, 10-second placement dwell; aggressive background 5 seconds |
| Restriction writes | Maximum 16 new assignments/minute; safety/foreground releases bypass that budget |
| Logging | State/counter diagnostics; continuous traces belong to the standalone tool |

CPU time is normalized to one logical processor, **not** measured IPC, capacity,
deadline slack or instruction throughput. Invalid/backward/stale samples release
restrictions rather than invent valid demand. New process starts can wait for
reconciliation. No kernel ETW session, per-frame process scan or new permanent
high-resolution timer was added. This bounded fallback is a deliberate latency
trade-off, not “instant universal classification.”

The 65-sample controlled-worker replays did not establish a robust advantage for
EWMA or CUSUM. Keep the existing threshold baseline; the candidates remain offline.
Do not shorten resident intervals based on one synthetic trace.

## Recovery and configuration

Settings: Power controls → **Experimental policy settings**. Tiers, adaptive or
strict placement, background policy and optional EcoQoS are independent. Rules
are full drive-qualified paths followed by `|0` automatic, `|1` all eligible,
`|2` efficient four or `|3` untouched. Maximum 64; malformed/duplicate/wildcard
rules are rejected. “Disable scheduling & restore” disables placement/background
and EcoQoS while preserving the independent power-tier preference.

Every mutation first persists `state/process-placement.bin` with PID, creation
time, selected IDs and original QoS bits. Only initially empty process-default
sets are taken over. Readback is mandatory. Release restores only values still
matching Pulse's owned values; distinct external changes are preserved and latch
a conflict for that tracked process. Windows offers no atomic compare-and-set,
so identical-value external writes are indistinguishable. A tiny race with a
concurrent setter cannot be eliminated by these APIs.

Pause, exit, source loss, topology change and the existing recovery companion
release ownership. Startup also recovers both new and old journals. PID reuse is
checked by creation time. Access/restore failures retain recovery evidence and
stop new restrictions. Callback lifetimes are drained before destruction.
No elevated privilege is requested for scheduling; access denial means skip.

## Evidence and release decision

See [recorded results](evidence/2026-10-29-adaptive/README.md). Five local probe
runs passed transactions, foreground rescue and crash recovery, but three failed
actual selected-core execution. A later battery run passed with 2,951 inside
samples and none outside. The failure is retained, not explained away by the
latest pass. Parking can legally affect CPU Sets; the collected flag snapshots
do **not** prove it caused earlier failures.

**Do not enable new scheduling by default yet.** Exact-four acceptance failed
intermittently. Broad media/UI tail latency, energy per completed task, sustained
compile/render coverage, Intel/LP-E hardware, physical source transitions,
reboot/resume and long-run overhead remain unverified. The synthetic single-thread
A/B is not the requested full production workload matrix. Default-off placement
is the specification's acceptance gate, not a claim that validation is complete.

## API contracts and evidence level

| Contract | Evidence level |
|---|---|
| [CPU Sets are soft; affinity and explicit thread sets take precedence](https://learn.microsoft.com/en-us/windows/win32/procthread/cpu-sets) | Documented; preserved settings tested; exact execution intermittently failed |
| [EfficiencyClass and parked/allocated flags](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-system_cpu_set_information) | Documented; target topology and flag snapshots observed; LP-E identity unverified |
| [SetProcessDefaultCpuSets](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-setprocessdefaultcpusets), [readback](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getprocessdefaultcpusets) | Documented; apply/readback/restore and external takeover tested on owned child |
| [ProcessPowerThrottling](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/ns-processthreadsapi-process_power_throttling_state), [setter](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-setprocessinformation) | Documented; execution-speed transaction and restoration tested; energy gain unverified |
| [Thread-pool wait lifetime](https://learn.microsoft.com/en-us/windows/win32/api/threadpoolapiset/nf-threadpoolapiset-setthreadpoolwait) | Documented; completion/recovery exercised; long-duration stress pending |
| [Waitable timer](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-setwaitabletimerex) | Documented; five samples at each of five intervals; not a latency guarantee |
| [ETW process lifecycle](https://learn.microsoft.com/en-us/windows/win32/etw/process) | Documented alternative; not enabled in this patch |
