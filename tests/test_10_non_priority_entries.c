/*
 * test_10_non_priority_entries.c
 *
 * Test suite for docs/testplan/10-non-priority-entries.md.
 *
 * Spec: §5.3 (Non-priority entries), §5.3.1, §5.3.2, §5.1.1 (prio_entry,
 *       prio_ent_prog, non_prio_en).
 *
 * SPEC-compliant. Entries with index >= prio_entry are non-priority: they must
 * cover ALL transaction bytes to match, several may match, "any match permits"
 * makes the txn legal, and a partial hit (0x04) is NEVER reported for them.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include "iopmp.h"
#include "test_utils.h"

/* ── Shared setup helpers ────────────────────────────────────────────── */

static IopmpParams_t MakeNprioParams(uint16_t rridNum, uint16_t entryNum,
                                     uint8_t mdNum, uint16_t prioEntry)
{
    IopmpParams_t params;
    memset(&params, 0, sizeof(params));
    params.rridNum   = rridNum;
    params.entryNum  = entryNum;
    params.mdNum     = mdNum;
    params.torEn     = true;
    params.hwcfg2En  = true;
    params.nonPrioEn = true;
    params.prioEntry = prioEntry;
    params.model     = IOPMP_MODEL_FULL;
    return params;
}

static void EnableIopmp(IopmpState_t *iopmp) { IopmpWriteReg(iopmp, REG_HWCFG0, HWCFG0_ENABLE_BIT); }
static void SetupSrcmd(IopmpState_t *iopmp, uint16_t rrid, uint8_t md)
{
    uint32_t off = REG_SRCMD_BASE + (uint32_t)rrid * REG_SRCMD_STRIDE + REG_SRCMD_EN_OFF;
    IopmpWriteReg(iopmp, off, IopmpReadReg(iopmp, off) | (1U << ((uint32_t)md + 1U)));
}
static void SetupMdcfg(IopmpState_t *iopmp, uint8_t md, uint32_t endEntry)
{
    IopmpWriteReg(iopmp, REG_MDCFG_BASE + (uint32_t)md * REG_MDCFG_STRIDE, endEntry);
}
static void SetEntryRaw(IopmpState_t *iopmp, uint32_t idx, uint32_t wordAddr, uint32_t cfg)
{
    uint32_t base = IopmpReadReg(iopmp, REG_ENTRYOFFSET) + idx * REG_ENTRY_STRIDE;
    IopmpWriteReg(iopmp, base + REG_ENTRY_ADDR_OFF, wordAddr);
    IopmpWriteReg(iopmp, base + REG_ENTRY_CFG_OFF, cfg);
}
static void SetNa4(IopmpState_t *iopmp, uint32_t idx, uint64_t addr, uint32_t perm)
{
    SetEntryRaw(iopmp, idx, (uint32_t)(addr >> 2U), (ADDR_MODE_NA4 << ENTRY_CFG_A_SHIFT) | perm);
}
/* NAPOT covering 'size' bytes (power of two >=8) at aligned addr. */
static void SetNapot(IopmpState_t *iopmp, uint32_t idx, uint64_t addr, uint32_t size, uint32_t perm)
{
    uint32_t k = 0U; while ((8U << k) < size) k++;
    uint32_t wordAddr = (uint32_t)(addr >> 2U) | ((1U << k) - 1U);
    SetEntryRaw(iopmp, idx, wordAddr, (ADDR_MODE_NAPOT << ENTRY_CFG_A_SHIFT) | perm);
}

