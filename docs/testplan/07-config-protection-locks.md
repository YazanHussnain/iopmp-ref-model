# 07 - Configuration Protection / Locks

**Spec:** §3 (Configuration Protection), §3.1 (SRCMD lock), §3.2 (MDCFG lock), §3.3 (Entry lock), §3.4 (lock summary), §3.5 (prelocked), §4.2 (MDLCK/MDLCKH, MDCFGLCK, ENTRYLCK), §4.5.1 (SRCMD_EN.l), §4.3.1 (ERR_CFG.l).

All locks are sticky until reset. A lock prevents the corresponding programmable field/register from being modified. The check happens in `iopmp_write_reg` before any value is applied. Error-record registers intentionally have **no** lock (modified at runtime).

---

## 7.1 SRCMD_EN(s).l - per-RRID SRCMD lock (WISS)

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-LOCK-001 | §3.1, §4.5.1 | SRCMD_EN(2).l=0 | Write SRCMD_EN(2).md, then set l=1 | md updated; l latches 1 | |
| IOPMP-LOCK-002 | §3.1 | SRCMD_EN(2).l=1 | Write SRCMD_EN(2).md | Rejected; md unchanged | |
| IOPMP-LOCK-003 | §3.1 | SRCMD_EN(2).l=1 | Write SRCMD_ENH(2).mdh | Rejected (l also locks ENH) | |
| IOPMP-LOCK-004 | §3.1 | SRCMD_EN(2).l=1 | Write SRCMD_EN(2).l=0 | l stays 1 (sticky) | |
| IOPMP-LOCK-005 | §3.1 | SRCMD_EN(2).l=1; SRCMD_EN(3).l=0 | Write SRCMD_EN(3).md | RRID 3 still writable (per-RRID scope) | |

## 7.2 MDLCK / MDLCKH - per-MD column lock (WARL md, WISS l)

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-LOCK-006 | §3.1, §4.2.1 | MDLCK.md[3]=1 | Write SRCMD_EN(s).md[3] for any s | Bit 3 rejected for all RRIDs; other bits writable | |
| IOPMP-LOCK-007 | §4.2.1 | MDLCK.l=1 | Write MDLCK.md or MDLCKH.mdh | Rejected; MDLCK frozen | |
| IOPMP-LOCK-008 | §4.2.1 | MDLCK.l=1 | Write MDLCK.l=0 | l stays 1 | |
| IOPMP-LOCK-009 | §4.2.1 | `md_num`≤31 (MDLCKH optional) | Read MDLCKH | Wired 0 / not implemented | |
| IOPMP-LOCK-010 | §4.2.1 | `md_num`>31; MDLCKH.mdh[5]=1 | Write SRCMD_ENH(s).mdh[5] | Rejected for all s | |
| IOPMP-LOCK-011 | §4.2.1 | MDLCK.md not implemented | Read MDLCK.md / .l | md wired 0; l wired 1 | - |

## 7.3 MDCFGLCK - MDCFG table lock (WISS l, WARL incremental f)

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-LOCK-012 | §3.2, §4.2.2 | MDCFGLCK.f=0 | Write MDCFGLCK.f=3 | f=3; MDCFG(0,1,2) locked (m<f) | |
| IOPMP-LOCK-013 | §4.2.2 | MDCFGLCK.f=3 | Write MDCFG(2).t | Rejected (m=2 < 3) | |
| IOPMP-LOCK-014 | §4.2.2 | MDCFGLCK.f=3 | Write MDCFG(3).t | Allowed (m=3 ≥ f) | |
| IOPMP-LOCK-015 | §4.2.2 | f=3 (incremental only) | Write MDCFGLCK.f=2 | Rejected; smaller value not accepted; f stays 3 | |
| IOPMP-LOCK-016 | §4.2.2 | f=3 | Write MDCFGLCK.f=5 | f=5 (larger accepted) | |
| IOPMP-LOCK-017 | §4.2.2 | Write f > md_num | Write f=255 | All MDCFG entries locked (f≥md_num ⇒ all) | |
| IOPMP-LOCK-018 | §4.2.2 | MDCFGLCK.l=1 | Write MDCFGLCK.f | Rejected; f frozen | |
| IOPMP-LOCK-019 | §3.2 note | Lock MD m requires MD 0..m−1 also locked | Lock MDCFG(m) | Spec requires all preceding locked (incremental f enforces this) | |

