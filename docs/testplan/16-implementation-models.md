# 16 - Implementation Models

**Spec:** Appendix A.8 - A.8.1 (Full), A.8.2 (Rapid-k), A.8.3 (Dynamic-k), A.8.4 (Isolation), A.8.5 (Compact-k); built on §A.4 (SRCMD reduction) and §A.6 (MDCFG reduction).

Each model is a specific `(srcmd_fmt, mdcfg_fmt)` combination with distinct behavioral consequences. This file (a) verifies each model's configuration & capability consistency, and (b) **re-tags** representative feature cases from earlier files to be run under each applicable model. The matrix at the end is the master "feature × model" coverage grid.

| Model | srcmd_fmt | mdcfg_fmt | SRCMD table | MDCFG table | SPS | Max RRID |
|-------|-----------|-----------|-------------|-------------|-----|----------|
| Full | 0 | 0 | yes | yes | yes | 65535 |
| Rapid-k | 0 | 1 | yes | no (k fixed) | yes | 65535 |
| Dynamic-k | 0 | 2 | yes | no (k prog) | yes | 65535 |
| Isolation | 1 | 0 | no (i->i) | yes | **no** | 63 |
| Compact-k | 1 | 1 | no (i->i) | no (k fixed) | **no** | 63 |

---

## 16.1 Full model (A.8.1)

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-MODEL-001 | §A.8.1 | srcmd_fmt=0, mdcfg_fmt=0 | Read HWCFG3.srcmd_fmt/mdcfg_fmt | Both 0; full SRCMD + MDCFG | |
| IOPMP-MODEL-002 | §A.8.1 | Full | Run baseline SRCMD/MDCFG/match suite | All file 02/03/05 behaviors hold | |
| IOPMP-MODEL-003 | §A.8.1 | Full | All extensions may be present | Supports SPS, stall, MSI, MFR, non-prio | |

## 16.2 Rapid-k model (A.8.2)

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-MODEL-004 | §A.8.2 | srcmd_fmt=0, mdcfg_fmt=1, k=md_entry_num+1 | Entries of MD m | Computed [m*k, m*k+k−1]; no MDCFG lookup | - |
| IOPMP-MODEL-005 | §A.8.2 | Rapid-k | SRCMD table | Full SRCMD with arbitrary RRID->MD associations | - |
| IOPMP-MODEL-006 | §A.8.2 | Rapid-k | SPS | Supported | - |
| IOPMP-MODEL-007 | §A.8.2 | Rapid-k, k=4 | Direct index calc | entry index = MD*4 + offset (Figure 7 rapid-4) | - |
| IOPMP-MODEL-008 | §A.8.2 | Rapid-k | MDCFGLCK | Omitted (no MDCFG) | - |

## 16.3 Dynamic-k model (A.8.3)

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-MODEL-009 | §A.8.3 | srcmd_fmt=0, mdcfg_fmt=2 | k programmable via md_entry_num | k set before enable; locked after | - |
| IOPMP-MODEL-010 | §A.8.3 | Dynamic-k | Full SRCMD + SPS | Supported | - |
| IOPMP-MODEL-011 | §A.8.3 | Dynamic-k | Reprogram k boundaries at runtime (under stall) | MD boundaries reconfigurable | - |

## 16.4 Isolation model (A.8.4)

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-MODEL-012 | §A.8.4 | srcmd_fmt=1, mdcfg_fmt=0 | Txn rrid=i | Associated exclusively with MD i (1:1) | - |
| IOPMP-MODEL-013 | §A.8.4 | Isolation | Read SRCMD_EN(s) | No physical SRCMD table | - |
| IOPMP-MODEL-014 | §A.8.4 | Isolation | MDCFG table | Full MDCFG; flexible entry allocation per MD | - |
| IOPMP-MODEL-015 | §A.8.4 | Isolation | sps_en | Must be 0 (SPS unsupported) | - |
| IOPMP-MODEL-016 | §A.8.4 | Isolation | RRID ≥ 63 | Not supported (≤63 RRIDs) | - |
| IOPMP-MODEL-017 | §A.8.4 | Isolation; rrid=i, MD i has no entries | Txn | ILLEGAL 0x05 | - |

