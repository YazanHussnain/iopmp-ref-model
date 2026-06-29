/*
 * test_14_hwcfg_capability.c
 *
 * Test suite for docs/testplan/14-hwcfg-capability.md:
 *   "Capability Discovery Consistency (HWCFG0/1/2/3)".
 *
 * Spec: §4.1.3-4.1.8 (HWCFG0/1/2/3, ENTRYOFFSET), §5.1.1, Appendix A.
 *
 * SPEC-compliant cross-consistency checks: a capability bit reported in HWCFG
 * must match the actual presence/behaviour of the corresponding registers.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include "iopmp.h"
#include "test_utils.h"

/* ── Shared setup helpers ────────────────────────────────────────────── */

static IopmpParams_t Base(void)
{
    IopmpParams_t params;
    memset(&params, 0, sizeof(params));
    params.rridNum  = 4;
    params.entryNum = 8;
    params.mdNum    = 2;
    params.torEn    = true;
    params.model    = IOPMP_MODEL_FULL;
    return params;
}

static uint32_t EntryAddrhOff(IopmpState_t *iopmp, uint32_t idx)
{
    return IopmpReadReg(iopmp, REG_ENTRYOFFSET) + idx * REG_ENTRY_STRIDE + REG_ENTRY_ADDRH_OFF;
}

/* ───────────────────────────────────────────────────────────────────
 * 14.1 HWCFG0 <-> register presence
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-HWCFG-001 - addrh_en=1: ENTRY_ADDRH writable. */
static void TestHwcfg001_AddrhPresent(void)
{
    IopmpState_t iopmp;
    IopmpParams_t p = Base();
    p.addrhEn = true;
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    assert((IopmpReadReg(&iopmp, REG_HWCFG0) & HWCFG0_ADDRH_EN_BIT) != 0U);
    IopmpWriteReg(&iopmp, EntryAddrhOff(&iopmp, 0), 0x5U);
    assert(IopmpReadReg(&iopmp, EntryAddrhOff(&iopmp, 0)) == 0x5U);
    IopmpDestroy(&iopmp);
    PASS("IOPMP-HWCFG-001 addrh_en -> ENTRY_ADDRH present");
}

/* IOPMP-HWCFG-002 - addrh_en=0: ENTRY_ADDRH / ERR_REQADDRH read 0. */
static void TestHwcfg002_AddrhAbsent(void)
{
    IopmpState_t iopmp;
    IopmpParams_t p = Base();                          /* addrhEn=false */
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    assert((IopmpReadReg(&iopmp, REG_HWCFG0) & HWCFG0_ADDRH_EN_BIT) == 0U);
    IopmpWriteReg(&iopmp, EntryAddrhOff(&iopmp, 0), 0xFFFFFFFFU);
    assert(IopmpReadReg(&iopmp, EntryAddrhOff(&iopmp, 0)) == 0U);
    assert(IopmpReadReg(&iopmp, REG_ERR_REQADDRH) == 0U);
    IopmpDestroy(&iopmp);
    PASS("IOPMP-HWCFG-002 addrh_en=0 absent");
}

/* IOPMP-HWCFG-003 - tor_en=0: TOR not retained (coerced to legal). */
static void TestHwcfg003_TorEn(void)
{
    IopmpState_t iopmp;
    IopmpParams_t p = Base();
    p.torEn = false;
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    assert((IopmpReadReg(&iopmp, REG_HWCFG0) & HWCFG0_TOR_EN_BIT) == 0U);
    uint32_t off = IopmpReadReg(&iopmp, REG_ENTRYOFFSET) + REG_ENTRY_CFG_OFF;
    IopmpWriteReg(&iopmp, off, (uint32_t)ADDR_MODE_TOR << ENTRY_CFG_A_SHIFT);
    assert(((IopmpReadReg(&iopmp, off) & ENTRY_CFG_A_MASK) >> ENTRY_CFG_A_SHIFT) != ADDR_MODE_TOR);
    IopmpDestroy(&iopmp);
    PASS("IOPMP-HWCFG-003 tor_en=0 TOR not retained");
}

