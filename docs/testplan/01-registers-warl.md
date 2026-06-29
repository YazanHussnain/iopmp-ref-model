# 01 - INFO Registers, Reset & Field-Behavior Semantics

**Spec:** §4 (Registers), §4.1 (INFO registers), §4.6 (Entry Array Registers), Table 1 (field behaviors), Table 3 (register summary).

Covers the fixed register file: VERSION, IMPLEMENTATION, HWCFG0/1, ENTRYOFFSET, and the WARL / WISS / W1CS / RW1C / ZERO / reserved field semantics shared by all registers. Capability-bit *consistency* across HWCFG0/1/2/3 lives in file 14; this file checks raw register read/write behavior and reset state.

---

## 1.1 Reset & default state

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-REG-001 | §4.1.1 | Instance init, any params | Read VERSION (0x0000) | `vendor` = configured JEDEC ID; `specver` = configured (e.g. 0x82 for v0.8.2 layout: minor in [31:28], major in [27:24]); read-only | |
| IOPMP-REG-002 | §4.1.2 | Instance init | Read IMPLEMENTATION (0x0004) | Returns configured `impid`; read-only | |
| IOPMP-REG-003 | §4.1.3 | `enable` programmable & init=0 | Read HWCFG0.enable after reset | `enable` = 0 (sticky-to-1, must init 0 if programmable) | |
| IOPMP-REG-004 | §4.1.3 | `enable` hardwired implementation | Read HWCFG0.enable | `enable` wired to 1 | |
| IOPMP-REG-005 | §4.3.2 | After reset, no violations | Read ERR_INFO.v | `v` = 0 (default) | |
| IOPMP-REG-006 | §4.2 | After reset, no lock writes | Read MDLCK/MDCFGLCK/ENTRYLCK | All lock `.f`/`.md` = reset value (0 unless prelocked) | |
| IOPMP-REG-007 | §4.6.2 | After reset | Read ENTRY_CFG(i).a for all i | Reset = DC per spec; model uses OFF(0) so inactive | |
| IOPMP-REG-008 | §4.1.8 | ENTRYOFFSET placed after VERSION | Read ENTRYOFFSET (0x002C) | Signed two's-complement offset to entry array base; matches configured layout | |
| IOPMP-REG-009 | §4.1.8 | Entry array placed *before* VERSION | Read ENTRYOFFSET | Negative (two's-complement) value is read back unchanged | - |

## 1.2 HWCFG0 / HWCFG1 read-only capability fields

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-REG-010 | §4.1.3 | `md_num`=N configured | Read HWCFG0[29:24] | Returns N; write attempt has no effect (R) | |
| IOPMP-REG-011 | §4.1.3 | `tor_en`=1 | Read HWCFG0[31] | = 1; read-only | |
| IOPMP-REG-012 | §4.1.3 | `tor_en`=0 | Read HWCFG0[31] | = 0; TOR unsupported (see file 04) | |
| IOPMP-REG-013 | §4.1.3 | `addrh_en`=1 | Read HWCFG0[30] | = 1 (ENTRY_ADDRH/ERR_REQADDRH available) | |
| IOPMP-REG-014 | §4.1.3 | `no_err_rec`=1 | Read HWCFG0[23] | = 1 (error-record regs not implemented) | |
| IOPMP-REG-015 | §4.1.3 | `HWCFG2_en`/`HWCFG3_en` configured | Read HWCFG0[1]/[2] | Match presence of HWCFG2/HWCFG3 | |
| IOPMP-REG-016 | §4.1.4 | `rrid_num`=R | Read HWCFG1[15:0] | = R; read-only | |
| IOPMP-REG-017 | §4.1.4 | `entry_num`=E (>0) | Read HWCFG1[31:16] | = E; spec requires E>0 | |
| IOPMP-REG-018 | §4.1.3 | HWCFG0[22:3] reserved | Write 0xFFFFFFFF to HWCFG0 | rsv bits read 0; reserved must-be-zero | |

## 1.3 WARL behavior (write-any, read-legal)

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-REG-019 | §4.6.2, Table1 | ENTRY_CFG(i) WARL, all of r/w/x/a programmable | Write a=0x3 (NAPOT) | Reads back 0x3 (legal) | |
| IOPMP-REG-020 | §4.6.2 | ENTRY_CFG(i).a WARL, TOR unsupported (`tor_en`=0) | Write a=0x1 (TOR) | Reads back a legal value (not TOR); illegal combo not retained | |
| IOPMP-REG-021 | §4.6.2 | Implementation hardwires entry.w=0 (read-only device) | Write w=1 | Reads back w=0 (WARL keeps legal) | |
| IOPMP-REG-022 | §4.6.2 | ENTRY_CFG.rsv [31:5] = ZERO | Write 0xFFFFFFFF to ENTRY_CFG(i) | rsv reads 0 | |
| IOPMP-REG-023 | §4.4.1 | MDCFG(m).t WARL [15:0], rsv [31:16] ZERO | Write 0xFFFFFFFF | t[15:0] retains legal value; [31:16]=0 | |

## 1.4 WISS - write-1-set-sticky-to-1

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-REG-024 | §4.1.3, Table1 | HWCFG0.enable is WISS & programmable, currently 0 | Write enable=1 | enable reads 1 | |
| IOPMP-REG-025 | §4.1.3 | enable=1 (set) | Write enable=0 | enable stays 1 (sticky) | |
| IOPMP-REG-026 | §4.5.1 | SRCMD_EN(s).l is WISS, currently 0 | Write SRCMD_EN(s).l=1 | l reads 1 | |
| IOPMP-REG-027 | §4.5.1 | SRCMD_EN(s).l=1 | Write l=0 | l stays 1 (sticky until reset) | |
| IOPMP-REG-028 | §4.2.1 | MDLCK.l is WISS | Write MDLCK.l=1 then 0 | l latches 1, stays 1 | |

## 1.5 W1CS - write-1-clear-sticky-to-0 (and ZERO fields)

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-REG-029 | §5.1.1, Table1 | HWCFG2.prio_ent_prog W1CS, reset=1 | Write prio_ent_prog=1 | clears to 0, sticky (prio_entry now fixed) | - |
| IOPMP-REG-030 | §5.1.1 | prio_ent_prog already 0 | Write 0 | no effect, stays 0 | - |
| IOPMP-REG-031 | §A HWCFG3 | rrid_transl_prog W1CS reset=1 | Write 1 | clears to 0; rrid_transl now fixed | - |

## 1.6 RW1C - read-status / write-1-clear (error valid)

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-REG-032 | §4.3.2 | ERR_INFO.v=1 (a violation captured) | Read ERR_INFO | Returns v=1 plus ttype/etype | |
| IOPMP-REG-033 | §4.3.2 | ERR_INFO.v=1 | Write ERR_INFO.v=1 | v clears to 0; recorder re-armed | |
| IOPMP-REG-034 | §4.3.2 | ERR_INFO.v=1 | Write ERR_INFO with bit0=0 | No effect; v stays 1 (write-0 ignored) | |
| IOPMP-REG-035 | §4.3.2 | ERR_INFO.ttype/etype | Attempt to write ttype/etype | Read-only; unchanged | |

## 1.7 Address decoding & out-of-range register access

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-REG-036 | §4 Table3 | `entry_num`=E | Read ENTRY_ADDR(E) (index = entry_num, out of range) | Not implemented; read returns 0 / implementation-defined, no crash | |
| IOPMP-REG-037 | §4.5 | `rrid_num`=R | Access SRCMD_EN(R) (out of range) | Not implemented region; reserved behavior | |
| IOPMP-REG-038 | §4 | Optional reg absent (`HWCFG2_en`=0) | Read HWCFG2 (0x0010) | Implementation-defined (model returns 0) | |
| IOPMP-REG-039 | §4 | Reserved offset (e.g. 0x0018) | Read/Write | Reserved; no functional effect | |
| IOPMP-REG-040 | §4 | Word-addressed access | Read each defined offset / 4 alignment | Correct register decoded by offset | |

---

## Cross-combinations (file-local)

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-REG-X01 | §4.1.3 + §4.6 | enable=0 (after reset) | Issue any transaction (file 05 path) | LEGAL - IOPMP bypassed while enable=0 | |
| IOPMP-REG-X02 | §4.1.3 + §3 | enable WISS set to 1, then locks applied | Confirm enable=1 cannot be cleared and gating now active | enable stays 1; checks enforced | |
| IOPMP-REG-X03 | §4.1.4 + §3.2 | `entry_num`=E, ENTRYLCK.f write > E | Write ENTRYLCK.f = E+5 | Clamped: all entries locked (f≥entry_num ⇒ all) | |
| IOPMP-REG-X04 | §4.1.3 + §4.3 | `no_err_rec`=1 | Cause a violation, read ERR_INFO | Error-record regs not implemented; ERR_REQID.eid wired 0xffff | - |
