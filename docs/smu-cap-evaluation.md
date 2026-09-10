# Evaluation: dynamic 12/35 W limits and discharge recovery

**September 10, 2026. Decision: component validation patch implemented; automatic SMU writes deferred.**

The supplied proposal identifies a useful experiment but overstates its diagnosis and the effect of a limit write. There is no measured basis yet for claiming that 12 W limits will eliminate this laptop's reported 16–28.5-second discharge tail. PulseTailProbe 0.2 adds the missing component-comparison path; it does not change the resident Pulse governor or hardware limits.

## Claims checked against the linked evidence

| Claim | Finding and consequence |
|---|---|
| The observed tail is SMU STAPM/Slow-PPT averaging | Not established. The previous rate samples came from `IOCTL_BATTERY_QUERY_STATUS`, not an SMU power table. `GetSystemPowerStatus` was used separately for source/charge status. |
| 100/128 seconds are confirmed HX 370 defaults | The issue reports those values on one HX **PRO** 370 Linux machine and reports that time-constant writes target the wrong field or have no effect. It is not confirmation of this ASUS's defaults or a discharge mechanism. |
| The measured decay matches those constants | Our descriptive fit was approximately 13–14 seconds. Threshold-crossing time is not a time constant, and neither number identifies an SMU budget or firmware filter without component telemetry. |
| Lower STAPM/PPT limits force all power to idle immediately | Limits constrain allowed power in their respective control domains. Fast PPT and averaged slow/STAPM limits are different constraints. A limit write is not an instantaneous whole-platform wattage command, nor a reset of the battery gauge. |
| A 12 W CPU limit will lower an already approximately 5 W package | A ceiling above actual consumption does not directly constrain it. Actual remaining package work above 12 W would be a different, testable case. |
| 35 W enforcement has been shown on this CPU family | The linked ProArt P16 report supports the possibility of a working SMU actuator. It concerns a different laptop/firmware and steady-state load, with no measured workload-end or battery-rate latency. |
| Apple eliminates this through the proposed mechanism | No matched Mac/Windows electrical measurements or source supporting the detailed Apple mechanism were supplied or obtained. No cross-platform equivalence is claimed. |

