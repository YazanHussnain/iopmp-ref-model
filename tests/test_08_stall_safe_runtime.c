/*
 * test_08_stall_safe_runtime.c
 *
 * Test suite for docs/testplan/08-stall-safe-runtime.md:
 *   "Stall Mechanism & Safe Runtime Configuration".
 *
 * Spec: §5.7 (Safe Runtime Configuration), §5.1.2 (MDSTALL/MDSTALLH),
 *       §5.1.3 (RRIDSCP), §5.1.4 (ERR_CFG.stall_violation_en).
 *
 * SPEC-compliant assertions. The key spec rule (§5.7.3) is:
 *   rrid_stall[s] = MDSTALL.exempt XOR Reduction_OR(SRCMD(s).md & stall_by_md)
 * sampled at the moment MDSTALL is written (snapshot semantics).
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include "iopmp.h"
#include "test_utils.h"

/* ── Shared setup helpers ────────────────────────────────────────────── */

static IopmpParams_t MakeStallParams(uint16_t rridNum, uint16_t entryNum, uint8_t mdNum)
{
    IopmpParams_t params;
    memset(&params, 0, sizeof(params));
    params.rridNum  = rridNum;
    params.entryNum = entryNum;
    params.mdNum    = mdNum;
    params.torEn    = true;
    params.stallEn  = true;
    params.hwcfg2En = true;
    params.model    = IOPMP_MODEL_FULL;
    return params;
}

static void EnableIopmp(IopmpState_t *iopmp)
{
    IopmpWriteReg(iopmp, REG_HWCFG0, HWCFG0_ENABLE_BIT);
}
static void SetupSrcmd(IopmpState_t *iopmp, uint16_t rrid, uint8_t md)
{
    uint32_t slot = REG_SRCMD_BASE + (uint32_t)rrid * REG_SRCMD_STRIDE;
    if (md < 31U) {
        uint32_t off = slot + REG_SRCMD_EN_OFF;
        IopmpWriteReg(iopmp, off, IopmpReadReg(iopmp, off) | (1U << ((uint32_t)md + 1U)));
    } else {
        uint32_t off = slot + REG_SRCMD_ENH_OFF;         /* MDs 31-62 */
        IopmpWriteReg(iopmp, off, IopmpReadReg(iopmp, off) | (1U << ((uint32_t)md - 31U)));
    }
}
static void SetupMdcfg(IopmpState_t *iopmp, uint8_t md, uint32_t endEntry)
{
    IopmpWriteReg(iopmp, REG_MDCFG_BASE + (uint32_t)md * REG_MDCFG_STRIDE, endEntry);
}
static void SetNa4(IopmpState_t *iopmp, uint32_t idx, uint64_t addr, uint32_t perm)
{
    uint32_t base = IopmpReadReg(iopmp, REG_ENTRYOFFSET) + idx * REG_ENTRY_STRIDE;
    IopmpWriteReg(iopmp, base + REG_ENTRY_ADDR_OFF, (uint32_t)(addr >> 2U));
    IopmpWriteReg(iopmp, base + REG_ENTRY_CFG_OFF, (ADDR_MODE_NA4 << ENTRY_CFG_A_SHIFT) | perm);
}

/* Commit an MDSTALL selection. mdSet: bit m = select MD m. */
static void Mdstall(IopmpState_t *iopmp, uint64_t mdSet, bool exempt)
{
    uint32_t high = (uint32_t)(mdSet >> 31U);            /* MDs 31-62 */
    if (high) IopmpWriteReg(iopmp, REG_MDSTALLH, high);
    uint32_t low = (uint32_t)((mdSet & 0x7FFFFFFFULL) << 1U);
    IopmpWriteReg(iopmp, REG_MDSTALL, low | (exempt ? MDSTALL_EXEMPT_BIT : 0U));
}

