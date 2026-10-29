# Adaptive scheduling evidence — 29 October 2026

These are functional and exploratory measurements on one Ryzen AI 9 HX 370
laptop, not production acceptance or energy-efficiency results. The installed
standard Pulse controller remained active. Only disposable probe children had
their CPU Sets/EcoQoS changed. No uncontrolled desktop app was reassigned.

## Results, including failures

| Run | Requested-set execution samples: inside / outside | Apply + API readback | Foreground clear | Controller CPU (window) | Handle count | Overall probe |
|---|---:|---:|---:|---:|---:|---|
| [initial](initial/summary.txt), sandbox | 0 / 2,975 | 0.0163 ms | 2.7914 ms | 46.875 ms (~12.1 s) | 76 → 76 | **Failed execution gate** |
| [normal-session](normal-session/summary.txt) | 2,263 / 0 | 0.0130 ms | 2.7437 ms | 15.625 ms (~12.1 s) | 97 → 97 | Passed |
| [trace-run](trace-run/summary.txt) | 48 / 2,830 | 0.0656 ms | 2.2716 ms | Below CPU counter resolution (~12.1 s) | 98 → 98 | **Failed execution gate** |
| [flags-run](flags-run/summary.txt), battery | 2,951 / 0 | 0.0412 ms | 2.6452 ms | 15.625 ms (~12.1 s) | 99 → 99 | Passed |
| [source-gate-run](source-gate-run/summary.txt), battery | 3,017 / 134 | 0.0195 ms | 2.4081 ms | 46.875 ms (~15.2 s) | 97 → 97 | **Failed execution gate** |

The source-gate run additionally verified a real assignment was cleared by an
injected weak-charger eligibility decision. This was not a physical plug/unplug
test. It preserved the execution-gate failure (134 outside samples).

All five completed the journal/EcoQoS transaction, external CPU Sets preservation,
PID/creation mismatch, compute release and simulated controller-death recovery
checks. Each foreground-clear timing is one observation, not p95/p99 latency.
The controller CPU totals are quantized process-time deltas while repeatedly
exercising its test path, not a settled background overhead benchmark. Zero
measured ticks do not mean zero cost.

Actual execution is sampled inside the worker using
`GetCurrentProcessorNumberEx`. It is not a full context-switch trace or proof of
exclusive residency. The selection is four physical cores/eight SMT processors
in one verified LLC. The trace-run worker retained all 24 affinity bits and had
zero explicit thread CPU Sets, yet most samples landed outside the requested
subset. The flags-run had parked selected siblings, used LP10/11 within the
subset and passed. Those observations do not isolate the cause of the failures.
CPU Sets are a documented soft policy. Do not advertise deterministic physical
execution or “fix” this by silently changing hard affinity/parking.

## Flags-run timing and workload comparison

Source: AC offline, battery 31%, Energy Saver off. The standard controller and
other normal desktop activity were not disabled. Fixed-work comparison alternated
order across four ALL/FOUR pairs, using one synthetic worker, 3,000 units per run.

| Placement request | Runs | Mean completion | Sample standard deviation |
|---|---:|---:|---:|
| All eligible | 4 | 473.478 ms | 12.033 ms |
| Efficient-four | 4 | 474.617 ms | 4.100 ms |

This is not evidence of a performance or energy advantage. CPU set acceptance
does not establish exclusive execution during each A/B arm. No package-energy,
display, whole-system discharge or completion-energy result was collected.

Five waitable-timer intervals each received five samples without a timer-resolution
request. Mean lateness / sample standard deviation in milliseconds:
50: 13.726 / 4.506; 100: 10.885 / 7.173; 150: 9.217 / 10.464;
250: 8.732 / 2.644; 1000: 2.504 / 1.869. These small samples quantify the local
measurement limits; they do not justify permanent sub-100 ms polling.

The flags-run [65-sample labeled worker trace](flags-run/workload-trace.csv) was
replayed with threshold, EWMA and CUSUM candidates. Onset observations:

| Candidate | Onsets | TP / FP / TN / FN |
|---|---|---|
| Existing threshold baseline | 1,561 / 769 ms | 25 / 8 / 25 / 7 |
| EWMA candidate | 1,049 / 1,785 ms | 23 / 9 / 24 / 9 |
| CUSUM candidate | 1,049 / 1,020 ms | 26 / 24 / 9 / 6 |

FP includes the intentional post-compute quiet hold. Labels are harness work
phases, not independently measured user intent. The earlier trace produced
different onset timings; neither trace is held-out calibration. Retain the cheap
threshold baseline and keep candidate mathematics out of the resident path.
Full results and samples are beside each summary.

## Automated verification

MSVC x64 Release, C++20, `/W4 /WX`: build passed. All **14 native CTest suites**
passed, including topology, arbitration, all 16 battery-gate combinations,
configuration persistence and native GUI Apply/restore wiring (100 checks in the new adaptive suite). The GUI test is
an automated native-control test, not a visual usability inspection. Three Python
replay tests cover elapsed evidence, deterministic candidates and invalid traces.

The controlled-process probe is intentionally separate from ordinary CTest.
It changes only its child processes and cleans up; run it on battery in a fresh
output folder, with normal desktop use paused for better repeatability:

```powershell
python build.py
build-native\PulseAdaptiveProbe.exe validation-local
python tools\replay_adaptive.py validation-local
```

The probe's simulated crash leaves a live child for journal recovery. It does not
crash Windows or alter firmware. Do not concurrently run multiple probes from
the same installation/state folder. Preserve a nonzero exit and failed summary;
re-running until a pass does not invalidate an earlier failure.

A final [transaction-only rerun](qos-readback/summary.txt) explicitly read back
both applied EcoQoS masks before restoration; it passed together with CPU Sets
ownership and PID-identity tests. Earlier probe runs checked successful setter
return plus restoration. The added readback closes that verification gap without
rerunning unrelated timing benchmarks.

## Acceptance still open

Exact-four execution is **not consistently established**. Browser/media playback,
interactive p95/p99 latency, broad compile/render/scientific workloads, optional
background EcoQoS effects, full five-configuration workload comparison, energy
per completed task, Intel/LP-E and multi-group hardware, real charger transitions,
reboot/resume and long-running handle/wakeup behavior need separate validation.
No new restriction is enabled by default and no runtime gain is claimed.
