# Usage, integration and recovery

This is an experimental source publication for review. There is no general reuse licence or public installer. The commands below document authorized local operation; obtain the rights holder's permission where required.

## Start with observation

Keep the executable in a writable per-user folder. Pulse stores its INI, restoration journal and optional diagnostics alongside it. It uses native power APIs at normal user privilege; access or platform capability failures should be investigated, not bypassed by routinely running it as administrator.

```powershell
# Open the controls without activating the controller.
.\Pulse.exe --paused

# Observe decisions without writing Windows power settings.
.\Pulse.exe --dry-run

# Normal controller, initially hidden in the tray.
.\Pulse.exe --hidden
```

Run only one controller instance. Close the window to leave Pulse in the tray. Use Pause to stop controlling and restore owned values, or Exit to restore and terminate. A hidden window is the intended low-overhead operating state.

## Controls

| Control | Effect |
|---|---|
| Automatic | Arm burst and sustained-compute policies on eligible power sources |
| Keep efficient | Keep eligible operation in Efficiency; suppress automatic bursts/Compute |
| Pause | Stop control and restore owned settings |
| Boost now | Request a short bounded performance burst; eligibility and policy limits still apply |
| Plugged-in control | Persist AC opt-in; OFF by default |
| Scheduler guard | Persist diagnostic topology/classification opt-in; OFF by default; no thread-placement changes |
| Diagnostics | Generate current power/runtime information locally |
| G Helper | Open the separately configured G Helper application |

## G Helper coexistence

Verified locally with G Helper Experimental executable version **0.279**. G Helper remains responsible for fans, GPU mode and hardware tuning. Simultaneous Windows power-mode/boost writers can conflict.

The existing local handoff disables G Helper's Windows-mode automation with `skip_powermode=1` and disables its existing `auto_boost_*` overrides with `-1`, while preserving unrelated settings. Any manual handoff should be performed with G Helper fully quit and its configuration backed up. These are version-sensitive G Helper settings; consult its [power-user documentation](https://github.com/seerge/g-helper/wiki/Power-user-settings). This repository does not distribute or automatically edit G Helper.

**AC bypass restores the Windows AC values Pulse owned; it does not reverse the global G Helper handoff.** To return automation fully to G Helper, quit Pulse, verify restoration, then restore the backed-up managed G Helper keys with G Helper closed. Do not overwrite unrelated later configuration changes. Existing local deployment recovery scripts/backups belong to that installation and are not included here because they contain machine-specific integration paths.

## Baseline settings while a source is owned

| Windows setting | DC | AC, only if opted in |
|---|---|---|
| Minimum active logical cores | 10% | 15% |
| Minimum processor state | 15% | 80% |
| Core parking policy | Standard | Standard |
| Long-thread scheduling policy | Automatic | Automatic |
| Short-thread scheduling policy | Automatic | Automatic |

The corresponding class-1 minimum settings are handled where supported/present. These values are Windows policy percentages, not a promise that physical clock frequency equals a percentage of advertised boost frequency. Scheduler guard OFF does not disable these pre-existing baseline settings.

## Configuration

Preferences live in `Pulse.ini` beside the executable. Missing AC/scheduler keys default to zero. UI button changes persist automatically. Do not copy someone else's INI or recovery journal.

```ini
[Preferences]
ManageAC=0
SchedulerEnabled=0
```

`ComputeApps` optionally accepts semicolon-separated exact executable basenames, for example `custom-builder.exe;custom-renderer.exe`. This extends the known candidate path; candidates still need measured CPU activity. It is a manual extension, not universal workload recognition.

## Recovery

1. Use Pause or Exit first. Pulse compares managed values before restoring the journaled snapshot.
2. If the controller terminated unexpectedly, its waiting companion attempts restoration. Inspect diagnostics and Windows settings afterward.
3. After the controller and guardian have stopped, `Pulse.exe --restore` attempts recovery from that installation's journal. A nonzero exit code is a failure requiring investigation.
4. Retain `state/restore.bin` and installation backups until recovery succeeds. Never replace a live journal with one from another machine or delete it merely to suppress an error.

The journal does not survive every possible system/storage failure. Restoration preserves distinguishable external changes, but cannot prove which program wrote an identical value. Unsupported APIs, inaccessible settings or a competing writer can make Pulse pause rather than maintain control.

## Known limitations

- Tested hardware coverage is one ASUS GA605WI / Ryzen AI 9 HX 370 laptop. Intel, multi-socket and broad OEM compatibility are unverified.
- Sustained-work discovery/settling is sampled and can take seconds. Very short jobs and unknown background tools may be missed.
- Browser navigation signals are not semantic page-load or playback decoding instrumentation.
- A historical visible-window test incurred high accessibility-query overhead. Close the UI to the tray for normal operation; see [validation](validation.md).
- No direct EPP, NPU, undervolting, package-power-limit or per-thread placement actuator is included.
- Binaries are unsigned; this snapshot does not promise crash-free operation or measured battery savings.