/* Read rrid_stall[s] via an RRIDSCP query: returns RRIDSCP_STAT_*. */
static uint32_t StallStat(IopmpState_t *iopmp, uint16_t s)
{
    IopmpWriteReg(iopmp, REG_RRIDSCP,
                  ((uint32_t)s & RRIDSCP_RRID_MASK) | (RRIDSCP_OP_QUERY << RRIDSCP_OP_SHIFT));
    return (IopmpReadReg(iopmp, REG_RRIDSCP) & RRIDSCP_OP_MASK) >> RRIDSCP_OP_SHIFT;
}

/* ───────────────────────────────────────────────────────────────────
 * 8.1 Capability & register presence
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-STALL-001 - stall_en=1: MDSTALL/MDSTALLH/RRIDSCP implemented & writable. */
static void TestStall001_Implemented(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeStallParams(2, 4, 40);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    Mdstall(&iopmp, 1ULL << 0, false);                   /* select MD0 */
    assert((IopmpReadReg(&iopmp, REG_MDSTALL) & MDSTALL_MD_MASK) == (1U << 1));
    IopmpWriteReg(&iopmp, REG_MDSTALLH, 0x2U);
    assert(IopmpReadReg(&iopmp, REG_MDSTALLH) == 0x2U);
    assert(StallStat(&iopmp, 0) != RRIDSCP_STAT_NOIMPL);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-STALL-001 stall registers implemented");
}

