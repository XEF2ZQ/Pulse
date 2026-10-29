# Battery PCIe policy guard — 1.4.2 preview

Requested behavior: keep maximum Windows PCIe link-state power savings selected on battery, including after another utility changes the setting. This patch adds a default-on guard independent of Pulse's temporary CPU boost controller.

## User behavior

- **Efficiency options → Keep PCIe maximum power savings on battery** is checked by default. Its saved preference is `Preferences/PcieBatteryGuard` in `Pulse.ini`; absence means enabled. A deliberate off selection persists across restart.
- Confirmed battery power: maintain DC ASPM index **2** in the active power plan. AC index values are never written. Unknown source: no policy mutation, explicit status and a delayed retry.
- Startup, power-source notifications, resume and plan changes trigger verification. A registry notification detects edits to the active plan's ASPM value.
- Closing the window leaves Pulse in the tray. CPU **Pause** does not pause this independent guard. Explicitly exiting Pulse stops the watcher; the saved Windows DC setting remains. Unchecking likewise stops enforcement without rolling back the permanent DC preference.
- On this installation the existing `Pulse.lnk` Windows Startup shortcut launches the desktop executable with `--hidden`. Thus the watcher resumes at sign-in after reboot. It is not a boot-time service and cannot correct another writer while Pulse is stopped, Windows is suspended, or before sign-in. A persisted power-plan value does not require Pulse to keep executing.

This is policy enforcement, not a guarantee of a particular physical PCIe state. It does not force L1 substates, disable devices, or force a discrete GPU into D3cold. The Radeon iGPU's internal connection is not an ordinary external PCIe GPU link. No battery-life improvement is claimed; DC index 2 was already selected on the test laptop.

## Design and failure handling

`src/pcie_policy.h` contains the testable enforcement transaction. `src/pcie_guard.cpp` implements the Windows API adapter, notification lifetime and preference handling. `src/main.cpp` integrates the dropdown, message dispatch and diagnostics.

Only `PowerWriteDCValueIndex` is used. The setting identifiers are the SDK's `GUID_PCIEXPRESS_SETTINGS_SUBGROUP` (`501a4d13-42af-4429-9fd1-a8218c268e20`) and `GUID_PCIEXPRESS_ASPM_POLICY` (`ee12f906-d277-404b-b6da-e5fa1a576df5`). On a required change, source and active plan are rechecked, the DC value is written, the still-active plan is reapplied, and DC readback is verified. Already-correct values incur neither a write nor reactivation. Failed activation retains a pending apply for retry.

The registry is observed, never directly written: a nonrecursive notification on the PowerSchemes root detects active-plan changes; another observes only the active plan's PCIe setting key. CPU-policy value writes therefore do not trigger a recursive whole-plan scan. Thread-pool waits post to the existing UI message loop; they do not run a busy loop or allocate a dedicated polling thread. Each one-shot registry notification is rearmed only after its event is consumed. Shutdown drains callbacks before closing handles.

Events are coalesced through a 100 ms timer with up to 100 ms timer tolerance. After a correction, a subsequent correction is delayed until roughly one second after the previous write to bound contention. There is no periodic success-path audit. API or watcher errors retain an explicit failure status and retry after 30 seconds. Scheduler delays, driver calls and competing writers prevent a hard real-time guarantee.

Windows offers no atomic compare-source/compare-plan/write operation. Rechecks avoid knowingly applying a stale plan, but cannot eliminate a source or plan race inside the final API call. Because only the DC value is written, AC policy remains untouched even across a source transition. A competing administrator can still change or block policy. If the registry layout/access changes, the watcher reports failure rather than claiming continuous verification.

Unlike temporary CPU settings, this is an explicitly requested permanent Windows preference. It does not enter the CPU recovery journal and is not reverted by CPU recovery. The menu's help and diagnostics describe this difference.

## Validation — 19 October 2026

- Release build with warnings as errors; all **11 CTest suites** passed.
- **17 deterministic checks** cover default DC correction, idempotence, AC/unknown-source refusal, source/plan changes, unsupported values, access failure, readback mismatch and failed-activation retry.
- Explicit manual `pcie_live` exercise used the production guard against the active Windows plan. Original values were DC 2 / AC 0. Three injected DC changes were corrected in **422, 391 and 406 ms**, observed at 20 ms intervals. These are individual local results, not a latency distribution or upper bound.
- Disable allowed an external DC value to remain; saved OFF survived guard restart; enabling corrected it; saved ON survived restart. AC remained 0. The harness stopped and restored the original DC value at completion.
- During a five-second quiet interval, the guard's check/write counters did not change. This supports absence of periodic polling in that interval, not a zero-energy claim.
- The first sandboxed exercise was denied Windows policy writes (error 5). The authorized run outside the sandbox passed. The failed run did not establish a product permissions failure.
- Physical AC transition and reboot tests remain deferred because the charger is unavailable. AC and source-transition logic have deterministic coverage; startup target/arguments were inspected. Resume and plan switching are wired but not claimed as live-validated on this patch.

The live exercise is intentionally excluded from automatic CTest because it modifies DC policy briefly. Run `pcie_live.exe NEW_OUTPUT_DIRECTORY` explicitly on battery; it records outcomes and restores the captured DC value. Local evidence: `work/pcie-guard-live-20261019-user/result.txt` relative to the parent workspace. Raw personal configuration is not published.

References: [Windows ASPM setting](https://learn.microsoft.com/en-us/windows-hardware/customize/power-settings/pci-express-settings-link-state-power-management), [registry change notification lifetime](https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regnotifychangekeyvalue).
