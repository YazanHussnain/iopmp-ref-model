/*
 * test_05_priority_matching.c
 *
 * Test suite for docs/testplan/05-priority-matching.md:
 *   "Priority & Matching Logic".
 *
 * Spec: §2.7 (Priority and Matching Logic), §2.8/Table 2 (error types),
 *       Figure 3 (check flow).
 *
 * Exercises the core check pipeline through the transaction API:
 *   RRID legality -> SRCMD -> MDCFG range -> priority (index) match ->
 *   permission grant -> all-bytes coverage.
 *
 * NOTE comments mark where the model's evaluation order or AMO handling
 * deviates from the test plan's ideal wording.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include "iopmp.h"
#include "test_utils.h"

/* ── Shared setup helpers ────────────────────────────────────────────── */

static IopmpParams_t MakeParams(uint16_t rridNum, uint16_t entryNum, uint8_t mdNum)
{
    IopmpParams_t params;
    memset(&params, 0, sizeof(params));
    params.rridNum  = rridNum;
    params.entryNum = entryNum;
    params.mdNum    = mdNum;
    params.torEn    = true;
    params.model    = IOPMP_MODEL_FULL;
    return params;
}

static void EnableIopmp(IopmpState_t *iopmp)
{
    IopmpWriteReg(iopmp, REG_HWCFG0, HWCFG0_ENABLE_BIT);
}

static void SetEntryRaw(IopmpState_t *iopmp, uint32_t idx,
                        uint32_t wordAddr, uint32_t cfg)
{
    uint32_t base = IopmpReadReg(iopmp, REG_ENTRYOFFSET);
    uint32_t slot = base + idx * REG_ENTRY_STRIDE;
    IopmpWriteReg(iopmp, slot + REG_ENTRY_ADDR_OFF, wordAddr);
    IopmpWriteReg(iopmp, slot + REG_ENTRY_CFG_OFF,  cfg);
}

/* NA4 entry at byteAddr with permission bits. */
static void SetNa4(IopmpState_t *iopmp, uint32_t idx, uint64_t byteAddr, uint32_t perm)
{
    SetEntryRaw(iopmp, idx, (uint32_t)(byteAddr >> 2U),
                (ADDR_MODE_NA4 << ENTRY_CFG_A_SHIFT) | perm);
}

/* NAPOT entry covering 'sizeBytes' (power of two, >=8) at aligned byteAddr. */
static void SetNapot(IopmpState_t *iopmp, uint32_t idx, uint64_t byteAddr,
                     uint32_t sizeBytes, uint32_t perm)
{
    /* size = 8 << k  ->  k trailing ones in the word address. */
    uint32_t k = 0U;
    while ((8U << k) < sizeBytes) k++;
    uint32_t wordAddr = (uint32_t)(byteAddr >> 2U) | ((1U << k) - 1U);
    SetEntryRaw(iopmp, idx, wordAddr,
                (ADDR_MODE_NAPOT << ENTRY_CFG_A_SHIFT) | perm);
}

static void SetupSrcmd(IopmpState_t *iopmp, uint16_t rrid, uint8_t mdIdx)
{
    uint32_t off = REG_SRCMD_BASE + (uint32_t)rrid * REG_SRCMD_STRIDE + REG_SRCMD_EN_OFF;
    IopmpWriteReg(iopmp, off, IopmpReadReg(iopmp, off) | (1U << ((uint32_t)mdIdx + 1U)));
}

static void SetupMdcfg(IopmpState_t *iopmp, uint8_t mdIdx, uint32_t endEntry)
{
    IopmpWriteReg(iopmp, REG_MDCFG_BASE + (uint32_t)mdIdx * REG_MDCFG_STRIDE, endEntry);
}

/* Wire a single-MD instance: enable, MD0 owns all entries, RRID0 -> MD0. */
static void Wire1Md(IopmpState_t *iopmp, uint16_t entryNum)
{
    EnableIopmp(iopmp);
    SetupMdcfg(iopmp, 0, entryNum);
    SetupSrcmd(iopmp, 0, 0);
}