/* IOPMP-STALL-002 - stall_en=0: all stall regs + stall_violation_en read 0. */
static void TestStall002_AbsentReadsZero(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeStallParams(2, 4, 40);
    params.stallEn = false;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    IopmpWriteReg(&iopmp, REG_MDSTALL,  0xFFFFFFFFU);
    IopmpWriteReg(&iopmp, REG_MDSTALLH, 0xFFFFFFFFU);
    IopmpWriteReg(&iopmp, REG_RRIDSCP,  0xFFFFFFFFU);
    IopmpWriteReg(&iopmp, REG_ERR_CFG,  ERR_CFG_STALL_VIOL_BIT);
    assert(IopmpReadReg(&iopmp, REG_MDSTALL)  == 0U);
    assert(IopmpReadReg(&iopmp, REG_MDSTALLH) == 0U);
    assert(IopmpReadReg(&iopmp, REG_RRIDSCP)  == 0U);
    assert((IopmpReadReg(&iopmp, REG_ERR_CFG) & ERR_CFG_STALL_VIOL_BIT) == 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-STALL-002 stall absent reads 0");
}

/* IOPMP-STALL-003 - md_num<32: MDSTALLH wired 0. */
static void TestStall003_MdstallhWiredZero(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeStallParams(2, 4, 8);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    IopmpWriteReg(&iopmp, REG_MDSTALLH, 0xFFFFFFFFU);
    assert(IopmpReadReg(&iopmp, REG_MDSTALLH) == 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-STALL-003 MDSTALLH wired 0 when md_num<32");
}

/* IOPMP-STALL-004 - unimplemented MDSTALL.md bits read back 0. */
static void TestStall004_PartialMdBits(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeStallParams(2, 8, 4);      /* MDs 0..3 */
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    IopmpWriteReg(&iopmp, REG_MDSTALL, 0xFFFFFFFEU);      /* all md bits, exempt=0 */
    /* Only MDs 0..3 (bits 1..4) are implemented. */
    assert((IopmpReadReg(&iopmp, REG_MDSTALL) & MDSTALL_MD_MASK) == 0x1EU);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-STALL-004 unimplemented md bits read 0");
}

/* ───────────────────────────────────────────────────────────────────
 * 8.2 MDSTALL semantics & rrid_stall derivation
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-STALL-005 - exempt=0: associated RRIDs are stalled. */
static void TestStall005_StallAssociated(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeStallParams(3, 4, 3);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    SetupSrcmd(&iopmp, 1, 2);                             /* RRID1 -> MD2 */

    Mdstall(&iopmp, 1ULL << 2, false);                   /* stall MD2, exempt=0 */
    assert(StallStat(&iopmp, 1) == RRIDSCP_STAT_STALLED);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-STALL-005 exempt=0 stalls associated RRID");
}

/* IOPMP-STALL-006 - exempt=1: associated run, others stall. */
static void TestStall006_ExemptInverts(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeStallParams(3, 4, 3);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    SetupSrcmd(&iopmp, 1, 2);                             /* RRID1 -> MD2 */
    /* RRID2 associated with no MD. */

    Mdstall(&iopmp, 1ULL << 2, true);                    /* exempt=1, select MD2 */
    assert(StallStat(&iopmp, 1) == RRIDSCP_STAT_NOTSTALLED);  /* associated runs */
    assert(StallStat(&iopmp, 2) == RRIDSCP_STAT_STALLED);     /* others stalled */

    IopmpDestroy(&iopmp);
    PASS("IOPMP-STALL-006 exempt=1 inverts selection");
}

/* IOPMP-STALL-007 - exempt=0, md=0: nothing stalled. */
static void TestStall007_NoneSelected(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeStallParams(2, 4, 2);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    SetupSrcmd(&iopmp, 0, 0);

    Mdstall(&iopmp, 0ULL, false);
    assert(StallStat(&iopmp, 0) == RRIDSCP_STAT_NOTSTALLED);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-STALL-007 exempt=0 md=0 stalls none");
}

/* IOPMP-STALL-008 - exempt=1, md=0: all RRIDs stalled. */
static void TestStall008_ExemptEmptyStallsAll(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeStallParams(3, 4, 2);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    SetupSrcmd(&iopmp, 0, 0);

    Mdstall(&iopmp, 0ULL, true);                         /* exempt of empty set */
    for (uint16_t s = 0U; s < 3U; s++)
        assert(StallStat(&iopmp, s) == RRIDSCP_STAT_STALLED);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-STALL-008 exempt=1 md=0 stalls all");
}

/* IOPMP-STALL-009 - MDSTALL.md reads back the selection. */
static void TestStall009_MdReadback(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeStallParams(2, 4, 4);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    Mdstall(&iopmp, (1ULL << 1) | (1ULL << 3), false);   /* MD1, MD3 */
    uint32_t md = IopmpReadReg(&iopmp, REG_MDSTALL) & MDSTALL_MD_MASK;
    assert(md == ((1U << 2) | (1U << 4)));               /* MD m -> bit m+1 */

    IopmpDestroy(&iopmp);
    PASS("IOPMP-STALL-009 MDSTALL.md readback");
}

/* IOPMP-STALL-010 - rrid_stall snapshots SRCMD at write time. */
static void TestStall010_Snapshot(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeStallParams(2, 4, 2);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    SetupSrcmd(&iopmp, 0, 0);                             /* RRID0 -> MD0 */

    Mdstall(&iopmp, 1ULL << 0, false);                   /* snapshot: RRID0 stalled */
    assert(StallStat(&iopmp, 0) == RRIDSCP_STAT_STALLED);

    /* Change SRCMD afterwards: snapshot must NOT change. */
    IopmpWriteReg(&iopmp, REG_SRCMD_BASE + REG_SRCMD_EN_OFF, 0U);   /* deassociate */
    assert(StallStat(&iopmp, 0) == RRIDSCP_STAT_STALLED);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-STALL-010 rrid_stall snapshot semantics");
}

/* IOPMP-STALL-011 - MDSTALLH selects a high MD; combined with MDSTALL. */
static void TestStall011_HighMd(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeStallParams(2, 4, 40);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    SetupSrcmd(&iopmp, 0, 35);                            /* RRID0 -> MD35 */

    Mdstall(&iopmp, 1ULL << 35, false);                  /* select high MD35 */
    assert(StallStat(&iopmp, 0) == RRIDSCP_STAT_STALLED);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-STALL-011 high-MD stall via MDSTALLH");
}

/* ───────────────────────────────────────────────────────────────────
 * 8.3 is_busy handshake
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-STALL-012/014 - is_busy reads 0 (synchronous model). */
static void TestStall012_IsBusyZero(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeStallParams(2, 4, 2);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    SetupSrcmd(&iopmp, 0, 0);

    Mdstall(&iopmp, 1ULL << 0, false);
    assert((IopmpReadReg(&iopmp, REG_MDSTALL) & MDSTALL_IS_BUSY_BIT) == 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-STALL-012/014 is_busy reads 0");
}

/* IOPMP-STALL-013 - resume: is_busy 0 and all resumed. */
static void TestStall013_ResumeIsBusy(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeStallParams(2, 4, 2);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    SetupSrcmd(&iopmp, 0, 0);

    Mdstall(&iopmp, 1ULL << 0, false);
    IopmpWriteReg(&iopmp, REG_MDSTALLH, 0U);
    IopmpWriteReg(&iopmp, REG_MDSTALL, 0U);              /* resume */
    assert((IopmpReadReg(&iopmp, REG_MDSTALL) & MDSTALL_IS_BUSY_BIT) == 0U);
    assert(StallStat(&iopmp, 0) == RRIDSCP_STAT_NOTSTALLED);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-STALL-013 resume clears is_busy");
}

/* ───────────────────────────────────────────────────────────────────
 * 8.4 Stall effect on transactions
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-STALL-015 - stalled RRID: txn held (not checked). */
static void TestStall015_TxnHeld(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeStallParams(2, 4, 2);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    EnableIopmp(&iopmp);
    SetupMdcfg(&iopmp, 0, 4);
    SetupSrcmd(&iopmp, 0, 0);
    SetNa4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_R_BIT);

    Mdstall(&iopmp, 1ULL << 0, false);
    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ);
    assert(r.stalled && !r.legal);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-STALL-015 stalled txn held");
}

/* IOPMP-STALL-016 - non-stalled RRID checked normally during others' stall. */
static void TestStall016_OthersUnaffected(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeStallParams(2, 4, 2);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    EnableIopmp(&iopmp);
    SetupMdcfg(&iopmp, 0, 2);
    SetupMdcfg(&iopmp, 1, 4);
    SetupSrcmd(&iopmp, 0, 0);
    SetupSrcmd(&iopmp, 1, 1);
    SetNa4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_R_BIT);
    SetNa4(&iopmp, 2, 0x2000ULL, ENTRY_CFG_R_BIT);

    Mdstall(&iopmp, 1ULL << 0, false);                   /* stall MD0 only */
    assert(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ).stalled);
    TxnResult_t r1 = IopmpCheckAccess(&iopmp, 1, 0x2000ULL, 4, IOPMP_TXN_READ);
    ASSERT_LEGAL(r1);
    assert(!r1.stalled);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-STALL-016 non-stalled RRID unaffected");
}