/* ───────────────────────────────────────────────────────────────────
 * 10.1 Capability & prio_entry boundary
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-NPRIO-001 - HWCFG2.prio_entry returns configured boundary. */
static void TestNprio001_BoundaryReadback(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeNprioParams(1, 8, 1, 3);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    assert((IopmpReadReg(&iopmp, REG_HWCFG2) & HWCFG2_PRIO_ENTRY_MASK) == 3U);
    assert((IopmpReadReg(&iopmp, REG_HWCFG2) & HWCFG2_NON_PRIO_EN_BIT) != 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-NPRIO-001 prio_entry boundary readback");
}

/* IOPMP-NPRIO-002 - non_prio_en=0: all entries behave as priority. */
static void TestNprio002_DisabledAllPriority(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params;
    memset(&params, 0, sizeof(params));
    params.rridNum = 1; params.entryNum = 4; params.mdNum = 1;
    params.torEn = true; params.model = IOPMP_MODEL_FULL;   /* non_prio_en = 0 */
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    EnableIopmp(&iopmp);
    SetupMdcfg(&iopmp, 0, 4);
    SetupSrcmd(&iopmp, 0, 0);
    SetNa4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_R_BIT);

    /* A partial overlap yields the priority partial-hit 0x04. */
    ASSERT_ETYPE(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 8, IOPMP_TXN_READ),
                 IOPMP_ETYPE_PARTIAL_HIT);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-NPRIO-002 non_prio_en=0 all priority");
}

/* IOPMP-NPRIO-003 - prio_ent_prog=1: prio_entry writable. */
static void TestNprio003_ProgrammableBoundary(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeNprioParams(1, 8, 1, 2);
    params.prioEntProg = true;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    IopmpWriteReg(&iopmp, REG_HWCFG2, 4U);               /* set prio_entry=4 */
    assert((IopmpReadReg(&iopmp, REG_HWCFG2) & HWCFG2_PRIO_ENTRY_MASK) == 4U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-NPRIO-003 programmable prio_entry");
}

/* IOPMP-NPRIO-004 - prio_ent_prog W1CS reset=1: write 1 clears, fixes boundary. */
static void TestNprio004_ProgW1cs(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeNprioParams(1, 8, 1, 2);
    params.prioEntProg = true;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    assert((IopmpReadReg(&iopmp, REG_HWCFG2) & HWCFG2_PRIO_ENT_PROG_BIT) != 0U);
    /* Single 32-bit write: keep prio_entry=2 while clearing prog (W1CS). */
    IopmpWriteReg(&iopmp, REG_HWCFG2, 2U | HWCFG2_PRIO_ENT_PROG_BIT);
    assert((IopmpReadReg(&iopmp, REG_HWCFG2) & HWCFG2_PRIO_ENT_PROG_BIT) == 0U);
    assert((IopmpReadReg(&iopmp, REG_HWCFG2) & HWCFG2_PRIO_ENTRY_MASK) == 2U);
    /* Boundary now fixed: further prio_entry writes ignored. */
    IopmpWriteReg(&iopmp, REG_HWCFG2, 5U);
    assert((IopmpReadReg(&iopmp, REG_HWCFG2) & HWCFG2_PRIO_ENTRY_MASK) == 2U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-NPRIO-004 prio_ent_prog W1CS fixes boundary");
}

/* IOPMP-NPRIO-005 - prio_entry=0: all entries non-priority. */
static void TestNprio005_AllNonPriority(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeNprioParams(1, 4, 1, 0);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    EnableIopmp(&iopmp);
    SetupMdcfg(&iopmp, 0, 4);
    SetupSrcmd(&iopmp, 0, 0);
    SetNa4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_R_BIT);

    /* Partial cover by a non-priority entry never matches -> NO_RULE (no 0x04). */
    ASSERT_ETYPE(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 8, IOPMP_TXN_READ),
                 IOPMP_ETYPE_NO_RULE);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-NPRIO-005 prio_entry=0 all non-priority");
}

/* IOPMP-NPRIO-006 - prio_entry=entry_num: all entries priority. */
static void TestNprio006_AllPriority(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeNprioParams(1, 4, 1, 4);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    EnableIopmp(&iopmp);
    SetupMdcfg(&iopmp, 0, 4);
    SetupSrcmd(&iopmp, 0, 0);
    SetNa4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_R_BIT);

    ASSERT_ETYPE(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 8, IOPMP_TXN_READ),
                 IOPMP_ETYPE_PARTIAL_HIT);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-NPRIO-006 prio_entry=entry_num all priority");
}

