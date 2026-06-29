# 14 - Capability Discovery Consistency (HWCFG0/1/2/3)

**Spec:** §4.1.3 (HWCFG0), §4.1.4 (HWCFG1), §4.1.5/5.1.1 (HWCFG2), §4.1.6/Appendix-A (HWCFG3), §4.1.7 (HWCFG_USER), §4.1.8 (ENTRYOFFSET), Tables 4–6.

This file checks **cross-register consistency**: that a capability bit set in HWCFG matches the actual presence/behavior of the corresponding registers and features. Raw read/write semantics are file 01; behavioral tests for each feature live in their own files. These are "the model is internally self-consistent" tests, run for every configuration the generator produces.

---

## 14.1 HWCFG0 ↔ register presence

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-HWCFG-001 | §4.1.3 | addrh_en bit | If HWCFG0.addrh_en=1 | ENTRY_ADDRH(i) and ERR_REQADDRH implemented & writable | - |
| IOPMP-HWCFG-002 | §4.1.3 | addrh_en=0 | ENTRY_ADDRH(i), ERR_REQADDRH | Not implemented (read 0) | |
| IOPMP-HWCFG-003 | §4.1.3 | tor_en bit | If tor_en=0, ENTRY_CFG.a=TOR | TOR not retained (file 04) | |
| IOPMP-HWCFG-004 | §4.1.3 | no_err_rec=1 | ERR_INFO/REQADDR/REQID/USER | Not implemented; ERR_REQID.eid wired 0xffff | - |
| IOPMP-HWCFG-005 | §4.1.3 | HWCFG2_en=1 | HWCFG2 readable at 0x0010 | Present & non-zero capability bits | - |
| IOPMP-HWCFG-006 | §4.1.3 | HWCFG2_en=0 | HWCFG2 | Not implemented; all extension features absent | |
| IOPMP-HWCFG-007 | §4.1.3 | HWCFG3_en=1 | HWCFG3 readable at 0x0014 | Present | - |
| IOPMP-HWCFG-008 | §4.1.3 | md_num field | HWCFG0[29:24] == params.md_num | Matches; MDLCKH present iff md_num>31 | |

## 14.2 HWCFG2 ↔ extension presence (§5.1.1)

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-HWCFG-009 | §5.1.1 | stall_en=1 | MDSTALL/MDSTALLH/RRIDSCP + ERR_CFG.stall_violation_en | All present (file 08) | - |
| IOPMP-HWCFG-010 | §5.1.1 | stall_en=0 | Those registers | Read 0 (file 08) | - |
| IOPMP-HWCFG-011 | §5.1.1 | sps_en=1 | SRCMD_R/W/X(s)(+H) | Present (file 09) | - |
| IOPMP-HWCFG-012 | §5.1.1 | msi_en=1 | ERR_MSIADDR(/H), ERR_CFG.msi_sel/msidata, ERR_INFO.msi_werr | Present (file 13) | - |
| IOPMP-HWCFG-013 | §5.1.1 | mfr_en=1 | ERR_MFR + ERR_INFO.svc | Present (file 12) | - |
| IOPMP-HWCFG-014 | §5.1.1 | peis=1 | ENTRY_CFG.sire/siwe/sixe | Present (file 11) | - |
| IOPMP-HWCFG-015 | §5.1.1 | pees=1 | ENTRY_CFG.sere/sewe/sexe | Present (file 11) | - |
| IOPMP-HWCFG-016 | §5.1.1 | non_prio_en=1 | prio_entry meaningful; non-priority entries | Present (file 10) | - |
| IOPMP-HWCFG-017 | §5.1.1 | prio_ent_prog=1 | prio_entry programmable | Writable; W1CS lock works (file 10) | - |
| IOPMP-HWCFG-018 | §5.1.1 | HWCFG2.rsv [25:18] | Write all-1s | Reads 0 | - |

## 14.3 HWCFG3 ↔ format/feature (Appendix A)

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-HWCFG-019 | §A HWCFG3 | mdcfg_fmt=0 | MDCFG table present | MDCFG(m) implemented (file 03) | |
| IOPMP-HWCFG-020 | §A HWCFG3 | mdcfg_fmt=1/2 | MDCFG table | Not present; md_entry_num used (files 15/16) | - |
| IOPMP-HWCFG-021 | §A HWCFG3 | srcmd_fmt=0 | SRCMD_EN/ENH present | Baseline SRCMD (file 02) | |
| IOPMP-HWCFG-022 | §A HWCFG3 | srcmd_fmt=1 | SRCMD table absent | RRID i->MD i exclusive (file 16) | - |
| IOPMP-HWCFG-023 | §A HWCFG3 | srcmd_fmt=2 | SRCMD_PERM(m)/PERMH(m) present | MD-indexed; SPS unsupported (file 15) | - |
| IOPMP-HWCFG-024 | §A HWCFG3 | md_entry_num (k−1) locked by HWCFG0.enable | Set enable=1 then write md_entry_num | Rejected after enable (file 16) | - |
| IOPMP-HWCFG-025 | §A HWCFG3 | no_w=1 | Global write deny (file 15) | Consistent: writes denied 0x05 | - |
| IOPMP-HWCFG-026 | §A HWCFG3 | no_x=1 | Global fetch deny (file 15) | Consistent: fetch denied 0x05 | - |
| IOPMP-HWCFG-027 | §A HWCFG3 | xinr=1 | Fetch treated as read (file 15) | x-related fields may be 0/absent | - |
| IOPMP-HWCFG-028 | §A HWCFG3 | rrid_transl_en=1 | rrid_transl field meaningful (file 15/17) | Outgoing RRID tagging supported | - |

## 14.4 ENTRYOFFSET & layout

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-HWCFG-029 | §4.1.8 | Entry array after control regs | ENTRYOFFSET + i*16 decode | ENTRY_ADDR/ADDRH/CFG/USER_CFG resolve correctly | |
| IOPMP-HWCFG-030 | §4.1.8 | ENTRYOFFSET negative (array before VERSION) | Decode | Two's-complement offset handled; entries addressable | - |
| IOPMP-HWCFG-031 | §4 | Reserved region 0x0000..(0x1000+rrid_num*32) | Access | Reserved; no aliasing with implemented regs | |

---

## Cross-combinations (file-local)

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-HWCFG-X01 | §5.1.1 + §A.8 | Model=Isolation (srcmd_fmt=1) | sps_en bit | Must be 0 (SPS unsupported) - consistency check (file 16) | - |
| IOPMP-HWCFG-X02 | §5.1.7 + §4.1.3 | msi_en=1 but addrh_en=0 | ERR_MSIADDRH | Absent; MSI target ≤34-bit only (file 13) | - |
| IOPMP-HWCFG-X03 | §A + §5.1.1 | mfr_en=1 requires ERR_INFO.svc | Read svc with mfr_en | svc field active iff mfr_en (file 12) | - |
| IOPMP-HWCFG-X04 | §A.6 + §4.1.4 | k*md_num vs entry_num | Validate k-entry layout | k*md_num may exceed entry_num; high entries treated OFF (file 16) | - |
