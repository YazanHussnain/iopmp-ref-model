# 13 - Message-Signaled Interrupts (MSI)

**Spec:** §5.6 (MSI Extension), §5.1.7 (ERR_MSIADDR / ERR_MSIADDRH), §5.1.4 (ERR_CFG.msi_sel, msidata), §5.1.5 (ERR_INFO.msi_werr), §5.1.1 (HWCFG2.msi_en).

When `msi_en=1` and `ERR_CFG.msi_sel=1`, the IOPMP delivers its violation interrupt by **writing `msidata` to the address in `ERR_MSIADDR(/H)`** (an IMSIC), instead of a wired interrupt. `ERR_MSIADDR(/H)` and `ERR_CFG` are locked by `ERR_CFG.l`. `msi_werr` flags a failed MSI write.

---

## 13.1 Capability & register presence

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-MSI-001 | §5.1.1 | HWCFG2.msi_en=1 | Read ERR_MSIADDR, ERR_CFG.msi_sel/msidata | Implemented | - |
| IOPMP-MSI-002 | §5.1.1 | msi_en=0 | Read MSI regs | Not implemented; msi_sel must be 0 | - |
| IOPMP-MSI-003 | §5.1.7 | addrh_en=1 & msi_en=1 | Read ERR_MSIADDRH | Implemented (target >34-bit) | - |
| IOPMP-MSI-004 | §5.1.7 | addrh_en=0 | Read ERR_MSIADDRH | Not implemented | - |

## 13.2 Interrupt delivery mode select

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-MSI-005 | §5.6, §5.1.4 | msi_en=1; msi_sel=0; ie=1 | Cause violation | Wired interrupt asserted (no MSI write) | - |
| IOPMP-MSI-006 | §5.6 | msi_sel=1; ie=1; ERR_MSIADDR=T, msidata=D | Cause violation | MSI write of value D to address T issued | - |
| IOPMP-MSI-007 | §5.1.4 | msi_sel hardwired (impl) | Write msi_sel | WARL retains hardwired value | - |
| IOPMP-MSI-008 | §5.6 | msi_sel=1; ie=0 | Cause violation | No MSI (interrupt globally disabled) | - |

## 13.3 MSI address composition

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-MSI-009 | §5.1.7 | addrh_en=0; ERR_MSIADDR.msiaddr=X | Effective target | Address = msiaddr with bits[33:2] semantics per spec (≤34-bit target) | - |
| IOPMP-MSI-010 | §5.1.7 | addrh_en=1; msiaddr + msiaddrh | Effective target | 64-bit address combining low/high; bit[63:32] from msiaddrh | - |
| IOPMP-MSI-011 | §5.1.7 | ERR_MSIADDR WARL | Write target address | Reads back legal value | - |

## 13.4 msidata & msi_werr

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-MSI-012 | §5.1.4 | msidata=D (WARL [18:8]) | Write msidata | MSI payload = D | - |
| IOPMP-MSI-013 | §5.1.5 | MSI write to IMSIC succeeds | After violation | msi_werr=0 | - |
| IOPMP-MSI-014 | §5.1.5 | MSI write fails (bus error on the MSI itself) | After violation | msi_werr=1 (RW1C) | - |
| IOPMP-MSI-015 | §5.1.5 | msi_werr=1 | Write 1 to msi_werr | Clears to 0 | - |
| IOPMP-MSI-016 | §5.1.5 | MSI not available impl | Read msi_werr | Wired 0 | - |

## 13.5 Lock interaction

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-MSI-017 | §5.6 | ERR_CFG.l=1 | Write ERR_MSIADDR / ERR_MSIADDRH / msidata / msi_sel | Rejected (locked by ERR_CFG.l) | - |

---

## Cross-combinations (file-local)

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-MSI-X01 | §5.6 + §4.3.2 | msi_sel=1; violation; v=1 | After MSI, clear v | MSI fired once per capture; clearing v re-arms (file 06) | - |
| IOPMP-MSI-X02 | §5.6 + §5.4 | msi_sel=1; matched entry sire=1 | Read denied | No MSI (interrupt suppressed per-entry) (file 11) | - |
| IOPMP-MSI-X03 | §5.6 + §5.5 | msi_sel=1; first violation MSI; subsequent RRIDs | MSI + MFR | One MSI on first capture; subsequent logged in MFR (file 12) | - |
| IOPMP-MSI-X04 | §5.6 + §5.1.7 | addrh_en=1 target >0x2_0000_0000 | MSI delivery | Full 64-bit address used; msiaddrh required | - |
