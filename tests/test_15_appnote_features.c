/*
 * test_15_appnote_features.c
 *
 * Test suite for docs/testplan/15-appnote-features.md (Appendix A features).
 *
 * Spec: A.1 (no_w), A.2 (no_x), A.3 (xinr), A.4 (SRCMD reductions:
 *       source-enforcement, Exclusive, MD-indexed), A.6 (k-entry MDCFG),
 *       A.7 (rrid_transl).
 *
 * SPEC-compliant. Each appendix feature is exercised in isolation.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include "iopmp.h"
#include "iopmp_internal.h"
#include "test_utils.h"

/* ── Shared setup helpers ────────────────────────────────────────────── */

static IopmpParams_t Base(uint16_t rridNum, uint16_t entryNum, uint8_t mdNum)
{
    IopmpParams_t p;
    memset(&p, 0, sizeof(p));
    p.rridNum = rridNum; p.entryNum = entryNum; p.mdNum = mdNum;
    p.torEn = true; p.hwcfg3En = true; p.model = IOPMP_MODEL_FULL;
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
 * 15.1 no_w
 * ─────────────────────────────────────────────────────────────────── */

static void TestAppn001to005_NoW(void)
{
    IopmpState_t iopmp;
    IopmpParams_t p = Base(1, 4, 1);
    p.noW = true;
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    Enable(&iopmp); Mdcfg(&iopmp, 0, 4); Srcmd(&iopmp, 0, 0);
    Na4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_R_BIT | ENTRY_CFG_W_BIT);

    /* 001: write to a w=1 entry -> globally denied 0x05. */
    ASSERT_ETYPE(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_WRITE), IOPMP_ETYPE_NO_RULE);
    /* 002: read still legal. */
    ASSERT_LEGAL(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ));
    /* 003: AMO denied (it is a write). */
    ASSERT_ETYPE(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_AMO), IOPMP_ETYPE_NO_RULE);
    /* 005: no_w is read-only in HWCFG3. */
    uint32_t h3 = IopmpReadReg(&iopmp, REG_HWCFG3);
    IopmpWriteReg(&iopmp, REG_HWCFG3, h3 & ~HWCFG3_NO_W_BIT);
    assert((IopmpReadReg(&iopmp, REG_HWCFG3) & HWCFG3_NO_W_BIT) != 0U);
    IopmpDestroy(&iopmp);

    /* 004: no_w=0 -> normal write checking. */
    p = Base(1, 4, 1);
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    Enable(&iopmp); Mdcfg(&iopmp, 0, 4); Srcmd(&iopmp, 0, 0);
    Na4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_R_BIT | ENTRY_CFG_W_BIT);
    ASSERT_LEGAL(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_WRITE));
    IopmpDestroy(&iopmp);
    PASS("IOPMP-APPN-001..005 no_w");
}

/* ───────────────────────────────────────────────────────────────────
 * 15.2 no_x
 * ─────────────────────────────────────────────────────────────────── */

static void TestAppn006to008_NoX(void)
{
    IopmpState_t iopmp;
    IopmpParams_t p = Base(1, 4, 1);
    p.noX = true;
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    Enable(&iopmp); Mdcfg(&iopmp, 0, 4); Srcmd(&iopmp, 0, 0);
    Na4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_R_BIT | ENTRY_CFG_W_BIT | ENTRY_CFG_X_BIT);

    ASSERT_ETYPE(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_EXEC), IOPMP_ETYPE_NO_RULE);
    ASSERT_LEGAL(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ));
    ASSERT_LEGAL(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_WRITE));
    IopmpDestroy(&iopmp);

    p = Base(1, 4, 1);                                   /* no_x=0 baseline */
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    Enable(&iopmp); Mdcfg(&iopmp, 0, 4); Srcmd(&iopmp, 0, 0);
    Na4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_X_BIT);
    ASSERT_LEGAL(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_EXEC));
    IopmpDestroy(&iopmp);
    PASS("IOPMP-APPN-006..008 no_x");
}

/* ───────────────────────────────────────────────────────────────────
 * 15.3 xinr
 * ─────────────────────────────────────────────────────────────────── */

