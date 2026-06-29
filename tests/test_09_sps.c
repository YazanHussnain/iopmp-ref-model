/*
 * test_09_sps.c
 *
 * Test suite for docs/testplan/09-sps.md:
 *   "Secondary Permission Setting (SPS)".
 *
 * Spec: §5.2, §5.1.8-10 (SRCMD_R/RH, SRCMD_W/WH, SRCMD_X/XH), §5.1.1 (sps_en).
 *
 * SPEC-compliant. SPS is a second permission layer: a txn is legal only if the
 * matching entry AND the per-RRID SPS register both allow it (SPS can only
 * restrict). Bit layout mirrors SRCMD_EN: MD m (0-30) at bit m+1 (bit 0
 * reserved); MD m (31-62) at bit m-31 in the *H registers.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include "iopmp.h"
#include "test_utils.h"

/* ── Shared setup helpers ────────────────────────────────────────────── */

static IopmpParams_t MakeSpsParams(uint16_t rridNum, uint16_t entryNum, uint8_t mdNum)
{
    IopmpParams_t params;
    memset(&params, 0, sizeof(params));
    params.rridNum  = rridNum;
    params.entryNum = entryNum;
    params.mdNum    = mdNum;
    params.torEn    = true;
    params.spsEn    = true;
    params.hwcfg2En = true;
    params.model    = IOPMP_MODEL_FULL;
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
static void SetNa4(IopmpState_t *iopmp, uint32_t idx, uint64_t addr, uint32_t perm)
{
    uint32_t base = IopmpReadReg(iopmp, REG_ENTRYOFFSET) + idx * REG_ENTRY_STRIDE;
    IopmpWriteReg(iopmp, base + REG_ENTRY_ADDR_OFF, (uint32_t)(addr >> 2U));
    IopmpWriteReg(iopmp, base + REG_ENTRY_CFG_OFF, (ADDR_MODE_NA4 << ENTRY_CFG_A_SHIFT) | perm);
}

/* Grant a low-MD (0-30) SPS permission: set bit (md+1) in SRCMD_{R,W,X}(rrid). */
static void SpsGrantLow(IopmpState_t *iopmp, uint16_t rrid, uint32_t fieldOff, uint8_t md)
{
    uint32_t off = REG_SRCMD_BASE + (uint32_t)rrid * REG_SRCMD_STRIDE + fieldOff;
    IopmpWriteReg(iopmp, off, IopmpReadReg(iopmp, off) | (1U << ((uint32_t)md + 1U)));
}
/* Grant a high-MD (31-62) SPS permission via the *H register: bit (md-31). */
static void SpsGrantHigh(IopmpState_t *iopmp, uint16_t rrid, uint32_t fieldOffH, uint8_t md)
{
    uint32_t off = REG_SRCMD_BASE + (uint32_t)rrid * REG_SRCMD_STRIDE + fieldOffH;
    IopmpWriteReg(iopmp, off, IopmpReadReg(iopmp, off) | (1U << ((uint32_t)md - 31U)));
}

/* ───────────────────────────────────────────────────────────────────
 * 9.1 Capability
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-SPS-001 - sps_en=1: SRCMD_R/W/X implemented (read/write storage). */
static void TestSps001_Implemented(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeSpsParams(2, 4, 4);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    assert((IopmpReadReg(&iopmp, REG_HWCFG2) & HWCFG2_SPS_EN_BIT) != 0U);

    SpsGrantLow(&iopmp, 0, REG_SRCMD_R_OFF, 0);          /* MD0 -> bit1 */
    assert(IopmpReadReg(&iopmp, REG_SRCMD_BASE + REG_SRCMD_R_OFF) == (1U << 1));

    IopmpDestroy(&iopmp);
    PASS("IOPMP-SPS-001 SPS implemented");
}

/* IOPMP-SPS-002 - sps_en=0: SRCMD_R/W/X not implemented (read 0). */
static void TestSps002_AbsentReadsZero(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeSpsParams(2, 4, 4);
    params.spsEn = false;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    IopmpWriteReg(&iopmp, REG_SRCMD_BASE + REG_SRCMD_R_OFF, 0xFFFFFFFFU);
    assert(IopmpReadReg(&iopmp, REG_SRCMD_BASE + REG_SRCMD_R_OFF) == 0U);
    assert((IopmpReadReg(&iopmp, REG_HWCFG2) & HWCFG2_SPS_EN_BIT) == 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-SPS-002 SPS absent reads 0");
}

/* IOPMP-SPS-003 - md_num<=31: SRCMD_RH/WH/XH wired 0. */
static void TestSps003_HighAbsent(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeSpsParams(2, 4, 8);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    IopmpWriteReg(&iopmp, REG_SRCMD_BASE + REG_SRCMD_RH_OFF, 0xFFFFFFFFU);
    assert(IopmpReadReg(&iopmp, REG_SRCMD_BASE + REG_SRCMD_RH_OFF) == 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-SPS-003 SPS high regs wired 0 (md_num<=31)");
}

/* ───────────────────────────────────────────────────────────────────
 * 9.2 Restrict semantics
 * ─────────────────────────────────────────────────────────────────── */

/* Build an enabled SPS instance: MD0 owns all entries, RRID0 -> MD0. */
static void WireSps(IopmpState_t *iopmp, IopmpParams_t *params, uint32_t entryCfg)
{
    assert(IopmpInit(iopmp, params) == IOPMP_OK);
    EnableIopmp(iopmp);
    SetupMdcfg(iopmp, 0, params->entryNum);
    SetupSrcmd(iopmp, 0, 0);
    SetNa4(iopmp, 0, 0x1000ULL, entryCfg);
}

/* IOPMP-SPS-004 - entry rw, SPS R=1: read legal. */
static void TestSps004_BothAllowRead(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeSpsParams(2, 4, 4);
    WireSps(&iopmp, &params, ENTRY_CFG_R_BIT | ENTRY_CFG_W_BIT);
    SpsGrantLow(&iopmp, 0, REG_SRCMD_R_OFF, 0);
    ASSERT_LEGAL(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ));
    IopmpDestroy(&iopmp);
    PASS("IOPMP-SPS-004 both layers allow read");
}

/* IOPMP-SPS-005 - entry rw, SPS W=0: write denied 0x02. */
static void TestSps005_SpsDeniesWrite(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeSpsParams(2, 4, 4);
    WireSps(&iopmp, &params, ENTRY_CFG_R_BIT | ENTRY_CFG_W_BIT);
    SpsGrantLow(&iopmp, 0, REG_SRCMD_R_OFF, 0);          /* read ok, write not granted */
    ASSERT_ETYPE(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_WRITE),
                 IOPMP_ETYPE_ILLEGAL_WRITE);
    IopmpDestroy(&iopmp);
    PASS("IOPMP-SPS-005 SPS denies write");
}

/* IOPMP-SPS-006 - entry r=0, SPS R=1: read denied (entry denies). */
static void TestSps006_EntryDenies(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeSpsParams(2, 4, 4);
    WireSps(&iopmp, &params, ENTRY_CFG_W_BIT);           /* no read on entry */
    SpsGrantLow(&iopmp, 0, REG_SRCMD_R_OFF, 0);          /* SPS cannot grant */
    ASSERT_ETYPE(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ),
                 IOPMP_ETYPE_ILLEGAL_READ);
    IopmpDestroy(&iopmp);
    PASS("IOPMP-SPS-006 entry denies, SPS cannot grant");
}