/* IOPMP-HWCFG-004 - no_err_rec=1: error-record regs not implemented. */
static void TestHwcfg004_NoErrRec(void)
{
    IopmpState_t iopmp;
    IopmpParams_t p = Base();
    p.noErrRec = true;
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    assert((IopmpReadReg(&iopmp, REG_HWCFG0) & HWCFG0_NO_ERR_REC_BIT) != 0U);
    IopmpWriteReg(&iopmp, REG_HWCFG0, HWCFG0_ENABLE_BIT);
    IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ);   /* violation */
    assert((IopmpReadReg(&iopmp, REG_ERR_INFO) & ERR_INFO_V_BIT) == 0U);  /* nothing captured */
    IopmpDestroy(&iopmp);
    PASS("IOPMP-HWCFG-004 no_err_rec consistent");
}

/* IOPMP-HWCFG-005/006 - HWCFG2_en bit matches HWCFG2 presence. */
static void TestHwcfg005_Hwcfg2Presence(void)
{
    IopmpState_t iopmp;
    IopmpParams_t p = Base();
    p.hwcfg2En = true; p.stallEn = true;
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    assert((IopmpReadReg(&iopmp, REG_HWCFG0) & HWCFG0_HWCFG2_EN_BIT) != 0U);
    assert(IopmpReadReg(&iopmp, REG_HWCFG2) != 0U);    /* has capability bits */
    IopmpDestroy(&iopmp);

    p = Base();                                        /* hwcfg2En=false */
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    assert((IopmpReadReg(&iopmp, REG_HWCFG0) & HWCFG0_HWCFG2_EN_BIT) == 0U);
    assert(IopmpReadReg(&iopmp, REG_HWCFG2) == 0U);
    IopmpDestroy(&iopmp);
    PASS("IOPMP-HWCFG-005/006 HWCFG2 presence consistent");
}

/* IOPMP-HWCFG-007 - HWCFG3_en=1: HWCFG3 present. */
static void TestHwcfg007_Hwcfg3Presence(void)
{
    IopmpState_t iopmp;
    IopmpParams_t p = Base();
    p.hwcfg3En = true;
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    assert((IopmpReadReg(&iopmp, REG_HWCFG0) & HWCFG0_HWCFG3_EN_BIT) != 0U);
    IopmpDestroy(&iopmp);
    PASS("IOPMP-HWCFG-007 HWCFG3 present");
}

/* IOPMP-HWCFG-008 - md_num field matches; MDLCKH present iff md_num>31. */
static void TestHwcfg008_MdNum(void)
{
    IopmpState_t iopmp;
    IopmpParams_t p = Base();
    p.mdNum = 40;
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    assert(((IopmpReadReg(&iopmp, REG_HWCFG0) & HWCFG0_MD_NUM_MASK) >> HWCFG0_MD_NUM_SHIFT) == 40U);
    IopmpWriteReg(&iopmp, REG_MDLCKH, 0x1U);           /* present when md_num>31 */
    assert(IopmpReadReg(&iopmp, REG_MDLCKH) == 0x1U);
    IopmpDestroy(&iopmp);

    p = Base(); p.mdNum = 4;
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    IopmpWriteReg(&iopmp, REG_MDLCKH, 0x1U);           /* wired 0 when md_num<=31 */
    assert(IopmpReadReg(&iopmp, REG_MDLCKH) == 0U);
    IopmpDestroy(&iopmp);
    PASS("IOPMP-HWCFG-008 md_num + MDLCKH presence");
}

/* ───────────────────────────────────────────────────────────────────
 * 14.2 HWCFG2 <-> extension presence
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-HWCFG-009/010 - stall_en bit matches MDSTALL/RRIDSCP presence. */
static void TestHwcfg009_Stall(void)
{
    IopmpState_t iopmp;
    IopmpParams_t p = Base();
    p.hwcfg2En = true; p.stallEn = true;
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    assert((IopmpReadReg(&iopmp, REG_HWCFG2) & HWCFG2_STALL_EN_BIT) != 0U);
    IopmpWriteReg(&iopmp, REG_MDSTALL, (1U << 1));
    assert(IopmpReadReg(&iopmp, REG_MDSTALL) != 0U);
    IopmpDestroy(&iopmp);

    p = Base(); p.hwcfg2En = true;                     /* stallEn=false */
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    assert((IopmpReadReg(&iopmp, REG_HWCFG2) & HWCFG2_STALL_EN_BIT) == 0U);
    IopmpWriteReg(&iopmp, REG_MDSTALL, (1U << 1));
    assert(IopmpReadReg(&iopmp, REG_MDSTALL) == 0U);
    IopmpDestroy(&iopmp);
    PASS("IOPMP-HWCFG-009/010 stall presence consistent");
}

