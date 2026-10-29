# Weak-charger recognition — 1.4.3 preview

## Problem and behavior

The test ASUS HX 370 laptop reports `ACLineStatus=0` with a weak USB-C charger connected. The battery can nevertheless be charging, or supply part of the load. Pulse previously relied on that Windows AC flag and therefore kept its battery profile.

**Efficiency options → Use AC profile with a weak charger** is now enabled by default (`Preferences/RecognizeWeakCharger=1`, including when absent). Recognized external supply selects Pulse's AC profile even while Windows continues to select DC policy. The UI displays **WEAK CHARGER**. Disabling the option preserves ordinary Windows-source behavior; the selection is remembered.

This identifies a Windows/charger-state mismatch, not the charger's negotiated wattage. It does not change the USB power contract, increase charger output, or guarantee the battery will charge under load.

## Evidence and event ownership

`src/power_source.h` separates physical supply evidence from the Windows policy slot. Normal Windows AC is authoritative. When Windows reports DC:

1. On supported ASUS hardware, the read-only DSTS charger query (`0x0012006C`) supplies charger presence. Known low bits distinguish barrel and USB supplies. Presence, output size and supported result bits are validated; unknown responses are rejected.
2. This cached ASUS result is used only when its disconnect-event subscription succeeded. A fresh disconnected ASUS result wins over a lagging Windows charging flag.
3. Without that supported path, the live Windows **charging** flag is positive external-supply evidence. A plain USB peripheral, battery discharge rate, or historic connection is not sufficient. On other hardware, a connected charger that supplies insufficient power and exposes neither signal remains undetected.
4. Unknown/failed Windows source reads block new power-control acquisition. The detector does not turn missing data into AC.

The ASUS handle requests no read/write file access; its only device control operation is the fixed read-only charger query. No INIT, DEVS, UCSI command or firmware-setting operation is sent. The interface identifiers and charger bits are independently documented by the [Linux ASUS platform header](https://github.com/torvalds/linux/blob/master/include/linux/platform_data/x86/asus-wmi.h); G Helper uses the same charger property. Pulse's implementation is original C++ and does not embed G Helper code.

An asynchronous, filtered extrinsic WMI subscription (`AsusAtkWmiEvent`, EventID 123) reports ASUS charger changes. There is no WQL `WITHIN` polling. Callbacks only post a refresh request; a 150 ms coalesced timer reads the current status and a one-shot two-second refresh accounts for negotiation settling. Startup, resume, Windows power-source and device changes also refresh evidence. There is no steady-state USB/ACPI polling timer. A terminal notification failure invalidates the ASUS override; the Windows charging-flag fallback remains available. Diagnostics expose both read and subscription errors.

Windows' UCSI stack knows slow/trickle charging states, but its driver-to-driver interfaces are not a generic user-mode port API. We use the verified read-only ASUS signal on this machine rather than inject UCSI commands into an active controller. [Microsoft UCSI architecture](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/ucsi), [slow-charger notification conditions](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/usb-type-c-troubleshooting-notifications).

## Applying the profile Windows actually uses

Changing the displayed source or writing unused AC values would not change behavior while Windows selects DC. During the mismatch, Pulse therefore captures the real **DC** settings and temporarily maps the configured AC parking, minimum-state, thread policies and enabled CPU options into those DC destinations. AC values themselves are not modified. Exact original DC values and intended DC targets remain in the existing recovery journal.

- **Plugged-in control OFF:** apply the configured AC baseline, the captured AC boost setting (unless the AC disable-boost option takes precedence), and the captured AC power-mode preference. Adaptive CPU hooks are not started for this passthrough state.
- **Plugged-in control ON:** use the same AC baseline and AC CPU options with the normal adaptive burst/compute controller. Writes still target DC while Windows selects DC.
- **Unplug, real AC-source change, Pause, option disabled, or exit:** release the temporary mapping through the existing restore path before acquiring the new source. Newer conflicting external values follow the existing ownership rules. Guardian recovery retains its existing crash-restoration role.

Logical charger changes and actual Windows AC/DC changes are tracked separately, including a weak-to-normal supply transition where logical external power stays true. Profile Apply clears the temporary mapping before persisting user profile changes. The PCIe battery guard remains independently tied to Windows DC, retaining maximum DC ASPM as previously requested.

This does not switch G Helper's fan, GPU or hardware power-limit profile. G Helper retains those controls. A competing power writer can still cause Pulse's normal conflict pause; this patch does not continuously fight another CPU policy manager.

## Validation — 23 October 2026

- Initial live snapshot: Windows AC offline, battery flag 10 (low + charging), battery driver state 4, positive rate about 14–16.5 W; ASUS raw response 65542, mode 6 (USB present). No administrator UAC was needed for the ASUS query.
- User unplugged/reconnected the weak charger during a read-only event capture. ASUS mode changed **6 → 0 → 6**, and detection changed **external → battery → external** while Windows AC remained 0 throughout. Reconnection was recognized with battery flag 2, before Windows indicated charging again.
- The 180-second event capture performed **five ASUS reads**: startup plus two reads per connection event. No reads were added during the remaining quiet interval. This is an observed notification/read count, not a measured battery-energy result.
- **20 source-classification checks** cover charging fallback, discharge with USB attached, charge-limit idle, unplug, stale charging flags, normal AC, unknown source, truncated/unsupported ASUS responses and the observed mode 6 response.
- All **12 native CTest suites** passed in Release with warnings treated as errors.
- Explicit `--charger-exercise` tested both passthrough and adaptive mappings on the real Windows plan using distinct AC/DC targets: AC cores 23 / minimum 67 / EPP 43 versus DC cores 19 / minimum 21 / EPP 81. It verified readback, AC boost-disable precedence, unchanged AC values/mode, exact DC restoration and removal of the recovery journal. All returned success.

The event trace and transaction report remain local under `work/weak-charger-events-20261023.txt` and `work/weak-charger-transaction-20261023.txt` in the parent workspace. These do not establish universal USB-C support, charger wattage, an upper latency bound, reboot/suspend coverage or improved battery life. The source probe and listener are standalone diagnostic tools, not startup programs.
