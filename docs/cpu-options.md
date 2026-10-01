# CPU options — 1.4.1 preview

The fourth navigation page, **CPU options**, follows CPU undervolt. It exposes
Windows EPP and a per-source boost-disable override, with an **Idle states &
thresholds** subpage. These are working Windows policy controls. The separate
Intel/AMD register panels are capability disclosures: their disabled controls
are **not implemented register actuators** and do not report the hardware as off.

All new options default off. Existing classifier behavior, AC bypass, parking
preferences, adaptive placement and G Helper settings are preserved. This patch
does not implement undervolting, direct CC6 control or a 12/35 W SMU swap.

## Controls and precise scope

| Control | Implementation | What it does not establish |
|---|---|---|
| Custom EPP, independent AC/DC values | `PowerWriteACValueIndex` / `PowerWriteDCValueIndex`, range 0–100; both existing EPP classes use the selected source value | Actual MSR value, per-core EPP, or guaranteed override of inherited overlays/firmware |
| Disable CPU boost, independent AC/DC checkboxes | Requests boost index 0 even during Burst or Compute while Pulse owns that source | A frequency ceiling, a power limit or direct Intel Turbo/AMD HWCR modification |
| Windows idle demotion/promotion thresholds | Optional AC/DC percentages; existing base-plan values only | Intel auto-demotion/undemotion bits, CC6 enablement, or a measured energy improvement |
| Windows idle-state ceiling | Optional 0–3 OS state index, gated on restorable originals; 0 means no ceiling | Intel package C-state encoding or AMD CC6/PC6 selection |
| Intel Speed Shift, SpeedStep, C1E, package limit, six demotion/undemotion switches | Visible, disabled, explicitly unverified | A signed register backend or Intel hardware validation |
| AMD direct CPPC/boost, CC6, PC6, Global C-State Control, requested state and demotion/undemotion | Visible, disabled, explicitly unverified | A validated HX370 register map or firmware mailbox contract |

The local HX370 plan has explicit EPP and idle-threshold values. Its idle-state
ceiling is inherited, so that control is disabled locally. Pulse does not create
an override that its journal cannot remove exactly. An explicit value in another
plan makes the Windows ceiling editor available, but does not prove the platform
honors the request. Unsupported indices are not labelled as physical C-states.

**Apply** saves options and re-acquires the eligible source after restoring the
previous transaction. If Pulse is paused or bypassing AC, it saves preferences
for the next eligible control session. **Load defaults** edits only that page's
form until Apply. Applying the EPP/boost form preserves saved idle options;
applying idle options preserves saved EPP/boost options. Unsaved changes on the
other form are not implicitly applied. **Refresh status** reads Windows policy;
it does not claim a hardware sensor reading.

A checked boost-disable option takes precedence over the classifier's boost
request. The classifier can still request Best Performance for productive work;
the Overview boost label accurately reports that the option keeps boost off.
The custom EPP is a fixed source preference while owned, not a new adaptive
per-state EPP algorithm. Inherited overlays can take priority; Pulse reports
this limit instead of claiming a raw hardware override.

## Re-evaluation of the supplied research

The supplied document was treated as research to verify, not as an instruction
to execute register writes. Several premises require correction:

