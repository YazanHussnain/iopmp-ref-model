/*
 * test_03_mdcfg_table.c
 *
 * Test suite for docs/testplan/03-mdcfg-table.md:
 *   "MDCFG Table (Memory Domain -> Entry range)" - Format 0 (MDCFG present).
 *
 * Spec: §2.6 (MDCFG Table), §4.4.1 (MDCFG(m)), §A.5 (improper settings),
 *       §A.6 (MDCFG reduction).
 *
 * Range computation is verified white-box via MdcfgGetEntryRange (the same
 * lookup the checker uses); register semantics and lock/stall interactions are
 * verified through the public MMIO + transaction API. NOTE comments mark
 * deliberate deviations from the test plan's ideal §A.5 options.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include "iopmp.h"
#include "iopmp_internal.h"   /* MdcfgGetEntryRange - white-box range checks */
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

static void SetupEntry(IopmpState_t *iopmp, uint32_t entryIdx,
                       uint64_t byteAddr, uint32_t cfgBits)
{
    uint32_t base = IopmpReadReg(iopmp, REG_ENTRYOFFSET);
    uint32_t slot = base + entryIdx * REG_ENTRY_STRIDE;
    IopmpWriteReg(iopmp, slot + REG_ENTRY_ADDR_OFF, (uint32_t)(byteAddr >> 2U));
    IopmpWriteReg(iopmp, slot + REG_ENTRY_CFG_OFF,
                  (ADDR_MODE_NA4 << ENTRY_CFG_A_SHIFT) | cfgBits);
}

static void SetupSrcmd(IopmpState_t *iopmp, uint16_t rrid, uint8_t mdIdx)
{
    uint32_t off = REG_SRCMD_BASE + (uint32_t)rrid * REG_SRCMD_STRIDE + REG_SRCMD_EN_OFF;
    uint32_t cur = IopmpReadReg(iopmp, off);
    IopmpWriteReg(iopmp, off, cur | (1U << ((uint32_t)mdIdx + 1U)));
}

static void SetupMdcfg(IopmpState_t *iopmp, uint8_t mdIdx, uint32_t endEntry)
{
    IopmpWriteReg(iopmp, REG_MDCFG_BASE + (uint32_t)mdIdx * REG_MDCFG_STRIDE, endEntry);
}

static uint32_t ReadMdcfg(IopmpState_t *iopmp, uint8_t mdIdx)
{
    return IopmpReadReg(iopmp, REG_MDCFG_BASE + (uint32_t)mdIdx * REG_MDCFG_STRIDE);
}

/* Assert MD 'md' owns exactly the half-open range [expStart, expEnd). */
static void AssertMdRange(IopmpState_t *iopmp, uint8_t md,
                          uint32_t expStart, uint32_t expEnd)
{
    uint32_t s = 0U, e = 0U;
    bool nonEmpty = MdcfgGetEntryRange(iopmp, md, &s, &e);
    assert(nonEmpty);
    assert(s == expStart);
    assert(e == expEnd);
}

static void AssertMdEmpty(IopmpState_t *iopmp, uint8_t md)
{
    uint32_t s = 0U, e = 0U;
    assert(!MdcfgGetEntryRange(iopmp, md, &s, &e));
}

/* ───────────────────────────────────────────────────────────────────
 * 3.1 Range computation
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-MDCFG-001 - MDCFG(0).t=2 -> MD0 owns {0,1}. */
static void TestMdcfg001_Md0Range(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(2, 8, 4);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    SetupMdcfg(&iopmp, 0, 2);
    AssertMdRange(&iopmp, 0, 0, 2);                 /* {0,1} */

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MDCFG-001 MD0 = {0,1}");
}

/* IOPMP-MDCFG-002 - MDCFG(0).t=2, MDCFG(1).t=5 -> MD1 owns {2,3,4}. */
static void TestMdcfg002_Md1Range(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(2, 8, 4);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    SetupMdcfg(&iopmp, 0, 2);
    SetupMdcfg(&iopmp, 1, 5);
    AssertMdRange(&iopmp, 1, 2, 5);                 /* {2,3,4} */

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MDCFG-002 MD1 = {2,3,4}");
}