/* ───────────────────────────────────────────────────────────────────
 * 5.1 Single-entry grant / deny
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-MATCH-001 - entry 0 covers region, r=1: read LEGAL, entry_idx=0. */
static void TestMatch001_SingleGrant(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire1Md(&iopmp, 4);

    SetNapot(&iopmp, 0, 0x1000ULL, 0x100U, ENTRY_CFG_R_BIT);
    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ);
    ASSERT_LEGAL(r);
    assert(r.entryIdx == 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MATCH-001 single-entry grant");
}

/* IOPMP-MATCH-002 - r=1,w=0: write -> 0x02, entry_idx=0. */
static void TestMatch002_WriteDenied(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire1Md(&iopmp, 4);

    SetNapot(&iopmp, 0, 0x1000ULL, 0x100U, ENTRY_CFG_R_BIT);
    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_WRITE);
    ASSERT_ETYPE(r, IOPMP_ETYPE_ILLEGAL_WRITE);
    assert(r.entryIdx == 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MATCH-002 write denied 0x02");
}

/* IOPMP-MATCH-003 - r=1,x=0: exec -> 0x03. */
static void TestMatch003_ExecDenied(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire1Md(&iopmp, 4);

    SetNapot(&iopmp, 0, 0x1000ULL, 0x100U, ENTRY_CFG_R_BIT);
    ASSERT_ETYPE(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_EXEC),
                 IOPMP_ETYPE_ILLEGAL_EXEC);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MATCH-003 exec denied 0x03");
}

/* IOPMP-MATCH-004 - w=1,r=0: read -> 0x01. */
static void TestMatch004_ReadDenied(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire1Md(&iopmp, 4);

    SetNapot(&iopmp, 0, 0x1000ULL, 0x100U, ENTRY_CFG_W_BIT);
    ASSERT_ETYPE(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ),
                 IOPMP_ETYPE_ILLEGAL_READ);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MATCH-004 read denied 0x01");
}

/* ───────────────────────────────────────────────────────────────────
 * 5.2 No-match / no-rule
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-MATCH-005 - active entries but none cover addr -> 0x05. */
static void TestMatch005_NoneCover(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire1Md(&iopmp, 4);

    SetNa4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_R_BIT);
    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x9000ULL, 4, IOPMP_TXN_READ);
    ASSERT_ETYPE(r, IOPMP_ETYPE_NO_RULE);
    assert(r.entryIdx == UINT32_MAX);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MATCH-005 no covering entry -> 0x05");
}

/* IOPMP-MATCH-006 - RRID associated to MD with no active entry -> 0x05. */
static void TestMatch006_NoActiveEntry(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire1Md(&iopmp, 4);
    /* MD0 owns {0..3} but all entries are OFF. */

    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ);
    ASSERT_ETYPE(r, IOPMP_ETYPE_NO_RULE);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MATCH-006 MD with no active entry -> 0x05");
}

/* ───────────────────────────────────────────────────────────────────
 * 5.3 Priority ordering (lowest index wins)
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-MATCH-007 - entry2 r=1, entry5 r=0: read legal via entry2. */
static void TestMatch007_LowerIndexGrants(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 8, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire1Md(&iopmp, 8);

    SetNa4(&iopmp, 2, 0x1000ULL, ENTRY_CFG_R_BIT);
    SetNa4(&iopmp, 5, 0x1000ULL, 0U);               /* would deny, never consulted */
    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ);
    ASSERT_LEGAL(r);
    assert(r.entryIdx == 2U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MATCH-007 lower index grants");
}

