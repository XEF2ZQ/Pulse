# Native loop efficiency follow-up — 1.5.1 experimental

The resident controller, process observer, event callbacks and power setters
already compile as native C++20. Python runs only during builds and offline
analysis. Rewriting Python is therefore not a runtime optimization for Pulse.

This follow-up removes two small sources of unnecessary work from
`src/process_placement.cpp` while retaining the battery-only gate, cadence,
thresholds and restoration rules:

1. Reserve one CPU-set query buffer per controller and reuse it on the GUI thread.
   Compare owned IDs directly against the journal's fixed record instead of
   constructing a temporary vector for each comparison. The scratch buffer never
   crosses callback threads or survives as a reference in a process record.
2. Read I/O counters only for windowless maintenance candidates, because only that
   role uses I/O evidence for restriction eligibility. CPU accounting and priority
   checks remain unchanged for productive-work and safety decisions. Losing a
   window clears stale I/O evidence; missing I/O data still prevents restriction.

New process discovery, journal transactions and restoration may still allocate.
This is not a claim that the entire controller is allocation-free. An empty
default CPU-set query is still a Windows API call; the optimization removes
buffer churn, not the syscall or the correctness check.

## Measurement

`PulsePlacementQueryBench` performs read-only queries on itself. It models the
old local-vector and new reusable-vector query pattern, including the same
256-ID capacity and sort. It does not change CPU Sets, EcoQoS, affinity or power
settings. After warm-up, ten pairs alternate execution order; each arm performs
50,000 queries. This is a microbenchmark of that query pattern, not a profiler
measurement of the whole application or a selected-process workload.

| Pattern | Mean for 50,000 queries | Sample standard deviation | Total buffer growths across ten rounds |
|---|---:|---:|---:|
| Temporary vector | 35.452 ms | 3.543 ms | 500,000 |
| Reused vector | 30.586 ms | 1.799 ms | 0 after initial reserve |

The mean difference is about 13.7% for this tiny loop, or a modeled **6.23 µs per
64 queries**. Background activity was not isolated and no energy result was
collected. Do not present this as a 14% improvement to Pulse, laptop performance
or battery runtime. Reducing unused I/O queries is evident from the guarded
call site; its independent wall-time/energy benefit was not measured.

[Raw query samples](evidence/2026-10-29-adaptive/query-benchmark.csv) and
[summary](evidence/2026-10-29-adaptive/query-benchmark.json) retain all pairs.
The owned-worker regression preserves the earlier execution-placement acceptance
gate; passing API/restore checks does not erase prior outside-subset execution.
The new placement controls therefore remain off by default.

Reproduce the read-only benchmark after building:

```powershell
build-native\PulsePlacementQueryBench.exe > query-benchmark.csv
```

No permanent instrumentation, worker, faster timer or additional power write was
added to the application. The one-time reserved buffer costs approximately 1 KiB
per controller, exchanging a small fixed allocation for repeated heap churn.

The [optimized-controller regression](evidence/2026-10-29-adaptive/hotloop-regression/summary.txt)
passed CPU-set execution sampling in that run, source-loss release, foreground
release, behavioral compute rescue, external-change preservation and simulated
controller-death recovery. The complete native suites also passed. This one
successful regression does not supersede the prior intermittent execution failures.

The regression's controller window consumed 62.5 ms of quantized process CPU time
in 15.203 s, with 93 handles before and after. The earlier source-gate run measured
46.875 ms in 15.188 s. These unpaired short windows do not demonstrate an overall
CPU-time improvement; only the narrower allocation/query benchmark supports a
measured speed claim. No battery-runtime gain is asserted.