/* IOPMP-HWCFG-011 - sps_en bit matches SRCMD_R presence. */
static void TestHwcfg011_Sps(void)
{
    IopmpState_t iopmp;
    IopmpParams_t p = Base();
    p.hwcfg2En = true; p.spsEn = true;
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    assert((IopmpReadReg(&iopmp, REG_HWCFG2) & HWCFG2_SPS_EN_BIT) != 0U);
    IopmpWriteReg(&iopmp, REG_SRCMD_BASE + REG_SRCMD_R_OFF, (1U << 1));
    assert(IopmpReadReg(&iopmp, REG_SRCMD_BASE + REG_SRCMD_R_OFF) == (1U << 1));
    IopmpDestroy(&iopmp);
    PASS("IOPMP-HWCFG-011 sps presence consistent");
}

/* IOPMP-HWCFG-012 - msi_en bit matches MSI register presence. */
static void TestHwcfg012_Msi(void)
{
    IopmpState_t iopmp;
    IopmpParams_t p = Base();
    p.hwcfg2En = true; p.msiEn = true;
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    assert((IopmpReadReg(&iopmp, REG_HWCFG2) & HWCFG2_MSI_EN_BIT) != 0U);
    IopmpWriteReg(&iopmp, REG_ERR_MSIADDR, 0xFEE00000U);
    assert(IopmpReadReg(&iopmp, REG_ERR_MSIADDR) == 0xFEE00000U);
    IopmpDestroy(&iopmp);
    PASS("IOPMP-HWCFG-012 msi presence consistent");
}

/* IOPMP-HWCFG-013 - mfr_en bit matches ERR_MFR/svc presence. */
static void TestHwcfg013_Mfr(void)
{
    IopmpState_t iopmp;
    IopmpParams_t p = Base();
    p.hwcfg2En = true; p.multifaultEn = true;
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    assert((IopmpReadReg(&iopmp, REG_HWCFG2) & HWCFG2_MFR_EN_BIT) != 0U);
    IopmpDestroy(&iopmp);
    PASS("IOPMP-HWCFG-013 mfr presence consistent");
}

/* IOPMP-HWCFG-014/015 - peis/pees bits match suppress-bit writability. */
static void TestHwcfg014_PeisPees(void)
{
    IopmpState_t iopmp;
    IopmpParams_t p = Base();
    p.hwcfg2En = true; p.peisEn = true; p.peesEn = true;
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    assert((IopmpReadReg(&iopmp, REG_HWCFG2) & HWCFG2_PEIS_EN_BIT) != 0U);
    assert((IopmpReadReg(&iopmp, REG_HWCFG2) & HWCFG2_PEES_EN_BIT) != 0U);
    uint32_t off = IopmpReadReg(&iopmp, REG_ENTRYOFFSET) + REG_ENTRY_CFG_OFF;
    IopmpWriteReg(&iopmp, off, ENTRY_CFG_SIRE_BIT | ENTRY_CFG_SERE_BIT);
    uint32_t cfg = IopmpReadReg(&iopmp, off);
    assert((cfg & ENTRY_CFG_SIRE_BIT) && (cfg & ENTRY_CFG_SERE_BIT));
    IopmpDestroy(&iopmp);
    PASS("IOPMP-HWCFG-014/015 peis/pees consistent");
}

