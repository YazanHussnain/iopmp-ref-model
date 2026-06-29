# RISC-V IOPMP Test Plan

**Spec under test:** RISC-V IOPMP Architecture Specification, Version 0.8.2 (February 2026) - `2026-0209-iopmp.pdf`
**Target:** IOPMP C reference model (`libiopmp` / `libsystem` / `libtables`)
**Status:** Living document. Every test case is traced to a spec section and (eventually) to one or more generated C tests.

---

## 1. Purpose

This test plan enumerates, feature by feature, every observable behavior the IOPMP specification defines, and the cross-feature interactions that matter for correctness and security. It is the source of truth for the IOPMP **Test Generator** (a separate deliverable) that emits C test cases runnable on a CPU against the reference model.

The plan does **not** deviate from the spec flow: features are ordered as they appear in the spec (Chapter 2 concepts -> Chapter 3 protection -> Chapter 4 registers -> Chapter 5 extensions -> Appendix A application note / models). Each feature file lists all single-feature cases first, then a cross-combination subsection. File 18 collects the larger multi-feature scenarios.

---

## 2. How to read a test case

Each case is a table row with these columns:

| Column | Meaning |
|--------|---------|
| **Test ID** | Stable unique identifier. Never re-used or renumbered. |
| **Spec Ref** | Spec section / table / figure the behavior comes from. |
| **Test Condition** | Configuration and preconditions: params (`rrid_num`, `entry_num`, `md_num`, capability bits), table contents (SRCMD/MDCFG/entries), register state, lock state. |
| **Test Description** | The stimulus and steps: the transaction (`rrid`, `addr`, `len`, `type`) issued, or the register write/read performed. |
| **Expected Scenario** | The required result: `LEGAL`/`ILLEGAL`, error type (`etype`), matched entry index, register read-back values, interrupt/MSI/bus-error reaction, stall state. |
| **Gap** | `-` if the current C reference model does not yet implement this; blank otherwise. Drives both test generation and future model work. |

### Test ID scheme

- `IOPMP-<FEAT>-<NNN>` - single-feature case (e.g. `IOPMP-MATCH-007`).
- `IOPMP-<FEAT>-X<NN>` - a cross-combination case local to that feature file.
- `IOPMP-XC-<NNN>` - a multi-feature scenario in `18-cross-combinations.md`.

Feature codes: `REG, SRCMD, MDCFG, ENTRY, MATCH, ERR, LOCK, STALL, SPS, NPRIO, SUPP, MFR, MSI, HWCFG, APPN, MODEL, SYS, XC`.

---

## 3. Reference legend

### 3.1 Transaction types (`ENTRY_CFG` / `ERR_INFO.ttype`)

| Type | Code | Permission required |
|------|------|---------------------|
| Read | `0x01` | entry `r=1` |
| Write / AMO | `0x02` | entry `w=1` (AMO additionally needs `r=1`) |
| Instruction fetch | `0x03` | entry `x=1` |

### 3.2 Error types (`ERR_INFO.etype`, spec Table 2 / Table 7)

| etype | Meaning | Source |
|-------|---------|--------|
| `0x00` | No error | baseline |
| `0x01` | Illegal read access | baseline |
| `0x02` | Illegal write access / AMO | baseline |
| `0x03` | Illegal instruction fetch | baseline |
| `0x04` | Partial hit on a priority rule | baseline |
| `0x05` | Not hit any rule | baseline |
| `0x06` | Unknown RRID | baseline |
| `0x07` | Error due to a stalled transaction | extension (5.7) |
| `0x08`–`0x0D` | Reserved | - |
| `0x0E`–`0x0F` | User-defined error | baseline/IMP |

### 3.3 Address modes (`ENTRY_CFG.a`)

| Mode | Code | Base | Size |
|------|------|------|------|
| OFF | `0x0` | - | disabled |
| TOR | `0x1` | `prev_entry.addr << 2` (0 if index 0) | `(this.addr << 2) − base` |
| NA4 | `0x2` | `addr << 2` | 4 bytes |
| NAPOT | `0x3` | `(addr & ~tz) << 2` | `(tz_run+1) << 2` |

### 3.4 Implementation models (Appendix A.8)

| Model | `srcmd_fmt` | `mdcfg_fmt` | Notes |
|-------|------------|------------|-------|
| Full | 0 | 0 | full SRCMD + MDCFG; all extensions |
| Rapid-k | 0 | 1 | fixed k entries/MD, no MDCFG lookup; SPS ok |
| Dynamic-k | 0 | 2 | programmable k; SPS ok |
| Isolation | 1 | 0 | RRID i -> MD i; **no SPS**; ≤63 RRIDs |
| Compact-k | 1 | 1 | RRID i -> MD i + fixed k; **no SPS**; min area |

### 3.5 Field-behavior abbreviations (spec Table 1)

`WARL` write-any-read-legal · `WISS` write-1-set-sticky · `W1CS` write-1-clear-sticky-to-0 · `RW1C` read-status/write-1-clear · `DC` don't-care · `IMP` implementation-defined.

---

## 4. Feature file index

