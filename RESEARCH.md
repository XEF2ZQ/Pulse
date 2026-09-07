# Pulse 1.1 research and design decisions

Reviewed 6 September 2026. These sources informed the design; Pulse does not include code copied from these projects.

Microsoft lists an Intel Gen3 Movidius 3700VC VPU in Surface Laptop Studio 2. Its documented Studio Effects applications include camera framing, background effects and eye contact. Windows Studio Effects documentation describes offloading camera/audio AI to an NPU. This does not establish that the Movidius chip selects Windows power modes or CPU boost settings. No public implementation establishing that proposed behavior was found in this review. Pulse therefore remains native C++ without an NPU runtime or trained classifier.

- [Surface Laptop Studio 2 features](https://support.microsoft.com/en-us/surface/models/surface-laptop-studio-2-features)
- [Windows Studio Effects](https://learn.microsoft.com/en-us/windows/apps/develop/windows-integration/studio-effects)

Intel Dynamic Tuning Technology is a separate OEM tuning framework that adjusts power and performance against platform constraints and can include AI/ML features. This supports the general idea of workload-responsive policy, but is not evidence that the Surface VPU implements it, or a portable interface for this ASUS AMD laptop.

- [Intel DTT overview](https://www.intel.com/content/www/us/en/support/articles/000102775/processors.html)

UXTU's published Adaptive Mode describes temperature monitoring and adaptive TDP limits. That operates in the hardware power-limit area already owned by G Helper here. Pulse instead attributes CPU work and changes supported Windows policy values. The reviewed PowerPlanSwitcher project illustrates lightweight tray-based switching, but its overview does not supply the requested voice-versus-build classifier.

- [UXTU README, Adaptive Mode](https://github.com/JamesCJ60/Universal-x86-Tuning-Utility/blob/master/README.md?plain=1)
- [PowerPlanSwitcher](https://github.com/SebastianBecker2/PowerPlanSwitcher)

Windows has richer thread QoS information and multimedia scheduling internally. A process CPU percentage alone does not reveal task intent. Pulse combines exact compute-tool names with actual CPU-time deltas, plus a conservative foreground fallback that excludes common browser/media/voice hosts. It does not infer that every busy process needs performance, and does not move threads between processor classes. Unknown applications and mixed-purpose hosts remain classification limitations.

- [Windows Quality of Service](https://learn.microsoft.com/en-us/windows/win32/procthread/quality-of-service)

Windows boost index 4 is commonly labeled Efficient Aggressive; Microsoft's table maps it to index 2 on supported control paths, including autonomous CPPC/PEP. It is not a guaranteed energy advantage. Pulse pairs index 4 with Balanced power mode for sustained compute, verifies the configured values through Windows APIs, and makes no claim that this directly fixes CPU frequency or limits watts.

- [PERFBOOSTMODE](https://learn.microsoft.com/en-us/windows-hardware/customize/power-settings/options-for-perf-state-engine-perfboostmode)
- [Windows power-slider customization and overlays](https://learn.microsoft.com/en-us/windows-hardware/customize/desktop/customize-power-slider)

G Helper exposes a global `skip_powermode` setting. The inspected mode-control source checks it when applying Windows mode. The existing integration keeps that global handoff; Pulse restores AC Windows values when AC control is disabled, while G Helper continues hardware profile control. It does not promise per-source re-enabling of G Helper Windows-mode automation, and avoids repeated configuration-file edits or elevated restarts on charger events.

- [G Helper power-user settings](https://github.com/seerge/g-helper/wiki/Power-user-settings)
- [G Helper mode-control implementation](https://github.com/seerge/g-helper/blob/main/app/Mode/ModeControl.cs)

The autonomous PassMark test uses its installed sample-script format and vendor-documented `/s` scripting interface, runs only `CPU_ALL`, exports a local CSV, and exits. Compilation testing invokes the existing MSVC compiler on a locally generated translation unit. Neither test uploads results.

- [PassMark scripting and export](https://forums.passmark.com/performancetest/5401-commandline-script-output-result)
