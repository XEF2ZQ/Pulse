# Undervolt backend investigation

Status: **blocked on verified firmware readback and per-core addressing**. This is a research result, not a completed undervolt feature. The installed 1.4 preview provides editable power profiles and optional adaptive thread placement; its undervolt page remains a capability inspector and draft editor.

## Required behavior and acceptance criteria

The requested control has independent offsets for performance and efficient cores, applies the user's values, and optionally detects and corrects a later reset. On this AMD processor, those values represent Curve Optimizer steps, not millivolts. A successful mailbox response alone cannot satisfy the requirement: the controller must read actual offsets, establish which physical cores each selector addresses, and verify the result of each write.

CPU identity, the presence of PawnIO, or a supported all-core command does not establish independent core control. Similarly, a G Helper configuration value and an `OK` acknowledgement are not hardware readback. The CPU's lock status therefore remains **unverified**, rather than being labelled locked because this investigation did not find a usable getter.

## Local observations

The approved diagnostic ran on an AMD Ryzen AI 9 HX 370. CPUID reported AuthenticAMD, family 26 (0x1A), model 36 (0x24). The installed PawnIO transport opened successfully with administrator elevation. SMU firmware reported `0x0B5D0B00` (11.93.11.0), and the signed module identified Strix Point with code 31.

The probe temporarily pinned only its own thread to read CPUID topology, restoring its prior affinity after each read:

| Windows logical processors | OS efficiency class | CPUID physical core IDs |
|---|---|---|
| 0–7 | 1, higher class | 0, 1, 2, 3 |
| 8–23 | 0, lower class | 8, 9, 10, 11, 12, 13, 14, 15 |

This proves a gap in CPUID core IDs. It does **not** prove that an SMU setter uses those IDs. Sequentially treating the last eight cores as IDs 4–11 is consequently not a validated implementation either.

The candidate getter was RSMU command `0xAF`, using the Strix register addresses and APU plain-index convention from the upstream sources below. It was tested against all twelve physical core IDs. Every request returned mailbox status 1 and the same six words:

```text
0x00000258 0 0 0 0 0
```

Interpreting the first word using the proposed signed-low-16-bit Curve Optimizer convention produces **+600**, not a usable negative offset. All twelve requests completed, but actual offset readback was **not verified**. Earlier checks using the module's generic command helper and the explicit RSMU mailbox both returned the same value for core 0.

G Helper's existing log reported an acknowledged all-core setting of −47 during the investigation. That is contextual evidence about its requested setting, not independent proof of the applied hardware offset. Pulse's probe issued no Curve Optimizer setter, and this investigation did not change G Helper's configuration.

The follow-up below identifies why that interpretation failed. These observations do not prove that the hardware is locked or that independent control is impossible.

## Firmware-matched follow-up — 22 September 2026

