# 06 - Error Capture & Reactions

**Spec:** §2.8 (Error Reactions), §4.3 (Error Capture Registers: ERR_CFG, ERR_INFO, ERR_REQADDR/H, ERR_REQID, ERR_USER), Table 2 / Table 7 (error types), §5.1.4-5 (ERR_CFG/ERR_INFO extension fields - MFR/MSI/stall fields covered in files 12/13/08).

On an illegal transaction the IOPMP may: (1) trigger an interrupt, (2) return a bus error or faked success, (3) log error details. The capture record holds the **first** illegal access while `ERR_INFO.v=1`; subsequent violations are dropped until `v` is cleared (RW1C).

---

## 6.1 First-capture-wins

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-ERR-001 | §2.8, §4.3.2 | v=0; deny entry r=0 | Read denied at addr A, rrid R | v->1; ttype=0x01; etype=0x01; REQADDR=A>>2; REQID.rrid=R; REQID.eid=matched idx | |
| IOPMP-ERR-002 | §2.8 | v=1 (already captured) | Second different violation | Record unchanged (first violation preserved) | |
| IOPMP-ERR-003 | §4.3.2 | v=1 | Write ERR_INFO.v=1 (RW1C) | v->0; recorder re-armed | |
| IOPMP-ERR-004 | §4.3.2 | v cleared, then new violation | Third violation after clear | New violation captured fresh | |
| IOPMP-ERR-005 | §2.8 | Condition: no interrupt triggered AND no bus error returned | Violation with ie=0, rs=1, fully suppressed | Capture record need not update (spec exception) | - |

## 6.2 Error type encoding (all etypes)

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-ERR-006 | Table2 | Illegal read | Read on r=0 entry | etype=0x01, ttype=0x01 | |
| IOPMP-ERR-007 | Table2 | Illegal write | Write on w=0 entry | etype=0x02, ttype=0x02 | |
| IOPMP-ERR-008 | Table2 | Illegal AMO | AMO without r&w | etype=0x02, ttype=0x02 | |
| IOPMP-ERR-009 | Table2 | Illegal instruction fetch | Exec on x=0 entry | etype=0x03, ttype=0x03 | |
| IOPMP-ERR-010 | Table2 | Partial hit | Read spanning region edge | etype=0x04 | |
| IOPMP-ERR-011 | Table2 | No rule | Read no match | etype=0x05; eid invalid | |
| IOPMP-ERR-012 | Table2 | Unknown RRID | rrid≥rrid_num | etype=0x06; eid invalid | |
| IOPMP-ERR-013 | §4.3.2 | Bus matrix has no fetch signal | Exec txn when fetch unsupported | ttype/etype never report 0x03 | - |
| IOPMP-ERR-014 | Table7 | User-defined error condition | IMP rule | etype=0x0E/0x0F; reaction user-defined | - |

## 6.3 ERR_REQADDR / ERR_REQADDRH / ERR_REQID

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-ERR-015 | §4.3.3 | addrh_en=0; violation at addr A (≤34-bit) | Read ERR_REQADDR | addr[33:2] = A>>2; REQADDRH not implemented | |
| IOPMP-ERR-016 | §4.3.3 | addrh_en=1; violation at high addr | Read ERR_REQADDR + ERR_REQADDRH | Lower [33:2] and higher [65:34] both populated | - |
| IOPMP-ERR-017 | §4.3.4 | Violation matched entry idx=K | Read ERR_REQID.eid | eid=K, rrid=requestor | |
| IOPMP-ERR-018 | §4.3.4 | etype=0x05 or 0x06 (no entry hit) | Read ERR_REQID.eid | eid invalid (0xffff if eid not implemented) | |
| IOPMP-ERR-019 | §4.3.4 | eid field not implemented | Read | eid wired 0xffff | - |
| IOPMP-ERR-020 | §4.3.1/2.8 | no_err_rec=1 | Cause violation | ERR_INFO/REQADDR/REQID/USER not implemented; eid wired 0xffff | - |

## 6.4 Interrupt reaction (ERR_CFG.ie)

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-ERR-021 | §2.8, §4.3.1 | ERR_CFG.ie=1 | Cause violation | Interrupt asserted (irq_pending=1; callback fired); stays asserted until v cleared | |
| IOPMP-ERR-022 | §2.8 | ERR_CFG.ie=0 | Cause violation | No interrupt; record still captured (if not otherwise suppressed) | |
| IOPMP-ERR-023 | §4.3.2 | ie=1, v=1, interrupt asserted | Clear v (write 1) | Interrupt deasserts | |
| IOPMP-ERR-024 | §2.8 | ie=1; multiple violations | First triggers irq; clear; next triggers again | Interrupt re-asserts per fresh capture | |

## 6.5 Bus-error reaction (ERR_CFG.rs)

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-ERR-025 | §2.8, §4.3.1 | ERR_CFG.rs=0 | Violation | Respond bus error (IMP-defined) to requester | |
| IOPMP-ERR-026 | §4.3.1 | ERR_CFG.rs=1 | Violation | Suppress bus error; respond success w/ predefined value (faked success) | - |
| IOPMP-ERR-027 | §2.8 | rs=1 but ie=1 | Violation | Interrupt still fires; bus error suppressed (independent reactions) | - |

## 6.6 ERR_CFG register semantics

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-ERR-028 | §4.3.1 | ERR_CFG.l WISS | Write ERR_CFG.l=1 | l sticky 1; ie/rs now locked (file 07) | |
| IOPMP-ERR-029 | §4.3.1 | ie/rs WARL | Write ie=1, rs=1 | Read back legal values | |
| IOPMP-ERR-030 | §4.3.1 | rsv [31:3] ZERO | Write 0xFFFFFFFF | rsv reads 0 | |
| IOPMP-ERR-031 | §4.3.5 | ERR_USER(0..7) implemented | Read/write | IMP-defined user fields | - |

---

## Cross-combinations (file-local)

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-ERR-X01 | §4.3 + §3 | ERR_CFG.l=1 | Write ie/rs | Rejected; error config immutable until reset (file 07) | |
| IOPMP-ERR-X02 | §4.3 + §5.4 | Deny entry with sire=1, ie=1 | Read denied | Record captured but interrupt suppressed per-entry (file 11) | - |
| IOPMP-ERR-X03 | §4.3 + §5.4 | Deny entry sere=1, rs=0 | Read denied | Bus error suppressed per-entry; success returned (file 11) | - |
| IOPMP-ERR-X04 | §4.3 + §5.5 | v=1, more violations from other RRIDs | Subsequent violations | First in ERR_INFO; others logged in ERR_MFR; svc=1 (file 12) | - |
| IOPMP-ERR-X05 | §4.3 + §5.6 | msi_sel=1, ie=1 | Violation | Interrupt delivered via MSI write to ERR_MSIADDR (file 13) | - |
| IOPMP-ERR-X06 | §4.3 + §5.7 | stall_violation_en=1 | Stalled txn faulted | etype=0x07 captured in ERR_INFO (file 08) | - |