/* IOPMP-STALL-017 - resume: previously stalled txn now checked. */
static void TestStall017_ResumeChecks(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeStallParams(2, 4, 2);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    EnableIopmp(&iopmp);
    SetupMdcfg(&iopmp, 0, 4);
    SetupSrcmd(&iopmp, 0, 0);
    SetNa4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_R_BIT);

    Mdstall(&iopmp, 1ULL << 0, false);
    assert(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ).stalled);
    Mdstall(&iopmp, 0ULL, false);                        /* resume */
    ASSERT_LEGAL(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ));

    IopmpDestroy(&iopmp);
    PASS("IOPMP-STALL-017 resume re-checks txn");
}

/* ───────────────────────────────────────────────────────────────────
 * 8.5 RRIDSCP - cherry pick
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-STALL-018 - RRIDSCP op=stall sets rrid_stall[s]. */
static void TestStall018_RridscpStall(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeStallParams(2, 4, 2);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    IopmpWriteReg(&iopmp, REG_RRIDSCP, 1U | (RRIDSCP_OP_STALL << RRIDSCP_OP_SHIFT));
    assert(StallStat(&iopmp, 1) == RRIDSCP_STAT_STALLED);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-STALL-018 RRIDSCP op=stall");
}

/* IOPMP-STALL-019 - RRIDSCP op=nostall clears rrid_stall[s]. */
static void TestStall019_RridscpNoStall(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeStallParams(2, 4, 2);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    SetupSrcmd(&iopmp, 1, 0);
    Mdstall(&iopmp, 1ULL << 0, false);                   /* stall RRID1 */
    assert(StallStat(&iopmp, 1) == RRIDSCP_STAT_STALLED);

    IopmpWriteReg(&iopmp, REG_RRIDSCP, 1U | (RRIDSCP_OP_NOSTALL << RRIDSCP_OP_SHIFT));
    assert(StallStat(&iopmp, 1) == RRIDSCP_STAT_NOTSTALLED);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-STALL-019 RRIDSCP op=nostall");
}