static void TestAppn009to012_Xinr(void)
{
    IopmpState_t iopmp;
    IopmpParams_t p = Base(1, 4, 1);
    p.xinr = true;
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    Enable(&iopmp); Mdcfg(&iopmp, 0, 4); Srcmd(&iopmp, 0, 0);

    /* 009: entry r=1,x=0; exec checked against read -> LEGAL. */
    Na4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_R_BIT);
    ASSERT_LEGAL(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_EXEC));
    /* 010: entry r=0; exec treated as read -> 0x01. */
    Na4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_W_BIT);
    ASSERT_ETYPE(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_EXEC), IOPMP_ETYPE_ILLEGAL_READ);
    IopmpDestroy(&iopmp);

    /* 012: xinr=0 baseline -> exec checked against x. */
    p = Base(1, 4, 1);
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    Enable(&iopmp); Mdcfg(&iopmp, 0, 4); Srcmd(&iopmp, 0, 0);
    Na4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_R_BIT);          /* no x */
    ASSERT_ETYPE(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_EXEC), IOPMP_ETYPE_ILLEGAL_EXEC);
    IopmpDestroy(&iopmp);
    PASS("IOPMP-APPN-009..012 xinr");
}

/* ───────────────────────────────────────────────────────────────────
 * 15.4 Exclusive SRCMD (srcmd_fmt=1)
 * ─────────────────────────────────────────────────────────────────── */

static void TestAppn013to016_Exclusive(void)
{
    IopmpState_t iopmp;
    IopmpParams_t p = Base(4, 4, 2);
    p.srcmdFmt = 1U; p.mdcfgFmt = 1U; p.model = IOPMP_MODEL_ISOLATION;
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    Enable(&iopmp);
    /* k = entry_num/md_num = 2: MD0 = {0,1}, MD1 = {2,3}. */
    Na4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_R_BIT);          /* MD0 */
    Na4(&iopmp, 2, 0x2000ULL, ENTRY_CFG_R_BIT);          /* MD1 */

    /* 013/015: RRID i exclusively associated with MD i. */
    ASSERT_LEGAL(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ));
    ASSERT_LEGAL(IopmpCheckAccess(&iopmp, 1, 0x2000ULL, 4, IOPMP_TXN_READ));
    /* 016: RRID 0 cannot reach MD1's region (no sharing). */
    ASSERT_ETYPE(IopmpCheckAccess(&iopmp, 0, 0x2000ULL, 4, IOPMP_TXN_READ), IOPMP_ETYPE_NO_RULE);
    /* 014: SRCMD table not implemented (reads 0). */
    assert(IopmpReadReg(&iopmp, REG_SRCMD_BASE + REG_SRCMD_EN_OFF) == 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-APPN-013..016 exclusive srcmd_fmt=1");
}

/* ───────────────────────────────────────────────────────────────────
 * 15.5 MD-indexed SRCMD (srcmd_fmt=2)
 * ─────────────────────────────────────────────────────────────────── */

/* SRCMD_PERM(md): RRID s read=bit 2s, write=bit 2s+1 (s<16). */
static void PermLow(IopmpState_t *i, uint8_t md, uint16_t rrid, bool rd, bool wr)
{
    uint32_t off = REG_SRCMD_BASE + (uint32_t)md * REG_SRCMD_STRIDE + REG_SRCMD_EN_OFF;
    uint32_t v = IopmpReadReg(i, off);
    if (rd) v |= (1U << (2U * rrid));
    if (wr) v |= (1U << (2U * rrid + 1U));
    IopmpWriteReg(i, off, v);
}

static void TestAppn017to022_MdIndexed(void)
{
    IopmpState_t iopmp;
    IopmpParams_t p = Base(4, 4, 2);
    p.srcmdFmt = 2U;
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    Enable(&iopmp);
    Mdcfg(&iopmp, 0, 2);                                 /* MD0 = {0,1} */

    /* 017: perm read + entry read -> legal (both allow). */
    PermLow(&iopmp, 0, 0, true, false);                  /* RRID0 read MD0 */
    Na4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_R_BIT);
    ASSERT_LEGAL(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ));
    /* 018: instruction fetch uses the read bit. */
    ASSERT_LEGAL(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_EXEC));
    IopmpDestroy(&iopmp);

    /* 019: SRCMD_PERM grants but entry denies -> legal (OR semantics). */
    p = Base(4, 4, 2); p.srcmdFmt = 2U;
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    Enable(&iopmp); Mdcfg(&iopmp, 0, 2);
    PermLow(&iopmp, 0, 0, true, false);                  /* perm read */
    Na4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_W_BIT);          /* entry denies read */
    ASSERT_LEGAL(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ));
    IopmpDestroy(&iopmp);

    /* 020: rrid_num>16, SRCMD_PERMH governs RRID 20. */
    p = Base(24, 4, 2); p.srcmdFmt = 2U;
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    Enable(&iopmp); Mdcfg(&iopmp, 0, 2);
    /* RRID20 read MD0 via PERMH bit 2*(20-16)=8. */
    IopmpWriteReg(&iopmp, REG_SRCMD_BASE + 0U * REG_SRCMD_STRIDE + REG_SRCMD_ENH_OFF, (1U << 8));
    Na4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_R_BIT);
    ASSERT_LEGAL(IopmpCheckAccess(&iopmp, 20, 0x1000ULL, 4, IOPMP_TXN_READ));
    /* 021: RRID 32 not supported (out of range here). */
    ASSERT_ETYPE(IopmpCheckAccess(&iopmp, 32, 0x1000ULL, 4, IOPMP_TXN_READ), IOPMP_ETYPE_UNKNOWN_RRID);
    IopmpDestroy(&iopmp);

    /* 022: SPS unsupported with srcmd_fmt=2 (rejected at init). */
    p = Base(4, 4, 2); p.srcmdFmt = 2U; p.spsEn = true; p.hwcfg2En = true;
    assert(IopmpInit(&iopmp, &p) != IOPMP_OK);

    PASS("IOPMP-APPN-017..022 MD-indexed srcmd_fmt=2");
}