/* IOPMP-MDCFG-003 - MDCFG(0).t=0 -> MD0 empty. */
static void TestMdcfg003_Md0Empty(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(2, 8, 4);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    SetupMdcfg(&iopmp, 0, 0);
    AssertMdEmpty(&iopmp, 0);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MDCFG-003 MD0 empty when t=0");
}

/* IOPMP-MDCFG-004 - MDCFG(m-1).t == MDCFG(m).t -> MD m empty. */
static void TestMdcfg004_EqualBoundsEmpty(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(2, 8, 4);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    SetupMdcfg(&iopmp, 0, 3);
    SetupMdcfg(&iopmp, 1, 3);                        /* MD1 = [3,3) -> empty */
    AssertMdRange(&iopmp, 0, 0, 3);
    AssertMdEmpty(&iopmp, 1);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MDCFG-004 equal adjacent bounds -> empty MD");
}

/* IOPMP-MDCFG-005 - MDCFG(last).t = entry_num -> last MD spans to entry_num-1. */
static void TestMdcfg005_LastMdSpansToEnd(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(2, 8, 3);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    SetupMdcfg(&iopmp, 0, 2);
    SetupMdcfg(&iopmp, 1, 4);
    SetupMdcfg(&iopmp, 2, 8);                        /* entry_num = 8 */
    AssertMdRange(&iopmp, 2, 4, 8);                  /* {4,5,6,7} */

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MDCFG-005 last MD spans to entry_num-1");
}

/* IOPMP-MDCFG-006 - RRID associated with MD1 only: candidate set = MD1 entries. */
static void TestMdcfg006_CandidateSetIsMd(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(2, 4, 2);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    EnableIopmp(&iopmp);

    SetupMdcfg(&iopmp, 0, 2);                        /* MD0 = {0,1} */
    SetupMdcfg(&iopmp, 1, 4);                        /* MD1 = {2,3} */
    SetupSrcmd(&iopmp, 0, 1);                        /* RRID0 -> MD1 only */
    SetupEntry(&iopmp, 0, 0x1000ULL, ENTRY_CFG_R_BIT);  /* MD0 */
    SetupEntry(&iopmp, 2, 0x2000ULL, ENTRY_CFG_R_BIT);  /* MD1 */

    TxnResult_t inMd1 = IopmpCheckAccess(&iopmp, 0, 0x2000ULL, 4, IOPMP_TXN_READ);
    ASSERT_LEGAL(inMd1);
    assert(inMd1.entryIdx == 2U);

    /* Entry 0 belongs to MD0, which is not a candidate for RRID0. */
    TxnResult_t inMd0 = IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ);
    ASSERT_ETYPE(inMd0, IOPMP_ETYPE_NO_RULE);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MDCFG-006 candidate set limited to associated MD");
}

/* IOPMP-MDCFG-007 - entry index belongs to exactly one MD (partition). */
static void TestMdcfg007_Partition(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(2, 6, 3);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    SetupMdcfg(&iopmp, 0, 2);                        /* {0,1} */
    SetupMdcfg(&iopmp, 1, 4);                        /* {2,3} */
    SetupMdcfg(&iopmp, 2, 6);                        /* {4,5} */

    for (uint32_t j = 0U; j < params.entryNum; j++) {
        uint32_t owners = 0U;
        for (uint8_t m = 0U; m < params.mdNum; m++) {
            uint32_t s = 0U, e = 0U;
            if (MdcfgGetEntryRange(&iopmp, m, &s, &e) && j >= s && j < e) {
                owners++;
            }
        }
        assert(owners == 1U);                        /* exactly one MD owns j */
    }

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MDCFG-007 entry belongs to exactly one MD");
}