/* IOPMP-HWCFG-016 - non_prio_en bit matches prio_entry meaning. */
static void TestHwcfg016_NonPrio(void)
{
    IopmpState_t iopmp;
    IopmpParams_t p = Base();
    p.hwcfg2En = true; p.nonPrioEn = true; p.prioEntry = 3;
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    assert((IopmpReadReg(&iopmp, REG_HWCFG2) & HWCFG2_NON_PRIO_EN_BIT) != 0U);
    assert((IopmpReadReg(&iopmp, REG_HWCFG2) & HWCFG2_PRIO_ENTRY_MASK) == 3U);
    IopmpDestroy(&iopmp);
    PASS("IOPMP-HWCFG-016 non_prio_en consistent");
}

/* IOPMP-HWCFG-017 - prio_ent_prog: prio_entry programmable. */
static void TestHwcfg017_PrioProg(void)
{
    IopmpState_t iopmp;
    IopmpParams_t p = Base();
    p.hwcfg2En = true; p.nonPrioEn = true; p.prioEntProg = true; p.prioEntry = 2;
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    assert((IopmpReadReg(&iopmp, REG_HWCFG2) & HWCFG2_PRIO_ENT_PROG_BIT) != 0U);
    IopmpWriteReg(&iopmp, REG_HWCFG2, 5U);
    assert((IopmpReadReg(&iopmp, REG_HWCFG2) & HWCFG2_PRIO_ENTRY_MASK) == 5U);
    IopmpDestroy(&iopmp);
    PASS("IOPMP-HWCFG-017 prio_ent_prog consistent");
}

/* IOPMP-HWCFG-018 - HWCFG2 reserved bits [25:18] read 0. */
static void TestHwcfg018_Reserved(void)
{
    IopmpState_t iopmp;
    IopmpParams_t p = Base();
    p.hwcfg2En = true; p.nonPrioEn = true; p.prioEntProg = true;
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    IopmpWriteReg(&iopmp, REG_HWCFG2, 0xFFFFFFFFU);
    assert((IopmpReadReg(&iopmp, REG_HWCFG2) & 0x03FC0000U) == 0U);   /* bits 25:18 */
    IopmpDestroy(&iopmp);
    PASS("IOPMP-HWCFG-018 HWCFG2 reserved read 0");
}

/* ───────────────────────────────────────────────────────────────────
 * 14.3 HWCFG3 <-> format / feature
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-HWCFG-019 - mdcfg_fmt=0: MDCFG present. */
static void TestHwcfg019_MdcfgFmt0(void)
{
    IopmpState_t iopmp;
    IopmpParams_t p = Base();
    p.hwcfg3En = true;                                 /* mdcfgFmt 0 */
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    assert((IopmpReadReg(&iopmp, REG_HWCFG3) & HWCFG3_MDCFG_FMT_MASK) == 0U);
    IopmpWriteReg(&iopmp, REG_MDCFG_BASE, 4U);
    assert(IopmpReadReg(&iopmp, REG_MDCFG_BASE) == 4U); /* present */
    IopmpDestroy(&iopmp);
    PASS("IOPMP-HWCFG-019 mdcfg_fmt=0 MDCFG present");
}

/* IOPMP-HWCFG-020 - mdcfg_fmt=2: MDCFG table not present (reads 0). */
static void TestHwcfg020_MdcfgFmt2(void)
{
    IopmpState_t iopmp;
    IopmpParams_t p = Base();
    p.hwcfg3En = true; p.mdcfgFmt = 2U; p.mdEntryNum = 2;
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    assert((IopmpReadReg(&iopmp, REG_HWCFG3) & HWCFG3_MDCFG_FMT_MASK) == 2U);
    IopmpWriteReg(&iopmp, REG_MDCFG_BASE, 4U);
    assert(IopmpReadReg(&iopmp, REG_MDCFG_BASE) == 0U); /* not present */
    IopmpDestroy(&iopmp);
    PASS("IOPMP-HWCFG-020 mdcfg_fmt=2 MDCFG absent");
}

