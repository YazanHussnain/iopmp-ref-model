# 09 - Secondary Permission Setting (SPS)

**Spec:** §5.2 (Secondary Permission Setting), §5.1.8 (SRCMD_R/RH), §5.1.9 (SRCMD_W/WH), §5.1.10 (SRCMD_X/XH), §5.1.1 (HWCFG2.sps_en), Figure 4.

SPS adds a **second** permission layer in the SRCMD Table. A transaction is legal only if permitted by **both** the matching entry's permission **and** the per-RRID SPS permission (`SRCMD_R/W/X(s).md[m]`). SPS can only **restrict**, never grant. `SRCMD_R(s).md[m]` covers MD 0–30; `SRCMD_RH(s).mdh[m]` covers MD 31–62 (when `md_num>31`). Instruction-fetch shares with read in some uses; here X is its own register. Shares locks with `SRCMD_EN(s).l` and `MDLCK`.

---

## 9.1 Capability

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-SPS-001 | §5.1.1 | HWCFG2.sps_en=1 | Read SRCMD_R/W/X(s) | Implemented | - |
| IOPMP-SPS-002 | §5.1.1 | sps_en=0 | Read SRCMD_R/W/X(s) | Not implemented / 0 | - |
| IOPMP-SPS-003 | §5.1.8-10 | `md_num`≤31 | Read SRCMD_RH/WH/XH | Wired 0 / not implemented | - |

## 9.2 Restrict semantics (both layers must allow)

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-SPS-004 | §5.2 | Entry r=1,w=1; RRID s assoc MD m; SRCMD_R(s).md[m]=1, SRCMD_W(s).md[m]=0 | Read txn | LEGAL (entry r + SPS R both allow) | - |
| IOPMP-SPS-005 | §5.2 | Same; SRCMD_W(s).md[m]=0 | Write txn | ILLEGAL etype=0x02 (SPS W denies even though entry w=1) | - |
| IOPMP-SPS-006 | §5.2 | Entry r=0,w=1; SRCMD_R(s).md[m]=1 | Read txn | ILLEGAL etype=0x01 (entry denies; SPS cannot grant) | - |
| IOPMP-SPS-007 | §5.2 | Entry r=1; SRCMD_R(s).md[m]=0 | Read txn | ILLEGAL etype=0x01 (SPS R denies) | - |
| IOPMP-SPS-008 | §5.2 | Entry x=1; SRCMD_X(s).md[m]=0 | Exec txn | ILLEGAL etype=0x03 (SPS X denies) | - |
| IOPMP-SPS-009 | §5.2 | Entry x=1; SRCMD_X(s).md[m]=1 | Exec txn | LEGAL | - |
| IOPMP-SPS-010 | §5.2 | All SPS bits 1 (no restriction) | R/W/X txns | Behaves identically to baseline (entry perms only) | - |
| IOPMP-SPS-011 | §5.2 | All SPS bits 0 | Any txn to MD m | All denied (SPS removes all perms) | - |

## 9.3 Shared entry / multiple RRIDs (the SPS use case)

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-SPS-012 | §5.2 | One MD/entry (r=1,w=1) shared by RRID A (RW via SPS) and RRID B (RO via SPS W=0) | A writes; B writes | A LEGAL; B ILLEGAL 0x02 - single entry serves both perms | - |
| IOPMP-SPS-013 | §5.2 | RRID A R-only, RRID B X-only on same region | A exec; B read | A: exec denied; B: read denied per their SPS masks | - |

## 9.4 High MDs (RH/WH/XH)

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-SPS-014 | §5.1.8-10 | `md_num`=40; MD 35; SRCMD_RH(s).mdh bit for MD35 | Read txn to MD35 region | Governed by SRCMD_RH; restrict honored | - |
| IOPMP-SPS-015 | §5.1.8 | SRCMD_R(s) rsv bit0 = ZERO | Write 0xFFFFFFFF | bit0 reads 0 | - |

## 9.5 Lock sharing

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-SPS-016 | §5.2 | SRCMD_EN(s).l=1 | Write SRCMD_R/W/X(s) | Rejected (l locks SPS registers too) | - |
| IOPMP-SPS-017 | §5.2 | MDLCK.md[m]=1 | Write SRCMD_R/W/X(s).md[m] | Bit m rejected for all RRIDs (MDLCK locks SPS columns) | - |
| IOPMP-SPS-018 | §5.2 | Prelocked SPS bits via MDLCK.md preset | After reset | Preset SPS bits immutable | - |

---

## Cross-combinations (file-local)

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-SPS-X01 | §5.2 + §2.7 | SPS restricts; two entries (priority) cover addr | Highest-priority entry matched, then SPS applied | SPS applied to the matched entry's MD; deny if SPS denies | - |
| IOPMP-SPS-X02 | §5.2 + §2.7 | AMO; entry r=1,w=1; SRCMD_W(s)=0 | AMO txn | ILLEGAL 0x02 (AMO needs W; SPS W denies) | - |
| IOPMP-SPS-X03 | §5.2 + §A.4.3 | MD-indexed format (srcmd_fmt=2) | SPS registers | SPS **not supported** in MD-indexed format (spec) - must be absent (file 15) | - |
| IOPMP-SPS-X04 | §5.2 + §A.8 | Isolation / Compact-k model | SPS usage | SPS **not supported** - capability bit must be 0 (file 16) | - |
| IOPMP-SPS-X05 | §5.2 + §5.4 | SPS denies write; entry sewe=1 (suppress) | Write txn | etype=0x02 captured but per-entry bus error suppressed (file 11) | - |