| # | File | Spec | Feature |
|---|------|------|---------|
| 1 | [01-registers-warl.md](01-registers-warl.md) | 4.1, 4.6 | INFO registers, reset, WARL/WISS/W1CS/RW1C semantics |
| 2 | [02-srcmd-table.md](02-srcmd-table.md) | 2.5, 4.5 | SRCMD Table - RRID->MD association |
| 3 | [03-mdcfg-table.md](03-mdcfg-table.md) | 2.6, 4.4 | MDCFG Table - MD->entry range |
| 4 | [04-entry-addr-modes.md](04-entry-addr-modes.md) | 2.4, 4.6 | Entry array & address-mode decode |
| 5 | [05-priority-matching.md](05-priority-matching.md) | 2.7 | Priority & matching logic |
| 6 | [06-error-capture-reactions.md](06-error-capture-reactions.md) | 2.8, 4.3 | Error capture & reactions |
| 7 | [07-config-protection-locks.md](07-config-protection-locks.md) | 3, 4.2 | Configuration protection / locks |
| 8 | [08-stall-safe-runtime.md](08-stall-safe-runtime.md) | 5.7, 5.1.2-3 | Stall mechanism & safe runtime config |
| 9 | [09-sps.md](09-sps.md) | 5.2 | Secondary Permission Setting |
| 10 | [10-non-priority-entries.md](10-non-priority-entries.md) | 5.3 | Non-priority IOPMP entries |
| 11 | [11-per-entry-suppression.md](11-per-entry-suppression.md) | 5.4 | Per-entry interrupt / bus-error suppression |
| 12 | [12-multi-fault-record.md](12-multi-fault-record.md) | 5.5 | Multi-Faults Record |
| 13 | [13-msi.md](13-msi.md) | 5.6 | Message-Signaled Interrupts |
| 14 | [14-hwcfg-capability.md](14-hwcfg-capability.md) | 4.1, 5.1.1, A | Capability discovery consistency |
| 15 | [15-appnote-features.md](15-appnote-features.md) | A.1-A.7 | no_w / no_x / xinr / rrid_transl / MD-indexed |
| 16 | [16-implementation-models.md](16-implementation-models.md) | A.8 | Implementation models |
| 17 | [17-multi-instance-system.md](17-multi-instance-system.md) | ref-model | Multi-instance system routing |
| 18 | [18-cross-combinations.md](18-cross-combinations.md) | - | Multi-feature cross scenarios |

---

## 5. Coverage / traceability matrix

Maps each spec section to the feature file(s) that cover it. (Test-ID-level traceability lives inside each file; this is the section-level roll-up.)

| Spec § | Title | File(s) |
|--------|-------|---------|
| 2.1 | Request-Role-ID and Transaction | 02, 05 |
| 2.2 | Requester/Receiver/Control Port | 17 |
| 2.3 | Memory Domain | 02, 03 |
| 2.4 | IOPMP Entry & Entry Array | 04 |
| 2.5 | SRCMD Table | 02 |
| 2.6 | MDCFG Table | 03 |
| 2.7 | Priority and Matching Logic | 05 |
| 2.8 | Error Reactions | 06 |
| 3.1 | SRCMD Table Protection | 07 |
| 3.2 | MDCFG Table Protection | 07 |
| 3.3 | Entry Protection | 07 |
| 3.4-3.5 | Lock summary / prelocked | 07 |
| 4.1 | INFO registers | 01, 14 |
| 4.2 | Configuration Protection Registers | 07 |
| 4.3 | Error Capture Registers | 06 |
| 4.4 | MDCFG Table Registers | 03 |
| 4.5 | SRCMD Table Registers | 02 |
| 4.6 | Entry Array Registers | 04 |
| 5.1.1 | HWCFG2 | 14 |
| 5.1.2-3 | MDSTALL / RRIDSCP | 08 |
| 5.1.4-5 | ERR_CFG / ERR_INFO extensions | 06, 08, 12, 13 |
| 5.1.6 | ERR_MFR | 12 |
| 5.1.7 | ERR_MSIADDR/H | 13 |
| 5.1.8-10 | SRCMD_R/W/X(+H) | 09 |
| 5.1.11 | ENTRY_CFG with extensions | 04, 11 |
| 5.2 | Secondary Permission Setting | 09 |
| 5.3 | Non-priority entries | 10 |
| 5.4 | Per-entry suppression | 11 |
| 5.5 | Multi-Faults Record | 12 |
| 5.6 | MSI Extension | 13 |
| 5.7 | Safe Runtime Configuration | 08 |
| A.1 | Write Protection Devices (no_w) | 15 |
| A.2 | Instruction Fetch Protection (no_x) | 15 |
| A.3 | Data Only Devices (xinr) | 15 |
| A.4 | SRCMD Table Reduction | 15, 16 |
| A.5 | Improper MDCFG behaviors | 03 |
| A.6 | MDCFG Table Reduction (k-entry) | 15, 16 |
| A.7 | Run Out of MDs (parallel/cascade, rrid_transl) | 15, 17 |
| A.8 | Implementation Models | 16 |

---

## 6. Test generation conventions (for the generator phase)

- Tests program the model via `libtables` helpers for setup speed, and via the MMIO register interface (`iopmp_write_reg`/`iopmp_read_reg`) where the test specifically exercises register semantics (WARL, locks, RW1C).
- Each test asserts using `test_utils.h` macros: `ASSERT_LEGAL`, `ASSERT_ERR(result, etype)`, `ASSERT_REG(iopmp, offset, expected)`.
- A test marked `- Gap` is generated but expected to be skipped/xfail until the model implements the feature.
- One generated C function per Test ID, named `test_<feat>_<nnn>()`, so failures map 1:1 to this plan.