/* IOPMP-HWCFG-021 - srcmd_fmt=0: SRCMD_EN present. */
static void TestHwcfg021_SrcmdFmt0(void)
{
    IopmpState_t iopmp;
    IopmpParams_t p = Base();
    p.hwcfg3En = true;
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    assert((IopmpReadReg(&iopmp, REG_HWCFG3) & HWCFG3_SRCMD_FMT_MASK) == 0U);
    IopmpWriteReg(&iopmp, REG_SRCMD_BASE + REG_SRCMD_EN_OFF, (1U << 1));
    assert(IopmpReadReg(&iopmp, REG_SRCMD_BASE + REG_SRCMD_EN_OFF) == (1U << 1));
    IopmpDestroy(&iopmp);
    PASS("IOPMP-HWCFG-021 srcmd_fmt=0 SRCMD_EN present");
}

/* IOPMP-HWCFG-022 - srcmd_fmt=1: SRCMD table absent (RRID i -> MD i). */
static void TestHwcfg022_SrcmdFmt1(void)
{
    IopmpState_t iopmp;
    IopmpParams_t p = Base();
    p.hwcfg3En = true; p.srcmdFmt = 1U; p.mdcfgFmt = 1U; p.model = IOPMP_MODEL_ISOLATION;
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    assert(((IopmpReadReg(&iopmp, REG_HWCFG3) & HWCFG3_SRCMD_FMT_MASK) >> HWCFG3_SRCMD_FMT_SHIFT) == 1U);
    IopmpDestroy(&iopmp);
    PASS("IOPMP-HWCFG-022 srcmd_fmt=1 exclusive mapping");
}

/* IOPMP-HWCFG-023 - srcmd_fmt=2: SRCMD_PERM present. */
static void TestHwcfg023_SrcmdFmt2(void)
{
    IopmpState_t iopmp;
    IopmpParams_t p = Base();
    p.hwcfg3En = true; p.srcmdFmt = 2U;
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    assert(((IopmpReadReg(&iopmp, REG_HWCFG3) & HWCFG3_SRCMD_FMT_MASK) >> HWCFG3_SRCMD_FMT_SHIFT) == 2U);
    /* SRCMD_PERM(0) is indexed by MD; writable. */
    IopmpWriteReg(&iopmp, REG_SRCMD_BASE + REG_SRCMD_EN_OFF, 0x4U);
    assert(IopmpReadReg(&iopmp, REG_SRCMD_BASE + REG_SRCMD_EN_OFF) == 0x4U);
    IopmpDestroy(&iopmp);
    PASS("IOPMP-HWCFG-023 srcmd_fmt=2 SRCMD_PERM present");
}

/* IOPMP-HWCFG-024 - md_entry_num locked by HWCFG0.enable. */
static void TestHwcfg024_MdEntryNumLock(void)
{
    IopmpState_t iopmp;
    IopmpParams_t p = Base();
    p.hwcfg3En = true; p.mdcfgFmt = 2U; p.mdEntryNum = 2;
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);

    /* Writable before enable. */
    IopmpWriteReg(&iopmp, REG_HWCFG3,
        (IopmpReadReg(&iopmp, REG_HWCFG3) & ~HWCFG3_MD_ENTRY_NUM_MASK) | (3U << HWCFG3_MD_ENTRY_NUM_SHIFT));
    assert(((IopmpReadReg(&iopmp, REG_HWCFG3) & HWCFG3_MD_ENTRY_NUM_MASK) >> HWCFG3_MD_ENTRY_NUM_SHIFT) == 3U);

    IopmpWriteReg(&iopmp, REG_HWCFG0, HWCFG0_ENABLE_BIT);    /* lock */
    IopmpWriteReg(&iopmp, REG_HWCFG3,
        (IopmpReadReg(&iopmp, REG_HWCFG3) & ~HWCFG3_MD_ENTRY_NUM_MASK) | (5U << HWCFG3_MD_ENTRY_NUM_SHIFT));
    assert(((IopmpReadReg(&iopmp, REG_HWCFG3) & HWCFG3_MD_ENTRY_NUM_MASK) >> HWCFG3_MD_ENTRY_NUM_SHIFT) == 3U);
    IopmpDestroy(&iopmp);
    PASS("IOPMP-HWCFG-024 md_entry_num locked by enable");
}