/* IOPMP-MATCH-008 - entry2 deny, entry5 allow: higher-priority deny wins. */
static void TestMatch008_HigherPriorityDeny(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 8, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire1Md(&iopmp, 8);

    SetNa4(&iopmp, 2, 0x1000ULL, 0U);               /* deny (active, no r) */
    SetNa4(&iopmp, 5, 0x1000ULL, ENTRY_CFG_R_BIT);  /* allow, lower priority */
    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ);
    ASSERT_ETYPE(r, IOPMP_ETYPE_ILLEGAL_READ);
    assert(r.entryIdx == 2U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MATCH-008 higher-priority deny wins");
}

/* IOPMP-MATCH-009 - highest-priority OFF skipped; next active covering grants. */
static void TestMatch009_OffSkipped(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire1Md(&iopmp, 4);

    SetEntryRaw(&iopmp, 0, (uint32_t)(0x1000ULL >> 2U), 0x00U);   /* OFF */
    SetNa4(&iopmp, 1, 0x1000ULL, ENTRY_CFG_R_BIT);
    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ);
    ASSERT_LEGAL(r);
    assert(r.entryIdx == 1U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MATCH-009 OFF entry skipped");
}

/* IOPMP-MATCH-010 - two MDs contribute candidates; global index order wins. */
static void TestMatch010_GlobalIndexOrder(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 6, 2);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    EnableIopmp(&iopmp);

    SetupMdcfg(&iopmp, 0, 3);                        /* MD0 = {0,1,2} */
    SetupMdcfg(&iopmp, 1, 6);                        /* MD1 = {3,4,5} */
    SetupSrcmd(&iopmp, 0, 0);
    SetupSrcmd(&iopmp, 0, 1);

    SetNa4(&iopmp, 1, 0x1000ULL, ENTRY_CFG_R_BIT);   /* MD0, index 1 */
    SetNa4(&iopmp, 4, 0x1000ULL, ENTRY_CFG_R_BIT);   /* MD1, index 4 */
    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ);
    ASSERT_LEGAL(r);
    assert(r.entryIdx == 1U);                        /* lowest index across MDs */

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MATCH-010 global index order across MDs");
}

/* IOPMP-MATCH-011 - partial-hit entry precedes full-cover entry -> 0x04. */
static void TestMatch011_PartialPrecedesFull(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire1Md(&iopmp, 4);

    SetNa4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_R_BIT);            /* 4 bytes (partial) */
    SetNapot(&iopmp, 1, 0x1000ULL, 0x20U, ENTRY_CFG_R_BIT);  /* covers all 8 */
    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 8, IOPMP_TXN_READ);
    ASSERT_ETYPE(r, IOPMP_ETYPE_PARTIAL_HIT);
    assert(r.entryIdx == 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MATCH-011 partial-hit entry wins by priority");
}

/* ───────────────────────────────────────────────────────────────────
 * 5.4 Partial hit (0x04)
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-MATCH-012 - highest-priority covers some bytes, perm granted -> 0x04. */
static void TestMatch012_PartialPermGranted(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire1Md(&iopmp, 4);

    SetNa4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_R_BIT);
    ASSERT_ETYPE(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 8, IOPMP_TXN_READ),
                 IOPMP_ETYPE_PARTIAL_HIT);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MATCH-012 partial hit (perm granted)");
}

/* IOPMP-MATCH-013 - partial cover takes precedence over permission -> 0x04. */
static void TestMatch013_PartialPermDenied(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire1Md(&iopmp, 4);

    /* Entry covers only 4 of the 8 requested bytes AND lacks read permission.
     * Per spec §2.7 the partial hit (0x04) is reported "irrespective of its
     * permission", so 0x04 wins over the permission error. */
    SetNa4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_W_BIT);   /* no read */
    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 8, IOPMP_TXN_READ);
    ASSERT_ETYPE(r, IOPMP_ETYPE_PARTIAL_HIT);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MATCH-013 partial hit takes precedence over permission");
}

