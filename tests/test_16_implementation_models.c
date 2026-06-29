/*
 * test_16_implementation_models.c
 *
 * Test suite for docs/testplan/16-implementation-models.md (Appendix A.8).
 *
 * Each model is a (srcmd_fmt, mdcfg_fmt) combination:
 *   Full(0,0)  Rapid-k(0,1)  Dynamic-k(0,2)  Isolation(1,0)  Compact-k(1,1)
 *
 * SPEC-compliant: verifies each model's configuration/capability consistency
 * and a representative access behaviour, plus the feature x model grid.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include "iopmp.h"
#include "iopmp_internal.h"
#include "test_utils.h"

/* ── Shared setup helpers ────────────────────────────────────────────── */

static IopmpParams_t ModelParams(IopmpModel_t model, uint16_t rridNum,
                                 uint16_t entryNum, uint8_t mdNum)
{
    IopmpParams_t p;
    memset(&p, 0, sizeof(p));
    p.rridNum = rridNum; p.entryNum = entryNum; p.mdNum = mdNum;
    p.torEn = true; p.hwcfg3En = true; p.model = model;
    switch (model) {
    case IOPMP_MODEL_FULL:      p.srcmdFmt = 0; p.mdcfgFmt = 0; break;
    case IOPMP_MODEL_RAPID_K:   p.srcmdFmt = 0; p.mdcfgFmt = 1; break;
    case IOPMP_MODEL_DYNAMIC_K: p.srcmdFmt = 0; p.mdcfgFmt = 2; p.mdEntryNum = 2; break;
    case IOPMP_MODEL_ISOLATION: p.srcmdFmt = 1; p.mdcfgFmt = 0; break;
    case IOPMP_MODEL_COMPACT:   p.srcmdFmt = 1; p.mdcfgFmt = 1; break;
    }
    return p;
}
static void Enable(IopmpState_t *i) { IopmpWriteReg(i, REG_HWCFG0, HWCFG0_ENABLE_BIT); }
static void Srcmd(IopmpState_t *i, uint16_t rrid, uint8_t md)
{
    uint32_t off = REG_SRCMD_BASE + (uint32_t)rrid * REG_SRCMD_STRIDE + REG_SRCMD_EN_OFF;
    IopmpWriteReg(i, off, IopmpReadReg(i, off) | (1U << ((uint32_t)md + 1U)));
}
static void Mdcfg(IopmpState_t *i, uint8_t md, uint32_t end)
{
    IopmpWriteReg(i, REG_MDCFG_BASE + (uint32_t)md * REG_MDCFG_STRIDE, end);
}
static void Na4(IopmpState_t *i, uint32_t idx, uint64_t addr, uint32_t perm)
{
    uint32_t b = IopmpReadReg(i, REG_ENTRYOFFSET) + idx * REG_ENTRY_STRIDE;
    IopmpWriteReg(i, b + REG_ENTRY_ADDR_OFF, (uint32_t)(addr >> 2U));
    IopmpWriteReg(i, b + REG_ENTRY_CFG_OFF, (ADDR_MODE_NA4 << ENTRY_CFG_A_SHIFT) | perm);
}

/* ───────────────────────────────────────────────────────────────────
 * 16.1 Full
 * ─────────────────────────────────────────────────────────────────── */

static void TestModel001to003_Full(void)
{
    IopmpState_t iopmp;
    IopmpParams_t p = ModelParams(IOPMP_MODEL_FULL, 4, 4, 2);
    p.hwcfg2En = true; p.spsEn = true; p.stallEn = true;
    p.msiEn = true; p.multifaultEn = true; p.nonPrioEn = true;
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);

    uint32_t h3 = IopmpReadReg(&iopmp, REG_HWCFG3);
    assert((h3 & HWCFG3_MDCFG_FMT_MASK) == 0U);
    assert(((h3 & HWCFG3_SRCMD_FMT_MASK) >> HWCFG3_SRCMD_FMT_SHIFT) == 0U);

    /* Baseline SRCMD/MDCFG/match still works (SPS on -> grant MD0 read, bit 1). */
    Enable(&iopmp); Mdcfg(&iopmp, 0, 4); Srcmd(&iopmp, 0, 0);
    IopmpWriteReg(&iopmp, REG_SRCMD_BASE + REG_SRCMD_R_OFF, (1U << 1));
    Na4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_R_BIT);
    ASSERT_LEGAL(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ));

    /* All extensions present. */
    uint32_t h2 = IopmpReadReg(&iopmp, REG_HWCFG2);
    assert((h2 & HWCFG2_SPS_EN_BIT) && (h2 & HWCFG2_STALL_EN_BIT)
           && (h2 & HWCFG2_MSI_EN_BIT) && (h2 & HWCFG2_MFR_EN_BIT)
           && (h2 & HWCFG2_NON_PRIO_EN_BIT));
    IopmpDestroy(&iopmp);
    PASS("IOPMP-MODEL-001..003 Full");
}