/* ───────────────────────────────────────────────────────────────────
 * 3.2 MDCFG(m).t register semantics
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-MDCFG-008 - MDCFG(m).t WARL [15:0]: write 7 reads back 7. */
static void TestMdcfg008_WarlBasic(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(2, 8, 4);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    SetupMdcfg(&iopmp, 1, 7);
    assert(ReadMdcfg(&iopmp, 1) == 7U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MDCFG-008 MDCFG.t WARL write 7");
}

/* IOPMP-MDCFG-009 - rsv [31:16] = ZERO: write all-ones keeps only t[15:0]. */
static void TestMdcfg009_ReservedZero(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(2, 8, 4);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    SetupMdcfg(&iopmp, 0, 0xFFFFFFFFU);
    uint32_t v = ReadMdcfg(&iopmp, 0);
    assert((v & MDCFG_T_MASK) == 0xFFFFU);
    assert((v & ~MDCFG_T_MASK) == 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MDCFG-009 reserved [31:16] read 0");
}

/* IOPMP-MDCFG-010 - md_num=4: MDCFG(0..3) implemented; MDCFG(4) not. */
static void TestMdcfg010_RegRange(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(2, 16, 4);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    for (uint8_t m = 0U; m < 4U; m++) {
        SetupMdcfg(&iopmp, m, (uint32_t)(m + 1U) * 2U);
        assert(ReadMdcfg(&iopmp, m) == (uint32_t)(m + 1U) * 2U);
    }

    /* MDCFG(4) is out of range: read 0, write dropped. */
    uint32_t off4 = REG_MDCFG_BASE + 4U * REG_MDCFG_STRIDE;
    IopmpWriteReg(&iopmp, off4, 9U);
    assert(IopmpReadReg(&iopmp, off4) == 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MDCFG-010 MDCFG range m=0..3");
}

/* ───────────────────────────────────────────────────────────────────
 * 3.3 Proper / monotonic settings
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-MDCFG-011 - strictly increasing t: all lookups well-defined. */
static void TestMdcfg011_StrictlyIncreasing(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(2, 8, 3);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    SetupMdcfg(&iopmp, 0, 2);
    SetupMdcfg(&iopmp, 1, 4);
    SetupMdcfg(&iopmp, 2, 6);
    AssertMdRange(&iopmp, 0, 0, 2);
    AssertMdRange(&iopmp, 1, 2, 4);
    AssertMdRange(&iopmp, 2, 4, 6);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MDCFG-011 strictly increasing table");
}

/* IOPMP-MDCFG-012 - equal adjacent t (non-decreasing): intermediate MD empty. */
static void TestMdcfg012_NonDecreasing(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(2, 8, 3);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    SetupMdcfg(&iopmp, 0, 2);
    SetupMdcfg(&iopmp, 1, 2);                        /* MD1 empty */
    SetupMdcfg(&iopmp, 2, 5);
    AssertMdRange(&iopmp, 0, 0, 2);
    AssertMdEmpty(&iopmp, 1);
    AssertMdRange(&iopmp, 2, 2, 5);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MDCFG-012 non-decreasing with empty intermediate MD");
}

/* ───────────────────────────────────────────────────────────────────
 * 3.4 Improper settings (§A.5 / §2.6)
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-MDCFG-013 - - improper MDCFG(m-1).t > MDCFG(m).t -> MD m has no entries. */
static void TestMdcfg013_ImproperMdEmpty(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(2, 8, 2);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    SetupMdcfg(&iopmp, 0, 4);
    SetupMdcfg(&iopmp, 1, 2);                        /* improper: 2 < 4 */

    /* §A.5 permits several behaviors; the model leaves the table as written
     * and reports the improper MD as owning no entries. */
    AssertMdEmpty(&iopmp, 1);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MDCFG-013 improper MD reports no entries");
}

/* IOPMP-MDCFG-014 - - §A.5(2) "reject improper write": model does NOT reject. */
static void TestMdcfg014_ImproperWriteAccepted(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(2, 8, 2);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    SetupMdcfg(&iopmp, 0, 4);
    SetupMdcfg(&iopmp, 1, 2);                        /* improper write */

    /* NOTE: MODEL DEVIATION from §A.5 option (2). The model does NOT reject an
     * improper write - the raw value is stored verbatim (read-back == 2).
     * Instead it implements option (3a): isolate the improper MD. */
    assert(ReadMdcfg(&iopmp, 1) == 2U);              /* write accepted, not rejected */
    AssertMdEmpty(&iopmp, 1);                        /* and MD1 is isolated */

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MDCFG-014 improper write accepted (model uses §A.5 option 3a)");
}

/* IOPMP-MDCFG-015 - - §A.5(3a): improper MDCFG(m) isolates only MD m. */
static void TestMdcfg015_IsolateMdM(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(2, 8, 2);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    EnableIopmp(&iopmp);

    SetupMdcfg(&iopmp, 0, 4);
    SetupMdcfg(&iopmp, 1, 2);                        /* MD1 improper -> isolated */
    SetupSrcmd(&iopmp, 0, 1);                        /* RRID0 -> MD1 */
    SetupEntry(&iopmp, 0, 0x1000ULL, ENTRY_CFG_R_BIT);

    AssertMdEmpty(&iopmp, 1);
    /* No entry belongs to MD1, so any txn from RRID0 hits NO_RULE. */
    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ);
    ASSERT_ETYPE(r, IOPMP_ETYPE_NO_RULE);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MDCFG-015 improper MD isolated (no entries)");
}

/* IOPMP-MDCFG-016 - - §A.5(3b) "isolate m..md_num-1": model isolates only MD m. */
static void TestMdcfg016_OnlyMdMIsolated(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(2, 8, 3);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    SetupMdcfg(&iopmp, 0, 4);
    SetupMdcfg(&iopmp, 1, 2);                        /* improper -> MD1 isolated */
    SetupMdcfg(&iopmp, 2, 8);                        /* MD2 = [2,8) still valid */

    AssertMdEmpty(&iopmp, 1);

    /* NOTE: MODEL DEVIATION from §A.5 option (3b). That option would isolate
     * MDs m..md_num-1; the model isolates ONLY the improper MD m. MD2 here
     * still owns the range [MDCFG(1).t, MDCFG(2).t) = [2,8). */
    AssertMdRange(&iopmp, 2, 2, 8);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MDCFG-016 only improper MD isolated (model uses 3a, not 3b)");
}

/* IOPMP-MDCFG-017 - - transient improper: deassociate before reprogramming. */
static void TestMdcfg017_TransientSafeWithDeassociation(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(2, 4, 2);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    EnableIopmp(&iopmp);

    SetupMdcfg(&iopmp, 0, 2);
    SetupMdcfg(&iopmp, 1, 4);
    SetupSrcmd(&iopmp, 0, 0);                        /* RRID0 -> MD0 */
    SetupEntry(&iopmp, 0, 0x1000ULL, ENTRY_CFG_R_BIT);
    ASSERT_LEGAL(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ));

    /* Programmer deassociates RRID0 from all MDs before touching MDCFG. */
    IopmpWriteReg(&iopmp, REG_SRCMD_BASE + REG_SRCMD_EN_OFF, 0U);

    /* Reprogram MDCFG transiently improper - RRID0 has no access regardless. */
    SetupMdcfg(&iopmp, 0, 4);
    SetupMdcfg(&iopmp, 1, 1);                        /* improper transient */
    TxnResult_t during = IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ);
    ASSERT_ETYPE(during, IOPMP_ETYPE_NO_RULE);       /* no security hole */

    /* Finish: proper table, then reassociate. */
    SetupMdcfg(&iopmp, 1, 4);
    SetupSrcmd(&iopmp, 0, 0);
    ASSERT_LEGAL(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ));

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MDCFG-017 deassociation closes transient window");
}

