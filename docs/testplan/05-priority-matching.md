# 05 - Priority & Matching Logic

**Spec:** §2.7 (Priority and Matching Logic), §2.8 (error types referenced), Table 2 (error types), Figure 3 (check flow).

The core transaction-check pipeline: RRID legality -> SRCMD lookup -> MDCFG range -> entry match in **priority (index) order** -> permission grant -> all-bytes coverage. This file is the heart of the plan; address-mode decode is file 04, error capture is file 06, non-priority matching is file 10.

**Matching rule (priority entries):** an entry matches if (a) its region covers **any** byte of the txn, (b) it is associated with the txn's RRID, and (c) it has the highest priority (lowest index) among such entries. The matched entry then must grant the access type **and** cover **all** bytes, else illegal.

---

## 5.1 Single-entry grant / deny

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-MATCH-001 | §2.7 | Entry 0 covers [A,A+0x100), r=1; rrid assoc | Read addr=A len=4 | LEGAL, entry_idx=0 | |
| IOPMP-MATCH-002 | §2.7 | Entry 0 r=1,w=0 | Write addr in region | ILLEGAL etype=0x02, entry_idx=0 | |
| IOPMP-MATCH-003 | §2.7 | Entry 0 r=1,x=0 | Exec addr in region | ILLEGAL etype=0x03 | |
| IOPMP-MATCH-004 | §2.7 | Entry 0 w=1,r=0 | Read | ILLEGAL etype=0x01 | |

## 5.2 No-match / no-rule

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-MATCH-005 | §2.7 | Entries active but none cover addr | Read addr outside all regions | ILLEGAL etype=0x05 (not hit any rule), entry_idx invalid | |
| IOPMP-MATCH-006 | §2.7 | RRID associated to MD but MD has no active entry | Any txn | ILLEGAL etype=0x05 | |

## 5.3 Priority ordering (lowest index wins)

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-MATCH-007 | §2.7 | Entry 2 and entry 5 both cover addr; both assoc; entry2 r=1, entry5 r=0 | Read | LEGAL via entry 2 (lower index) - entry 5 never consulted | |
| IOPMP-MATCH-008 | §2.7 | Entry 2 r=0 (deny), entry 5 r=1, both cover addr | Read | ILLEGAL etype=0x01 via entry 2 - higher priority deny wins over lower-priority allow | |
| IOPMP-MATCH-009 | §2.7 | Overlapping regions; highest-priority is OFF, next covers & grants | Read | OFF skipped; matches next active covering entry | |
| IOPMP-MATCH-010 | §2.7 | Two MDs contribute candidate entries with interleaved indices | Read addr in overlap | Global index order across all candidate entries respected | |
| IOPMP-MATCH-011 | §2.7 | Entry indices ordered so a partial-hit entry precedes a full-cover entry | Read | Highest-priority covering entry (the partial one) is the match ⇒ 0x04 | |

## 5.4 Partial hit (0x04)

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-MATCH-012 | §2.7 | Highest-priority matching entry covers some but not all txn bytes; perm granted | Read spanning region end | ILLEGAL etype=0x04 (partial hit), irrespective of permission | |
| IOPMP-MATCH-013 | §2.7 | Same as above but perm NOT granted | Read | etype=0x04 still (partial hit takes precedence over perm per spec wording "irrespective of its permission") | |
| IOPMP-MATCH-014 | §2.7 | Txn starts before region, ends inside | Read | Entry covers some bytes ⇒ matches ⇒ 0x04 | |
| IOPMP-MATCH-015 | §2.7 | Two adjacent entries that together cover all bytes | Read spanning both | Only the highest-priority (first covering) entry is the match ⇒ 0x04 (no merging across entries) | |

## 5.5 AMO semantics

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-MATCH-016 | §2.7 note | Entry r=1,w=1 | AMO txn fully inside | LEGAL (AMO needs r AND w) | |
| IOPMP-MATCH-017 | §2.7 note | Entry r=1,w=0 | AMO txn | ILLEGAL etype=0x02 (write/AMO) | |
| IOPMP-MATCH-018 | §2.7 note | Entry r=0,w=1 | AMO txn | ILLEGAL etype=0x02 | |
| IOPMP-MATCH-019 | §2.7 note | AMO modeled as read-modify-write; read perm missing | Read phase of RMW | May capture 0x01 (illegal read) per spec note | - |

## 5.6 Coverage edge cases

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-MATCH-020 | §2.7 | len=1 at exact region base | Read | Single byte covered ⇒ LEGAL | |
| IOPMP-MATCH-021 | §2.7 | len=1 at region end−1 | Read | LEGAL (last byte inside) | |
| IOPMP-MATCH-022 | §2.7 | len=1 at region end (first byte outside) | Read | No match by this entry | |
| IOPMP-MATCH-023 | §2.7 | Large len wrapping address space | Read | No overflow; bytes beyond region ⇒ partial/no-match handled safely | |
| IOPMP-MATCH-024 | §2.7 | enable=0 | Any txn | LEGAL (IOPMP bypassed) | |

---

## Cross-combinations (file-local)

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-MATCH-X01 | §2.7 + §2.8 | Matching deny entry; ERR_CFG.ie=1 | Read denied | etype=0x01 captured AND interrupt asserted (file 06) | |
| IOPMP-MATCH-X02 | §2.7 + §5.3 | prio_entry=N; some candidates priority, some non-priority | Read covered by a non-priority entry but a priority entry partially covers | Priority entries evaluated first; partial-hit may dominate (file 10) | - |
| IOPMP-MATCH-X03 | §2.7 + §5.2 | SPS on; entry grants RW, SRCMD_W(s)=0 | Write fully inside | Entry matches & permits write, but SPS denies ⇒ ILLEGAL 0x02 (file 09) | - |
| IOPMP-MATCH-X04 | §2.7 + §5.4 | Deny entry with sire=1; ERR_CFG.ie=1 | Read denied | etype captured but interrupt suppressed (file 11) | - |
| IOPMP-MATCH-X05 | §2.7 + §A.8 | Isolation/Compact model (RRID i->MD i) | Txn rrid=i | Candidates only from MD i; first-match semantics (file 16) | - |