/* IOPMP-STALL-020 - RRIDSCP query stat reflects stall state. */
static void TestStall020_RridscpQuery(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeStallParams(2, 4, 2);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    SetupSrcmd(&iopmp, 0, 0);

    assert(StallStat(&iopmp, 0) == RRIDSCP_STAT_NOTSTALLED);
    Mdstall(&iopmp, 1ULL << 0, false);
    assert(StallStat(&iopmp, 0) == RRIDSCP_STAT_STALLED);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-STALL-020 RRIDSCP query stat");
}

/* IOPMP-STALL-021 - RRIDSCP not implemented (stall_en=0): stat=0. */
static void TestStall021_RridscpNotImpl(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeStallParams(2, 4, 2);
    params.stallEn = false;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    assert(StallStat(&iopmp, 0) == RRIDSCP_STAT_NOIMPL);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-STALL-021 RRIDSCP not implemented stat=0");
}

/* IOPMP-STALL-022 - RRIDSCP op=3 reserved: no effect. */
static void TestStall022_RridscpReservedOp(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeStallParams(2, 4, 2);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    SetupSrcmd(&iopmp, 1, 0);

    IopmpWriteReg(&iopmp, REG_RRIDSCP, 1U | (3U << RRIDSCP_OP_SHIFT));   /* reserved op */
    assert(StallStat(&iopmp, 1) == RRIDSCP_STAT_NOTSTALLED);            /* unchanged */

    IopmpDestroy(&iopmp);
    PASS("IOPMP-STALL-022 RRIDSCP reserved op no effect");
}

/* IOPMP-STALL-023 - unselectable RRID: stat=3. */
static void TestStall023_Unselectable(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeStallParams(2, 4, 2);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    assert(StallStat(&iopmp, 2) == RRIDSCP_STAT_UNSEL);   /* rrid >= rrid_num */

    IopmpDestroy(&iopmp);
    PASS("IOPMP-STALL-023 unselectable RRID stat=3");
}

/* ───────────────────────────────────────────────────────────────────
 * 8.6 Faulting stalled transactions (etype 0x07)
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-STALL-024 - stall_violation_en=1: stalled txn faulted 0x07. */
static void TestStall024_StallFault(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeStallParams(2, 4, 2);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    EnableIopmp(&iopmp);
    SetupMdcfg(&iopmp, 0, 4);
    SetupSrcmd(&iopmp, 0, 0);
    SetNa4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_R_BIT);
    IopmpWriteReg(&iopmp, REG_ERR_CFG, ERR_CFG_STALL_VIOL_BIT);
    Mdstall(&iopmp, 1ULL << 0, false);

    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ);
    ASSERT_ETYPE(r, IOPMP_ETYPE_STALL_VIOL);
    assert(((IopmpReadReg(&iopmp, REG_ERR_INFO) & ERR_INFO_ETYPE_MASK) >> ERR_INFO_ETYPE_SHIFT)
           == 0x07U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-STALL-024 stall violation faulted 0x07");
}

