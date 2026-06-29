# 10 - Non-Priority IOPMP Entries

**Spec:** §5.3 (Non-priority IOPMP entries), §5.3.1 (functional description), §5.3.2 (error reporting), §5.1.1 (HWCFG2.prio_entry, prio_ent_prog, non_prio_en).

When `non_prio_en=1`, entries with index `< prio_entry` are **priority** entries (index-ordered, as baseline); entries with index `≥ prio_entry` are **non-priority** entries - all share the lowest priority and are treated equally. Key differences for non-priority entries: they must cover **all** bytes of the transaction to match (no partial hit), multiple may match, and error type `0x04` (partial hit) is never reported for them.

---

## 10.1 Capability & prio_entry boundary

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-NPRIO-001 | §5.1.1 | HWCFG2.non_prio_en=1 | Read HWCFG2.prio_entry | Returns configured boundary | - |
| IOPMP-NPRIO-002 | §5.3.1 | non_prio_en=0 | Read prio_entry | DC / default (all entries prioritized) | - |
| IOPMP-NPRIO-003 | §5.1.1 | prio_ent_prog=1 (programmable) | Write HWCFG2.prio_entry=4 | Reads back 4 | - |
| IOPMP-NPRIO-004 | §5.1.1 | prio_ent_prog W1CS reset=1 | Write prio_ent_prog=1 | Clears to 0; prio_entry now fixed (file 01) | - |
| IOPMP-NPRIO-005 | §5.3.1 | prio_entry=0 | All entries | All entries non-priority | - |
| IOPMP-NPRIO-006 | §5.3.1 | prio_entry=entry_num | All entries | All entries priority (baseline behavior) | - |

## 10.2 Non-priority matching (cover ALL bytes)

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-NPRIO-007 | §5.3.1 | prio_entry=2; entry 3 (non-prio) covers all txn bytes, r=1 | Read fully inside | LEGAL via entry 3 | - |
| IOPMP-NPRIO-008 | §5.3.1 | Entry 3 (non-prio) covers only some txn bytes | Read spanning region edge | Entry 3 does NOT match (non-prio needs all bytes) | - |
| IOPMP-NPRIO-009 | §5.3.2 | No non-priority entry covers all bytes; none priority match | Read | ILLEGAL etype=0x01 (NOT 0x04) - no partial-hit for non-prio | - |
| IOPMP-NPRIO-010 | §5.3.1 note | Two non-priority entries both cover all bytes (multi-match) | Read; entry A r=1, entry B r=0 | Multiple matches allowed; legal if **any** matching entry permits ⇒ LEGAL | - |
| IOPMP-NPRIO-011 | §5.3.2 | Two non-prio entries cover all bytes, both r=0 | Read | ILLEGAL etype=0x01 (no matching entry permits) | - |

## 10.3 Priority vs non-priority interaction

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-NPRIO-012 | §5.3.1 | prio_entry=2; priority entry 1 covers any byte & denies; non-prio entry 3 allows | Read | Priority entry 1 wins (higher priority) ⇒ deny / partial-hit per its coverage | - |
| IOPMP-NPRIO-013 | §5.3.1 | Priority entries don't match; non-priority entry matches all bytes | Read | Falls through to non-priority; LEGAL if permitted | - |
| IOPMP-NPRIO-014 | §5.3.1 | Priority entry partially covers (0x04 candidate); also a non-prio full-cover allow | Read | Priority partial-hit dominates ⇒ etype=0x04 (priority evaluated first) | - |
| IOPMP-NPRIO-015 | §5.3.2 | One matching non-priority entry is the only match and is priority-type fallback | Reaction same as baseline when a priority entry is the single match | Baseline reaction applies | - |

## 10.4 Error reporting differences

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-NPRIO-016 | §5.3.2 | Non-priority match, write on r-only set | Write | etype=0x02; never 0x04 | - |
| IOPMP-NPRIO-017 | §5.3.2 | Multiple non-prio entries matched on illegal txn; interrupt/bus error raised | ERR_REQID.eid | eid = index of any one of the matching entries | - |
| IOPMP-NPRIO-018 | §5.3.1 | Non-prio entries cached/parallel (impl) | Functional equivalence check | Result independent of evaluation order among non-prio entries | - |

---

## Cross-combinations (file-local)

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-NPRIO-X01 | §5.3 + §5.4 | Multiple non-prio entries matched, illegal; to suppress interrupt ALL must have sire=1 | Read denied | Interrupt suppressed only if every matched entry suppresses (file 11 AND/OR rule) | - |
| IOPMP-NPRIO-X02 | §5.3 + §5.4 | Multiple non-prio matched; only some sere=1 | Read denied | Bus error NOT suppressed (needs all) (file 11) | - |
| IOPMP-NPRIO-X03 | §5.3 + §3.3 | Priority entries locked (critical), non-prio entries dynamic | Reconfig non-prio at runtime | Locked priority rules enforce defense-in-depth; non-prio bulk rules flexible | - |
| IOPMP-NPRIO-X04 | §5.3 + §5.5 | Multiple non-prio illegal matches across RRIDs | MFR logging | Subsequent violations recorded in ERR_MFR by RRID (file 12) | - |
| IOPMP-NPRIO-X05 | §5.3 + §2.7 | prio_entry boundary exactly at a TOR entry | Region decode for boundary entry | TOR still decodes from prev entry regardless of priority class | - |
