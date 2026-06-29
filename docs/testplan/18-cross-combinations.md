# 18 - Multi-Feature Cross Scenarios

**Spec:** spans all chapters. These are the curated, security- and correctness-relevant interactions that touch **three or more** features or that capture a realistic end-to-end programming/attack scenario. Pairwise interactions local to a single feature live in that feature's own file; this file collects the larger compositions.

Each scenario is written as a short sequence (setup -> stimulus -> expected), because the value is in the *interaction*, not a single transaction.

---

## 18.1 Priority × permission × partial-hit × suppression

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-XC-001 | §2.7, §5.4 | Entry 0 (prio) covers first half of txn, r=0, sire=1; entry 1 covers all, r=1; ie=1 | Read spanning both | Entry 0 is highest-priority covering ⇒ matched ⇒ partial-hit 0x04; interrupt suppressed by entry 0.sire | - |
| IOPMP-XC-002 | §2.7, §5.4 | Entry 0 fully covers, r=0, sere=1, rs=0; entry 1 r=1 lower priority | Read | 0x01 via entry 0; bus error suppressed; success faked; entry 1 never consulted | - |
| IOPMP-XC-003 | §2.7, §2.8 | Two MDs both supply candidates; interleaved indices; lowest index denies | Read | Global index order decides; deny captured with that entry's eid | |

## 18.2 SPS × entry-permission × locks

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-XC-004 | §5.2, §2.7, §3.1 | Shared MD entry r=1,w=1; RRID A SPS=RW, RRID B SPS=RO; lock SRCMD_EN(B).l | A writes, B writes, then attempt to widen B's SPS | A LEGAL; B write ILLEGAL 0x02; B's SPS now immutable (locked) | - |
| IOPMP-XC-005 | §5.2, §3.1 | MDLCK.md[m]=1 locks SPS column m | Compromised SW tries to grant RRID B write on MD m | Rejected; SPS restriction preserved | - |
| IOPMP-XC-006 | §5.2, §2.7 | AMO; entry r=1,w=1; SRCMD_W(B).md[m]=0 | AMO from B | ILLEGAL 0x02 (AMO needs W; SPS denies) | - |

## 18.3 Lock × stall × runtime reconfiguration

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-XC-007 | §3, §5.7 | Critical entries 0-3 locked (ENTRYLCK.f=4); MD with entries 4+ unlocked | Stall RRIDs of that MD -> rewrite entries 4-7 -> resume | Locked 0-3 unchanged; 4-7 updated; no txn checked mid-update | - |
| IOPMP-XC-008 | §5.7, §5.7.5, §4.3 | stall_violation_en=1; buffer exhausted during reconfig | Stalled txn that can't be buffered | Faulted 0x07; captured in ERR_INFO; interrupt if ie=1 | - |
| IOPMP-XC-009 | §5.7, §2.6 | Reprogram MDCFG range (grow MD) under stall | Stall->change MDCFG(m).t->resume | Post-resume new entries active; no transient improper exposure | - |
| IOPMP-XC-010 | §5.7.4, §2.5 | MDSTALL grouping stalls extra RRIDs; RRIDSCP exempts a latency-critical one | MDSTALL then RRIDSCP op=2 on display-RRID | Display RRID keeps running; others stalled | - |

## 18.4 Non-priority × suppression × MFR

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-XC-011 | §5.3, §5.4 | Non-prio entries i0,i1 both cover all bytes, both deny read; i0.sire=1, i1.sire=0; ie=1 | Read | 0x01; interrupt asserted (NOT all matched suppress) | - |
| IOPMP-XC-012 | §5.3, §5.4 | Same but both sere=1, rs=0 | Read | 0x01; bus error suppressed (all matched suppress) | - |
| IOPMP-XC-013 | §5.3, §5.5 | Non-prio multi-match illegal from RRID 5, then RRID 9 | Two violations, v already set | First->ERR_INFO; RRID 9->ERR_MFR SV[9]=1; svc=1 | - |