/* IOPMP-STALL-025 - stall_violation_en=0: stalled txn held, never 0x07. */
static void TestStall025_NoFaultWhenDisabled(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeStallParams(2, 4, 2);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    EnableIopmp(&iopmp);
    SetupMdcfg(&iopmp, 0, 4);
    SetupSrcmd(&iopmp, 0, 0);
    SetNa4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_R_BIT);
    Mdstall(&iopmp, 1ULL << 0, false);

    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ);
    assert(r.stalled && !r.legal);
    assert((IopmpReadReg(&iopmp, REG_ERR_INFO) & ERR_INFO_V_BIT) == 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-STALL-025 held, no fault when disabled");
}

/* IOPMP-STALL-026 - stall_violation_en WARL write 1/0. */
static void TestStall026_StallViolWarl(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeStallParams(2, 4, 2);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    IopmpWriteReg(&iopmp, REG_ERR_CFG, ERR_CFG_STALL_VIOL_BIT);
    assert((IopmpReadReg(&iopmp, REG_ERR_CFG) & ERR_CFG_STALL_VIOL_BIT) != 0U);
    IopmpWriteReg(&iopmp, REG_ERR_CFG, 0U);
    assert((IopmpReadReg(&iopmp, REG_ERR_CFG) & ERR_CFG_STALL_VIOL_BIT) == 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-STALL-026 stall_violation_en WARL");
}

/* ───────────────────────────────────────────────────────────────────
 * 8.7 Programming order (§5.7.7)
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-STALL-027 - full atomic stall -> update -> resume sequence. */
static void TestStall027_FullSequence(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeStallParams(2, 4, 2);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    EnableIopmp(&iopmp);
    SetupMdcfg(&iopmp, 0, 4);
    SetupSrcmd(&iopmp, 0, 0);
    SetNa4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_R_BIT);
    ASSERT_LEGAL(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ));

    /* Step 1: stall. */
    IopmpWriteReg(&iopmp, REG_MDSTALLH, 0U);
    IopmpWriteReg(&iopmp, REG_MDSTALL, (1U << 1));        /* select MD0, exempt=0 */
    assert((IopmpReadReg(&iopmp, REG_MDSTALL) & MDSTALL_IS_BUSY_BIT) == 0U);
    assert(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ).stalled);

    /* Step 2: update rules while stalled (revoke read permission). */
    SetNa4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_W_BIT);

    /* Step 3: resume. */
    IopmpWriteReg(&iopmp, REG_MDSTALLH, 0U);
    IopmpWriteReg(&iopmp, REG_MDSTALL, 0U);
    assert((IopmpReadReg(&iopmp, REG_MDSTALL) & MDSTALL_IS_BUSY_BIT) == 0U);

    /* New rule in effect: read now denied. */
    ASSERT_ETYPE(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ),
                 IOPMP_ETYPE_ILLEGAL_READ);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-STALL-027 full atomic sequence");
}

/* IOPMP-STALL-028 - second MDSTALL before resume still yields a defined state. */
static void TestStall028_DoubleStall(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeStallParams(2, 4, 2);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    SetupSrcmd(&iopmp, 0, 0);

    Mdstall(&iopmp, 1ULL << 0, false);
    Mdstall(&iopmp, 1ULL << 0, false);                   /* second write before resume */
    /* Spec leaves this undefined; the model re-snapshots, so RRID0 stays stalled. */
    assert(StallStat(&iopmp, 0) == RRIDSCP_STAT_STALLED);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-STALL-028 double stall handled");
}