/* ───────────────────────────────────────────────────────────────────
 * 15.6 Source-enforcement
 * ─────────────────────────────────────────────────────────────────── */

static const bool kBypass[2] = { true, false };

static void TestAppn023_SourceEnforcement(void)
{
    IopmpState_t iopmp;
    IopmpParams_t p = Base(2, 4, 1);
    p.rridBypassVec = kBypass;                           /* RRID0 enforced upstream */
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    Enable(&iopmp); Mdcfg(&iopmp, 0, 4); Srcmd(&iopmp, 1, 0);
    /* No entries: RRID1 would be NO_RULE, but RRID0 bypasses the check. */
    ASSERT_LEGAL(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_WRITE));
    ASSERT_ILLEGAL(IopmpCheckAccess(&iopmp, 1, 0x1000ULL, 4, IOPMP_TXN_READ));
    IopmpDestroy(&iopmp);
    PASS("IOPMP-APPN-023 source-enforcement bypass");
}

/* ───────────────────────────────────────────────────────────────────
 * 15.7 k-entry MDCFG reduction
 * ─────────────────────────────────────────────────────────────────── */

static void TestAppn024to028_KEntry(void)
{
    IopmpState_t iopmp;
    uint32_t s, e;

    /* 024: mdcfg_fmt=1, k = entry_num/md_num = 2. MD m = [m*2, m*2+2). */
    IopmpParams_t p = Base(2, 8, 4);
    p.mdcfgFmt = 1U;
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    assert(MdcfgGetEntryRange(&iopmp, 0, &s, &e) && s == 0 && e == 2);
    assert(MdcfgGetEntryRange(&iopmp, 2, &s, &e) && s == 4 && e == 6);
    /* 028: MDCFG table absent in fmt 1 (reads 0). */
    assert(IopmpReadReg(&iopmp, REG_MDCFG_BASE) == 0U);
    IopmpDestroy(&iopmp);

    /* 025: k=1 -> each MD owns exactly one entry (MD index == entry index). */
    p = Base(2, 4, 4); p.mdcfgFmt = 1U;
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    assert(MdcfgGetEntryRange(&iopmp, 3, &s, &e) && s == 3 && e == 4);
    IopmpDestroy(&iopmp);

    /* 027: k*md_num > entry_num -> high entries clamped (treated OFF). */
    p = Base(2, 5, 4); p.mdcfgFmt = 2U; p.mdEntryNum = 2;   /* needs 8 entries, has 5 */
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    assert(MdcfgGetEntryRange(&iopmp, 0, &s, &e) && s == 0 && e == 2);
    /* MD2 would be [4,6) but clamps to entry_num=5 -> [4,5). */
    assert(MdcfgGetEntryRange(&iopmp, 2, &s, &e) && s == 4 && e == 5);
    /* MD3 = [6,8) entirely beyond entry_num -> empty. */
    assert(!MdcfgGetEntryRange(&iopmp, 3, &s, &e));
    IopmpDestroy(&iopmp);

    /* 026: mdcfg_fmt=2 md_entry_num programmable, locked by enable. */
    p = Base(2, 8, 2); p.mdcfgFmt = 2U; p.mdEntryNum = 2;
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    IopmpWriteReg(&iopmp, REG_HWCFG3,
        (IopmpReadReg(&iopmp, REG_HWCFG3) & ~HWCFG3_MD_ENTRY_NUM_MASK) | (3U << HWCFG3_MD_ENTRY_NUM_SHIFT));
    assert(((IopmpReadReg(&iopmp, REG_HWCFG3) & HWCFG3_MD_ENTRY_NUM_MASK) >> HWCFG3_MD_ENTRY_NUM_SHIFT) == 3U);
    IopmpWriteReg(&iopmp, REG_HWCFG0, HWCFG0_ENABLE_BIT);
    IopmpWriteReg(&iopmp, REG_HWCFG3,
        (IopmpReadReg(&iopmp, REG_HWCFG3) & ~HWCFG3_MD_ENTRY_NUM_MASK) | (4U << HWCFG3_MD_ENTRY_NUM_SHIFT));
    assert(((IopmpReadReg(&iopmp, REG_HWCFG3) & HWCFG3_MD_ENTRY_NUM_MASK) >> HWCFG3_MD_ENTRY_NUM_SHIFT) == 3U);
    IopmpDestroy(&iopmp);

    PASS("IOPMP-APPN-024..028 k-entry MDCFG reduction");
}

