# Validation

For the current **1.2.0-demand-preview**, see [productive-demand patch validation](productive-demand-patch.md). Results below are the historical 1.0–1.1.2 baseline, not new-release claims.

This report separates tested policy behavior, local integration observations and work still needed. Dates refer to local tests performed September 5–8, 2026. Historical measurements are retained with their original version scope. Personal machine logs, configuration files and benchmark exports are not published.

## Test platform

ASUS ROG Zephyrus G16 GA605WI; AMD Ryzen AI 9 HX 370, 12 physical cores / 24 logical processors; Windows build 26200.9168 recorded in the September 5 session; G Helper Experimental 0.279. Later test sessions used the same laptop. This is not a cross-device certification matrix.

## Current source verification

The 1.1.2 scheduler-preview source passed native Release compilation and local policy, topology and GUI checks on September 7. On September 8, the publication snapshot rebuilt successfully and passed all five local CTest entries: power policy, scheduler unit, live topology, scheduler live and native GUI. No live power-control exercise was rerun for this documentation/build-only publication.

The initial GitHub-hosted run was blocked before any job steps started. Local results below must not be interpreted as a successful hosted CI run. The configured workflow can be rerun when hosted execution is available.

| Layer | Recorded result / scope |
|---|---|
| Existing burst/compute policies | 202,806 assertions: load edges, sustained work, deadlines, quiet exit, budget and blocked states |
| Scheduler parser and guards, with local OS checks | 60,778 checks: truncated/malformed records, multiple groups, zero CPU Set IDs, classifications, cost evidence, residency and rolling budget |
| Native scheduler GUI | OFF default, ON/OFF button events, persisted reload, accurate label, non-overlapping layout and no power writes |
| Live topology | 12 cores / 24 logical processors; two OS-reported LLC domains; SMT and NUMA identity cross-checks passed |
| Default-off installed startup | Scheduler disabled, no topology queries, no scheduler writes |

Counts include loops and repeated boundary checks, not hundreds of thousands of independent real-world workloads. Synthetic multiple-group buffers do not replace physical multi-socket testing. The GUI test explicitly invokes one read-only topology discovery; normal default-OFF startup does not.

The local Windows topology reported 4 cores with EfficiencyClass 1 and 8 with EfficiencyClass 0, with different raw SchedulingClass values. Missing die information remained unavailable rather than inferred from a marketing CPU name. No thread migrations were commanded or benchmarked.

## Power controller observations — version 1.1, September 6

| Experiment | Observation |
|---|---|
| Three-state native power exercise | Efficiency/Burst/Compute accepted and read back; Compute = Balanced + boost `4`; restoration matched |
| Real MSVC compilation, DC | Compute observed 4.265 s after process launch; Efficiency within 3.187 s of observed exit |
| Real MSVC compilation, AC opt-in | Compute observed after 4.235 s; Efficiency within 3.141 s of observed exit |
| Installed PassMark CPU suite | CPU suite completed; Balanced/boost `4` observed; settled to Efficiency |
| Normal exit and forced controller exit | Inspected original source values/modes restored, including forced exit during Compute |
| Physical charger removal/reconnection | DC control engaged; reconnection released ownership and restored inspected values; G Helper remained running |
| AC button | ON activated control, preference survived restart, OFF restored owned AC settings |

The compilation fixture used generated independent C++ functions and the installed MSVC compiler. It was a bounded integration workload, not a representative project build. Workload diagnostics were sampled about once per second, which both limits timestamp precision and adds observation overhead.

PassMark's first Compute observation occurred 12.703 s after the launcher began, including UAC consent and startup. This must not be presented as work-onset detection latency. Efficiency was confirmed 1.047 s after observed process exit; the last pre-exit observation was already efficient. No comparative benchmark-score claim is made. Cinebench names are recognized, but Cinebench R23 was not directly rerun for this revision.

## Controller overhead — version 1.1

| Interval | Controller CPU time | Other observations |
|---|---|---|
| Active tray, 50.047 s | 46.87 ms = 0.0937% of one logical core | 16 process scans; 2.23 MiB private memory at end |
| AC bypass, hidden, 50.016 s | 0 ms at Windows accounting resolution | 0 workload samples, 0 scans, 0 writes; no new journal |
| Instrumented PassMark session, 125.234 s | 984.38 ms = 0.786% of one logical core | Includes startup, transitions and repeated diagnostics |

Percentages use `100 × CPU-time / elapsed-time` for one logical processor, not total 24-thread machine utilization. Private memory is not total system memory cost. A zero time delta is an accounting-resolution result, not zero energy. These short measurements have no reported confidence interval and do not establish long-term battery drain.

## Historical user-assisted checks — version 1.0, September 5

- User-selected 4K YouTube playback: separate ~55 s AC and DC intervals produced no new bursts or power writes. Decoded resolution and dropped frames were not instrumented.
- User-confirmed voice input: ~55 s AC interval produced no new bursts or writes; speech/audio was not recorded.
- The user reported that both video and voice felt smooth. This is subjective feedback, not an objective latency/quality score.
- One Modern Standby cycle of about 43 s preserved the processes and restored efficient operation. It does not cover all suspend/hibernate/shutdown cases.
- Native power-transition call measurements were roughly 110–128 ms in one early exercise. Windows API completion is not physical clock settling.

These media/sleep observations were not all repeated on 1.1.2. Its inherited policy tests passing does not substitute for those live reruns.

## Known adverse observation

A historical 30 s visible-window sample consumed 4,656 ms of controller CPU, about 15.5% of one logical core, alongside hundreds of Windows accessibility queries per second. A trial accessibility-provider change did not resolve it and was not shipped. Closing the window to the tray removed that sustained cost in the measured setup. Do not generalize tray-overhead measurements to the open settings window.

## Not established

Battery-runtime improvement, energy per completed job, temperature reduction, gaming behavior, voice on battery, universal unknown-workload detection, physical Intel/multi-socket behavior, measured scheduler-placement benefit and crash-free reliability remain unverified. The recovery mechanism is best effort, not a guarantee against whole-system failure.

The planned [paired runtime experiment](benchmark-plan.md) will report absolute results, variation, completed work and responsiveness, including neutral or negative outcomes.