/* IOPMP-SPS-007 - entry r=1, SPS R=0: read denied (SPS denies). */
static void TestSps007_SpsDeniesRead(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeSpsParams(2, 4, 4);
    WireSps(&iopmp, &params, ENTRY_CFG_R_BIT);           /* SPS R left 0 */
    ASSERT_ETYPE(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ),
                 IOPMP_ETYPE_ILLEGAL_READ);
    IopmpDestroy(&iopmp);
    PASS("IOPMP-SPS-007 SPS denies read");
}

/* IOPMP-SPS-008 - entry x=1, SPS X=0: exec denied 0x03. */
static void TestSps008_SpsDeniesExec(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeSpsParams(2, 4, 4);
    WireSps(&iopmp, &params, ENTRY_CFG_X_BIT);           /* SPS X left 0 */
    ASSERT_ETYPE(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_EXEC),
                 IOPMP_ETYPE_ILLEGAL_EXEC);
    IopmpDestroy(&iopmp);
    PASS("IOPMP-SPS-008 SPS denies exec");
}

/* IOPMP-SPS-009 - entry x=1, SPS X=1: exec legal. */
static void TestSps009_SpsAllowsExec(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeSpsParams(2, 4, 4);
    WireSps(&iopmp, &params, ENTRY_CFG_X_BIT);
    SpsGrantLow(&iopmp, 0, REG_SRCMD_X_OFF, 0);
    ASSERT_LEGAL(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_EXEC));
    IopmpDestroy(&iopmp);
    PASS("IOPMP-SPS-009 SPS allows exec");
}

