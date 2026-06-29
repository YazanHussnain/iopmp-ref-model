# 11 - Per-Entry Interrupt / Bus-Error Suppression

**Spec:** §5.4 (Per-entry Interrupt/Bus Error Suppression), §5.4.1 (interrupt suppression), §5.4.2 (bus error suppression), §5.4.3 (suppression on non-priority entries), §5.1.11 (ENTRY_CFG sire/siwe/sixe/sere/sewe/sexe), §5.1.1 (HWCFG2.peis, pees).

Per-entry fields suppress the *reactions* (interrupt and/or bus error) for violations caught by that entry - without changing legality. Interrupt-suppress bits: `sire` (read), `siwe` (write/AMO), `sixe` (fetch). Bus-error-suppress bits: `sere`, `sewe`, `sexe`. These only matter when an entry is **matched but denies** the access.

**Interrupt fires** for an illegal access at entry i iff `ERR_CFG.ie && !ENTRY_CFG(i).si<r/w/x>e`.
**Bus error returns** iff `!ERR_CFG.rs && !ENTRY_CFG(i).se<r/w/x>e`.

---

## 11.1 Capability

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-SUPP-001 | §5.1.1 | HWCFG2.peis=1 | Read ENTRY_CFG(i).sire/siwe/sixe | Implemented (WARL) | - |
| IOPMP-SUPP-002 | §5.1.1 | peis=0 | Those bits | Not implemented / wired 0 | - |
| IOPMP-SUPP-003 | §5.1.1 | HWCFG2.pees=1 | Read ENTRY_CFG(i).sere/sewe/sexe | Implemented | - |
| IOPMP-SUPP-004 | §5.1.1 | pees=0 | Those bits | Not implemented / wired 0 | - |

## 11.2 Interrupt suppression (single priority entry)

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-SUPP-005 | §5.4.1 | ie=1; entry i r=0, sire=1 | Read denied (matched entry i) | ILLEGAL 0x01; **no interrupt** (suppressed) | - |
| IOPMP-SUPP-006 | §5.4.1 | ie=1; entry i r=0, sire=0 | Read denied | ILLEGAL 0x01; interrupt asserted | - |
| IOPMP-SUPP-007 | §5.4.1 | ie=1; entry i w=0, siwe=1 | Write denied | 0x02; no interrupt | - |
| IOPMP-SUPP-008 | §5.4.1 | ie=1; entry i x=0, sixe=1 | Exec denied | 0x03; no interrupt | - |
| IOPMP-SUPP-009 | §5.4.1 | ie=1; entry i r=0, siwe=1 (wrong type bit) | Read denied | 0x01; interrupt asserted (siwe doesn't suppress read) | - |
| IOPMP-SUPP-010 | §5.4.1 | ie=0; sire=0 | Read denied | No interrupt anyway (global ie=0) | - |

## 11.3 Bus-error suppression (single priority entry)

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-SUPP-011 | §5.4.2 | rs=0; entry i r=0, sere=1 | Read denied | 0x01; **no bus error** (user-defined, e.g. faked success) | - |
| IOPMP-SUPP-012 | §5.4.2 | rs=0; entry i r=0, sere=0 | Read denied | 0x01; bus error returned | - |
| IOPMP-SUPP-013 | §5.4.2 | rs=0; entry i w=0, sewe=1 | Write denied | 0x02; no bus error | - |
| IOPMP-SUPP-014 | §5.4.2 | rs=0; entry i x=0, sexe=1 | Exec denied | 0x03; no bus error | - |
| IOPMP-SUPP-015 | §5.4.2 | rs=1 (global suppress) | Any denied | Bus error globally suppressed regardless of per-entry bits | - |

## 11.4 Independent interrupt vs bus-error suppression

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-SUPP-016 | §5.4 | ie=1, rs=0; entry sire=1, sere=0 | Read denied | No interrupt; bus error returned (independent) | - |
| IOPMP-SUPP-017 | §5.4 | ie=1, rs=0; entry sire=0, sere=1 | Read denied | Interrupt asserted; no bus error | - |
| IOPMP-SUPP-018 | §5.4 | The guard-region use case: speculative prefetch hits suppressed entry | Read denied at guard entry | No noise: neither interrupt nor bus error, access still blocked | - |

## 11.5 Suppression with non-priority entries (AND / OR rule §5.4.3)

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-SUPP-019 | §5.4.3 | Non-prio match set {i0,i1,i2}, illegal read; all sire=1 | Read denied | Interrupt suppressed (all matched suppress) | - |
| IOPMP-SUPP-020 | §5.4.3 | Same set; i1.sire=0 | Read denied | Interrupt asserted - OR over `!sire` means NOT all suppress | - |
| IOPMP-SUPP-021 | §5.4.3 | Non-prio set; all sere=1, rs=0 | Read denied | Bus error suppressed (all matched suppress) | - |
| IOPMP-SUPP-022 | §5.4.3 | Non-prio set; one sere=0 | Read denied | Bus error returned | - |
| IOPMP-SUPP-023 | §5.4.3 | Write across non-prio matches: all siwe=1, one sewe=0 | Write denied | Interrupt suppressed; bus error returned (independent dimensions) | - |

---

## Cross-combinations (file-local)

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-SUPP-X01 | §5.4 + §4.3 | sire=1, sere=1, ie=1, rs=0; v=0 | Read denied | Capture record: spec exception - when no irq and no bus error, capture need not update (file 06 ERR-005) | - |
| IOPMP-SUPP-X02 | §5.4 + §2.7 | Two priority entries cover addr; higher-priority denies w/ sire=1 | Read | Higher-priority entry is the match; its sire governs suppression | - |
| IOPMP-SUPP-X03 | §5.4 + §5.2 | SPS denies; matched entry sere=1 | Write denied by SPS | Suppression of the matched entry applies to the SPS-induced denial | - |
| IOPMP-SUPP-X04 | §5.4 + §5.6 | MSI interrupt mode; sire=1 | Read denied | MSI not sent when interrupt suppressed (file 13) | - |
| IOPMP-SUPP-X05 | §5.4 + §A.2 | no_x=1 global fetch deny (etype 0x05, no entry) | Exec denied | No matched entry ⇒ sixe/sexe don't apply; reaction per global ie/rs | - |
