# RISC-V IOPMP Reference Model

A C reference model of the **RISC-V Input/Output Physical Memory Protection (IOPMP)** unit:
a software implementation of the IOPMP transaction-checking pipeline, register file,
configuration-protection locks, and the optional extensions and implementation models
defined by the specification. It is intended for spec study, conformance experimentation,
and as a golden model to validate RTL or other implementations against.

---

## 1. Introduction

The IOPMP governs which memory regions a bus initiator (identified by its **RRID** —
Requestor Role ID) may read, write, or fetch from. This project models that behavior
faithfully in portable C11:

- **`libiopmp`** — the IOPMP model: register MMIO, RRID → Memory-Domain (SRCMD) lookup,
  MD → entry-range (MDCFG) lookup, address-mode decode (OFF/TOR/NA4/NAPOT), priority and
  non-priority matching, permission checks, error capture, interrupt/MSI reactions, and
  the configuration-protection locks.
- **`libsystem`** — a thin multi-instance router that dispatches MMIO accesses and
  transactions to one of several IOPMP instances (parallel / cascading topologies).
- **`tests`** — a spec-traced, feature-categorized test suite (see §6).

**Specification:** RISC-V IOPMP Architecture Specification, **Version 0.8.2** (February 2026).

- Official spec repository: <https://github.com/riscv-non-isa/iopmp-spec>
- The `docs/testplan/` directory traces every modeled behavior to a spec section/table/figure.

> The model targets the behavior described in v0.8.2. Where the spec leaves a choice to the
> implementation (e.g. §A.5 improper-MDCFG handling, AMO reaction), the model picks one legal
> option and the test suite documents it.

---

## 2. Configuration parameters

An instance is created with `IopmpInit(&iopmp, &params)` where `params` is an
`IopmpParams_t` (see `libiopmp/include/iopmp_types.h`). The most relevant fields:

### Core sizing
| Field | Type | Meaning |
|-------|------|---------|
| `rridNum` | `uint16_t` | Number of supported RRIDs (1..65535). |
| `entryNum` | `uint16_t` | Number of protection entries (1..65535). |
| `mdNum` | `uint8_t` | Number of Memory Domains (1..64). |
| `entryOffset` | `uint32_t` | Byte offset of the entry array (0 → default `0x4000`). |
| `model` | `IopmpModel_t` | Implementation model (see §4). |

### Address modes & capabilities (HWCFG0)
| Field | Meaning |
|-------|---------|
| `torEn` | TOR address mode supported. |
| `addrhEn` | 64-bit addresses (`ENTRY_ADDRH` / `ERR_REQADDRH`). |
| `noErrRec` | Error-capture registers not implemented. |

### Formats (HWCFG3, Appendix A)
| Field | Meaning |
|-------|---------|
| `srcmdFmt` | SRCMD format: `0` baseline bitmap, `1` exclusive (RRID i → MD i), `2` MD-indexed PERM. |
| `mdcfgFmt` | MDCFG format: `0` programmable table, `1` fixed `k`, `2` programmable `k`. |
| `mdEntryNum` | Entries per MD (`k`) when `mdcfgFmt = 2`. |
| `noW` / `noX` | Global write- / instruction-fetch-disable. |
| `xinr` | Treat instruction fetches as data reads. |
| `rridTranslEn` / `rridTranslProg` | RRID translation supported / table is SW-programmable. |

### Extensions (HWCFG2)
| Field | Meaning |
|-------|---------|
| `hwcfg2En` / `hwcfg3En` | HWCFG2 / HWCFG3 registers present. |
| `stallEn` | Stall mechanism (MDSTALL/MDSTALLH/RRIDSCP). |
| `spsEn` | Secondary Permission Setting (requires `srcmdFmt = 0`). |
| `msiEn` | MSI interrupt delivery. |
| `multifaultEn` | Multi-Fault Record (ERR_MFR). |
| `peisEn` / `peesEn` | Per-entry interrupt / bus-error suppression. |
| `nonPrioEn` / `prioEntProg` / `prioEntry` | Non-priority entries and the priority boundary. |

