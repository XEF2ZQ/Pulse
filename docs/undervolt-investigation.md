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

These observations do not prove that the hardware is locked, that independent control is impossible, or that `600` has a particular undocumented meaning. They show that the proposed getter/argument/decoder combination has not established the required semantics on this firmware.

## Source review and conflicting assumptions

| Primary source | Relevant evidence | Limit on its use |
|---|---|---|
| [ZenStates-Core Strix settings](https://github.com/irusanov/ZenStates-Core/blob/master/Hardware/Smu/Settings/APUSettings1_Strix.cs) and [CPU implementation](https://github.com/irusanov/ZenStates-Core/blob/master/Cpu.cs) | Strix candidate getter `0xAF`; APU getter path uses a plain core index | The tested combination did not yield usable offsets here |
| [linux-corecycler hardware notes](https://github.com/Daaboulex/linux-corecycler/blob/main/docs/hardware.md) | Distinguishes all-core support from unresolved Strix per-core index mapping | Does not establish this laptop's independent P/E mapping |
| [g-helper-linux implementation](https://github.com/utajum/g-helper-linux/blob/master/src/Platform/Linux/RyzenSmu.cs) and [issue 84](https://github.com/utajum/g-helper-linux/issues/84) | Alternative `0xD5` getter mapping; an issue reports command acceptance on firmware 11.93.11.0 | Command acceptance is not numerical readback validation; this alternative was not executed locally |
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

The original captured report used the label `co_steps` for the signed interpretation and recorded `readback_complete=0`. The retained source now labels it `candidate_signed_low16` and separates transport completion, a candidate range check, and semantic verification. It always reports `semantic_readback_verified=0` and exits with code 20 after the research queries, even if an unverified result happens to fall within the draft range. That prevents an apparently plausible value from accidentally qualifying the production backend.

No automatic offset reapplication, periodic hardware probing, or undervolt setter was added to the running application. The probe adds no resident CPU overhead. Existing profile and scheduler tests are distinct from this manual hardware investigation; passing them does not validate undervolting.

## What remains before enabling Apply

1. Demonstrate a correct getter and decoder on the actual firmware, with reproducible numerical agreement to a known setting.
2. Establish the setter/getter selector mapping for every physical core, including the gap between IDs 3 and 8. Prove that one domain can change without changing the other.
3. Implement a constrained privileged helper, exclusive undervolt ownership with G Helper, and a recoverable original-value journal.
4. Verify a controlled write/readback/restore sequence, partial-failure restoration, and reset detection. Only then add optional low-duty reapplication based on actual readback.
5. Validate stability separately. A correctly accepted offset is not necessarily stable, and an AMD result does not certify Intel support or another laptop's firmware policy.

The remaining blocker is technical, not missing administrator approval. Enabling Apply based only on an acknowledgement, storing the requested number as a purported readback, or replacing independent P/E control with an undisclosed all-core write would not complete the requested feature.
