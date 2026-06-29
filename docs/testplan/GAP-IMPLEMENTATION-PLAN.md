# IOPMP Reference Model - Gap Implementation Plan

> **STATUS: WU-1…WU-10 DONE** (branch `feat/close-spec-gaps`, all 10 suites pass,
> clean build). Commits:
> - `3d29c36` feat(model): spec-align registers + close extension gaps (WU-1…9, WU-10 enum)
> - `2b2f499` test(phase9): gap-closure suite (12 tests)
> - `<this>`  feat: WU-10 - HWCFG3 writability + config validation, +4 tests
>
> WU-10 closes: `md_entry_num` programmable-until-enable / Dynamic-k runtime `k`
> (HWCFG-024 / APPN-026 / MODEL-009), `rrid_transl_prog` W1CS (APPN-030/031),
> init rejection of `spsEn` with srcmd_fmt 1/2 (MODEL-X01 / HWCFG-X01), and
> MDCFG-absent reads when mdcfg_fmt != 0 (HWCFG-020).
>
> HWCFG0/HWCFG3 bit layout is now byte-exact with spec Table 8 (no_w/no_x,
> rrid_transl_en moved into HWCFG3; md_entry_num narrowed to bits 10:4).
>
> **Remaining (intentional, IMP-defined by the spec):** ENTRY_USER_CFG enforcement
> is a storage/no-op hook (the check API carries no user attribute to evaluate);
> source-enforcement and per-RRID illegality are driven by caller-supplied config
> vectors. Nothing else from the audit is open.


Derived from a 6-cluster source audit reconciling the test plan's 317 - gap flags against the actual `libiopmp`/`libsystem` source. Many flags were stale (feature already implemented). This plan lists only the **true gaps**, grouped into work units, with the spec basis, files touched, and test strategy.

Baseline: model builds clean; all 9 existing test binaries pass.

---

## Two architectural divergences (model ≠ spec)

These are the largest items and were implemented in the model with a simplified shape that does not match the spec. The **test plan is already written to the spec**, so the model must move to the spec shape (and the existing phase6/phase8 tests that use the old shape will be updated).

### D1 - Per-entry suppression: 2-bit -> 6-bit (spec §5.1.11, §5.4)
- **Now:** single `ENTRY_CFG_PEIS_BIT` (bit 6) + `ENTRY_CFG_PEES_BIT` (bit 7), applied to all access types.
- **Spec:** `sire`(5) `siwe`(6) `sixe`(7) interrupt-suppress + `sere`(8) `sewe`(9) `sexe`(10) bus-error-suppress, **per access type**.
- Interrupt fires iff `ie && !si<x>e`; bus error iff `!rs && !se<x>e`; the two dimensions independent.

### D2 - Non-priority entries: global bool -> boundary index (spec §5.1.1, §5.3)
- **Now:** global `prientProg` bool gates the feature; matcher returns on first full-cover match.
- **Spec:** `HWCFG2.prio_entry`(15:0) boundary, `prio_ent_prog`(16, W1CS), `non_prio_en`(17). Entries `< prio_entry` are priority; `≥ prio_entry` are non-priority. Must **collect all** matching non-priority entries (each covering all bytes); legal if **any** permits; never report 0x04 for NP; §5.4.3 AND/OR suppression across the matched NP set.

---

## Work units (true gaps only)

### WU-1 - Per-entry suppression rework (D1)  ·  §5.1.11, §5.4  ·  files 11, (10 cross)
- Add 6 ENTRY_CFG bits + masks; gate by `peisEn`/`peesEn`.
- Rework `iopmp_error.c` interrupt decision and `iopmp_translate.c` `suppressError` to be per-access-type.
- Update existing phase6/phase8 tests that used PEIS/PEES single bits.

### WU-2 - Non-priority boundary + multi-match (D2)  ·  §5.1.1, §5.3  ·  file 10
- Add `prio_entry`/`prio_ent_prog`/`non_prio_en` to HWCFG2 + state; W1CS write path.
- Rework matcher: priority pass (index order, partial-hit) then non-priority pass (collect all full-cover matches, OR permits, no 0x04).
- §5.4.3 AND/OR suppression across matched NP set (depends on WU-1).

### WU-3 - Multi-Faults Record  ·  §5.1.5/5.1.6, §5.5  ·  file 12
- Add `ERR_MFR.svw/svi/svs` fields + `ERR_INFO.svc` bit + masks.
- Add per-RRID `SV[]` bitmap state (alloc when `multifaultEn`).
- `ErrorRecord`: on subsequent violation (v=1) set `SV[rrid]`, set `svc`.
- `ERR_MFR` read: window scan from `svi`, wrap-around, clear-on-read, `svs`.