/* IOPMP-SPS-010 - all SPS bits 1: behaves like baseline. */
static void TestSps010_AllOnBaseline(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeSpsParams(2, 4, 4);
    WireSps(&iopmp, &params, ENTRY_CFG_R_BIT | ENTRY_CFG_W_BIT);
    IopmpWriteReg(&iopmp, REG_SRCMD_BASE + REG_SRCMD_R_OFF, 0xFFFFFFFEU);
    IopmpWriteReg(&iopmp, REG_SRCMD_BASE + REG_SRCMD_W_OFF, 0xFFFFFFFEU);
    ASSERT_LEGAL(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ));
    ASSERT_LEGAL(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_WRITE));
    /* No exec permission on the entry -> still denied (baseline). */
    ASSERT_ETYPE(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_EXEC),
                 IOPMP_ETYPE_ILLEGAL_EXEC);
    IopmpDestroy(&iopmp);
    PASS("IOPMP-SPS-010 all SPS bits set = baseline");
}

/* IOPMP-SPS-011 - all SPS bits 0: everything to MD m denied. */
static void TestSps011_AllOffDenies(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeSpsParams(2, 4, 4);
    WireSps(&iopmp, &params, ENTRY_CFG_R_BIT | ENTRY_CFG_W_BIT | ENTRY_CFG_X_BIT);
    /* SPS R/W/X all 0. */
    ASSERT_ILLEGAL(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ));
    ASSERT_ILLEGAL(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_WRITE));
    ASSERT_ILLEGAL(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_EXEC));
    IopmpDestroy(&iopmp);
    PASS("IOPMP-SPS-011 all SPS bits clear denies all");
}

/* ───────────────────────────────────────────────────────────────────
 * 9.3 Shared entry / multiple RRIDs
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-SPS-012 - shared rw entry: RRID A RW, RRID B RO via SPS. */
static void TestSps012_SharedEntryDifferentPerms(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeSpsParams(2, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    EnableIopmp(&iopmp);
    SetupMdcfg(&iopmp, 0, 4);
    SetupSrcmd(&iopmp, 0, 0);
    SetupSrcmd(&iopmp, 1, 0);
    SetNa4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_R_BIT | ENTRY_CFG_W_BIT);

    /* RRID0: R+W. RRID1: R only (W not granted). */
    SpsGrantLow(&iopmp, 0, REG_SRCMD_R_OFF, 0);
    SpsGrantLow(&iopmp, 0, REG_SRCMD_W_OFF, 0);
    SpsGrantLow(&iopmp, 1, REG_SRCMD_R_OFF, 0);

    ASSERT_LEGAL(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_WRITE));
    ASSERT_ETYPE(IopmpCheckAccess(&iopmp, 1, 0x1000ULL, 4, IOPMP_TXN_WRITE),
                 IOPMP_ETYPE_ILLEGAL_WRITE);
    ASSERT_LEGAL(IopmpCheckAccess(&iopmp, 1, 0x1000ULL, 4, IOPMP_TXN_READ));

    IopmpDestroy(&iopmp);
    PASS("IOPMP-SPS-012 shared entry, per-RRID SPS perms");
}