/* ───────────────────────────────────────────────────────────────────
 * 15.8 RRID translation
 * ─────────────────────────────────────────────────────────────────── */

static void TestAppn029to032_RridTransl(void)
{
    IopmpState_t iopmp;

    /* 029/030: rrid_transl_prog=1 -> table writable; translation applied. */
    IopmpParams_t p = Base(4, 4, 1);
    p.rridTranslEn = true; p.rridTranslProg = true;
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    Enable(&iopmp); Mdcfg(&iopmp, 0, 4); Srcmd(&iopmp, 2, 0);   /* RRID2 -> MD0 */
    Na4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_R_BIT);

    /* RRID0 not associated; translate RRID0 -> RRID2. */
    IopmpWriteReg(&iopmp, REG_RRIDTRANSL_BASE + 0U * REG_RRIDTRANSL_STRIDE, 2U);
    assert(IopmpReadReg(&iopmp, REG_RRIDTRANSL_BASE) == 2U);    /* 030 writable */
    ASSERT_LEGAL(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ));  /* 029 applied */

    /* 031: rrid_transl_prog W1CS -> write 1 locks the table. */
    IopmpWriteReg(&iopmp, REG_HWCFG3, HWCFG3_RRID_TRANSL_PROG_BIT);
    assert((IopmpReadReg(&iopmp, REG_HWCFG3) & HWCFG3_RRID_TRANSL_PROG_BIT) == 0U);
    IopmpWriteReg(&iopmp, REG_RRIDTRANSL_BASE, 3U);            /* now frozen */
    assert(IopmpReadReg(&iopmp, REG_RRIDTRANSL_BASE) == 2U);
    IopmpDestroy(&iopmp);

    /* 032: rrid_transl_en=0 -> table wired 0, no tagging. */
    p = Base(4, 4, 1);                                          /* rridTranslEn=false */
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    assert(IopmpReadReg(&iopmp, REG_RRIDTRANSL_BASE) == 0U);
    IopmpDestroy(&iopmp);

    PASS("IOPMP-APPN-029..032 rrid_transl");
}

/* ───────────────────────────────────────────────────────────────────
 * Cross-combinations
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-APPN-X01 - no_w AND no_x (read-only device). */
static void TestAppnX01_NoWNoX(void)
{
    IopmpState_t iopmp;
    IopmpParams_t p = Base(1, 4, 1);
    p.noW = true; p.noX = true;
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    Enable(&iopmp); Mdcfg(&iopmp, 0, 4); Srcmd(&iopmp, 0, 0);
    Na4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_R_BIT | ENTRY_CFG_W_BIT | ENTRY_CFG_X_BIT);
    ASSERT_ETYPE(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_WRITE), IOPMP_ETYPE_NO_RULE);
    ASSERT_ETYPE(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_EXEC), IOPMP_ETYPE_NO_RULE);
    ASSERT_LEGAL(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ));
    IopmpDestroy(&iopmp);
    PASS("IOPMP-APPN-X01 no_w+no_x read-only device");
}