/* ───────────────────────────────────────────────────────────────────
 * 16.2 Rapid-k
 * ─────────────────────────────────────────────────────────────────── */

static void TestModel004to008_RapidK(void)
{
    IopmpState_t iopmp;
    uint32_t s, e;
    IopmpParams_t p = ModelParams(IOPMP_MODEL_RAPID_K, 4, 16, 4);   /* k=16/4=4 */
    p.hwcfg2En = true; p.spsEn = true;
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    assert((IopmpReadReg(&iopmp, REG_HWCFG3) & HWCFG3_MDCFG_FMT_MASK) == 1U);

    /* 004/007: MD m = [m*4, m*4+4); no MDCFG lookup. */
    assert(MdcfgGetEntryRange(&iopmp, 0, &s, &e) && s == 0 && e == 4);
    assert(MdcfgGetEntryRange(&iopmp, 2, &s, &e) && s == 8 && e == 12);
    /* 008: MDCFG table omitted (reads 0). */
    assert(IopmpReadReg(&iopmp, REG_MDCFG_BASE) == 0U);
    /* 005: full SRCMD with arbitrary association. */
    Enable(&iopmp); Srcmd(&iopmp, 1, 2);                 /* RRID1 -> MD2 (entries 8..11) */
    /* SPS on -> grant RRID1 read for MD2 (bit 2+1=3). */
    IopmpWriteReg(&iopmp, REG_SRCMD_BASE + 1U * REG_SRCMD_STRIDE + REG_SRCMD_R_OFF, (1U << 3));
    Na4(&iopmp, 8, 0x8000ULL, ENTRY_CFG_R_BIT);
    ASSERT_LEGAL(IopmpCheckAccess(&iopmp, 1, 0x8000ULL, 4, IOPMP_TXN_READ));
    /* 006: SPS supported. */
    assert((IopmpReadReg(&iopmp, REG_HWCFG2) & HWCFG2_SPS_EN_BIT) != 0U);
    IopmpDestroy(&iopmp);
    PASS("IOPMP-MODEL-004..008 Rapid-k");
}

/* ───────────────────────────────────────────────────────────────────
 * 16.3 Dynamic-k
 * ─────────────────────────────────────────────────────────────────── */

static void TestModel009to011_DynamicK(void)
{
    IopmpState_t iopmp;
    uint32_t s, e;
    IopmpParams_t p = ModelParams(IOPMP_MODEL_DYNAMIC_K, 4, 16, 4);
    p.hwcfg2En = true; p.spsEn = true;
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    assert((IopmpReadReg(&iopmp, REG_HWCFG3) & HWCFG3_MDCFG_FMT_MASK) == 2U);

    /* 009: k programmable before enable. md_entry_num=2 -> MD m = [m*2, m*2+2). */
    assert(MdcfgGetEntryRange(&iopmp, 1, &s, &e) && s == 2 && e == 4);
    IopmpWriteReg(&iopmp, REG_HWCFG3,
        (IopmpReadReg(&iopmp, REG_HWCFG3) & ~HWCFG3_MD_ENTRY_NUM_MASK) | (4U << HWCFG3_MD_ENTRY_NUM_SHIFT));
    assert(MdcfgGetEntryRange(&iopmp, 1, &s, &e) && s == 4 && e == 8);   /* k=4 now */
    /* 009: locked after enable. */
    Enable(&iopmp);
    IopmpWriteReg(&iopmp, REG_HWCFG3,
        (IopmpReadReg(&iopmp, REG_HWCFG3) & ~HWCFG3_MD_ENTRY_NUM_MASK) | (2U << HWCFG3_MD_ENTRY_NUM_SHIFT));
    assert(MdcfgGetEntryRange(&iopmp, 1, &s, &e) && s == 4 && e == 8);   /* unchanged */
    /* 010: SPS supported. */
    assert((IopmpReadReg(&iopmp, REG_HWCFG2) & HWCFG2_SPS_EN_BIT) != 0U);
    IopmpDestroy(&iopmp);
    PASS("IOPMP-MODEL-009..011 Dynamic-k");
}