/* ───────────────────────────────────────────────────────────────────
 * 10.2 Non-priority matching (cover ALL bytes)
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-NPRIO-007 - non-priority entry covering all bytes permits -> LEGAL. */
static void TestNprio007_FullCoverLegal(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeNprioParams(1, 4, 1, 2);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    EnableIopmp(&iopmp);
    SetupMdcfg(&iopmp, 0, 4);
    SetupSrcmd(&iopmp, 0, 0);
    SetNapot(&iopmp, 3, 0x1000ULL, 0x20U, ENTRY_CFG_R_BIT);   /* entry 3 non-prio */

    ASSERT_LEGAL(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 8, IOPMP_TXN_READ));

    IopmpDestroy(&iopmp);
    PASS("IOPMP-NPRIO-007 non-prio full cover legal");
}

/* IOPMP-NPRIO-008 - non-priority entry covering only some bytes: no match. */
static void TestNprio008_PartialNoMatch(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeNprioParams(1, 4, 1, 2);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    EnableIopmp(&iopmp);
    SetupMdcfg(&iopmp, 0, 4);
    SetupSrcmd(&iopmp, 0, 0);
    SetNa4(&iopmp, 3, 0x1000ULL, ENTRY_CFG_R_BIT);       /* 4 bytes, non-prio */

    /* 8-byte txn only partially covered -> no match -> NO_RULE (never 0x04). */
    ASSERT_ETYPE(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 8, IOPMP_TXN_READ),
                 IOPMP_ETYPE_NO_RULE);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-NPRIO-008 non-prio partial -> no match");
}

/* IOPMP-NPRIO-009 - non-priority full-cover entry denies -> 0x01 (not 0x04). */
static void TestNprio009_FullCoverDeny(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeNprioParams(1, 4, 1, 2);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    EnableIopmp(&iopmp);
    SetupMdcfg(&iopmp, 0, 4);
    SetupSrcmd(&iopmp, 0, 0);
    SetNapot(&iopmp, 3, 0x1000ULL, 0x20U, ENTRY_CFG_W_BIT);   /* covers all, no read */

    ASSERT_ETYPE(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 8, IOPMP_TXN_READ),
                 IOPMP_ETYPE_ILLEGAL_READ);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-NPRIO-009 non-prio deny -> 0x01 not 0x04");
}

/* IOPMP-NPRIO-010 - two non-priority entries both cover all; any-permits -> LEGAL. */
static void TestNprio010_AnyPermits(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeNprioParams(1, 4, 1, 2);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    EnableIopmp(&iopmp);
    SetupMdcfg(&iopmp, 0, 4);
    SetupSrcmd(&iopmp, 0, 0);
    SetNapot(&iopmp, 2, 0x1000ULL, 0x20U, ENTRY_CFG_W_BIT);   /* denies read */
    SetNapot(&iopmp, 3, 0x1000ULL, 0x20U, ENTRY_CFG_R_BIT);   /* permits read */

    ASSERT_LEGAL(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 8, IOPMP_TXN_READ));

    IopmpDestroy(&iopmp);
    PASS("IOPMP-NPRIO-010 any matching non-prio permits");
}

/* IOPMP-NPRIO-011 - two non-priority full-cover entries both deny -> 0x01. */
static void TestNprio011_AllDeny(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeNprioParams(1, 4, 1, 2);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    EnableIopmp(&iopmp);
    SetupMdcfg(&iopmp, 0, 4);
    SetupSrcmd(&iopmp, 0, 0);
    SetNapot(&iopmp, 2, 0x1000ULL, 0x20U, ENTRY_CFG_W_BIT);
    SetNapot(&iopmp, 3, 0x1000ULL, 0x20U, ENTRY_CFG_W_BIT);

    ASSERT_ETYPE(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 8, IOPMP_TXN_READ),
                 IOPMP_ETYPE_ILLEGAL_READ);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-NPRIO-011 no matching non-prio permits -> 0x01");
}