/* IOPMP-STALL-029 - resume (MDSTALL=0) clears rrid_stall. */
static void TestStall029_ResumeClears(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeStallParams(2, 4, 2);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    SetupSrcmd(&iopmp, 0, 0);

    Mdstall(&iopmp, 1ULL << 0, false);
    assert(StallStat(&iopmp, 0) == RRIDSCP_STAT_STALLED);
    Mdstall(&iopmp, 0ULL, false);
    assert(StallStat(&iopmp, 0) == RRIDSCP_STAT_NOTSTALLED);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-STALL-029 resume clears rrid_stall");
}

/* IOPMP-STALL-030 - MDSTALLH alone does not update rrid_stall. */
static void TestStall030_MdstallhAloneNoUpdate(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeStallParams(2, 4, 40);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    SetupSrcmd(&iopmp, 0, 35);

    IopmpWriteReg(&iopmp, REG_MDSTALLH, (1U << 4));       /* stage MD35, no MDSTALL */
    assert(StallStat(&iopmp, 0) == RRIDSCP_STAT_NOTSTALLED);   /* not committed */

    IopmpDestroy(&iopmp);
    PASS("IOPMP-STALL-030 MDSTALLH alone does not commit");
}

/* ───────────────────────────────────────────────────────────────────
 * Cross-combinations (file-local)
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-STALL-X01 - stall, reprogram MDCFG range, resume: new range applies. */
static void TestStallX01_ReconfigMdcfg(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeStallParams(2, 4, 2);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    EnableIopmp(&iopmp);
    SetupMdcfg(&iopmp, 0, 2);
    SetupSrcmd(&iopmp, 0, 0);
    SetNa4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_R_BIT);
    ASSERT_LEGAL(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ));

    Mdstall(&iopmp, 1ULL << 0, false);
    SetupMdcfg(&iopmp, 0, 0);                             /* MD0 now empty */
    Mdstall(&iopmp, 0ULL, false);                        /* resume */
    ASSERT_ETYPE(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ),
                 IOPMP_ETYPE_NO_RULE);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-STALL-X01 reconfig MDCFG under stall");
}

/* IOPMP-STALL-X02 - locked entries untouched while reconfiguring under stall. */
static void TestStallX02_LockedPreserved(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeStallParams(2, 8, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    EnableIopmp(&iopmp);
    SetupMdcfg(&iopmp, 0, 8);
    SetupSrcmd(&iopmp, 0, 0);
    SetNa4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_R_BIT);       /* locked entry */
    uint32_t locked0 = IopmpReadReg(&iopmp,
        IopmpReadReg(&iopmp, REG_ENTRYOFFSET) + REG_ENTRY_CFG_OFF);
    IopmpWriteReg(&iopmp, REG_ENTRYLCK, 1U << ENTRYLCK_F_SHIFT);

    Mdstall(&iopmp, 1ULL << 0, false);
    SetNa4(&iopmp, 1, 0x2000ULL, ENTRY_CFG_W_BIT);       /* unlocked: updates */
    uint32_t cfg0Off = IopmpReadReg(&iopmp, REG_ENTRYOFFSET) + REG_ENTRY_CFG_OFF;
    IopmpWriteReg(&iopmp, cfg0Off, ENTRY_CFG_W_BIT);     /* locked: rejected */
    Mdstall(&iopmp, 0ULL, false);

    assert(IopmpReadReg(&iopmp, cfg0Off) == locked0);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-STALL-X02 locked entry preserved under stall");
}

/* IOPMP-STALL-X03 - stall_violation_en + ie: fault 0x07 captured, interrupt fires. */
static void TestStallX03_FaultCaptureIrq(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeStallParams(2, 4, 2);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    EnableIopmp(&iopmp);
    SetupMdcfg(&iopmp, 0, 4);
    SetupSrcmd(&iopmp, 0, 0);
    SetNa4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_R_BIT);
    IopmpWriteReg(&iopmp, REG_ERR_CFG, ERR_CFG_STALL_VIOL_BIT | ERR_CFG_IE_BIT);
    Mdstall(&iopmp, 1ULL << 0, false);

    IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ);
    assert(((IopmpReadReg(&iopmp, REG_ERR_INFO) & ERR_INFO_ETYPE_MASK) >> ERR_INFO_ETYPE_SHIFT)
           == 0x07U);
    assert(IopmpIsIrqPending(&iopmp));

    IopmpDestroy(&iopmp);
    PASS("IOPMP-STALL-X03 stall fault captured + IRQ");
}