/* ───────────────────────────────────────────────────────────────────
 * 16.4 Isolation
 * ─────────────────────────────────────────────────────────────────── */

static void TestModel012to017_Isolation(void)
{
    IopmpState_t iopmp;
    IopmpParams_t p = ModelParams(IOPMP_MODEL_ISOLATION, 4, 8, 4);
    p.hwcfg2En = true;
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    Enable(&iopmp);

    /* 014: full MDCFG present - flexible allocation. MD2 = entries [3,5). */
    Mdcfg(&iopmp, 0, 1); Mdcfg(&iopmp, 1, 3); Mdcfg(&iopmp, 2, 5); Mdcfg(&iopmp, 3, 8);
    Na4(&iopmp, 3, 0x3000ULL, ENTRY_CFG_R_BIT);          /* in MD2 */

    /* 012: RRID 2 -> MD 2 exclusively. */
    ASSERT_LEGAL(IopmpCheckAccess(&iopmp, 2, 0x3000ULL, 4, IOPMP_TXN_READ));
    /* RRID 0 cannot reach MD2. */
    ASSERT_ETYPE(IopmpCheckAccess(&iopmp, 0, 0x3000ULL, 4, IOPMP_TXN_READ), IOPMP_ETYPE_NO_RULE);
    /* 013: no SRCMD table. */
    assert(IopmpReadReg(&iopmp, REG_SRCMD_BASE + REG_SRCMD_EN_OFF) == 0U);
    /* 015: sps_en = 0. */
    assert((IopmpReadReg(&iopmp, REG_HWCFG2) & HWCFG2_SPS_EN_BIT) == 0U);
    /* 016: RRID >= rrid_num unsupported. */
    ASSERT_ETYPE(IopmpCheckAccess(&iopmp, 4, 0x3000ULL, 4, IOPMP_TXN_READ), IOPMP_ETYPE_UNKNOWN_RRID);
    /* 017: MD with no entries -> NO_RULE. */
    Mdcfg(&iopmp, 1, 1);                                 /* MD1 now empty [1,1) */
    ASSERT_ETYPE(IopmpCheckAccess(&iopmp, 1, 0x9000ULL, 4, IOPMP_TXN_READ), IOPMP_ETYPE_NO_RULE);
    IopmpDestroy(&iopmp);
    PASS("IOPMP-MODEL-012..017 Isolation");
}

/* ───────────────────────────────────────────────────────────────────
 * 16.5 Compact-k
 * ─────────────────────────────────────────────────────────────────── */

static void TestModel018to022_CompactK(void)
{
    IopmpState_t iopmp;
    IopmpParams_t p = ModelParams(IOPMP_MODEL_COMPACT, 4, 16, 4);   /* k=4 */
    p.hwcfg2En = true;
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    Enable(&iopmp);

    /* 018/021: MD i = RRID i; entries [i*4, i*4+4). RRID2 -> entries 8..11. */
    Na4(&iopmp, 8, 0x8000ULL, ENTRY_CFG_R_BIT);
    ASSERT_LEGAL(IopmpCheckAccess(&iopmp, 2, 0x8000ULL, 4, IOPMP_TXN_READ));
    ASSERT_ETYPE(IopmpCheckAccess(&iopmp, 0, 0x8000ULL, 4, IOPMP_TXN_READ), IOPMP_ETYPE_NO_RULE);
    /* 019: neither SRCMD nor MDCFG table implemented. */
    assert(IopmpReadReg(&iopmp, REG_SRCMD_BASE + REG_SRCMD_EN_OFF) == 0U);
    assert(IopmpReadReg(&iopmp, REG_MDCFG_BASE) == 0U);
    /* 020: sps_en = 0. */
    assert((IopmpReadReg(&iopmp, REG_HWCFG2) & HWCFG2_SPS_EN_BIT) == 0U);
    /* 022: RRID >= rrid_num unsupported. */
    ASSERT_ETYPE(IopmpCheckAccess(&iopmp, 4, 0x8000ULL, 4, IOPMP_TXN_READ), IOPMP_ETYPE_UNKNOWN_RRID);
    IopmpDestroy(&iopmp);
    PASS("IOPMP-MODEL-018..022 Compact-k");
}