/* ───────────────────────────────────────────────────────────────────
 * 10.3 Priority vs non-priority interaction
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-NPRIO-012 - priority deny wins over a non-priority allow. */
static void TestNprio012_PriorityDenyWins(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeNprioParams(1, 4, 1, 2);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    EnableIopmp(&iopmp);
    SetupMdcfg(&iopmp, 0, 4);
    SetupSrcmd(&iopmp, 0, 0);
    SetNapot(&iopmp, 1, 0x1000ULL, 0x20U, ENTRY_CFG_W_BIT);   /* priority, denies read */
    SetNapot(&iopmp, 3, 0x1000ULL, 0x20U, ENTRY_CFG_R_BIT);   /* non-prio, allows */

    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ);
    ASSERT_ETYPE(r, IOPMP_ETYPE_ILLEGAL_READ);
    assert(r.entryIdx == 1U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-NPRIO-012 priority deny wins");
}

/* IOPMP-NPRIO-013 - priority entries don't match; non-priority matches -> LEGAL. */
static void TestNprio013_FallThrough(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeNprioParams(1, 4, 1, 2);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    EnableIopmp(&iopmp);
    SetupMdcfg(&iopmp, 0, 4);
    SetupSrcmd(&iopmp, 0, 0);
    SetNa4(&iopmp, 0, 0x9000ULL, ENTRY_CFG_R_BIT);       /* priority, elsewhere */
    SetNapot(&iopmp, 3, 0x1000ULL, 0x20U, ENTRY_CFG_R_BIT);

    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ);
    ASSERT_LEGAL(r);
    assert(r.entryIdx == 3U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-NPRIO-013 fall through to non-priority");
}

/* IOPMP-NPRIO-014 - priority partial-hit dominates non-priority full allow. */
static void TestNprio014_PriorityPartialDominates(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeNprioParams(1, 4, 1, 2);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    EnableIopmp(&iopmp);
    SetupMdcfg(&iopmp, 0, 4);
    SetupSrcmd(&iopmp, 0, 0);
    SetNa4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_R_BIT);       /* priority, 4 bytes (partial) */
    SetNapot(&iopmp, 3, 0x1000ULL, 0x20U, ENTRY_CFG_R_BIT);  /* non-prio full cover */

    ASSERT_ETYPE(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 8, IOPMP_TXN_READ),
                 IOPMP_ETYPE_PARTIAL_HIT);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-NPRIO-014 priority partial-hit dominates");
}

/* IOPMP-NPRIO-015 - single priority match: baseline reaction. */
static void TestNprio015_PriorityBaseline(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeNprioParams(1, 4, 1, 2);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    EnableIopmp(&iopmp);
    SetupMdcfg(&iopmp, 0, 4);
    SetupSrcmd(&iopmp, 0, 0);
    SetNa4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_R_BIT);       /* priority */

    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ);
    ASSERT_LEGAL(r);
    assert(r.entryIdx == 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-NPRIO-015 single priority match baseline");
}

/* ───────────────────────────────────────────────────────────────────
 * 10.4 Error reporting differences
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-NPRIO-016 - non-priority write on r-only set -> 0x02, never 0x04. */
static void TestNprio016_WriteEtype(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeNprioParams(1, 4, 1, 2);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    EnableIopmp(&iopmp);
    SetupMdcfg(&iopmp, 0, 4);
    SetupSrcmd(&iopmp, 0, 0);
    SetNapot(&iopmp, 3, 0x1000ULL, 0x20U, ENTRY_CFG_R_BIT);   /* read only */

    ASSERT_ETYPE(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 8, IOPMP_TXN_WRITE),
                 IOPMP_ETYPE_ILLEGAL_WRITE);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-NPRIO-016 non-prio write etype 0x02");
}

/* IOPMP-NPRIO-017 - multiple non-prio matched illegal: eid is one of them. */
static void TestNprio017_EidOfMatch(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeNprioParams(1, 4, 1, 2);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    EnableIopmp(&iopmp);
    SetupMdcfg(&iopmp, 0, 4);
    SetupSrcmd(&iopmp, 0, 0);
    SetNapot(&iopmp, 2, 0x1000ULL, 0x20U, ENTRY_CFG_W_BIT);
    SetNapot(&iopmp, 3, 0x1000ULL, 0x20U, ENTRY_CFG_W_BIT);

    IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 8, IOPMP_TXN_READ);
    uint32_t eid = (IopmpReadReg(&iopmp, REG_ERR_REQID) & ERR_REQID_EID_MASK) >> ERR_REQID_EID_SHIFT;
    assert(eid == 2U || eid == 3U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-NPRIO-017 eid is one matched non-prio entry");
}