/* IOPMP-STALL-X04 - MDSTALL then RRIDSCP overrides per RRID. */
static void TestStallX04_RridscpOverride(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeStallParams(3, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    SetupSrcmd(&iopmp, 0, 0);
    SetupSrcmd(&iopmp, 1, 0);
    SetupSrcmd(&iopmp, 2, 0);

    Mdstall(&iopmp, 1ULL << 0, false);                   /* stall all (all in MD0) */
    assert(StallStat(&iopmp, 1) == RRIDSCP_STAT_STALLED);
    /* Cherry-pick: let RRID1 run. */
    IopmpWriteReg(&iopmp, REG_RRIDSCP, 1U | (RRIDSCP_OP_NOSTALL << RRIDSCP_OP_SHIFT));
    assert(StallStat(&iopmp, 1) == RRIDSCP_STAT_NOTSTALLED);
    assert(StallStat(&iopmp, 0) == RRIDSCP_STAT_STALLED);   /* others still stalled */

    IopmpDestroy(&iopmp);
    PASS("IOPMP-STALL-X04 RRIDSCP overrides MDSTALL per RRID");
}

/* IOPMP-STALL-X05 - isolation model: MDSTALL MD m stalls only RRID m. */
static void TestStallX05_IsolationModel(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeStallParams(3, 6, 3);
    params.model    = IOPMP_MODEL_ISOLATION;
    params.srcmdFmt = 1U;                                /* RRID i -> MD i */
    params.mdcfgFmt = 1U;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    Mdstall(&iopmp, 1ULL << 1, false);                   /* select MD1 */
    assert(StallStat(&iopmp, 1) == RRIDSCP_STAT_STALLED);    /* RRID1 -> MD1 */
    assert(StallStat(&iopmp, 0) == RRIDSCP_STAT_NOTSTALLED);
    assert(StallStat(&iopmp, 2) == RRIDSCP_STAT_NOTSTALLED);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-STALL-X05 isolation model 1:1 stall");
}

/* ── Runner ──────────────────────────────────────────────────────── */

int main(void)
{
    TestStall001_Implemented();
    TestStall002_AbsentReadsZero();
    TestStall003_MdstallhWiredZero();
    TestStall004_PartialMdBits();

    TestStall005_StallAssociated();
    TestStall006_ExemptInverts();
    TestStall007_NoneSelected();
    TestStall008_ExemptEmptyStallsAll();
    TestStall009_MdReadback();
    TestStall010_Snapshot();
    TestStall011_HighMd();

    TestStall012_IsBusyZero();
    TestStall013_ResumeIsBusy();

    TestStall015_TxnHeld();
    TestStall016_OthersUnaffected();
    TestStall017_ResumeChecks();

    TestStall018_RridscpStall();
    TestStall019_RridscpNoStall();
    TestStall020_RridscpQuery();
    TestStall021_RridscpNotImpl();
    TestStall022_RridscpReservedOp();
    TestStall023_Unselectable();

    TestStall024_StallFault();
    TestStall025_NoFaultWhenDisabled();
    TestStall026_StallViolWarl();

    TestStall027_FullSequence();
    TestStall028_DoubleStall();
    TestStall029_ResumeClears();
    TestStall030_MdstallhAloneNoUpdate();

    TestStallX01_ReconfigMdcfg();
    TestStallX02_LockedPreserved();
    TestStallX03_FaultCaptureIrq();
    TestStallX04_RridscpOverride();
    TestStallX05_IsolationModel();

    printf("\nAll file-08 stall / safe-runtime tests passed.\n");
    return 0;
}