/* ───────────────────────────────────────────────────────────────────
 * 16.6 Feature x model grid (representative checks)
 * ─────────────────────────────────────────────────────────────────── */

/* Run a representative deny + capture under each model. */
static void RunPermDenyAndCapture(IopmpModel_t model)
{
    IopmpState_t iopmp;
    IopmpParams_t p = ModelParams(model, 4, 16, 4);
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    Enable(&iopmp);

    /* Configure MD0/entry0 reachable by RRID0 in every model. */
    if (model == IOPMP_MODEL_FULL) { Mdcfg(&iopmp, 0, 4); Srcmd(&iopmp, 0, 0); }
    else if (model == IOPMP_MODEL_RAPID_K || model == IOPMP_MODEL_DYNAMIC_K) { Srcmd(&iopmp, 0, 0); }
    else if (model == IOPMP_MODEL_ISOLATION) { Mdcfg(&iopmp, 0, 4); }
    /* Compact-k: RRID0 -> MD0 -> entries [0,k). */

    Na4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_R_BIT);          /* deny write */
    /* MATCH-002 perm deny + ERR-001 capture. */
    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_WRITE);
    ASSERT_ETYPE(r, IOPMP_ETYPE_ILLEGAL_WRITE);
    assert((IopmpReadReg(&iopmp, REG_ERR_INFO) & ERR_INFO_V_BIT) != 0U);
    /* MATCH-007 read legal. */
    ASSERT_LEGAL(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ));
    IopmpDestroy(&iopmp);
}

static void TestModelGrid(void)
{
    RunPermDenyAndCapture(IOPMP_MODEL_FULL);
    RunPermDenyAndCapture(IOPMP_MODEL_RAPID_K);
    RunPermDenyAndCapture(IOPMP_MODEL_DYNAMIC_K);
    RunPermDenyAndCapture(IOPMP_MODEL_ISOLATION);
    RunPermDenyAndCapture(IOPMP_MODEL_COMPACT);
    PASS("IOPMP-MODEL grid: deny/capture/read across all models");
}

/* SPS applies only to Full/Rapid-k/Dynamic-k (srcmd_fmt=0). */
static void TestModelGridSps(void)
{
    /* SPS present in Full. */
    IopmpState_t iopmp;
    IopmpParams_t p = ModelParams(IOPMP_MODEL_FULL, 4, 4, 2);
    p.hwcfg2En = true; p.spsEn = true;
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    assert((IopmpReadReg(&iopmp, REG_HWCFG2) & HWCFG2_SPS_EN_BIT) != 0U);
    IopmpDestroy(&iopmp);

    /* SPS absent (n/a) in Isolation and Compact-k: capability bit 0. */
    p = ModelParams(IOPMP_MODEL_ISOLATION, 4, 8, 4);
    p.hwcfg2En = true;
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    assert((IopmpReadReg(&iopmp, REG_HWCFG2) & HWCFG2_SPS_EN_BIT) == 0U);
    IopmpDestroy(&iopmp);

    p = ModelParams(IOPMP_MODEL_COMPACT, 4, 8, 4);
    p.hwcfg2En = true;
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    assert((IopmpReadReg(&iopmp, REG_HWCFG2) & HWCFG2_SPS_EN_BIT) == 0U);
    IopmpDestroy(&iopmp);
    PASS("IOPMP-MODEL grid: SPS present/absent per model");
}

/* ───────────────────────────────────────────────────────────────────
 * Cross-combinations
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-MODEL-X01 - Isolation + sps_en is an illegal config (rejected). */
static void TestModelX01_IsolationSpsInvalid(void)
{
    IopmpState_t iopmp;
    IopmpParams_t p = ModelParams(IOPMP_MODEL_ISOLATION, 4, 8, 4);
    p.hwcfg2En = true; p.spsEn = true;                   /* invalid: SPS needs srcmd_fmt 0 */
    assert(IopmpInit(&iopmp, &p) != IOPMP_OK);
    PASS("IOPMP-MODEL-X01 isolation+sps rejected");
}