### WU-4 - MSI extension  ·  §5.1.4/5.1.5/5.1.7, §5.6  ·  file 13
- Add `ERR_CFG.msi_sel`(3) + `msidata`(18:8); `ERR_INFO.msi_werr`(3, RW1C) + masks.
- Delivery selection by `msi_sel` (not just `msiEn`); no MSI when interrupt suppressed / `ie=0`.
- `ERR_MSIADDR/H` composition + WARL; `ERR_CFG.l` locks MSI registers.

### WU-5 - RRIDSCP + stall-violation  ·  §5.1.3, §5.7.4/5.7.5  ·  file 8
- RRIDSCP register handler: `op` 0=query/1=stall/2=unstall/3=rsv; `stat`; per-RRID override of stall mask.
- Add `IOPMP_ETYPE_STALL_VIOLATION` (0x07) + `ERR_CFG.stall_violation_en`(4); fault stalled txn -> capture 0x07 when set.
- **Bug fix:** `iopmp_stall.c` - MDSTALLH-only write must NOT recompute `rrid_stall` (only `MDSTALL.exempt` write triggers).

### WU-6 - SPS high-MD + lock enforcement  ·  §5.1.8-10, §5.2  ·  file 9
- Add `srcmdRh/Wh/Xh` tables (MD 31-62) + register offsets/handlers; extend `SpsCheckPermission` past MD 32.
- Enforce `SRCMD_EN(s).l` and `MDLCK.md[m]` on `SRCMD_R/W/X` writes.

### WU-7 - Lock prelocked/hardwired + optional-disable  ·  §3.5, §4.2  ·  file 7, (6)
- New params: `mdcfglckResetF`, `entrylckResetF`, `srcmdPrelock`, `entrylckHardwired`, `mdlckEn`, `eidEn`.
- Init writes prelocked reset values; read/write honor hardwired; `ERR_REQID.eid` / read path honor `eidEn`.

### WU-8 - Register/WARL + error-record smaller gaps  ·  §4.1/4.3/4.6  ·  files 1, 4, 6
- HWCFG2/HWCFG3 W1CS write path (`prio_ent_prog`, `rrid_transl_prog`).
- ENTRYOFFSET signed (int32_t) semantics.
- `no_err_rec` ⇒ `ERR_REQID.eid` reads 0xffff.
- ERR_USER(0..7) population; user-defined etype 0x0E/0x0F hook.
- ENTRY_ADDRH WARL mask (hardwired MSBs) via `addrhMask` param.
- ENTRY_USER_CFG semantics + enforcement.
- Verify `ERR_REQADDRH` bit-range ([65:34] vs the model's `>>32`).

### WU-9 - Improper MDCFG (§A.5)  ·  file 3
- Implement one reference behavior (recommend **reject-on-write** - cleanest, testable). Validate write keeps table monotonic; reject otherwise.

### WU-10 - Models + config validation  ·  §A.6, §A.8  ·  files 14, 15, 16, 17
- Add `IOPMP_MODEL_DYNAMIC_K`; validate enum ↔ `(srcmd_fmt, mdcfg_fmt)`.
- Reject illegal combos at init (`srcmd_fmt=1`/`fmt=2` with `spsEn=1`).
- HWCFG3 `md_entry_num` writable-before-enable, locked-after.
- MDCFG reads return 0/absent when `mdcfg_fmt=1/2`.
- `rrid_transl_prog` W1CS lock.

---

## Recommended execution order

1. WU-8 (register plumbing) + WU-7 (lock params) - foundational, unblocks others.
2. WU-1 (suppression) -> WU-2 (non-priority) - sequential (WU-2 §5.4.3 depends on WU-1).
3. WU-3 (MFR) + WU-4 (MSI) - error-path features, can pair.
4. WU-5 (RRIDSCP/stall) + WU-6 (SPS) - independent.
5. WU-9 (improper MDCFG) + WU-10 (models/validation) - finalize.

Each WU done TDD: write/generate the corresponding test-plan cases as C tests first (red), implement (green), keep all prior phase tests passing.

---

## Items recommended for DEFER / IMP-defined (not forced into the model)

These spec points are explicitly implementation-defined; forcing one behavior adds config surface without clear value. Propose marking the corresponding test-plan rows "IMP-defined - config-gated / not modeled" rather than implementing:
- `IOPMP-APPN-023` source-enforcement subset (system-integration concern, not core IOPMP logic).
- `IOPMP-SRCMD-017` per-RRID implementation-defined illegality (needs an arbitrary illegal-RRID vector).
- `IOPMP-ENTRY-027` permission-combo WARL constraints (arbitrary per-impl).
- `IOPMP-ENTRY-028` / ENTRY_USER_CFG semantics (user-defined by definition) - implement storage + a hook, but enforcement stays a no-op unless a use case is defined.