1. **Windows EPP and raw EPP use different scales.** Microsoft maps 0–100 policy
   values onto the 0–255 hardware preference. EPP is relevant when the platform
   implements the appropriate autonomous performance interface. An accepted
   policy is not a delivered-frequency guarantee.
   [Microsoft PerfEnergyPreference](https://learn.microsoft.com/en-us/windows-hardware/customize/power-settings/options-for-perf-state-engine-perfenergypreference).
2. **Boost labels do not imply distinct hardware algorithms everywhere.**
   Microsoft's table describes several indices as equivalent for particular
   ACPI/CPPC modes. Pulse retains its established 0/2/4 requests and makes no
   universal efficiency claim about index 4.
   [Microsoft PERFBOOSTMODE](https://learn.microsoft.com/en-us/windows-hardware/customize/power-settings/options-for-perf-state-engine-perfboostmode).
3. **Intel enable bits and lock bits must not be conflated.** SpeedStep's enable
   field is distinct from its lock field. For the documented processor tables,
   `MSR_PKG_CST_CONFIG_CONTROL` CFG lock protects bits 15:0, not necessarily every
   high demotion field. Scope, encodings and writable fields still require the
   exact model table. C1E switching is not an established low-risk latency win,
   and limiting deep idle can increase energy use. This patch implements no
   speculative C1E or package-state switching.
   [Intel SDM Volume 4](https://cdrdv2-public.intel.com/874253/335592-090-sdm-vol-4.pdf).
4. **A faster instruction is not an ownership protocol.** A direct MSR write
   would compete with Windows/OEM management unless access, per-CPU scope,
   power-source transitions, hotplug/resume, readback and restoration were
   validated. Pulse currently has no such general register-control backend.
   CPUID HWP/EPP or CPPC capability bits do not prove the interface is enabled
   or grant write access. AMD also has distinct MSR and shared-memory CPPC paths;
   absence of the MSR feature is not proof that all CPPC support is absent.
   [Linux kernel AMD P-State architecture](https://www.kernel.org/doc/html/latest/admin-guide/pm/amd-pstate.html).

## AMD CC6, PC6 and requested C-states

CC6 is a **core** deep-idle state; PC6 is a **package** state. Neither is a renamed
Intel C1E promotion bit. A permission to enter a deep state is also different
from forcing that state while useful work is running. Firmware and the OS choose
entry based on activity and platform constraints.

The original [ZenStates-Linux implementation](https://github.com/r4m0n/ZenStates-Linux/blob/master/zenstates.py)
uses `0xC0010296` bits 6/14/22 for core C6 and `0xC0010292` bit 32 for package C6.
This is evidence for that implementation, **not validation for family 1Ah model
24h Strix Point**. Copying the masks into a Windows writer would not establish
their meaning on the HX370, preservation of other fields, or firmware ownership.
No such register writes were attempted.

AMD SMI documents CC6-related CPU APIs, but its CPU support targets EPYC through
`esmi_ib_library` on its supported Linux platforms. It is not a supported
Windows/HX370 actuator. [AMD SMI installation and supported platforms](https://rocm.docs.amd.com/projects/amdsmi/en/latest/install/install.html).

The inspected [PawnIO IntelMSR module](https://github.com/namazso/PawnIO.Modules/blob/main/IntelMSR.p)
does not allow the HWP, EIST, POWER_CTL or package C-state registers needed here.
The current upstream [AMD family module](https://github.com/namazso/PawnIO.Modules/blob/main/AMDFamily17.p)
does allow the candidate CC6/PC6 and CPPC registers, unlike the locally examined
G Helper RyzenSMU transport. That is a potential access path, but not evidence
that the corresponding signed module is installed, that these fields have the
required semantics on Strix Point, or that Pulse can safely own them. Installing
PawnIO alone therefore does not make these controls available. This patch neither
broadens a signed module's allowlist nor installs a vulnerable driver or changes
driver-signing protections.

**Windows alternatives are deliberately separate.** Microsoft documents OS idle
promotion and demotion thresholds. The SDK also defines an idle-state maximum,
where zero leaves the ceiling unspecified. These guide the Windows idle engine;
they are not one-to-one representations of hardware demotion bits or physical
core/package states. Documentation and SDK comments differ in wording about
idleness versus busy thresholds, so the UI preserves the neutral policy names
and percentages instead of inventing a tuning rule.
[IdleDemoteThreshold](https://learn.microsoft.com/en-us/windows-hardware/customize/power-settings/options-for-perf-state-engine-idledemotethreshold),
[IdlePromoteThreshold](https://learn.microsoft.com/en-us/windows-hardware/customize/power-settings/options-for-perf-state-engine-idlepromotethreshold).

## Ownership, recovery and runtime cost

`cpu_options.h` contains bounded preferences, feature enumeration and the boost
override rule. `cpu_ui.inl` and `idle_ui.inl` contain the separate native editors.
`power.cpp` remains the single Windows power-policy writer. It captures only
opted-in settings, stores originals before writes, reads written values back,
and restores only values still matching Pulse's targets. Distinguishable
external changes are preserved; equal-value external writes remain ambiguous.

Journal version 4 adds stable setting indices 8–12. Its layout retains the
existing row format, and the reader accepts prior v1–v3 records. An older binary
must not be substituted while a v4 journal remains: use Restore & exit first.
The ordinary **Apply AC + battery** parking editor excludes CPU-options rows
from its permanent baseline transaction. CPU options stay temporary and
source-specific, including when parking profiles are saved.

Preferences live in `[CpuOptions]` of `Pulse.ini`. Writes use a temporary copy
and replacement, retaining unrelated sections. Invalid values fail to inactive
defaults. A failed Apply attempts to restore previous preferences and control;
unresolved recovery stays visible. No driver, new polling loop, sensor scan,
ETW session or background reapplication task is added. Discovery runs during UI
creation or explicit refresh; writes use existing ownership and source events.

## Validation — 1 October 2026

| Check | Result and limit |
|---|---|
| MSVC Release `/W4 /WX` | Compiled successfully |
| CTest | 10/10 passed: policy, persistence, existing demand/placement suites, topology and native GUI |
| New GUI assertions | Fourth tab, inactive hardware controls, malformed-input rejection, dry-run Apply, draft restoration, idle navigation and header button separation passed |
| Live EPP / idle thresholds | Non-default requests read back through Windows APIs; exact original values restored |
| Source isolation | DC-only and AC-only captures changed only owned source values; both restored with no journal left |
| Boost-disable precedence | Burst and Compute remained at boost index 0 on the active battery source |
| Preferences | Installed INI hash unchanged during the live exercise |
| Intel register operations | Not tested or implemented; no Intel machine attached |
| CC6 / PC6 / hardware demotion | Not written, not validated; no physical-state or energy claim |
| OS ceiling | Locally unavailable because its original is inherited; no live ceiling write |
| Visual inspection | Native automation runtime failed to initialize (`os error 3`) even after reset; no screenshot-based sign-off |

The manual `--cpu-options-exercise <report-path>` runs with the recovery companion
and without saving preferences. It tests both source masks, value restoration and
the active source's override precedence. The AC source was inactive during the
local run; this does not certify an actual charger transition or Intel hardware.
The live exercise is intentionally excluded from unattended CTest.

No package-power, wake-latency or battery-runtime improvement has been measured
for these new options. Direct hardware controls require a validated model map,
a constrained signed access path, ownership and recovery, and hardware testing
before their UI controls can truthfully become active.

The local desktop installation uses executable SHA-256
`9F96A590F698E75E0FBDB469CF3AE1E5C67C35746853970D46F5BFDEA219A057`.
The prior executable was backed up, existing preferences were preserved, and
the new options remain off. Local installation is separate from GitHub
publication; this patch does not claim a hosted CI run or release.