## 18.5 Error capture × MSI × MFR (full reaction chain)

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-XC-014 | §4.3, §5.6, §5.5 | msi_en=1, msi_sel=1, ie=1, mfr_en=1; violations from RRID 2 then 4,6 | Sequence | RRID2 -> ERR_INFO + MSI write(msidata->ERR_MSIADDR); RRID4,6 -> ERR_MFR; svc=1 | - |
| IOPMP-XC-015 | §5.6, §5.1.5 | MSI target address faults | Violation triggers MSI; MSI write errors | msi_werr=1; original violation still recorded | - |
| IOPMP-XC-016 | §4.3, §5.4, §5.6 | msi_sel=1; matched entry sire=1 | Read denied | No MSI sent (interrupt suppressed); record per capture exception | - |

## 18.6 Address mode × 64-bit × error address capture

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-XC-017 | §2.4, §4.6.1, §4.3.3 | addrh_en=1; NAPOT region above 34-bit; deny | Read at high addr | 0x01; ERR_REQADDR + ERR_REQADDRH capture full 66-bit addr | - |
| IOPMP-XC-018 | §2.4, §4.3.3 | TOR spanning 34-bit boundary; partial hit | Read crossing boundary & region end | 0x04; high/low addr captured | - |

## 18.7 Global protection × per-entry × models

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-XC-019 | §A.1, §A.2, §2.7 | no_w=1 AND no_x=1 (data-only read device) | Read/Write/Exec to a granting entry | Read LEGAL; Write/Exec ILLEGAL 0x05 (global deny precedes entry) | - |
| IOPMP-XC-020 | §A.3, §2.7 | xinr=1; entry r=1,x=0 | Exec | LEGAL (fetch checked as read) | - |
| IOPMP-XC-021 | §A.8.5, §2.7, §5.7 | Compact-k; stall MD m; reconfig its k entries; resume | End-to-end on RRID m (=MD m) | Only RRID m stalled; its k entries updated atomically | - |

## 18.8 Enable / bypass × locks × capture

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-XC-022 | §4.1.3, §2.7 | enable=0 | Any txn (would otherwise be denied) | LEGAL - IOPMP bypassed; no capture | |
| IOPMP-XC-023 | §4.1.3, §3 | Program rules + locks, then set enable=1 (WISS) | Set enable, attempt to clear | enable stays 1; rules now enforced; locks immutable | |
| IOPMP-XC-024 | §4.1.3, §A.6 | md_entry_num locked by enable | Set enable=1, write md_entry_num | Rejected (k frozen) | - |

## 18.9 Multi-instance × mixed-model × independent reactions

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-XC-025 | §A.7, §A.8, §4.3 | Instance A=Full (ie=1 wired), B=Isolation (msi_sel=1) | Violations on both | A asserts wired IRQ; B issues MSI; records independent (files 16,17) | - |
| IOPMP-XC-026 | §A.7.2, §A.7.1 | Cascade: inner tags RRID, outer is one of a parallel pair | Multi-hop txn | Inner check -> RRID translate -> address route -> outer check | - |

---

## 18.10 Negative / robustness sweeps (apply across features)

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-XC-027 | §2.7 | len=0 transaction | Issue zero-length txn | Defined behavior (no match / no crash) - document model choice | |
| IOPMP-XC-028 | §2.7 | Max address / max len near 2^66 | Txn at top of space | No overflow in region math (addrh path) | - |
| IOPMP-XC-029 | various | Write every WARL register with 0xFFFFFFFF then read | Sweep all registers | Each returns only legal bits; no illegal state retained | |
| IOPMP-XC-030 | §3 | After setting every lock, attempt every protected write | Full lock sweep | All protected writes rejected; error-record regs still mutable | |
| IOPMP-XC-031 | §4 | Reset after heavy configuration | iopmp_reset then re-read | All non-prelocked state returns to reset defaults | |