Sources: [RyzenAdj issue 412](https://github.com/FlyGoat/RyzenAdj/issues/412), [G Helper issue 4996](https://github.com/seerge/g-helper/issues/4996), [RyzenAdj options: fast versus average PPT](https://github.com/FlyGoat/RyzenAdj/wiki/Options), [Windows battery status](https://learn.microsoft.com/en-us/windows/win32/power/battery-status-str). The RyzenAdj wiki's older generation-specific STT notes must not be generalized to Strix Point.

The distinction is causal: an averaged power *budget* can influence future performance while work is runnable. That does not mean historical high consumption must continue after the work stops. Similarly, modifying a budget does not erase history from an independent reported battery-rate signal. If real component power is already back at baseline, changing the displayed tail is not an energy-saving result.

### Unsupported premises checked during review

A supplementary hypothesis assumed an SMU limit patch was already applied successfully. That premise is false for Pulse: no such patch was installed or exercised. It also describes SMU telemetry as Pulse's current classification input; Pulse actually uses activity events and sampled CPU-time evidence. Package power was unavailable in our previous captures. Do not label it “confirmed fast” in a diagnostic output based on that hypothesis.

The recommendation to separate battery reporting from actual consumption is useful, but the conclusion that all remaining delay is confirmed EC filtering is unsupported. Other platform components can consume power after CPU work stops. Windows's battery interface exposes firmware-reported data without proving the exact internal filtering location or algorithm on this ASUS. There is no demonstrated supported Pulse setting that resets that reporting history.

An inline USB-C meter measures adapter input, not battery discharge. Plugging it in changes the source conditions, and its readings include conversion/charging effects. Such a test can provide additional evidence under controlled conditions, but is not a like-for-like replacement for the DC captures. No direct battery-bus access or firmware modification is proposed.

## G Helper coexistence audit

Reviewed local G Helper reference revision `5b0029057541bff4cbd025e1a01e0641e11bd64e`, specifically `Mode/ModeControl.cs`, `Pawn/RyzenSmu.cs`, `AppConfig.cs`, `Program.cs` and `Helpers/ProcessHelper.cs`. This is a source audit, not proof that every byte of the installed binary corresponds to that revision.

The reference has internal methods for setting all three limits and reading limit values through a PawnIO-backed SMU service. Its power-limit routing still tests ASUS support before choosing SMU fallback. The source's per-service mutex is unnamed; it is not an ownership agreement with an independent Pulse process. The methods are not an external Pulse request/lease API. No named-pipe or comparable dynamic power-limit interface was found in the reviewed source.

The laptop's current G Helper configuration enables automatic power application. Its selected mode has configured limits of 20 W sustained, 25 W slow and 38 W fast. These are configuration values, not verified hardware readback. Applying 35 W to all three would raise two of those configured ceilings; it is not uniformly a reduction. Rewriting the configuration and relaunching G Helper on each Pulse event would add overhead and disturb existing ownership. It is not an acceptable integration mechanism.

The linked issue's WinRing0 experiment does not imply Pulse should add that driver. No driver was installed, no security settings changed, no embedded G Helper module copied, and no SMU/ACPI limit writes performed in this patch.

## Implemented patch

`PulseTailProbe 0.2 --require-hwinfo` refuses to start a synthetic experiment if shared readings are absent at startup. This prevents another battery-only run being mistaken for a component comparison. It does not demand a faster sensor polling interval.

`tools/component_tail.py` selects one sensor by its full numeric identity from the captured catalog. It checks watts, coverage, timestamps, freshness, gaps and power-source continuity. It compares the component's recovery to baseline plus a stated margin with the separate battery-rate recovery, and counts recovery frames above the proposed ceiling. A flat sensor that never responds to the workload is not accepted as proof of fast recovery. Nothing in this offline report enables hardware control.

Example after identifying an actual package-power sensor in the live table and catalog:

```powershell
.\PulseTailProbe.exe --out .\component-run --seconds 100 --exercise 4 --require-hwinfo
python .\component_tail.py .\component-run --sensor SENSOR_ID INSTANCE READING_ID --ceiling-w 12
```

Replace the three identity placeholders with the catalog's numbers. A limit sensor also has units W, so human verification of sensor meaning remains necessary. The analyzer does not infer physical meaning from a name, sum overlapping CPU/GPU domains, or treat missing data as zero power. Producer timestamps have whole-second precision; results are sample-resolution observations, not hard real-time bounds.

Release build and seven native test suites passed. Ten new offline component-comparison tests passed, alongside five existing recovery-analysis tests. The new tests use synthetic telemetry and do not establish real hardware timing. Live component validation remains outstanding: the latest inventory returned shared-mapping error 2. The live sensor producer was not available for the component experiment. No live component-power result is claimed.

## Requirements before implementing the hardware actuator

If fresh component telemetry demonstrates useful remaining CPU demand above the proposed cap after Pulse has released boost, perform a reversible cap experiment through one verified hardware owner. A production implementation needs all of the following:

1. **One decision stream:** consume Pulse's final Efficiency/Burst/Compute state and existing DC/AC eligibility. Use 12 W only for the validated efficiency state and 35 W for elevated states; never use battery-reported decay as the trigger. Coalesce obsolete requests and reject stale sequence numbers.
2. **One hardware owner:** add a narrow, authenticated request interface to G Helper, or a dedicated owner to which G Helper explicitly relinquishes these settings. Other G Helper functions remain under its control. A separate mutex that G Helper never participates in is insufficient.
3. **Capability and effect verification:** identify CPU, model, firmware and PM-table layout; reject unknown layouts. Distinguish successful mailbox acknowledgement, readback of a configured limit and observed enforcement under load. Never assume ASUS capability flags prove effective writes.
4. **Serialized transactions:** apply on state changes, not a permanent sensor loop. Fast-PPT control addresses the short-window ceiling; slow/STAPM writes must be evaluated separately. The three writes are not atomic. Define partial-failure handling and readback outside Pulse's UI/event thread.
5. **Recovery ownership:** snapshot actual prior limits; restore on Pause, exit, source bypass and connection/process loss. Use a process-handle wait and an owner-side expiry for connection failure, rather than high-rate heartbeats. On outside-owner changes, relinquish rather than repeatedly overwrite. Revalidate after sleep/resume.
6. **Evidence gate:** compare completed work, responsiveness and integrated energy across repeated trials. Retain the feature only if it reduces real energy or a measured component tail without harming interactive or sustained work. No acceptance criterion should depend only on a faster-looking battery number.

This is a proposed actuator contract, not implemented hardware control. Until those prerequisites are met, the installed Pulse 1.2 controller remains unchanged. The currently justified outcome is a more reliable measurement patch and a rejected claim of a proven instant-discharge fix.

Copyright © 2026 Peter Kosanyi. All rights reserved.