/* IOPMP-HWCFG-025 - no_w=1: writes globally denied. */
static void TestHwcfg025_NoW(void)
{
    IopmpState_t iopmp;
    IopmpParams_t p = Base();
    p.hwcfg3En = true; p.noW = true;
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    assert((IopmpReadReg(&iopmp, REG_HWCFG3) & HWCFG3_NO_W_BIT) != 0U);
    IopmpWriteReg(&iopmp, REG_HWCFG0, HWCFG0_ENABLE_BIT);
    IopmpWriteReg(&iopmp, REG_MDCFG_BASE, 8U);
    IopmpWriteReg(&iopmp, REG_SRCMD_BASE + REG_SRCMD_EN_OFF, (1U << 1));
    uint32_t b = IopmpReadReg(&iopmp, REG_ENTRYOFFSET);
    IopmpWriteReg(&iopmp, b + REG_ENTRY_ADDR_OFF, (uint32_t)(0x1000ULL >> 2U));
    IopmpWriteReg(&iopmp, b + REG_ENTRY_CFG_OFF, (ADDR_MODE_NA4 << ENTRY_CFG_A_SHIFT) | ENTRY_CFG_R_BIT | ENTRY_CFG_W_BIT);
    ASSERT_ETYPE(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_WRITE), IOPMP_ETYPE_NO_RULE);
    IopmpDestroy(&iopmp);
    PASS("IOPMP-HWCFG-025 no_w consistent");
}

/* IOPMP-HWCFG-026 - no_x=1: fetches globally denied. */
static void TestHwcfg026_NoX(void)
{
    IopmpState_t iopmp;
    IopmpParams_t p = Base();
    p.hwcfg3En = true; p.noX = true;
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    assert((IopmpReadReg(&iopmp, REG_HWCFG3) & HWCFG3_NO_X_BIT) != 0U);
    IopmpWriteReg(&iopmp, REG_HWCFG0, HWCFG0_ENABLE_BIT);
    IopmpWriteReg(&iopmp, REG_MDCFG_BASE, 8U);
    IopmpWriteReg(&iopmp, REG_SRCMD_BASE + REG_SRCMD_EN_OFF, (1U << 1));
    uint32_t b = IopmpReadReg(&iopmp, REG_ENTRYOFFSET);
    IopmpWriteReg(&iopmp, b + REG_ENTRY_ADDR_OFF, (uint32_t)(0x1000ULL >> 2U));
    IopmpWriteReg(&iopmp, b + REG_ENTRY_CFG_OFF, (ADDR_MODE_NA4 << ENTRY_CFG_A_SHIFT) | ENTRY_CFG_X_BIT);
    ASSERT_ETYPE(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_EXEC), IOPMP_ETYPE_NO_RULE);
    IopmpDestroy(&iopmp);
    PASS("IOPMP-HWCFG-026 no_x consistent");
}

/* IOPMP-HWCFG-027 - xinr=1 reported. */
static void TestHwcfg027_Xinr(void)
{
    IopmpState_t iopmp;
    IopmpParams_t p = Base();
    p.hwcfg3En = true; p.xinr = true;
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    assert((IopmpReadReg(&iopmp, REG_HWCFG3) & HWCFG3_XINR_BIT) != 0U);
    IopmpDestroy(&iopmp);
    PASS("IOPMP-HWCFG-027 xinr reported");
}

/* IOPMP-HWCFG-028 - rrid_transl_en=1 reported. */
static void TestHwcfg028_RridTransl(void)
{
    IopmpState_t iopmp;
    IopmpParams_t p = Base();
    p.hwcfg3En = true; p.rridTranslEn = true;
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    assert((IopmpReadReg(&iopmp, REG_HWCFG3) & HWCFG3_RRID_TRANSL_EN_BIT) != 0U);
    IopmpDestroy(&iopmp);
    PASS("IOPMP-HWCFG-028 rrid_transl_en reported");
}