/* IOPMP-SPS-013 - RRID A R-only, RRID B X-only on same region. */
static void TestSps013_DistinctPerRrid(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeSpsParams(2, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    EnableIopmp(&iopmp);
    SetupMdcfg(&iopmp, 0, 4);
    SetupSrcmd(&iopmp, 0, 0);
    SetupSrcmd(&iopmp, 1, 0);
    SetNa4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_R_BIT | ENTRY_CFG_X_BIT);

    SpsGrantLow(&iopmp, 0, REG_SRCMD_R_OFF, 0);          /* A: read only */
    SpsGrantLow(&iopmp, 1, REG_SRCMD_X_OFF, 0);          /* B: exec only */

    ASSERT_ETYPE(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_EXEC),
                 IOPMP_ETYPE_ILLEGAL_EXEC);              /* A exec denied */
    ASSERT_ETYPE(IopmpCheckAccess(&iopmp, 1, 0x1000ULL, 4, IOPMP_TXN_READ),
                 IOPMP_ETYPE_ILLEGAL_READ);              /* B read denied */

    IopmpDestroy(&iopmp);
    PASS("IOPMP-SPS-013 distinct per-RRID SPS perms");
}

/* ───────────────────────────────────────────────────────────────────
 * 9.4 High MDs
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-SPS-014 - high MD35 governed by SRCMD_RH. */
static void TestSps014_HighMd(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeSpsParams(2, 40, 40);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    EnableIopmp(&iopmp);
    /* MD35 = [35,36). */
    SetupMdcfg(&iopmp, 34, 35);
    SetupMdcfg(&iopmp, 35, 36);
    /* RRID0 -> MD35 (SRCMD_ENH bit 4). */
    IopmpWriteReg(&iopmp, REG_SRCMD_BASE + REG_SRCMD_ENH_OFF, (1U << 4));
    SetNa4(&iopmp, 35, 0x35000ULL, ENTRY_CFG_R_BIT);

    /* SPS RH not granted -> read denied. */
    ASSERT_ETYPE(IopmpCheckAccess(&iopmp, 0, 0x35000ULL, 4, IOPMP_TXN_READ),
                 IOPMP_ETYPE_ILLEGAL_READ);
    /* Grant SRCMD_RH MD35 -> read legal. */
    SpsGrantHigh(&iopmp, 0, REG_SRCMD_RH_OFF, 35);
    ASSERT_LEGAL(IopmpCheckAccess(&iopmp, 0, 0x35000ULL, 4, IOPMP_TXN_READ));

    IopmpDestroy(&iopmp);
    PASS("IOPMP-SPS-014 high-MD SPS via SRCMD_RH");
}