/* ───────────────────────────────────────────────────────────────────
 * Cross-combinations (file-local)
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-MDCFG-X01 - TOR entry 3 derives its base from entry 2 (same MD). */
static void TestMdcfgX01_TorBaseWithinMd(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(2, 6, 2);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    EnableIopmp(&iopmp);

    /* MD1 owns entries {2,3,4}. */
    SetupMdcfg(&iopmp, 0, 2);
    SetupMdcfg(&iopmp, 1, 5);
    SetupSrcmd(&iopmp, 0, 1);

    uint32_t base = IopmpReadReg(&iopmp, REG_ENTRYOFFSET);
    /* Entry 2 (OFF) supplies the TOR lower bound = 0x1000. */
    IopmpWriteReg(&iopmp, base + 2U * REG_ENTRY_STRIDE + REG_ENTRY_ADDR_OFF,
                  (uint32_t)(0x1000ULL >> 2U));
    IopmpWriteReg(&iopmp, base + 2U * REG_ENTRY_STRIDE + REG_ENTRY_CFG_OFF, 0x00U);
    /* Entry 3 (TOR) top = 0x2000 -> region [0x1000, 0x2000). */
    IopmpWriteReg(&iopmp, base + 3U * REG_ENTRY_STRIDE + REG_ENTRY_ADDR_OFF,
                  (uint32_t)(0x2000ULL >> 2U));
    IopmpWriteReg(&iopmp, base + 3U * REG_ENTRY_STRIDE + REG_ENTRY_CFG_OFF,
                  (ADDR_MODE_TOR << ENTRY_CFG_A_SHIFT) | ENTRY_CFG_R_BIT);

    TxnResult_t inRange = IopmpCheckAccess(&iopmp, 0, 0x1800ULL, 4, IOPMP_TXN_READ);
    ASSERT_LEGAL(inRange);
    assert(inRange.entryIdx == 3U);

    /* Below the derived base is outside the TOR region. */
    TxnResult_t below = IopmpCheckAccess(&iopmp, 0, 0x0800ULL, 4, IOPMP_TXN_READ);
    ASSERT_ETYPE(below, IOPMP_ETYPE_NO_RULE);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MDCFG-X01 TOR base derived from same-MD predecessor");
}