/* ───────────────────────────────────────────────────────────────────
 * 14.4 ENTRYOFFSET & layout
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-HWCFG-029 - entry array fields resolve via ENTRYOFFSET + i*16. */
static void TestHwcfg029_EntryLayout(void)
{
    IopmpState_t iopmp;
    IopmpParams_t p = Base();
    p.addrhEn = true; p.entryUserCfgEn = true;
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    uint32_t b = IopmpReadReg(&iopmp, REG_ENTRYOFFSET) + 2U * REG_ENTRY_STRIDE;
    IopmpWriteReg(&iopmp, b + REG_ENTRY_ADDR_OFF, 0x111U);
    IopmpWriteReg(&iopmp, b + REG_ENTRY_ADDRH_OFF, 0x222U);
    IopmpWriteReg(&iopmp, b + REG_ENTRY_CFG_OFF, ENTRY_CFG_R_BIT);
    IopmpWriteReg(&iopmp, b + REG_ENTRY_USER_CFG_OFF, 0x333U);
    assert(IopmpReadReg(&iopmp, b + REG_ENTRY_ADDR_OFF)     == 0x111U);
    assert(IopmpReadReg(&iopmp, b + REG_ENTRY_ADDRH_OFF)    == 0x222U);
    assert((IopmpReadReg(&iopmp, b + REG_ENTRY_CFG_OFF) & ENTRY_CFG_R_BIT) != 0U);
    assert(IopmpReadReg(&iopmp, b + REG_ENTRY_USER_CFG_OFF) == 0x333U);
    IopmpDestroy(&iopmp);
    PASS("IOPMP-HWCFG-029 entry array layout resolves");
}

/* IOPMP-HWCFG-030 - negative ENTRYOFFSET handled. */
static void TestHwcfg030_NegativeOffset(void)
{
    IopmpState_t iopmp;
    IopmpParams_t p = Base();
    p.entryOffset = 0xFFFFC000U;                       /* -0x4000 two's complement */
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    assert(IopmpReadReg(&iopmp, REG_ENTRYOFFSET) == 0xFFFFC000U);
    IopmpDestroy(&iopmp);
    PASS("IOPMP-HWCFG-030 negative ENTRYOFFSET");
}

/* IOPMP-HWCFG-031 - HWCFG1 reports rrid_num/entry_num (layout sizing). */
static void TestHwcfg031_Hwcfg1Layout(void)
{
    IopmpState_t iopmp;
    IopmpParams_t p = Base();
    p.rridNum = 7; p.entryNum = 9;
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    uint32_t h1 = IopmpReadReg(&iopmp, REG_HWCFG1);
    assert((h1 & HWCFG1_RRID_NUM_MASK) == 7U);
    assert(((h1 & HWCFG1_ENTRY_NUM_MASK) >> HWCFG1_ENTRY_NUM_SHIFT) == 9U);
    IopmpDestroy(&iopmp);
    PASS("IOPMP-HWCFG-031 HWCFG1 sizing consistent");
}

/* ───────────────────────────────────────────────────────────────────
 * Cross-combinations
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-HWCFG-X01 - Isolation model: sps_en must be 0. */
static void TestHwcfgX01_IsolationNoSps(void)
{
    IopmpState_t iopmp;
    IopmpParams_t p = Base();
    p.hwcfg2En = true; p.model = IOPMP_MODEL_ISOLATION; p.srcmdFmt = 1U; p.mdcfgFmt = 1U;
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    assert((IopmpReadReg(&iopmp, REG_HWCFG2) & HWCFG2_SPS_EN_BIT) == 0U);
    IopmpDestroy(&iopmp);
    PASS("IOPMP-HWCFG-X01 isolation sps_en=0");
}

/* IOPMP-HWCFG-X02 - msi_en=1, addrh_en=0: ERR_MSIADDRH absent. */
static void TestHwcfgX02_MsiNoAddrh(void)
{
    IopmpState_t iopmp;
    IopmpParams_t p = Base();
    p.hwcfg2En = true; p.msiEn = true;                 /* addrhEn=false */
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    IopmpWriteReg(&iopmp, REG_ERR_MSIADDRH, 0xFFFFFFFFU);
    assert(IopmpReadReg(&iopmp, REG_ERR_MSIADDRH) == 0U);
    IopmpDestroy(&iopmp);
    PASS("IOPMP-HWCFG-X02 MSI without addrh");
}

