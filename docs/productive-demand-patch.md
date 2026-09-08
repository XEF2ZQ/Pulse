# Productive demand patch — 1.2.0 preview

## Contract

Measured productive CPU demand requests **Best performance + Efficient Aggressive (4)**. This state outranks the short UI burst, so window changes do not replace productive boost 4 with boost 2. Once all eligible demand settles, the controller returns to Best efficiency + Disabled (0), subject to any independently accepted short burst. Battery-only remains the default; AC opt-in and scheduler guard OFF are preserved.

Known compilers and benchmarks are a fast path, not an allowlist for productive work. Unknown applications in the current interactive session are grouped by cached parent identity and creation time; actual CPU demand over that tree supplies the evidence. This covers unfamiliar compilers, renderers and other CPU producers with the same observable behavior. It does **not** prove that every possible application is productive: inaccessible/service-session workers, explicitly low-priority or EcoQoS work, excluded media hosts, and jobs shorter than discovery can be missed. CPU time is not an instruction-throughput or semantic-intent measurement.

## Detection and cost controls

- Cache at most 1,024 process entries. Open handles once, use limited query rights, and back off denied candidates for 30 seconds. Use process identity sequence numbers where available and creation-time checks for ancestry.
- Discover identities every 3 seconds while quiet, or 1 second with demand evidence. Use `SystemBasicProcessInformation` dynamically, with bounded buffer growth and defensive record parsing; fall back to Toolhelp if unavailable. Native identity discovery avoids collecting global process timing data on each discovery.
- Read all cached CPU times every 3 seconds while quiet, or 1 second with active evidence. Sample hot/candidate/active targets at 250 ms. New entries receive a short observation window after their baseline sample.
- Register bounded thread-pool waits for process exit. Callbacks only coalesce a window message; the owner thread accounts for surviving children and changes policy. Disarm and drain waits before closing handles. Persistent hosts still need quiet evidence; a parent exit alone is not job completion.
- Evaluate each tree independently: known threshold 65% of one logical CPU with 250 ms observed evidence; unknown threshold 80% with 500 ms evidence. Hold while at least 20% remains; release after 500 ms observed quiet. Unavailable evidence expires after 1.5 seconds and cannot prove quiet through an unobserved child.
- A foreground value/capture event or navigation arrow starts a 1.25-second editing probe. It does not itself enable performance. A fresh CPU sample of at least 35% can admit productive work, with 125 ms sampling and 125 ms quiet evidence for that editing episode. Continuous events extend the bounded probe without repeatedly discarding its CPU baseline.
- Observe only the current foreground process's edit events. No global mouse-move feed, UI-tree polling, thread enumeration, ETW event stream, WMI subscription, NPU runtime or sensor driver is added. Paused/AC-bypassed control clears process handles/waits and removes input hooks.

These are sampling targets, not hard deadlines: message delivery, timer coalescing, scheduling and power API execution add delay. Registering an exit notification cannot force firmware to lower physical clocks at the same instant.

## Media and editing boundaries

Existing browser, playback and voice-host exclusions remain. Unknown helpers inherit those exclusions until a shell boundary; separately recognized compilers remain eligible. This avoids a global playback veto suppressing an unrelated build. Unknown mixed-purpose applications remain an imperfect classification case.

The editor path is application-independent and requires CPU evidence. It was exercised in Darktable, whose custom controls produced capture events. Other editors can benefit when they expose equivalent events or sustained CPU demand. GPU-bound work without sufficient CPU demand does not justify an additional CPU boost under this detector. DaVinci Resolve compatibility, export throughput and GPU power behavior were **not** established by the Darktable test.

## Local evidence — September 8, 2026

Host: the existing ASUS Ryzen AI 9 HX 370 laptop, Windows 11 x64. Release build: MSVC 19.51, SDK 10.0.26100.0. Diagnostics reported battery power during the live session. AC opt-in was temporarily enabled in the test configuration, but this did not change the physical power source. These were functional checks on battery, not controlled discharge experiments.