/* IOPMP-NPRIO-018 - result independent of non-priority evaluation order. */
static void TestNprio018_OrderIndependent(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeNprioParams(1, 4, 1, 2);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    EnableIopmp(&iopmp);
    SetupMdcfg(&iopmp, 0, 4);
    SetupSrcmd(&iopmp, 0, 0);

    /* Permitting entry placed at the higher index; result must still be legal. */
    SetNapot(&iopmp, 2, 0x1000ULL, 0x20U, ENTRY_CFG_W_BIT);
    SetNapot(&iopmp, 3, 0x1000ULL, 0x20U, ENTRY_CFG_R_BIT);
    ASSERT_LEGAL(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ));

    IopmpDestroy(&iopmp);
    PASS("IOPMP-NPRIO-018 order-independent non-prio result");
}

/* ───────────────────────────────────────────────────────────────────
 * Cross-combinations
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-NPRIO-X01 - interrupt suppressed only if ALL matched non-prio have sire. */
static void TestNprioX01_AllSireToSuppress(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeNprioParams(1, 4, 1, 2);
    params.peisEn = true;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    EnableIopmp(&iopmp);
    SetupMdcfg(&iopmp, 0, 4);
    SetupSrcmd(&iopmp, 0, 0);
    IopmpWriteReg(&iopmp, REG_ERR_CFG, ERR_CFG_IE_BIT);

    /* Only entry 2 has sire; entry 3 does not -> interrupt NOT suppressed. */
    SetNapot(&iopmp, 2, 0x1000ULL, 0x20U, ENTRY_CFG_W_BIT | ENTRY_CFG_SIRE_BIT);
    SetNapot(&iopmp, 3, 0x1000ULL, 0x20U, ENTRY_CFG_W_BIT);
    IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ);
    assert(IopmpIsIrqPending(&iopmp));

    /* Now make both suppress -> interrupt suppressed. */
    IopmpWriteReg(&iopmp, REG_ERR_INFO, ERR_INFO_V_BIT);   /* clear */
    IopmpClearIrq(&iopmp);
    SetNapot(&iopmp, 3, 0x1000ULL, 0x20U, ENTRY_CFG_W_BIT | ENTRY_CFG_SIRE_BIT);
    IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ);
    assert(!IopmpIsIrqPending(&iopmp));

    IopmpDestroy(&iopmp);
    PASS("IOPMP-NPRIO-X01 suppress needs all matched sire");
}

/* IOPMP-NPRIO-X02 - bus error suppressed only if ALL matched non-prio have sere. */
static void TestNprioX02_AllSereToSuppress(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeNprioParams(1, 4, 1, 2);
    params.peesEn = true;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    EnableIopmp(&iopmp);
    SetupMdcfg(&iopmp, 0, 4);
    SetupSrcmd(&iopmp, 0, 0);

    SetNapot(&iopmp, 2, 0x1000ULL, 0x20U, ENTRY_CFG_W_BIT | ENTRY_CFG_SERE_BIT);
    SetNapot(&iopmp, 3, 0x1000ULL, 0x20U, ENTRY_CFG_W_BIT);   /* no sere */
    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ);
    assert(!r.suppressError);                            /* not all suppress */

    IopmpDestroy(&iopmp);
    PASS("IOPMP-NPRIO-X02 bus-error suppress needs all matched sere");
}

/* IOPMP-NPRIO-X03 - locked priority rule enforces while non-prio is reconfigured. */
static void TestNprioX03_LockedPriorityHolds(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeNprioParams(1, 4, 1, 2);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    EnableIopmp(&iopmp);
    SetupMdcfg(&iopmp, 0, 4);
    SetupSrcmd(&iopmp, 0, 0);
    SetNapot(&iopmp, 0, 0x1000ULL, 0x20U, ENTRY_CFG_W_BIT);   /* priority deny-read */
    IopmpWriteReg(&iopmp, REG_ENTRYLCK, 2U << ENTRYLCK_F_SHIFT);  /* lock priority entries */

    /* Reconfigure a non-priority entry to allow; locked priority deny still wins. */
    SetNapot(&iopmp, 3, 0x1000ULL, 0x20U, ENTRY_CFG_R_BIT);
    ASSERT_ETYPE(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ),
                 IOPMP_ETYPE_ILLEGAL_READ);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-NPRIO-X03 locked priority enforces over non-prio");
}