/* IOPMP-SPS-015 - SRCMD_R bit0 reserved reads 0. */
static void TestSps015_ReservedBit0(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeSpsParams(2, 4, 4);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    IopmpWriteReg(&iopmp, REG_SRCMD_BASE + REG_SRCMD_R_OFF, 0xFFFFFFFFU);
    assert((IopmpReadReg(&iopmp, REG_SRCMD_BASE + REG_SRCMD_R_OFF) & 1U) == 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-SPS-015 SRCMD_R bit0 reserved");
}

/* ───────────────────────────────────────────────────────────────────
 * 9.5 Lock sharing
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-SPS-016 - SRCMD_EN(s).l=1 locks SPS registers too. */
static void TestSps016_LockLocksSps(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeSpsParams(2, 4, 4);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    IopmpWriteReg(&iopmp, REG_SRCMD_BASE + REG_SRCMD_EN_OFF, SRCMD_EN_L_BIT);
    IopmpWriteReg(&iopmp, REG_SRCMD_BASE + REG_SRCMD_R_OFF, (1U << 1));
    assert(IopmpReadReg(&iopmp, REG_SRCMD_BASE + REG_SRCMD_R_OFF) == 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-SPS-016 SRCMD_EN.l locks SPS");
}

/* IOPMP-SPS-017 - MDLCK.md[m]=1 locks SPS column for all RRIDs. */
static void TestSps017_MdlckLocksSpsColumn(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeSpsParams(2, 4, 4);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    IopmpWriteReg(&iopmp, REG_MDLCK, (1U << (1 + 1)));    /* lock MD1 (bit m+1) */
    for (uint16_t s = 0U; s < 2U; s++) {
        uint32_t off = REG_SRCMD_BASE + (uint32_t)s * REG_SRCMD_STRIDE + REG_SRCMD_W_OFF;
        IopmpWriteReg(&iopmp, off, (1U << 1) | (1U << 2));  /* MD0 + MD1 */
        uint32_t v = IopmpReadReg(&iopmp, off);
        assert((v & (1U << 1)) != 0U);                   /* MD0 writable */
        assert((v & (1U << 2)) == 0U);                   /* MD1 locked */
    }

    IopmpDestroy(&iopmp);
    PASS("IOPMP-SPS-017 MDLCK locks SPS column");
}

/* IOPMP-SPS-018 - prelocked SPS column via MDLCK preset is immutable. */
static void TestSps018_PrelockedSps(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeSpsParams(2, 4, 4);
    params.mdlckPreset = (1U << (2 + 1));                /* MD2 prelocked */
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    uint32_t off = REG_SRCMD_BASE + REG_SRCMD_R_OFF;
    IopmpWriteReg(&iopmp, off, (1U << 3));               /* try set MD2 (bit3) */
    assert((IopmpReadReg(&iopmp, off) & (1U << 3)) == 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-SPS-018 prelocked SPS column immutable");
}

/* ───────────────────────────────────────────────────────────────────
 * Cross-combinations
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-SPS-X01 - SPS applies to the matched (highest-priority) entry's MD. */
static void TestSpsX01_AppliedToMatchedMd(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeSpsParams(2, 4, 2);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    EnableIopmp(&iopmp);
    /* MD0 = {0,1}, MD1 = {2,3}. RRID0 -> both. */
    SetupMdcfg(&iopmp, 0, 2);
    SetupMdcfg(&iopmp, 1, 4);
    SetupSrcmd(&iopmp, 0, 0);
    SetupSrcmd(&iopmp, 0, 1);
    /* Entry 0 (MD0) is the highest-priority match at 0x1000. */
    SetNa4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_R_BIT);
    /* SPS grants read for MD1 but NOT MD0 -> the matched entry's MD (MD0) denies. */
    SpsGrantLow(&iopmp, 0, REG_SRCMD_R_OFF, 1);
    ASSERT_ETYPE(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ),
                 IOPMP_ETYPE_ILLEGAL_READ);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-SPS-X01 SPS keyed to matched entry's MD");
}

/* IOPMP-SPS-X02 - AMO needs write; SPS W=0 -> 0x02. */
static void TestSpsX02_AmoNeedsSpsWrite(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeSpsParams(2, 4, 4);
    WireSps(&iopmp, &params, ENTRY_CFG_R_BIT | ENTRY_CFG_W_BIT);
    SpsGrantLow(&iopmp, 0, REG_SRCMD_R_OFF, 0);          /* read granted, write not */
    ASSERT_ETYPE(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_AMO),
                 IOPMP_ETYPE_ILLEGAL_WRITE);
    IopmpDestroy(&iopmp);
    PASS("IOPMP-SPS-X02 AMO denied by SPS write");
}