| Check | Observation and scope |
|---|---|
| Six CTest suites | All passed, including current demand transitions, missing/invalid evidence, independent overlap, quiet noise and malformed native records; legacy policy tests are separately labelled |
| Native power exercise | Efficiency, Burst and Compute accepted and read back; Compute requested Best performance and boost 4; source settings matched the pre-test snapshot after restoration |
| Actual MSVC `/O2` compilation | Repeated runs entered Compute and held boost 4 with Best performance, then settled. First run: 1.594 s onset, 0.422 s after observed exit to Efficiency. Reviewed build: onset 2.344 s, release 0.750 s after observed exit. These are individual observations, not percentiles |
| Unlisted Python bytecode compilation | Actual `compile()` workload, not a compiler-name override. Read-only detector observed onset about 3.55 s after launch and settled about 0.47 s after observed exit. It measured the detector path, not Windows writes |
| Darktable 5.6.0 local contrast | Disposable 24 MP TIFF, separate configuration/cache and in-memory library. Two slider drags visibly changed detail (125% to 311%, then lower); foreground probes arrived, short Compute intervals appeared and each returned to Efficiency. No controller fault. Pipeline logs recorded CPU processing. This is functional evidence, not a paired edit-latency benchmark |
| Settled full controller | 46.875 ms CPU over 40.067 s: **0.117% of one logical CPU**, 2.67 MiB private memory. Companion: 0 ms at accounting resolution over 40.041 s, 1.52 MiB private memory. Frequent status recording was stopped during this interval |
| Installed PassMark CPU suite | Five-second subtests, fresh CSV export and successful application exit; productive observations read back Best performance + boost 4, then Efficiency. Observer assertions completed without a controller fault. The first observer attempt incorrectly treated the report's `Battery` label as a setting suffix; the corrected run maps it to DC |
| Recovery after bounded live sessions | Preview quit, owned values restored, original installed controller restarted. No G Helper hardware settings edited |

The editor trace includes an approximately 198 ms final policy transition. Very short edits may finish before Windows completes the boost request. The current checks do not establish a speed or energy benefit for such edits; a paired, repeated edit workload is needed. Idle CPU accounting is not a package-power or battery-energy measurement.

Cinebench was not rerun in this patch validation. Known benchmark candidates share the measured-demand route; idle menus do not qualify solely by executable name. New media playback, suspend/resume, physical charger transitions, multiple-machine coverage and battery-runtime distributions remain to be revalidated.

## Research and decisions

1. Microsoft documents [basic process identity discovery](https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-ntquerysysteminformation) as faster and lower-memory than full process information, avoiding timing synchronization. This directly motivated the discovery path. Older systems retain a fallback.
2. [System Informer](https://github.com/winsiderss/systeminformer) is relevant prior art for careful native process discovery and identity handling. Pulse uses its own bounded implementation against SDK declarations; no System Informer driver or source was incorporated.
3. [CPUDoc](https://github.com/mann1x/CPUDoc) exposes adaptive power-plan and ThreadBooster ideas. Its author-reported outcomes do not establish an energy gain on this laptop. Pulse retains bounded state evidence without importing its hardware drivers or CPU-placement machinery.
4. [UXTU adaptive-mode discussion](https://github.com/JamesCJ60/Universal-x86-Tuning-Utility/discussions/254) discusses adaptive/ML approaches but does not provide evidence that inference would outperform this detector at lower total cost. No model was adopted.
5. VID is a voltage request influenced by policy and hardware activity, not a reliable productive-work label. Retired instructions/cycle measurements also do not identify user intent; Microsoft cautions about interpreting [process cycle times](https://learn.microsoft.com/en-us/windows/win32/api/realtimeapiset/nf-realtimeapiset-queryprocesscycletime). No VID, PMU or direct-register reader is justified by these tests.
6. Darktable's [documented invocation options](https://docs.darktable.org/usermanual/development/en/special-topics/program-invocation/darktable/) supplied isolated settings, an in-memory library, temporary welcome-screen override and pipeline performance output. The user's original image/library remained untouched.

No third-party implementation was copied into this patch. Existing copyright and all-rights-reserved terms remain unchanged. The public package excludes personal configurations, raw machine diagnostics and test images.