/* IOPMP-APPN-X02 - xinr AND no_x: no_x deny dominates. */
static void TestAppnX02_XinrNoX(void)
{
    IopmpState_t iopmp;
    IopmpParams_t p = Base(1, 4, 1);
    p.xinr = true; p.noX = true;
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    Enable(&iopmp); Mdcfg(&iopmp, 0, 4); Srcmd(&iopmp, 0, 0);
    Na4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_R_BIT);          /* read perm present */
    /* no_x denies the fetch before xinr would reinterpret it as a read. */
    ASSERT_ETYPE(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_EXEC), IOPMP_ETYPE_NO_RULE);
    IopmpDestroy(&iopmp);
    PASS("IOPMP-APPN-X02 no_x dominates xinr");
}

/* IOPMP-APPN-X03 - srcmd_fmt=2 + SPS unavailable (consistency). */
static void TestAppnX03_Fmt2NoSps(void)
{
    IopmpState_t iopmp;
    IopmpParams_t p = Base(4, 4, 2);
    p.srcmdFmt = 2U; p.spsEn = true; p.hwcfg2En = true;
    assert(IopmpInit(&iopmp, &p) != IOPMP_OK);
    PASS("IOPMP-APPN-X03 fmt2 + SPS rejected");
}

/* IOPMP-APPN-X04 - rrid_transl feeds a translated RRID to the checker. */
static void TestAppnX04_TranslGateway(void)
{
    IopmpState_t iopmp;
    IopmpParams_t p = Base(4, 4, 1);
    p.rridTranslEn = true; p.rridTranslProg = true;
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    Enable(&iopmp); Mdcfg(&iopmp, 0, 4); Srcmd(&iopmp, 1, 0);
    Na4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_R_BIT);
    IopmpWriteReg(&iopmp, REG_RRIDTRANSL_BASE + 3U * REG_RRIDTRANSL_STRIDE, 1U);  /* RRID3 -> 1 */
    /* The outer/secondary check sees the translated RRID (1), which is allowed. */
    ASSERT_LEGAL(IopmpCheckAccess(&iopmp, 3, 0x1000ULL, 4, IOPMP_TXN_READ));
    IopmpDestroy(&iopmp);
    PASS("IOPMP-APPN-X04 rrid_transl gateway");
}

/* IOPMP-APPN-X05 - k-entry + TOR: TOR derives base from previous entry. */
static void TestAppnX05_KEntryTor(void)
{
    IopmpState_t iopmp;
    IopmpParams_t p = Base(2, 4, 2);
    p.srcmdFmt = 1U; p.mdcfgFmt = 1U; p.model = IOPMP_MODEL_ISOLATION;
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    Enable(&iopmp);
    /* k=2: MD1 = entries {2,3}. Entry 2 (first of MD1) is TOR; its base comes
     * from entry 1 (in MD0's block) - the documented boundary hazard. */
    uint32_t b = IopmpReadReg(&iopmp, REG_ENTRYOFFSET);
    IopmpWriteReg(&iopmp, b + 1U * REG_ENTRY_STRIDE + REG_ENTRY_ADDR_OFF, (uint32_t)(0x1000ULL >> 2U));
    IopmpWriteReg(&iopmp, b + 2U * REG_ENTRY_STRIDE + REG_ENTRY_ADDR_OFF, (uint32_t)(0x2000ULL >> 2U));
    IopmpWriteReg(&iopmp, b + 2U * REG_ENTRY_STRIDE + REG_ENTRY_CFG_OFF,
                  (ADDR_MODE_TOR << ENTRY_CFG_A_SHIFT) | ENTRY_CFG_R_BIT);
    assert(EntryGetBase(&iopmp, 2) == 0x1000ULL);        /* from entry 1 */
    ASSERT_LEGAL(IopmpCheckAccess(&iopmp, 1, 0x1800ULL, 4, IOPMP_TXN_READ));
    IopmpDestroy(&iopmp);
    PASS("IOPMP-APPN-X05 k-entry TOR boundary");
}

/* ── Runner ──────────────────────────────────────────────────────── */

int main(void)
{
    TestAppn001to005_NoW();
    TestAppn006to008_NoX();
    TestAppn009to012_Xinr();
    TestAppn013to016_Exclusive();
    TestAppn017to022_MdIndexed();
    TestAppn023_SourceEnforcement();
    TestAppn024to028_KEntry();
    TestAppn029to032_RridTransl();

    TestAppnX01_NoWNoX();
    TestAppnX02_XinrNoX();
    TestAppnX03_Fmt2NoSps();
    TestAppnX04_TranslGateway();
    TestAppnX05_KEntryTor();

    printf("\nAll file-15 appnote-feature tests passed.\n");
    return 0;
}