/* IOPMP-SPS-X03 - SPS not supported with MD-indexed format (srcmd_fmt=2). */
static void TestSpsX03_NotSupportedFmt2(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeSpsParams(2, 4, 4);
    params.srcmdFmt = 2U;
    assert(IopmpInit(&iopmp, &params) != IOPMP_OK);     /* rejected */
    PASS("IOPMP-SPS-X03 SPS rejected with srcmd_fmt=2");
}

/* IOPMP-SPS-X04 - SPS not supported with Isolation model; cap bit 0. */
static void TestSpsX04_NotSupportedIsolation(void)
{
    IopmpState_t iopmp;
    IopmpParams_t bad = MakeSpsParams(2, 4, 4);
    bad.model    = IOPMP_MODEL_ISOLATION;
    bad.srcmdFmt = 1U;
    assert(IopmpInit(&iopmp, &bad) != IOPMP_OK);        /* sps+fmt1 rejected */

    /* Without SPS, the isolation model reports sps_en=0. */
    IopmpParams_t ok = MakeSpsParams(2, 4, 4);
    ok.spsEn = false; ok.model = IOPMP_MODEL_ISOLATION; ok.srcmdFmt = 1U;
    assert(IopmpInit(&iopmp, &ok) == IOPMP_OK);
    assert((IopmpReadReg(&iopmp, REG_HWCFG2) & HWCFG2_SPS_EN_BIT) == 0U);
    IopmpDestroy(&iopmp);
    PASS("IOPMP-SPS-X04 SPS unsupported in isolation model");
}

/* IOPMP-SPS-X05 - SPS denies write; per-entry sewe suppresses the bus error. */
static void TestSpsX05_SpsDenyWithSuppress(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeSpsParams(2, 4, 4);
    params.peesEn = true;
    WireSps(&iopmp, &params, ENTRY_CFG_R_BIT | ENTRY_CFG_W_BIT | ENTRY_CFG_SEWE_BIT);
    SpsGrantLow(&iopmp, 0, REG_SRCMD_R_OFF, 0);          /* write not granted by SPS */

    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_WRITE);
    ASSERT_ETYPE(r, IOPMP_ETYPE_ILLEGAL_WRITE);
    assert(r.suppressError);                            /* per-entry sewe */

    IopmpDestroy(&iopmp);
    PASS("IOPMP-SPS-X05 SPS deny with per-entry bus-error suppress");
}

/* ── Runner ──────────────────────────────────────────────────────── */

int main(void)
{
    TestSps001_Implemented();
    TestSps002_AbsentReadsZero();
    TestSps003_HighAbsent();

    TestSps004_BothAllowRead();
    TestSps005_SpsDeniesWrite();
    TestSps006_EntryDenies();
    TestSps007_SpsDeniesRead();
    TestSps008_SpsDeniesExec();
    TestSps009_SpsAllowsExec();
    TestSps010_AllOnBaseline();
    TestSps011_AllOffDenies();

    TestSps012_SharedEntryDifferentPerms();
    TestSps013_DistinctPerRrid();

    TestSps014_HighMd();
    TestSps015_ReservedBit0();

    TestSps016_LockLocksSps();
    TestSps017_MdlckLocksSpsColumn();
    TestSps018_PrelockedSps();

    TestSpsX01_AppliedToMatchedMd();
    TestSpsX02_AmoNeedsSpsWrite();
    TestSpsX03_NotSupportedFmt2();
    TestSpsX04_NotSupportedIsolation();
    TestSpsX05_SpsDenyWithSuppress();

    printf("\nAll file-09 SPS tests passed.\n");
    return 0;
}