/* IOPMP-MDCFG-X02 - MDCFGLCK.f=2 locks MD0,1; MDCFG(2) still writable. */
static void TestMdcfgX02_MdcfglckBlocksLocked(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(2, 16, 4);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    SetupMdcfg(&iopmp, 1, 5);                        /* seed a value before lock */
    /* Lock the first two MDs: MDCFGLCK.f = 2. */
    IopmpWriteReg(&iopmp, REG_MDCFGLCK, 2U << MDCFGLCK_F_SHIFT);

    /* Write to locked MDCFG(1) is rejected. */
    SetupMdcfg(&iopmp, 1, 9);
    assert(ReadMdcfg(&iopmp, 1) == 5U);              /* unchanged */

    /* MDCFG(2) is not locked -> writable. */
    SetupMdcfg(&iopmp, 2, 7);
    assert(ReadMdcfg(&iopmp, 2) == 7U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MDCFG-X02 MDCFGLCK.f locks low MDs only");
}

/* IOPMP-MDCFG-X03 - locking MD m implies all preceding MDs are locked. */
static void TestMdcfgX03_LockImpliesPreceding(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(2, 16, 4);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    /* Seed values, then lock MDs 0..2 (f=3). */
    for (uint8_t m = 0U; m < 4U; m++) SetupMdcfg(&iopmp, m, (uint32_t)(m + 1U) * 2U);
    IopmpWriteReg(&iopmp, REG_MDCFGLCK, 3U << MDCFGLCK_F_SHIFT);

    /* MDs 0,1,2 are all locked (count model -> preceding always included). */
    for (uint8_t m = 0U; m < 3U; m++) {
        uint32_t before = ReadMdcfg(&iopmp, m);
        SetupMdcfg(&iopmp, m, 15U);
        assert(ReadMdcfg(&iopmp, m) == before);      /* rejected */
    }
    /* MD3 remains writable. */
    SetupMdcfg(&iopmp, 3, 13U);
    assert(ReadMdcfg(&iopmp, 3) == 13U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MDCFG-X03 lock count locks all preceding MDs");
}

/* IOPMP-MDCFG-X04 - - reprogram MDCFG under stall: held txns are not checked. */
static void TestMdcfgX04_ReprogramUnderStall(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(2, 4, 2);
    params.stallEn = true;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    EnableIopmp(&iopmp);

    SetupMdcfg(&iopmp, 0, 2);                        /* MD0 = {0,1} */
    SetupMdcfg(&iopmp, 1, 4);
    SetupSrcmd(&iopmp, 0, 0);                        /* RRID0 -> MD0 */
    SetupEntry(&iopmp, 0, 0x1000ULL, ENTRY_CFG_R_BIT);
    ASSERT_LEGAL(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ));

    /* Stall RRID0 via RRIDSCP (op = STALL). */
    IopmpWriteReg(&iopmp, REG_RRIDSCP,
                  (0U & RRIDSCP_RRID_MASK) | (RRIDSCP_OP_STALL << RRIDSCP_OP_SHIFT));
    TxnResult_t held = IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ);
    assert(held.stalled && !held.legal);             /* held, not checked */

    /* Reprogram MDCFG while stalled: MD0 now empty. */
    SetupMdcfg(&iopmp, 0, 0);

    /* Resume RRID0 (op = NOSTALL). Now the txn is checked against the new
     * setting -> MD0 empty -> NO_RULE. */
    IopmpWriteReg(&iopmp, REG_RRIDSCP,
                  (0U & RRIDSCP_RRID_MASK) | (RRIDSCP_OP_NOSTALL << RRIDSCP_OP_SHIFT));
    TxnResult_t after = IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ);
    ASSERT_ETYPE(after, IOPMP_ETYPE_NO_RULE);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MDCFG-X04 no txn checked against partial setting during stall");
}

