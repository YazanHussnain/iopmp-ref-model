# 15 - Application-Note Features

**Spec:** Appendix A - A.1 (Write Protection / no_w), A.2 (Instruction-Fetch Protection / no_x), A.3 (Data-Only Devices / xinr), A.4 (SRCMD Table Reduction: source-enforcement, Exclusive, MD-indexed), A.5 (improper MDCFG - covered in file 03), A.6 (MDCFG reduction / k-entry), A.7 (Run out of MDs: parallel/cascade, rrid_transl), plus HWCFG3 and SRCMD_PERM(m)/SRCMD_PERMH(m).

These are optional, implementation-specific reductions/extensions. The implementation-model *combinations* are file 16; this file tests each appendix feature in isolation.

---

## 15.1 Global write protection - no_w (§A.1)

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-APPN-001 | §A.1 | HWCFG3.no_w=1 | Write txn to a region whose entry has w=1 | ILLEGAL etype=0x05 (not hit any rule) - global deny overrides entry | - |
| IOPMP-APPN-002 | §A.1 | no_w=1 | Read txn (entry r=1) | LEGAL (only writes globally denied) | - |
| IOPMP-APPN-003 | §A.1 | no_w=1 | AMO txn | ILLEGAL etype=0x05 (AMO is a write) | - |
| IOPMP-APPN-004 | §A.1 | no_w=0 | Write txn | Normal per-entry checking (baseline) | |
| IOPMP-APPN-005 | §A.1 | no_w read-only param | Write HWCFG3.no_w | No effect (R) | - |

## 15.2 Global instruction-fetch protection - no_x (§A.2)

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-APPN-006 | §A.2 | HWCFG3.no_x=1 | Exec txn (entry x=1) | ILLEGAL etype=0x05 (global fetch deny) | - |
| IOPMP-APPN-007 | §A.2 | no_x=1 | Read/Write txns | Unaffected; per-entry checking | - |
| IOPMP-APPN-008 | §A.2 | no_x=0 | Exec txn | Per-entry x checking (baseline) | |

## 15.3 Instruction-fetch-as-read - xinr (§A.3)

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-APPN-009 | §A.3 | HWCFG3.xinr=1; entry r=1,x=0 | Exec txn | LEGAL - fetch checked against read permission | - |
| IOPMP-APPN-010 | §A.3 | xinr=1; entry r=0 | Exec txn | ILLEGAL etype=0x01 (treated as read) | - |
| IOPMP-APPN-011 | §A.3 | xinr=1 | x-related fields (entry.x, sixe, sexe) | May be fixed 0 / unavailable | - |
| IOPMP-APPN-012 | §A.3 | xinr=0 | Exec txn | Checked against x permission (baseline) | |

## 15.4 Exclusive SRCMD format - srcmd_fmt=1 (§A.4.2)

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-APPN-013 | §A.4.2 | srcmd_fmt=1; no SRCMD table | Txn rrid=i | RRID i associated **exclusively** with MD i (1:1) | - |
| IOPMP-APPN-014 | §A.4.2 | srcmd_fmt=1 | Read SRCMD_EN(s) | SRCMD table not implemented | - |
| IOPMP-APPN-015 | §A.4.2 | srcmd_fmt=1; supports ≤63 RRIDs | Txn rrid=i to MD i region | LEGAL if entry perms allow | - |
| IOPMP-APPN-016 | §A.4.2 | srcmd_fmt=1; shared region needs duplicate entries | Two RRIDs need same region | Each RRID's MD needs its own entry (no sharing) | - |