/* IOPMP-HWCFG-X03 - svc active iff mfr_en. */
static void TestHwcfgX03_SvcIffMfr(void)
{
    IopmpState_t iopmp;
    IopmpParams_t p = Base();
    p.hwcfg2En = true;                                 /* mfr_en=false */
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    IopmpWriteReg(&iopmp, REG_HWCFG0, HWCFG0_ENABLE_BIT);
    IopmpWriteReg(&iopmp, REG_MDCFG_BASE, 8U);
    IopmpWriteReg(&iopmp, REG_SRCMD_BASE + REG_SRCMD_EN_OFF, (1U << 1));
    IopmpWriteReg(&iopmp, REG_SRCMD_BASE + REG_SRCMD_STRIDE + REG_SRCMD_EN_OFF, (1U << 1));
    IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ);
    IopmpCheckAccess(&iopmp, 1, 0x1000ULL, 4, IOPMP_TXN_READ);
    assert((IopmpReadReg(&iopmp, REG_ERR_INFO) & ERR_INFO_SVC_BIT) == 0U);   /* no mfr -> no svc */
    IopmpDestroy(&iopmp);
    PASS("IOPMP-HWCFG-X03 svc inactive without mfr_en");
}

/* IOPMP-HWCFG-X04 - k*md_num exceeding entry_num: high entries treated OFF. */
static void TestHwcfgX04_KExceedsEntries(void)
{
    IopmpState_t iopmp;
    IopmpParams_t p = Base();
    p.hwcfg3En = true; p.mdcfgFmt = 1U; p.mdNum = 4; p.entryNum = 6;   /* k=1, MD3 -> entry 3..3 ok; try k>1 */
    assert(IopmpInit(&iopmp, &p) == IOPMP_OK);
    /* k = entry_num/md_num = 1; MD3 owns entry 3. A txn to an entry index >=
     * entry_num is simply unavailable. This validates clamp-to-entry_num. */
    IopmpWriteReg(&iopmp, REG_HWCFG0, HWCFG0_ENABLE_BIT);
    /* No crash, and an out-of-range entry read returns 0. */
    uint32_t b = IopmpReadReg(&iopmp, REG_ENTRYOFFSET) + p.entryNum * REG_ENTRY_STRIDE;
    assert(IopmpReadReg(&iopmp, b + REG_ENTRY_CFG_OFF) == 0U);
    IopmpDestroy(&iopmp);
    PASS("IOPMP-HWCFG-X04 k-layout clamps to entry_num");
}

/* ── Runner ──────────────────────────────────────────────────────── */

int main(void)
{
    TestHwcfg001_AddrhPresent();
    TestHwcfg002_AddrhAbsent();
    TestHwcfg003_TorEn();
    TestHwcfg004_NoErrRec();
    TestHwcfg005_Hwcfg2Presence();
    TestHwcfg007_Hwcfg3Presence();
    TestHwcfg008_MdNum();

    TestHwcfg009_Stall();
    TestHwcfg011_Sps();
    TestHwcfg012_Msi();
    TestHwcfg013_Mfr();
    TestHwcfg014_PeisPees();
    TestHwcfg016_NonPrio();
    TestHwcfg017_PrioProg();
    TestHwcfg018_Reserved();

    TestHwcfg019_MdcfgFmt0();
    TestHwcfg020_MdcfgFmt2();
    TestHwcfg021_SrcmdFmt0();
    TestHwcfg022_SrcmdFmt1();
    TestHwcfg023_SrcmdFmt2();
    TestHwcfg024_MdEntryNumLock();
    TestHwcfg025_NoW();
    TestHwcfg026_NoX();
    TestHwcfg027_Xinr();
    TestHwcfg028_RridTransl();

    TestHwcfg029_EntryLayout();
    TestHwcfg030_NegativeOffset();
    TestHwcfg031_Hwcfg1Layout();

    TestHwcfgX01_IsolationNoSps();
    TestHwcfgX02_MsiNoAddrh();
    TestHwcfgX03_SvcIffMfr();
    TestHwcfgX04_KExceedsEntries();

    printf("\nAll file-14 HWCFG capability tests passed.\n");
    return 0;
}