/* IOPMP-MODEL-X02 - Compact-k under stall: MDSTALL MD m stalls only RRID m. */
static void TestModelX02_CompactStall(void)
{
    IopmpState_t iopmp;
    IopmpParams_t p = ModelParams(IOPMP_MODEL_COMPACT, 4, 16, 4);
    p.hwcfg2En = true; p.stallEn = true;
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    Enable(&iopmp);
    Na4(&iopmp, 8, 0x8000ULL, ENTRY_CFG_R_BIT);          /* MD2/RRID2 region */

    IopmpWriteReg(&iopmp, REG_MDSTALL, (1U << (2 + 1)));  /* select MD2 */
    /* RRID2 (== MD2) stalled. */
    assert(IopmpCheckAccess(&iopmp, 2, 0x8000ULL, 4, IOPMP_TXN_READ).stalled);
    IopmpWriteReg(&iopmp, REG_RRIDSCP, (0U & RRIDSCP_RRID_MASK) | (RRIDSCP_OP_QUERY << RRIDSCP_OP_SHIFT));
    assert(((IopmpReadReg(&iopmp, REG_RRIDSCP) & RRIDSCP_OP_MASK) >> RRIDSCP_OP_SHIFT)
           == RRIDSCP_STAT_NOTSTALLED);                  /* RRID0 not stalled */
    IopmpDestroy(&iopmp);
    PASS("IOPMP-MODEL-X02 compact-k 1:1 stall");
}

/* IOPMP-MODEL-X03 - Rapid-k: k*md_num > entry_num: high entries OFF. */
static void TestModelX03_RapidKClamp(void)
{
    IopmpState_t iopmp;
    uint32_t s, e;
    IopmpParams_t p = ModelParams(IOPMP_MODEL_RAPID_K, 4, 6, 4);   /* k=1 (6/4), MD3=entry3 */
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    /* k=1: MD m = [m,m+1); MD3 -> entry 3 (within 6). Out-of-range entry reads 0. */
    assert(MdcfgGetEntryRange(&iopmp, 3, &s, &e) && s == 3 && e == 4);
    uint32_t b = IopmpReadReg(&iopmp, REG_ENTRYOFFSET) + p.entryNum * REG_ENTRY_STRIDE;
    assert(IopmpReadReg(&iopmp, b + REG_ENTRY_CFG_OFF) == 0U);
    IopmpDestroy(&iopmp);
    PASS("IOPMP-MODEL-X03 rapid-k clamp to entry_num");
}

/* IOPMP-MODEL-X04 - two instances of different models behave independently. */
static void TestModelX04_TwoModels(void)
{
    IopmpState_t full, comp;
    IopmpParams_t pf = ModelParams(IOPMP_MODEL_FULL, 4, 4, 2);
    IopmpParams_t pc = ModelParams(IOPMP_MODEL_COMPACT, 4, 16, 4);
    assert(IopmpInit(&full, &pf) == IOPMP_OK);
    assert(IopmpInit(&comp, &pc) == IOPMP_OK);

    Enable(&full); Mdcfg(&full, 0, 4); Srcmd(&full, 0, 0);
    Na4(&full, 0, 0x1000ULL, ENTRY_CFG_R_BIT);
    Enable(&comp); Na4(&comp, 0, 0x1000ULL, ENTRY_CFG_R_BIT);   /* RRID0 -> MD0 -> entries 0..3 */

    ASSERT_LEGAL(IopmpCheckAccess(&full, 0, 0x1000ULL, 4, IOPMP_TXN_READ));
    ASSERT_LEGAL(IopmpCheckAccess(&comp, 0, 0x1000ULL, 4, IOPMP_TXN_READ));
    /* Compact-k RRID1 maps to MD1 (entries 4..7) -> cannot reach entry 0. */
    ASSERT_ETYPE(IopmpCheckAccess(&comp, 1, 0x1000ULL, 4, IOPMP_TXN_READ), IOPMP_ETYPE_NO_RULE);

    IopmpDestroy(&full);
    IopmpDestroy(&comp);
    PASS("IOPMP-MODEL-X04 two models independent");
}

/* ── Runner ──────────────────────────────────────────────────────── */

int main(void)
{
    TestModel001to003_Full();
    TestModel004to008_RapidK();
    TestModel009to011_DynamicK();
    TestModel012to017_Isolation();
    TestModel018to022_CompactK();

    TestModelGrid();
    TestModelGridSps();

    TestModelX01_IsolationSpsInvalid();
    TestModelX02_CompactStall();
    TestModelX03_RapidKClamp();
    TestModelX04_TwoModels();

    printf("\nAll file-16 implementation-model tests passed.\n");
    return 0;
}