/* IOPMP-NPRIO-X04 - non-prio illegal across RRIDs logged in ERR_MFR. */
static void TestNprioX04_MfrAcrossRrids(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeNprioParams(3, 4, 1, 2);
    params.multifaultEn = true;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    EnableIopmp(&iopmp);
    SetupMdcfg(&iopmp, 0, 4);
    SetupSrcmd(&iopmp, 0, 0);
    SetupSrcmd(&iopmp, 1, 0);
    SetNapot(&iopmp, 3, 0x1000ULL, 0x20U, ENTRY_CFG_W_BIT);   /* deny read */

    IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ);   /* first */
    IopmpCheckAccess(&iopmp, 1, 0x1000ULL, 4, IOPMP_TXN_READ);   /* subsequent */
    assert((IopmpReadReg(&iopmp, REG_ERR_INFO) & ERR_INFO_SVC_BIT) != 0U);
    assert((IopmpReadReg(&iopmp, REG_ERR_MFR) & ERR_MFR_SVS_BIT) != 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-NPRIO-X04 subsequent non-prio violations in MFR");
}

/* IOPMP-NPRIO-X05 - TOR entry at the prio_entry boundary decodes from predecessor. */
static void TestNprioX05_TorAtBoundary(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeNprioParams(1, 4, 1, 2);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    EnableIopmp(&iopmp);
    SetupMdcfg(&iopmp, 0, 4);
    SetupSrcmd(&iopmp, 0, 0);

    /* Entry 1 (priority) supplies the TOR base; entry 2 (first non-priority) is
     * TOR and must derive [entry1.addr<<2, entry2.addr<<2). */
    SetEntryRaw(&iopmp, 1, (uint32_t)(0x1000ULL >> 2U), 0x00U);
    SetEntryRaw(&iopmp, 2, (uint32_t)(0x2000ULL >> 2U),
                (ADDR_MODE_TOR << ENTRY_CFG_A_SHIFT) | ENTRY_CFG_R_BIT);

    /* Non-priority TOR must cover ALL bytes; a read inside [0x1000,0x2000) matches. */
    ASSERT_LEGAL(IopmpCheckAccess(&iopmp, 0, 0x1800ULL, 4, IOPMP_TXN_READ));

    IopmpDestroy(&iopmp);
    PASS("IOPMP-NPRIO-X05 TOR at boundary decodes from predecessor");
}

/* ── Runner ──────────────────────────────────────────────────────── */

int main(void)
{
    TestNprio001_BoundaryReadback();
    TestNprio002_DisabledAllPriority();
    TestNprio003_ProgrammableBoundary();
    TestNprio004_ProgW1cs();
    TestNprio005_AllNonPriority();
    TestNprio006_AllPriority();

    TestNprio007_FullCoverLegal();
    TestNprio008_PartialNoMatch();
    TestNprio009_FullCoverDeny();
    TestNprio010_AnyPermits();
    TestNprio011_AllDeny();

    TestNprio012_PriorityDenyWins();
    TestNprio013_FallThrough();
    TestNprio014_PriorityPartialDominates();
    TestNprio015_PriorityBaseline();

    TestNprio016_WriteEtype();
    TestNprio017_EidOfMatch();
    TestNprio018_OrderIndependent();

    TestNprioX01_AllSireToSuppress();
    TestNprioX02_AllSereToSuppress();
    TestNprioX03_LockedPriorityHolds();
    TestNprioX04_MfrAcrossRrids();
    TestNprioX05_TorAtBoundary();

    printf("\nAll file-10 non-priority entry tests passed.\n");
    return 0;
}