## 16.5 Compact-k model (A.8.5)

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-MODEL-018 | §A.8.5 | srcmd_fmt=1, mdcfg_fmt=1 | Txn rrid=i | MD i = RRID i; entries [i*k, i*k+k−1] (Figure 8) | - |
| IOPMP-MODEL-019 | §A.8.5 | Compact-k | Both SRCMD & MDCFG tables | Neither implemented | - |
| IOPMP-MODEL-020 | §A.8.5 | Compact-k | sps_en | Must be 0 | - |
| IOPMP-MODEL-021 | §A.8.5 | Compact-k, k=4 | entry index = RRID*4 + offset | Direct calc; minimum area | - |
| IOPMP-MODEL-022 | §A.8.5 | Compact-k | ≤63 RRIDs | RRID 63+ unsupported | - |

---

## 16.6 Feature × Model coverage matrix

Re-run the listed representative feature cases under each model where the feature applies. `✓` = applies/runs; `n/a` = feature absent in that model (assert capability bit = 0 instead).

| Feature (rep. case) | Full | Rapid-k | Dynamic-k | Isolation | Compact-k |
|---------------------|------|---------|-----------|-----------|-----------|
| SRCMD assoc (SRCMD-001) | ✓ | ✓ | ✓ | n/a (i->i) | n/a (i->i) |
| MDCFG range (MDCFG-002) | ✓ | n/a (k) | n/a (k) | ✓ | n/a (k) |
| Priority match (MATCH-007) | ✓ | ✓ | ✓ | ✓ | ✓ |
| Partial hit (MATCH-012) | ✓ | ✓ | ✓ | ✓ | ✓ |
| Perm deny (MATCH-002) | ✓ | ✓ | ✓ | ✓ | ✓ |
| Error capture (ERR-001) | ✓ | ✓ | ✓ | ✓ | ✓ |
| Locks (LOCK-012/020) | ✓ | ✓ (ENTRYLCK) | ✓ | ✓ | ✓ (ENTRYLCK) |
| Stall (STALL-005) | ✓ | ✓ | ✓ | ✓ (1:1) | ✓ (1:1) |
| SPS (SPS-005) | ✓ | ✓ | ✓ | **n/a** | **n/a** |
| Non-priority (NPRIO-007) | ✓ | ✓ | ✓ | ✓ | ✓ |
| Suppression (SUPP-005) | ✓ | ✓ | ✓ | ✓ | ✓ |
| MSI (MSI-006) | ✓ | ✓ | ✓ | ✓ | ✓ |
| MFR (MFR-003) | ✓ | ✓ | ✓ | ✓ | ✓ |

---

## Cross-combinations (file-local)

| Test ID | Spec Ref | Test Condition | Test Description | Expected Scenario | Gap |
|---------|----------|----------------|------------------|-------------------|-----|
| IOPMP-MODEL-X01 | §A.8.4 + §5.2 | Isolation declares sps_en=1 (illegal config) | Init | Inconsistent capability - generator must not produce; flagged invalid | - |
| IOPMP-MODEL-X02 | §A.8.5 + §5.7 | Compact-k under stall; MDSTALL.md selects MD m | Stall | Only RRID m stalled (1:1) (file 08 STALL-X05) | - |
| IOPMP-MODEL-X03 | §A.8.2 + §A.6 | Rapid-k, k*md_num vs entry_num | Out-of-range entry | High entries OFF (file 04) | - |
| IOPMP-MODEL-X04 | §A.8.* + §17 | Two instances different models coexist | System routing | Independent behavior (file 17) | |