The BIOS installed on this GA605WI is version 316. The official [ASUS support package](https://www.asus.com/us/supportonly/ga605wi/helpdesk_bios/) was downloaded and unpacked **offline**, without running a firmware updater. Its embedded main SMU version matches the live `0x0B5D0B00` response. The download's SHA-256 matched ASUS's support API metadata:

| Artifact | SHA-256 |
|---|---|
| `GA605WIAS316.zip` | `0F97A8B0204C207DF781D337C897D988D13024A3AC1D553FF19579FF2CC3F5C0` |
| Decompressed main SMU body, 393216 bytes | `92D3B77A027EAE84C02C07A508E831831660F3622811C679EAE2165F7EB8048F` |
| Decompressed CCX body, 196608 bytes | `D96AC821D6D22808198D88891B9487D966299E12CCFA906E2796C0DAC78F6964` |

These are reproducibility identifiers, not a claim that an extracted firmware signature was independently verified. Firmware and third-party analysis dependencies are not distributed in this repository. Offline inspection used PSPTool and Capstone's Xtensa decoder; undecoded instructions were marked unknown, not assigned guessed behavior.

The main command table begins at body offset `0x6438`. The following are **local reverse-engineering findings for this exact firmware**, not an AMD API contract:

| External request | Main handler | Internal CCX request | Observed operation |
|---|---|---|---|
| RSMU `0xAF` | `0x490A4` | `0x0F`, handler `0xCA28` | Evaluates a per-core curve; does not return the stored CO offset |
| RSMU `0xC3` | `0x49044` | `0x10`, handler `0xC988` | Alternate curve path that references the stored offset; live request was rejected |
| MP1 `0x4B` / RSMU `0x53` | `0x48E5C` | `0x47`, handler `0x1445C` | Stores a signed per-core offset |
| MP1 `0x4C` / RSMU `0x5D` | `0x48ED8` | `0x48`, handler `0x14498` | Stores a signed offset across both complexes |
| RSMU `0xD5` | `0x48CA0` | `0x1F` | Sends a state-changing request; **not an offset getter** |

For `0xAF`, complex and slot are selected by bits 31:28 and 27:24. The previous plain arguments 0–15 therefore selected the same core while changing the curve input. The per-core setter uses a different slot field, bits 27:20, and checks for fewer than eight slots in each of two complexes. A flat `core << 20` selector for core IDs 8–15 would fail this check. Translating CPUID IDs into those complexes remains a hypothesis until verified against actual per-core behavior.

The internal setters store a signed value at CCX-body address `0x1E678 + complex*0x246C + slot*0x350 + 0x3B0`. This establishes where the firmware stores the offset, **not** a supported host readback interface. The installed signed PawnIO module does not expose that SRAM region; its access restrictions were not bypassed.

The subsequent elevated, read-only live probe produced:

| Core selector | Request | Input | Mailbox status | Returned value |
|---|---|---:|---:|---:|
| Complex 0 / slot 0 | `0xAF` | 0 | 1 | 600 |
| Complex 0 / slot 0 | `0xAF` | 1000 | 1 | 5297 |
| Complex 0 / slot 0 | `0xAF` | 3000 | 1 | 21351 |
| Complex 0 / slot 0 | `0xC3` | 800 | 253 (`0xFD`) | No accepted result |

The probe stopped at the rejected request. This was **not** a completed twelve-core curve validation. It confirms that `600` came from the wrong command/argument interpretation, rather than proving a +600 CO setting or a transport error. Precise physical units and the reason for the `0xC3` rejection remain unverified. No voltage or CO setter was sent, and `0xD5` was deliberately not executed.

**Decision:** correct the diagnostic and retain the disabled Apply control. A voltage-curve response is not enough to recover original offsets, verify independent domains, or safely detect and correct a reset. No arbitrary opcode scan, firmware patch, or repeated write loop is justified by these results.

## Source review and conflicting assumptions

| Primary source | Relevant evidence | Limit on its use |
|---|---|---|
| [ZenStates-Core Strix settings](https://github.com/irusanov/ZenStates-Core/blob/master/Hardware/Smu/Settings/APUSettings1_Strix.cs) and [CPU implementation](https://github.com/irusanov/ZenStates-Core/blob/master/Cpu.cs) | Strix candidate getter `0xAF`; APU getter path uses a plain core index | The tested combination did not yield usable offsets here |
| [linux-corecycler hardware notes](https://github.com/Daaboulex/linux-corecycler/blob/main/docs/hardware.md) | Distinguishes all-core support from unresolved Strix per-core index mapping | Does not establish this laptop's independent P/E mapping |
| [g-helper-linux implementation](https://github.com/utajum/g-helper-linux/blob/master/src/Platform/Linux/RyzenSmu.cs) and [issue 84](https://github.com/utajum/g-helper-linux/issues/84) | Alternative `0xD5` getter mapping; an issue reports command acceptance on firmware 11.93.11.0 | Offline analysis contradicts this mapping on the matching firmware: it is a state-changing request and must not be used as a getter |
| [ZenMaster command tables](https://github.com/HorizonUnix/ZenMaster/blob/main/zenmaster/runner.py) | Additional mobile setter definitions and family dispatch | Does not supply the missing demonstrated independent-domain readback result |
| [PawnIO RyzenSMU module](https://github.com/namazso/PawnIO.Modules/blob/master/RyzenSMU.p) | Restricted module transport and shared PCI mutex convention | Transport access does not validate a firmware command's meaning |

These are reverse-engineered upstream implementations, not an AMD-supported firmware contract. No GPL implementation was copied into Pulse; the local diagnostic was written for this investigation. Protocol facts and references do not confer redistribution permission for third-party modules.

## Reproducible diagnostic and boundaries

`tools/uv_probe.cpp` builds as `PulseUvProbe` when `BUILD_TESTING` is enabled. It is a manual research executable, not a CTest test, installed service, or resident background task. Its inputs are a module path and a report path. It loads the already-installed PawnIO library; it does not install a driver or include a third-party module in the distribution.

The module content must match SHA-256:

```text
B84ECA7F32C63B3D8C14B2C6D45482706DF8683AA6F43EB8BEAD9DC62181D38F
```

The probe restricts CPU identity and command selection, bounds module size and mailbox waits, and acquires `Global\Access_PCI`. There is no arbitrary opcode or register input. The mailbox transaction necessarily writes argument and request registers to submit the candidate getter; “read-only” describes its intended operation on undervolt settings, not an absence of transport writes. The mutex coordinates only with clients using the same mutex; it cannot guarantee exclusion against every other hardware utility.

The original captured report used the label `co_steps` for the signed interpretation and recorded `readback_complete=0`. The retained source now labels results `curve_result`, requires exact firmware `0x0B5D0B00`, and permits only the fixed `0xAF`/`0xC3` queries. It verifies request registers before submission and the command register after completion. It always reports `semantic_readback_verified=0` and exits with code 20 after the research queries. A rejected query stops subsequent requests. That prevents an apparently plausible value from accidentally qualifying the production backend.

No automatic offset reapplication, periodic hardware probing, or undervolt setter was added to the running application. The probe adds no resident CPU overhead. Existing profile and scheduler tests are distinct from this manual hardware investigation; passing them does not validate undervolting.

## What remains before enabling Apply

1. Demonstrate a correct getter and decoder on the actual firmware, with reproducible numerical agreement to a known setting.
2. Establish the setter/getter selector mapping for every physical core, including the gap between IDs 3 and 8. Prove that one domain can change without changing the other.
3. Implement a constrained privileged helper, exclusive undervolt ownership with G Helper, and a recoverable original-value journal.
4. Verify a controlled write/readback/restore sequence, partial-failure restoration, and reset detection. Only then add optional low-duty reapplication based on actual readback.
5. Validate stability separately. A correctly accepted offset is not necessarily stable, and an AMD result does not certify Intel support or another laptop's firmware policy.

The remaining blocker is technical, not missing administrator approval. Enabling Apply based only on an acknowledgement, storing the requested number as a purported readback, or replacing independent P/E control with an undisclosed all-core write would not complete the requested feature.