### Implementation-defined behavior (spec §3.5, §4, App. A)
`mdcfglckResetF`, `entrylckResetF`, `entrylckHardwired`, `mdlckDisable`, `mdlckPreset`,
`mdlckhPreset`, `eidDisable`, `entryAddrhMask`, `entryPermWImpliesR`, `entryUserCfgEn`,
`rridIllegalVec`, `rridBypassVec`, `msiInjectWriteErr` — prelocked/hardwired reset state and
optional WARL/IMP hooks. See the struct comments for exact semantics.

---

## 3. Getting started & compilation

### Prerequisites
- A C11 compiler (GCC or Clang)
- CMake ≥ 3.16

### Build
```sh
cmake -S . -B build
cmake --build build
```
The build is warning-clean under `-Wall -Wextra -Werror`.

### Run the tests
```sh
cd build
ctest                       # run all suites
ctest --output-on-failure
ctest -R sps                # run a single suite by name match
```

### Use the model in your own code
```c
#include "iopmp.h"

IopmpState_t  iopmp;
IopmpParams_t params = { .rridNum = 4, .entryNum = 8, .mdNum = 2,
                         .torEn = true, .model = IOPMP_MODEL_FULL };
IopmpInit(&iopmp, &params);

IopmpWriteReg(&iopmp, REG_HWCFG0, HWCFG0_ENABLE_BIT);   /* enable checking */
/* ... program MDCFG / SRCMD / entries via IopmpWriteReg ... */

TxnResult_t r = IopmpCheckAccess(&iopmp, /*rrid=*/0, /*addr=*/0x1000, /*len=*/4,
                                 IOPMP_TXN_READ);
if (!r.legal) { /* inspect r.etype, r.entryIdx, r.suppressError, r.stalled */ }

IopmpDestroy(&iopmp);
```
Link against the `iopmp` static library (and `iopmp_system` for multi-instance routing).

### Repository layout
```
libiopmp/        IOPMP model library (single static lib: `iopmp`)
libsystem/       Multi-instance router (`iopmp_system`)
tests/           Feature-categorized, spec-traced test suite
docs/testplan/   The test plan: every behavior traced to a spec reference
```

---

## 4. Supported implementation models

Each model is a `(srcmd_fmt, mdcfg_fmt)` combination (spec Appendix A.8):

| Model | `srcmd_fmt` | `mdcfg_fmt` | SRCMD table | MDCFG table | SPS | Max RRID |
|-------|:-----------:|:-----------:|:-----------:|:-----------:|:---:|:--------:|
| **Full** (`IOPMP_MODEL_FULL`) | 0 | 0 | yes | yes | yes | 65535 |
| **Rapid-k** (`IOPMP_MODEL_RAPID_K`) | 0 | 1 | yes | no (fixed `k`) | yes | 65535 |
| **Dynamic-k** (`IOPMP_MODEL_DYNAMIC_K`) | 0 | 2 | yes | no (prog `k`) | yes | 65535 |
| **Isolation** (`IOPMP_MODEL_ISOLATION`) | 1 | 0 | no (i→i) | yes | no | 63 |
| **Compact-k** (`IOPMP_MODEL_COMPACT`) | 1 | 1 | no (i→i) | no (fixed `k`) | no | 63 |

---

## 5. Supported features

- **Address modes:** OFF, TOR, NA4, NAPOT; 32-bit and 64-bit (66-bit) addresses.
- **Matching:** priority (index-ordered) and **non-priority** entries; partial-hit detection;
  per-access permission (R/W/X, AMO = R∧W).
- **SRCMD formats:** baseline bitmap (`SRCMD_EN/ENH`), exclusive (RRID i → MD i),
  MD-indexed `SRCMD_PERM/PERMH` (2 bits/RRID, OR semantics).