/* IOPMP-MATCH-014 - txn starts before region, ends inside -> 0x04. */
static void TestMatch014_StartsBefore(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire1Md(&iopmp, 4);

    /* NA4 [0x1000,0x1004); txn [0x0FFC,0x1004) overlaps the low end only. */
    SetNa4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_R_BIT);
    ASSERT_ETYPE(IopmpCheckAccess(&iopmp, 0, 0x0FFCULL, 8, IOPMP_TXN_READ),
                 IOPMP_ETYPE_PARTIAL_HIT);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MATCH-014 starts before region -> 0x04");
}

/* IOPMP-MATCH-015 - two adjacent entries cover all; only first matches -> 0x04. */
static void TestMatch015_NoMergeAcrossEntries(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire1Md(&iopmp, 4);

    SetNa4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_R_BIT);   /* [0x1000,0x1004) */
    SetNa4(&iopmp, 1, 0x1004ULL, ENTRY_CFG_R_BIT);   /* [0x1004,0x1008) */
    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 8, IOPMP_TXN_READ);
    ASSERT_ETYPE(r, IOPMP_ETYPE_PARTIAL_HIT);        /* no merging */
    assert(r.entryIdx == 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MATCH-015 no coverage merging across entries");
}

/* ───────────────────────────────────────────────────────────────────
 * 5.5 AMO semantics
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-MATCH-016 - r=1,w=1: AMO fully inside -> LEGAL. */
static void TestMatch016_AmoLegal(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire1Md(&iopmp, 4);

    SetNapot(&iopmp, 0, 0x1000ULL, 0x100U, ENTRY_CFG_R_BIT | ENTRY_CFG_W_BIT);
    ASSERT_LEGAL(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_AMO));

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MATCH-016 AMO legal with r+w");
}

/* IOPMP-MATCH-017 - r=1,w=0: AMO -> 0x02. */
static void TestMatch017_AmoNoWrite(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire1Md(&iopmp, 4);

    SetNapot(&iopmp, 0, 0x1000ULL, 0x100U, ENTRY_CFG_R_BIT);
    ASSERT_ETYPE(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_AMO),
                 IOPMP_ETYPE_ILLEGAL_WRITE);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MATCH-017 AMO without write -> 0x02");
}

/* IOPMP-MATCH-018 - r=0,w=1: AMO -> 0x02. */
static void TestMatch018_AmoNoRead(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire1Md(&iopmp, 4);

    SetNapot(&iopmp, 0, 0x1000ULL, 0x100U, ENTRY_CFG_W_BIT);
    ASSERT_ETYPE(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_AMO),
                 IOPMP_ETYPE_ILLEGAL_WRITE);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MATCH-018 AMO without read -> 0x02");
}

/* IOPMP-MATCH-019 - - AMO with read perm missing. */
static void TestMatch019_AmoRmwReadStep(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire1Md(&iopmp, 4);

    SetNapot(&iopmp, 0, 0x1000ULL, 0x100U, ENTRY_CFG_W_BIT);  /* w only, no r */
    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_AMO);

    /* NOTE: MODEL DEVIATION. The spec note permits modelling AMO as a
     * read-modify-write and capturing 0x01 for the missing read step. The
     * model treats AMO atomically as a write-class access and reports 0x02. */
    ASSERT_ETYPE(r, IOPMP_ETYPE_ILLEGAL_WRITE);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MATCH-019 AMO atomic write-class (0x02, deviation noted)");
}

/* ───────────────────────────────────────────────────────────────────
 * 5.6 Coverage edge cases
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-MATCH-020 - len=1 at exact region base -> LEGAL. */
static void TestMatch020_Len1AtBase(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire1Md(&iopmp, 4);

    SetNa4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_R_BIT);
    ASSERT_LEGAL(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 1, IOPMP_TXN_READ));

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MATCH-020 len=1 at base legal");
}

/* IOPMP-MATCH-021 - len=1 at region end-1 -> LEGAL. */
static void TestMatch021_Len1AtEndMinus1(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire1Md(&iopmp, 4);

    SetNa4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_R_BIT);   /* [0x1000,0x1004) */
    ASSERT_LEGAL(IopmpCheckAccess(&iopmp, 0, 0x1003ULL, 1, IOPMP_TXN_READ));

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MATCH-021 len=1 at end-1 legal");
}