## 7.4 ENTRYLCK - entry array lock (WISS l, WARL incremental f)

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-LOCK-020 | §3.3, §4.2.3 | ENTRYLCK.f=0 | Write ENTRYLCK.f=4 | Entries 0..3 locked (i<f) | |
| IOPMP-LOCK-021 | §4.2.3 | ENTRYLCK.f=4 | Write ENTRY_ADDR(2)/ADDRH(2)/CFG(2) | All rejected (i=2<4) | |
| IOPMP-LOCK-022 | §4.2.3 | ENTRYLCK.f=4 | Write ENTRY_CFG(4) | Allowed (i≥f) | |
| IOPMP-LOCK-023 | §4.2.3 | f=4 | Write ENTRYLCK.f=2 | Rejected (incremental); f stays 4 | |
| IOPMP-LOCK-024 | §4.2.3 | Write f > entry_num | Write f=0xFFFF | All entries locked | |
| IOPMP-LOCK-025 | §4.2.3 | ENTRYLCK.l=1 | Write ENTRYLCK.f | Rejected; frozen | |

## 7.5 ERR_CFG.l - error-config lock (WISS)

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-LOCK-026 | §4.3.1 | ERR_CFG.l=0 | Set ie=1, rs=1, then l=1 | ie/rs set; l latches | |
| IOPMP-LOCK-027 | §4.3.1 | ERR_CFG.l=1 | Write ie/rs | Rejected; unchanged | |
| IOPMP-LOCK-028 | §3.4 note | Error-record regs (ERR_INFO etc.) have no lock | With ERR_CFG.l=1, write ERR_INFO.v | v still clearable (no lock on record) | |

## 7.6 Prelocked configurations (§3.5)

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-LOCK-029 | §3.5 | MDCFGLCK.f reset value = 2 (prelocked) | After reset, write MDCFG(1).t | Rejected; pre-locked entries reflect reset f | - |
| IOPMP-LOCK-030 | §3.5 | ENTRYLCK.f reset = 4 (prelocked) | Write ENTRY_CFG(0) after reset | Rejected | - |
| IOPMP-LOCK-031 | §3.5 | SRCMD_EN(s) prelocked bits via MDLCK.md preset | After reset, write locked md bit | Rejected | - |
| IOPMP-LOCK-032 | §3.3 | ENTRYLCK hardwired (ENTRYLCK.l wired 1, f fixed) | Any entry write within f | Permanently rejected | - |

---

## Cross-combinations (file-local)

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-LOCK-X01 | §3 + §2.7 | Lock entry 0 (deny-all high-priority), unlocked entry 5 allows | Compromised SW writes entry 5 to allow | Locked higher-priority entry 0 still denies (defense-in-depth, §3.3) | |
| IOPMP-LOCK-X02 | §3.1 + §3.1 note | High-priority MD with no-perm locked & associated to all RRIDs via MDLCK | Attempt to bypass via SRCMD | Cannot re-associate; protection holds | |
| IOPMP-LOCK-X03 | §3 + §5.7 | Locked region + stall reconfig of unlocked region | Stall, update unlocked entries, resume | Locked entries untouched; only unlocked updated (file 08) | - |
| IOPMP-LOCK-X04 | §3.2 + §2.6 | MDCFGLCK.f locks MD m but MDCFG(m−1) unlocked path | Try to grow MD m via MDCFG(m−1).t | §3.2 note: locking MD m requires preceding locked ⇒ cannot manipulate | |
| IOPMP-LOCK-X05 | §3 + §A.8 | Compact/Isolation model (no SRCMD table) | Apply SRCMD-related locks | MDLCK semantics apply to SRCMD_PERM/exclusive mapping or are wired (file 16) | - |