## 15.5 MD-indexed SRCMD format - srcmd_fmt=2 (§A.4.3 / SRCMD_PERM)

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-APPN-017 | §A.4.3 | srcmd_fmt=2; SRCMD_PERM(m).perm: 2 bits/RRID (read=2s, write=2s+1) | RRID s read to MD m, perm read-bit=1 | LEGAL if entry also allows (checks BOTH) | - |
| IOPMP-APPN-018 | §A.4.3 | SRCMD_PERM(m) read-bit=1 for RRID s | Exec txn | Instruction fetch uses read permission (same bit) | - |
| IOPMP-APPN-019 | §A.4.3 | SRCMD_PERM(m) allows but entry denies (legal if EITHER allows) | Read | Per spec: "legal if either of them allows" - verify OR semantics for MD-indexed | - |
| IOPMP-APPN-020 | §A.4.3 | `rrid_num`>16; SRCMD_PERMH(m) for RRID 16–31 | RRID 20 perm | Governed by SRCMD_PERMH | - |
| IOPMP-APPN-021 | §A.4.3 | srcmd_fmt=2 supports ≤32 RRIDs | RRID 32 | Not supported in MD-indexed format | - |
| IOPMP-APPN-022 | §A.4.3 | srcmd_fmt=2 | SPS registers | SPS **not supported** in MD-indexed format | - |

## 15.6 Source-enforcement / IOPMP-SE (§A.4.1)

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-APPN-023 | §A.4.1 | Rules enforced only for a subset of requesters | Txn from unprotected requester (e.g. PMP-backed hart) | IOPMP does not re-check (enforced upstream) - IMP/config | - |

## 15.7 MDCFG reduction / k-entry (§A.6)

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-APPN-024 | §A.6, §A.6.1 | mdcfg_fmt=1; md_entry_num=k−1 | Entries of MD m | Range = [m*k, m*k+k−1] directly computed; no MDCFG lookup | - |
| IOPMP-APPN-025 | §A.6.1 | k=1 (md_entry_num=0) | MD concept | Each MD has exactly 1 entry; MD index == entry index | - |
| IOPMP-APPN-026 | §A.6 | mdcfg_fmt=2; md_entry_num programmable | Write md_entry_num before enable | Accepted; locked once HWCFG0.enable=1 | - |
| IOPMP-APPN-027 | §A.6 | k*md_num > entry_num | Entry index ≥ entry_num | Treated as OFF (file 04) | - |
| IOPMP-APPN-028 | §A.6 | MDCFGLCK | With mdcfg_fmt≠0 | MDCFG table & lock omitted | - |

## 15.8 RRID translation / IOPMP gateway (§A.7.2)

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-APPN-029 | §A.7.2 | HWCFG3.rrid_transl_en=1; rrid_transl=T | Txn passes checks | Outgoing transaction tagged with new RRID = T | - |
| IOPMP-APPN-030 | §A HWCFG3 | rrid_transl_prog=1 | Write rrid_transl | Writable (WARL) | - |
| IOPMP-APPN-031 | §A HWCFG3 | rrid_transl_prog W1CS, write 1 | Lock rrid_transl | rrid_transl_prog->0; rrid_transl now fixed | - |
| IOPMP-APPN-032 | §A HWCFG3 | rrid_transl_en=0 | rrid_transl field | Wired 0; no tagging | - |

---

## Cross-combinations (file-local)

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-APPN-X01 | §A.1 + §A.2 | no_w=1 AND no_x=1 (data-only read device) | Write and Exec txns | Both denied 0x05; only reads allowed per-entry | - |
| IOPMP-APPN-X02 | §A.3 + §A.2 | xinr=1 AND no_x=1 | Exec txn | Conflicting intent; spec: xinr treats fetch as read, no_x denies fetch - verify precedence (no_x deny dominates) | - |
| IOPMP-APPN-X03 | §A.4.3 + §5.2 | srcmd_fmt=2 + attempt SPS | Configure SPS | SPS must be unavailable (consistency, file 09) | - |
| IOPMP-APPN-X04 | §A.7.2 + §17 | rrid_transl gateway feeding a second IOPMP | Cascaded check | Outer IOPMP sees translated RRID (file 17) | - |
| IOPMP-APPN-X05 | §A.6 + §2.7 | k-entry layout + TOR entry within MD | TOR decode in k-block | TOR uses prev entry (may be in prior MD block) - boundary hazard | - |