/* IOPMP-MATCH-022 - len=1 at region end (first byte outside) -> no match. */
static void TestMatch022_Len1AtEnd(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire1Md(&iopmp, 4);

    SetNa4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_R_BIT);
    ASSERT_ETYPE(IopmpCheckAccess(&iopmp, 0, 0x1004ULL, 1, IOPMP_TXN_READ),
                 IOPMP_ETYPE_NO_RULE);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MATCH-022 len=1 at end -> no match");
}

/* IOPMP-MATCH-023 - large len wrapping address space handled safely. */
static void TestMatch023_WrappingLen(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire1Md(&iopmp, 4);

    SetNa4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_R_BIT);
    /* Address near the top of the space; the entry region is far below, so
     * there is no overlap and the check must not crash or wrap into a match. */
    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0xFFFFFFFFFFFFFFF0ULL, 0x20,
                                     IOPMP_TXN_READ);
    ASSERT_ETYPE(r, IOPMP_ETYPE_NO_RULE);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MATCH-023 wrapping length handled safely");
}

/* IOPMP-MATCH-024 - enable=0: any txn LEGAL (bypassed). */
static void TestMatch024_DisabledBypass(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    /* Not enabled. */

    ASSERT_LEGAL(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_WRITE));

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MATCH-024 disabled IOPMP bypassed");
}

/* ───────────────────────────────────────────────────────────────────
 * Cross-combinations (file-local)
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-MATCH-X01 - deny entry with ERR_CFG.ie=1: capture + interrupt. */
static void TestMatchX01_DenyFiresIrq(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire1Md(&iopmp, 4);
    IopmpWriteReg(&iopmp, REG_ERR_CFG, ERR_CFG_IE_BIT);

    SetNa4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_W_BIT);   /* read denied */
    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ);
    ASSERT_ETYPE(r, IOPMP_ETYPE_ILLEGAL_READ);

    assert((IopmpReadReg(&iopmp, REG_ERR_INFO) & ERR_INFO_V_BIT) != 0U);
    assert(IopmpIsIrqPending(&iopmp));

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MATCH-X01 deny captures + asserts IRQ");
}

/* IOPMP-MATCH-X02 - - priority entries evaluated before non-priority. */
static void TestMatchX02_PriorityBeforeNonPriority(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    params.hwcfg2En  = true;
    params.nonPrioEn = true;
    params.prioEntry = 1;                            /* entry 0 priority, 1+ non-prio */
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire1Md(&iopmp, 4);

    /* Priority entry 0 covers only 4 of 8 bytes; non-priority entry 1 covers
     * all 8. The priority partial-hit fires first and dominates. */
    SetNa4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_R_BIT);
    SetNapot(&iopmp, 1, 0x1000ULL, 0x20U, ENTRY_CFG_R_BIT);
    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 8, IOPMP_TXN_READ);
    ASSERT_ETYPE(r, IOPMP_ETYPE_PARTIAL_HIT);
    assert(r.entryIdx == 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MATCH-X02 priority partial-hit dominates non-priority");
}

/* IOPMP-MATCH-X03 - - SPS denies a write the entry would otherwise permit. */
static void TestMatchX03_SpsDeniesWrite(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    params.spsEn = true;                             /* srcmdFmt 0 */
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire1Md(&iopmp, 4);

    SetNapot(&iopmp, 0, 0x1000ULL, 0x100U, ENTRY_CFG_R_BIT | ENTRY_CFG_W_BIT);
    /* SRCMD_W(0) left 0 -> SPS denies the write despite entry w=1. */
    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_WRITE);
    ASSERT_ETYPE(r, IOPMP_ETYPE_ILLEGAL_WRITE);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MATCH-X03 SPS denies entry-permitted write");
}