/* IOPMP-MDCFG-X05 - - mdcfg_fmt=1 (no MDCFG table): range = [m*k, m*k+k). */
static void TestMdcfgX05_FixedFormatNoTable(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(2, 8, 4);
    params.mdcfgFmt = 1U;                            /* fixed equal partition */
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    /* k = entry_num / md_num = 2. MDm = [m*2, m*2+2). */
    AssertMdRange(&iopmp, 0, 0, 2);
    AssertMdRange(&iopmp, 1, 2, 4);
    AssertMdRange(&iopmp, 2, 4, 6);
    AssertMdRange(&iopmp, 3, 6, 8);

    /* MDCFG registers are not used in format 1 -> reads return 0. */
    assert(ReadMdcfg(&iopmp, 0) == 0U);
    assert(ReadMdcfg(&iopmp, 2) == 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MDCFG-X05 fixed-format range from k, MDCFG unused");
}

/* ── Runner ──────────────────────────────────────────────────────── */

int main(void)
{
    /* 3.1 Range computation */
    TestMdcfg001_Md0Range();
    TestMdcfg002_Md1Range();
    TestMdcfg003_Md0Empty();
    TestMdcfg004_EqualBoundsEmpty();
    TestMdcfg005_LastMdSpansToEnd();
    TestMdcfg006_CandidateSetIsMd();
    TestMdcfg007_Partition();

    /* 3.2 Register semantics */
    TestMdcfg008_WarlBasic();
    TestMdcfg009_ReservedZero();
    TestMdcfg010_RegRange();

    /* 3.3 Proper / monotonic */
    TestMdcfg011_StrictlyIncreasing();
    TestMdcfg012_NonDecreasing();

    /* 3.4 Improper settings */
    TestMdcfg013_ImproperMdEmpty();
    TestMdcfg014_ImproperWriteAccepted();
    TestMdcfg015_IsolateMdM();
    TestMdcfg016_OnlyMdMIsolated();
    TestMdcfg017_TransientSafeWithDeassociation();

    /* Cross-combinations */
    TestMdcfgX01_TorBaseWithinMd();
    TestMdcfgX02_MdcfglckBlocksLocked();
    TestMdcfgX03_LockImpliesPreceding();
    TestMdcfgX04_ReprogramUnderStall();
    TestMdcfgX05_FixedFormatNoTable();

    printf("\nAll file-03 MDCFG-table tests passed.\n");
    return 0;
}
