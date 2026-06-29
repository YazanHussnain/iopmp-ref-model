# 02 - SRCMD Table (RRID -> Memory Domain association)

**Spec:** §2.1 (RRID), §2.3 (Memory Domain), §2.5 (SRCMD Table), §4.5 (SRCMD_EN(s) / SRCMD_ENH(s)), §2.7 (unknown RRID).

The SRCMD Table is indexed by RRID. `SRCMD_EN(s).md[m]=1` associates MD `m` (0–30) with RRID `s`; `SRCMD_ENH(s).mdh[m]` covers MD 31–62. Lock bit `SRCMD_EN(s).l` (file 07) protects the entry. This file covers the association lookup and register field behavior for **Format 0 (baseline)**. Exclusive (fmt 1) and MD-indexed (fmt 2) formats live in files 15/16.

---

## 2.1 Basic RRID -> MD association

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-SRCMD-001 | §2.5, §4.5.1 | enable=1; SRCMD_EN(3).md = bit2 set; MD2 owns entry covering addr w/ r=1 | Read txn rrid=3 to that addr | LEGAL - RRID 3 associated with MD2 | |
| IOPMP-SRCMD-002 | §2.5 | SRCMD_EN(3).md = 0 (no MD) | Read txn rrid=3 | ILLEGAL etype=0x05 (no rule - no MD ⇒ no candidate entries) | |
| IOPMP-SRCMD-003 | §2.5 | RRID 0 associated with entries {0,1,2,3,4}; RRID 1 with {0,1,2,5,6} (spec Fig.2) | Issue txns from rrid 0 and rrid 1 to a region in entry 5 | rrid1 ⇒ candidate; rrid0 ⇒ entry5 not a candidate | |
| IOPMP-SRCMD-004 | §2.3 | One MD associated with multiple RRIDs (md bit set in SRCMD_EN(1) and SRCMD_EN(2)) | Txns from rrid 1 and rrid 2 to MD region | Both reach the same entries | |
| IOPMP-SRCMD-005 | §2.3 | One RRID associated with multiple MDs (md bits 0 and 3 set) | Txn to region owned by MD3 | Candidate set spans both MDs' entries | |
| IOPMP-SRCMD-006 | §2.5 | SRCMD_EN(s).md[m]=1 but MD m owns zero entries (MDCFG empty range) | Txn rrid=s | ILLEGAL etype=0x05 (no candidate entry active) | |

## 2.2 SRCMD_ENH - memory domains 31–62

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-SRCMD-007 | §4.5.1 | `md_num`=40; RRID 5 associated with MD 35 via SRCMD_ENH(5).mdh[4] | Txn rrid=5 to MD35 region | LEGAL - high MD association honored | |
| IOPMP-SRCMD-008 | §4.5.1 | `md_num`≤31 | Read SRCMD_ENH(s) | Not implemented; reads 0 (hardwired) | |
| IOPMP-SRCMD-009 | §2.5 | `md_num`=33; SRCMD_ENH(s).mdh bits for MD ≥33 (j+31≥33) | Write SRCMD_ENH to associate MD 33,34 | Bits for unimplemented MDs read 0; valid bits retained | |
| IOPMP-SRCMD-010 | §4.5.1 | md set in both SRCMD_EN.md (low) and SRCMD_ENH.mdh (high) | Txn to a low-MD region and a high-MD region | Both associations independently effective | |

## 2.3 SRCMD_EN field semantics

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-SRCMD-011 | §4.5.1 | SRCMD_EN(s).md is WARL [31:1] | Write md bitmap 0x0000_000A | Reads back 0x0000_000A (bits 1,3) | |
| IOPMP-SRCMD-012 | §4.5.1 | SRCMD_EN(s) bit0 = l (WISS) | Write SRCMD_EN(s)=0x1 | l=1 set; md unchanged | |
| IOPMP-SRCMD-013 | §4.5.1 | Unimplemented MD bits (md ≥ md_num) | Write all-ones to SRCMD_EN(s).md | Bits ≥ md_num read 0 (hardwired) | |
| IOPMP-SRCMD-014 | §2.5 | `rrid_num`=8 | Read SRCMD_EN(s) for s=0..7 | All implemented; s=8 not implemented | |

## 2.4 Unknown / illegal RRID

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-SRCMD-015 | §2.7 | `rrid_num`=8; enable=1 | Txn rrid=8 (≥ rrid_num) | ILLEGAL etype=0x06 (Unknown RRID) | |
| IOPMP-SRCMD-016 | §2.7 | `rrid_num`=8; rrid=100 | Txn rrid=100 | ILLEGAL etype=0x06 | |
| IOPMP-SRCMD-017 | §2.7 note | Implementation deems some rrid < rrid_num illegal (IMP) | Txn with that rrid | ILLEGAL etype=0x06 (legality is IMP-defined) | - |
| IOPMP-SRCMD-018 | §2.7 | rrid = rrid_num−1 (max legal) | Txn with valid association | LEGAL (boundary) | |

---

## Cross-combinations (file-local)

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-SRCMD-X01 | §2.5 + §2.7 | enable=1, rrid out of range AND addr in a protected region | Txn rrid≥rrid_num | etype=0x06 takes precedence (RRID checked before entries) | |
| IOPMP-SRCMD-X02 | §2.5 + §2.6 | RRID->MD set, but MDCFG improper for that MD | Txn rrid | Behavior per §A.5 (improper MDCFG); no candidate or IMP-defined | - |
| IOPMP-SRCMD-X03 | §2.5 + §3.1 | SRCMD_EN(s).l=1 (locked) | Write SRCMD_EN(s).md | Write rejected; association unchanged (see file 07) | |
| IOPMP-SRCMD-X04 | §2.5 + §3.1 | MDLCK.md[m]=1 | Write SRCMD_EN(s).md[m] for any s | Bit m rejected for all RRIDs | |
| IOPMP-SRCMD-X05 | §2.5 + §5.2 | SPS enabled; RRID associated w/ MD but SRCMD_W(s).md[m]=0 | Write txn to MD m region (entry w=1) | ILLEGAL - SPS restricts write even though associated (file 09) | - |