/* IOPMP-MATCH-X04 - - deny entry with sire=1: capture but suppress interrupt. */
static void TestMatchX04_InterruptSuppressed(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    params.hwcfg2En = true;
    params.peisEn   = true;                          /* per-entry irq suppression */
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire1Md(&iopmp, 4);
    IopmpWriteReg(&iopmp, REG_ERR_CFG, ERR_CFG_IE_BIT);

    /* Read-deny entry with suppress-interrupt-on-illegal-read set. */
    SetNa4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_W_BIT | ENTRY_CFG_SIRE_BIT);
    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ);
    ASSERT_ETYPE(r, IOPMP_ETYPE_ILLEGAL_READ);

    assert((IopmpReadReg(&iopmp, REG_ERR_INFO) & ERR_INFO_V_BIT) != 0U);  /* captured */
    assert(!IopmpIsIrqPending(&iopmp));                                    /* suppressed */

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MATCH-X04 captured but interrupt suppressed");
}

/* IOPMP-MATCH-X05 - - Isolation model (RRID i -> MD i), first-match semantics. */
static void TestMatchX05_IsolationModel(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(2, 4, 2);
    params.model    = IOPMP_MODEL_ISOLATION;
    params.srcmdFmt = 1U;                            /* RRID i -> MD i */
    params.mdcfgFmt = 1U;                            /* fixed equal partition */
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    EnableIopmp(&iopmp);

    /* k = entry_num / md_num = 2: MD0 = {0,1}, MD1 = {2,3}. */
    SetNa4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_R_BIT);   /* MD0 */
    SetNa4(&iopmp, 2, 0x2000ULL, ENTRY_CFG_R_BIT);   /* MD1 */

    /* RRID1 sees only MD1's entries. */
    TxnResult_t ok = IopmpCheckAccess(&iopmp, 1, 0x2000ULL, 4, IOPMP_TXN_READ);
    ASSERT_LEGAL(ok);
    assert(ok.entryIdx == 2U);

    TxnResult_t no = IopmpCheckAccess(&iopmp, 1, 0x1000ULL, 4, IOPMP_TXN_READ);
    ASSERT_ETYPE(no, IOPMP_ETYPE_NO_RULE);           /* MD0 not a candidate */

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MATCH-X05 isolation model candidate scoping");
}

/* ── Runner ──────────────────────────────────────────────────────── */

int main(void)
{
    /* 5.1 Single-entry grant / deny */
    TestMatch001_SingleGrant();
    TestMatch002_WriteDenied();
    TestMatch003_ExecDenied();
    TestMatch004_ReadDenied();

    /* 5.2 No-match / no-rule */
    TestMatch005_NoneCover();
    TestMatch006_NoActiveEntry();

    /* 5.3 Priority ordering */
    TestMatch007_LowerIndexGrants();
    TestMatch008_HigherPriorityDeny();
    TestMatch009_OffSkipped();
    TestMatch010_GlobalIndexOrder();
    TestMatch011_PartialPrecedesFull();

    /* 5.4 Partial hit */
    TestMatch012_PartialPermGranted();
    TestMatch013_PartialPermDenied();
    TestMatch014_StartsBefore();
    TestMatch015_NoMergeAcrossEntries();

    /* 5.5 AMO semantics */
    TestMatch016_AmoLegal();
    TestMatch017_AmoNoWrite();
    TestMatch018_AmoNoRead();
    TestMatch019_AmoRmwReadStep();

    /* 5.6 Coverage edge cases */
    TestMatch020_Len1AtBase();
    TestMatch021_Len1AtEndMinus1();
    TestMatch022_Len1AtEnd();
    TestMatch023_WrappingLen();
    TestMatch024_DisabledBypass();

    /* Cross-combinations */
    TestMatchX01_DenyFiresIrq();
    TestMatchX02_PriorityBeforeNonPriority();
    TestMatchX03_SpsDeniesWrite();
    TestMatchX04_InterruptSuppressed();
    TestMatchX05_IsolationModel();

    printf("\nAll file-05 priority-matching tests passed.\n");
    return 0;
}
