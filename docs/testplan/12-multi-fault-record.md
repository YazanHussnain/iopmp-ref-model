# 12 - Multi-Faults Record (MFR)

**Spec:** §5.5 (Multi-Faults Record), §5.1.6 (ERR_MFR svw/svi/svs), §5.1.5 (ERR_INFO.svc), §5.1.1 (HWCFG2.mfr_en).

The primary capture record (ERR_INFO) locks on the **first** violation. MFR is a secondary, low-cost log that records **which RRIDs** generated *subsequent* violations (a bitmap, not full details). `SV[s]=1` means RRID `s` had ≥1 subsequent violation. `ERR_MFR` is read as a sliding window: each window holds 16 SV bits (`svw[j]` ⇒ RRID = `svi*16 + j`); `svi` is the window index, `svs` the found/not-found status. Reading clears the returned window. `ERR_INFO.svc=1` indicates subsequent violations exist.

---

## 12.1 Capability

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-MFR-001 | §5.1.1 | HWCFG2.mfr_en=1 | Read ERR_MFR / ERR_INFO.svc | Implemented | - |
| IOPMP-MFR-002 | §5.1.1 | mfr_en=0 | Read ERR_MFR / ERR_INFO.svc | ERR_INFO.svc wired 0; ERR_MFR not implemented | - |

## 12.2 Subsequent-violation logging

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-MFR-003 | §5.5 | v=1 from RRID 3 (first); then RRID 7 violates | Second violation | ERR_INFO unchanged; SV[7]=1; ERR_INFO.svc=1 | - |
| IOPMP-MFR-004 | §5.5 | v=1; RRID 7 violates twice | Two subsequent from same RRID | SV[7]=1 (single bit; counts presence not count) | - |
| IOPMP-MFR-005 | §5.5 | v=1; subsequent from RRID 7, 20, 35 | Multiple RRIDs | SV[7]=SV[20]=SV[35]=1; svc=1 | - |
| IOPMP-MFR-006 | §5.5 | v=0 (no first violation) | A violation occurs | Goes to ERR_INFO (first capture); SV not set; svc=0 | - |
| IOPMP-MFR-007 | §5.5 | The first-capturing RRID (3) violates again while v=1 | Subsequent from RRID 3 | SV[3]=1 (its own subsequent violations are logged) | - |

## 12.3 ERR_MFR window read & scan

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-MFR-008 | §5.1.6 | SV[7]=1 only | Write svi=0, read ERR_MFR | Scans from window 0; finds window 0 (RRID 0-15) with svw bit7=1; svs=1; svi=0 | - |
| IOPMP-MFR-009 | §5.1.6 | SV[35]=1 (window 2, bit3) | Read ERR_MFR (svi starts 0) | Sequential scan to window 2; svw bit3=1; svi=2; svs=1 | - |
| IOPMP-MFR-010 | §5.1.6 | No SV bits set | Read ERR_MFR | svs=0; svw=0; svi unchanged | - |
| IOPMP-MFR-011 | §5.1.6 | Window read returns content | Read same window again | svw cleared on previous read ⇒ now 0 (clear-on-read) | - |
| IOPMP-MFR-012 | §5.1.6 | SV bits in windows 1 and 3; svi set to 2 | Read ERR_MFR | Scan from window 2 forward, wraps past last window back to 0; finds next set window (3) | - |
| IOPMP-MFR-013 | §5.1.6 | Scan reaches last window, wraps | Continue reading | Next scanned window is window 0 (wrap-around) | - |
| IOPMP-MFR-014 | §5.1.6 | svi WARL [27:16] | Write svi=5, read | Returns 5 (or rounded to found window after a read) | - |

## 12.4 svc interaction

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-MFR-015 | §5.1.5 | Subsequent violations logged | Read ERR_INFO.svc | svc=1 (subsequent violations in log) | - |
| IOPMP-MFR-016 | §5.1.5 | All SV windows read out (log drained) | Read ERR_INFO.svc | svc reflects whether any SV remains | - |
| IOPMP-MFR-017 | §5.5 | ERR_INFO.v cleared then re-armed | New first violation | Window log behavior resets per capture cycle | - |

---

## Cross-combinations (file-local)

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-MFR-X01 | §5.5 + §4.3 | First violation RRID A captured; subsequent RRID B,C | Read ERR_INFO then drain ERR_MFR | Full picture: ERR_INFO=A details; MFR={B,C} | - |
| IOPMP-MFR-X02 | §5.5 + §5.3 | Non-priority multi-match illegal from several RRIDs | MFR logging | Each offending RRID's SV bit set | - |
| IOPMP-MFR-X03 | §5.5 + §5.4 | Subsequent violation fully suppressed (sire+sere) | Does it set SV? | If no interrupt and no bus error, treated like no-reaction capture - SV behavior follows spec capture exception | - |
| IOPMP-MFR-X04 | §5.5 + §5.6 | MSI mode; multiple violations | Interrupt + MFR | First triggers MSI; subsequent RRIDs logged in MFR (file 13) | - |