- **MDCFG formats:** programmable table and fixed/programmable `k`-entry reduction.
- **Configuration protection / locks:** `SRCMD_EN.l`, `MDLCK/MDLCKH`, `MDCFGLCK`,
  `ENTRYLCK`, `ERR_CFG.l`; prelocked and hardwired reset configurations.
- **Error capture & reactions:** `ERR_INFO` (first-capture-wins, RW1C), `ERR_REQADDR/H`,
  `ERR_REQID`, `ERR_USER`; interrupt (`ie`) and bus-error (`rs`) reactions.
- **Secondary Permission Setting (SPS):** per-RRID R/W/X filtering layered on entry permission.
- **Stall / safe runtime reconfiguration:** `MDSTALL/MDSTALLH`, `RRIDSCP` cherry-pick,
  `stall_violation_en` faulting.
- **Per-entry suppression:** `sire/siwe/sixe` (interrupt) and `sere/sewe/sexe` (bus error),
  with the non-priority AND-reduction rule.
- **Multi-Fault Record (MFR):** `ERR_MFR` windowed SV log and `ERR_INFO.svc`.
- **MSI interrupt delivery:** `ERR_MSIADDR/H`, `msi_sel`, `msidata`, `msi_werr`.
- **Global protection:** `no_w`, `no_x`, `xinr`.
- **RRID translation / gateway:** `rrid_transl` tagging for cascaded IOPMPs.
- **Multi-instance systems:** MMIO-base routing and per-instance independence.

---

## 6. Test cases

Tests live in `tests/` and are **categorized by feature**, one file per test-plan document
in `docs/testplan/`. Each assertion is spec-compliant and traced to a spec reference.
The shared assertion macros are in `tests/test_utils.h`.

| Test file | Feature area |
|-----------|--------------|
| `test_01_registers_warl.c` | INFO registers, reset, WARL/WISS/W1CS/RW1C field semantics |
| `test_02_srcmd_table.c` | SRCMD table (RRID → MD association) |
| `test_03_mdcfg_table.c` | MDCFG table (MD → entry range) |
| `test_04_entry_addr_modes.c` | Entry array & address-mode decode |
| `test_05_priority_matching.c` | Priority & matching pipeline |
| `test_06_error_capture_reactions.c` | Error capture & interrupt/bus-error reactions |
| `test_07_config_protection_locks.c` | Configuration-protection locks |
| `test_08_stall_safe_runtime.c` | Stall mechanism & safe runtime reconfig |
| `test_09_sps.c` | Secondary Permission Setting |
| `test_10_non_priority_entries.c` | Non-priority entries |
| `test_11_per_entry_suppression.c` | Per-entry interrupt/bus-error suppression |
| `test_12_multi_fault_record.c` | Multi-Fault Record |
| `test_13_msi.c` | MSI interrupt delivery |
| `test_14_hwcfg_capability.c` | HWCFG capability ↔ register-presence consistency |
| `test_15_appnote_features.c` | Appendix-A features (no_w/no_x/xinr, formats, k-entry, rrid_transl) |
| `test_16_implementation_models.c` | The five implementation models |
| `test_17_multi_instance_system.c` | Multi-instance routing (parallel & cascade) |
| `test_18_cross_combinations.c` | Multi-feature end-to-end scenarios |

Each test executable is registered with CTest; run `ctest` from the `build/` directory.

---

## 7. Contributing

Contributions are welcome. Please read **[CONTRIBUTING.md](CONTRIBUTING.md)** for the
workflow, PR and issue templates, and the policy on AI-assisted contributions
(permitted, but human-reviewed). All participants are expected to follow the
**[Code of Conduct](CODE_OF_CONDUCT.md)**.

---

## License

See the repository `LICENSE` file (add one if not yet present).
