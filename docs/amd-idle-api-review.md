# Decision: AMD SMI and the proposed Strix deep-idle writes

Reviewed 1 October 2026. **No new hardware actuator is enabled by this review.**
The supplied AMD SMI link identifies real functionality, but the accompanying
generated register recipes do not establish a working Windows/HX370 interface.
This is a compatibility investigation, not a successful hardware patch or an
energy benchmark. The installed Pulse 1.4.1 executable remains unchanged.

## Platform checked

Read-only Windows registry queries returned:

| Property | Value |
|---|---|
| Processor | AMD Ryzen AI 9 HX 370 |
| Family / model / stepping | 26 / 36 / 0 decimal; 1Ah / 24h / 0 hexadecimal |
| Firmware | GA605WI.316 |
| Windows | 25H2, build 26200.9457 |

These identify the target. They do not measure enabled C-states or residency.
This review did not send a firmware mailbox command, write an MSR/PCI register,
install a driver, alter power preferences or interrupt G Helper.

## What the AMD link actually provides

The [AMD SMI changelog](https://rocmdocs.amd.com/projects/amdsmi/en/develop/reference/changelog.html)
lists CPU Data Fabric C-state, PC6 and CC6 controls, with corresponding queries.
Its experimental WSL addition is a **GPU** backend; it does not establish native
Windows CPU support. The [CPU API reference](https://rocm.docs.amd.com/projects/amdsmi/en/latest/reference/amdsmi-py-api.html)
documents per-socket CC6/PC6 controls and explicit unsupported-feature errors.

AMD's [E-SMI project](https://github.com/amd/esmi_ib_library) describes its CPU
backend as a Linux EPYC library using the `amd_hsmp` driver. Its documented
family-1Ah support range is models 00h–1Fh, which does not include this model 24h.
This is evidence against using that supported stack on this laptop, not proof
that the silicon lacks deep idle. A Windows driver with arbitrary access would
not supply the missing model-specific firmware contract.

The actual [E-SMI implementation](https://github.com/amd/esmi_ib_library/blob/d494a3194ceb4cc4dbb2debf9fcbe8773c6d3bef/src/e_smi.c)
checks command support and uses HSMP transfers for these operations. Its queries
set argument bit 31; writing zero as a supposedly harmless query would instead
select the setter. Protocol identifiers must not be sent to Pulse's RyzenSMU
transport: different mailboxes may assign different operations to the same ID.

| Feature | HSMP identifier in AMD's header | Status for this Pulse target |
|---|---|---|
| Data Fabric C-state control | `0x28` | No verified Windows/Strix transport and contract |
| Package C6 enable | `0x29` | No verified Windows/Strix transport and contract |
| Core C6 enable | `0x2A` | No verified Windows/Strix transport and contract |

The pinned [AMD HSMP header](https://github.com/amd/amd_hsmp/blob/6654c067d8c0c7d8d723c0636c01153e4abdabcb/amd_hsmp.h)
also specifies that CC6 enablement does not itself transition the cores into or
out of CC6. PC6/CC6 queries report the last control value configured through
HSMP/APML, not a residency measurement. Thus even a successful supported write
would not substantiate an immediate whole-laptop discharge reduction.

## Corrections to the pasted recipes

| Proposal | Finding | Decision |
|---|---|---|
| `PowerSetRequest(PowerRequestExecutionRequired)` as a performance/C-state switch | Microsoft defines a process-lifetime request, not a boost or core-idle control | Do not add it to the classifier |
| HWCR `0xC0010015`, bit 15, described as Zen 5 C1E | No matching Strix register definition supplied or found; AMD's older definition names this bit SSEDIS | Reject this write |
| PCI `00:18.4`, offset `0x94`, bit 0, as a DF enable | The text calls the address an example and provides no matching PPR, device identity, mask or ownership rules | Reject this write |
| Linux `/dev/cpu_dma_latency = 0` | A CPU latency QoS constraint, not a Windows API or a package-sleep command | Do not port it as a power-saving switch |
| Linux `amd_pstate` EPP tuning | A Linux policy interface; Pulse already offers optional Windows EPP | Keep the existing supported Windows path |

[Microsoft's PowerSetRequest contract](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-powersetrequest)
concerns execution/sleep requests. Preventing suspension is different from
preventing a core from idling between runnable work. Adding this request would
not implement the claimed C-state policy.

AMD's [Family 0Fh BKDG, publication 32559, printed pages 400–401](https://www.amd.com/content/dam/amd/en/documents/archived-tech-docs/programmer-references/32559.pdf)
explicitly assigns HWCR bit 15 to SSE-instruction disable. This old table is
**not** a claim about the current meaning of every Strix bit. It is a concrete
reason not to accept the supplied universal C1E interpretation. No experimental
write is justified by that recipe.

The [Linux PM QoS documentation](https://www.kernel.org/doc/html/latest/power/pm_qos_interface.html)
defines the device as a latency constraint retained while its handle is open.
Restricting eligible idle states is not the same as actively driving a package
into deeper sleep, and the mechanism is not available through Windows user mode.

## Validation and the limit of backtesting

Completed: target identity check, primary-source contract review, pinned HSMP
header/implementation inspection, and review against Pulse's existing policy
writer and disabled hardware panels. No implementation was added or advertised
as working; no new CTest or energy result is claimed for this review.

Historical Pulse logs can replay classification decisions. They cannot prove a
new register's meaning, write support, safe restoration, physical residency or
the counterfactual battery draw of a policy that was never applied. Running an
A/B benchmark with both sides using the same unchanged actuator would be invalid.

Reconsider integration when a matching Strix model/firmware contract and a
constrained Windows access path exist. First verify read semantics and ownership,
then a single reversible change with exact restoration. Only then compare
counterbalanced baseline/treatment runs using package energy, independently
timestamped battery readings, useful-work duration and wake/boost-return latency.
Report the sensor update interval and EC filtering separately. A success response
alone is neither a power measurement nor a performance result.

For reproducibility, the downloaded reference files were hashed locally:

| Reference | SHA-256 |
|---|---|
| E-SMI `e_smi.c`, commit `d494a3194ceb4cc4dbb2debf9fcbe8773c6d3bef` | `2590923068978C6EE1874FA8A2CCBA790751FE482156E5FBA695E6AFD356A77C` |
| HSMP `amd_hsmp.h`, commit `6654c067d8c0c7d8d723c0636c01153e4abdabcb` | `BCDD26EB1CC95A4723E9D216FBE644EDF639A504AE367D76935A7DE77F11C7C1` |

Upstream implementations are references only, not copied into Pulse's runtime.
See [CPU options](cpu-options.md) for the implemented Windows controls and their
previous apply/readback/restoration evidence.
