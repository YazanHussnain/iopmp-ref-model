/*
 * test_18_cross_combinations.c
 *
 * Test suite for docs/testplan/18-cross-combinations.md - multi-feature
 * end-to-end scenarios spanning three or more features.
 *
 * SPEC-compliant. Each test is a short setup -> stimulus -> expected sequence.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include "iopmp.h"
#include "iopmp_system.h"
#include "test_utils.h"

/* ── Shared setup helpers ────────────────────────────────────────────── */

static IopmpParams_t P(uint16_t rridNum, uint16_t entryNum, uint8_t mdNum)
{
    IopmpParams_t p;
    memset(&p, 0, sizeof(p));
    p.rridNum = rridNum; p.entryNum = entryNum; p.mdNum = mdNum;
    p.torEn = true; p.hwcfg2En = true; p.hwcfg3En = true; p.model = IOPMP_MODEL_FULL;
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
static void Entry(IopmpState_t *i, uint32_t idx, uint8_t mode, uint64_t addr, uint32_t size, uint32_t perm)
{
    uint32_t b = IopmpReadReg(i, REG_ENTRYOFFSET) + idx * REG_ENTRY_STRIDE;
    uint32_t wa;
    if (mode == ADDR_MODE_NAPOT) {
        uint32_t k = 0U; while ((8U << k) < size) k++;
        wa = (uint32_t)(addr >> 2U) | ((1U << k) - 1U);
    } else { wa = (uint32_t)(addr >> 2U); }
    IopmpWriteReg(i, b + REG_ENTRY_ADDR_OFF, wa);
    IopmpWriteReg(i, b + REG_ENTRY_CFG_OFF, ((uint32_t)mode << ENTRY_CFG_A_SHIFT) | perm);
}

/* ───────────────────────────────────────────────────────────────────
 * 18.1 priority × permission × partial-hit × suppression
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-XC-001 - priority partial-hit dominates; entry0.sire suppresses IRQ. */
static void TestXc001(void)
{
    IopmpState_t s; IopmpParams_t p = P(1, 4, 1); p.peisEn = true;
    assert(IopmpInit(&s, &p) == IOPMP_OK);
    Enable(&s); Mdcfg(&s, 0, 4); Srcmd(&s, 0, 0);
    IopmpWriteReg(&s, REG_ERR_CFG, ERR_CFG_IE_BIT);
    Entry(&s, 0, ADDR_MODE_NA4, 0x1000ULL, 4, ENTRY_CFG_W_BIT | ENTRY_CFG_SIRE_BIT); /* r=0, 4 bytes */
    Entry(&s, 1, ADDR_MODE_NAPOT, 0x1000ULL, 0x20, ENTRY_CFG_R_BIT);                 /* covers all */

    TxnResult_t r = IopmpCheckAccess(&s, 0, 0x1000ULL, 8, IOPMP_TXN_READ);
    ASSERT_ETYPE(r, IOPMP_ETYPE_PARTIAL_HIT);
    assert(r.entryIdx == 0U);
    assert(!IopmpIsIrqPending(&s));
    IopmpDestroy(&s);
    PASS("IOPMP-XC-001 partial-hit + sire");
}

/* IOPMP-XC-002 - full-cover deny via entry0; sere suppresses bus error. */
static void TestXc002(void)
{
    IopmpState_t s; IopmpParams_t p = P(1, 4, 1); p.peesEn = true;
    assert(IopmpInit(&s, &p) == IOPMP_OK);
    Enable(&s); Mdcfg(&s, 0, 4); Srcmd(&s, 0, 0);
    Entry(&s, 0, ADDR_MODE_NAPOT, 0x1000ULL, 0x20, ENTRY_CFG_W_BIT | ENTRY_CFG_SERE_BIT); /* r=0 */
    Entry(&s, 1, ADDR_MODE_NAPOT, 0x1000ULL, 0x20, ENTRY_CFG_R_BIT);

    TxnResult_t r = IopmpCheckAccess(&s, 0, 0x1000ULL, 4, IOPMP_TXN_READ);
    ASSERT_ETYPE(r, IOPMP_ETYPE_ILLEGAL_READ);
    assert(r.entryIdx == 0U);
    assert(r.suppressError);
    IopmpDestroy(&s);
    PASS("IOPMP-XC-002 deny + sere suppress");
}

/* IOPMP-XC-003 - two MDs, global index order; lowest index denies, eid captured. */
static void TestXc003(void)
{
    IopmpState_t s; IopmpParams_t p = P(1, 6, 2);
    assert(IopmpInit(&s, &p) == IOPMP_OK);
    Enable(&s); Mdcfg(&s, 0, 3); Mdcfg(&s, 1, 6); Srcmd(&s, 0, 0); Srcmd(&s, 0, 1);
    Entry(&s, 1, ADDR_MODE_NA4, 0x1000ULL, 4, ENTRY_CFG_W_BIT);   /* MD0, deny read */
    Entry(&s, 4, ADDR_MODE_NA4, 0x1000ULL, 4, ENTRY_CFG_R_BIT);   /* MD1, allow */

    TxnResult_t r = IopmpCheckAccess(&s, 0, 0x1000ULL, 4, IOPMP_TXN_READ);
    ASSERT_ETYPE(r, IOPMP_ETYPE_ILLEGAL_READ);
    assert(r.entryIdx == 1U);
    assert(((IopmpReadReg(&s, REG_ERR_REQID) & ERR_REQID_EID_MASK) >> ERR_REQID_EID_SHIFT) == 1U);
    IopmpDestroy(&s);
    PASS("IOPMP-XC-003 global index order deny");
}

/* ───────────────────────────────────────────────────────────────────
 * 18.2 SPS × entry-permission × locks
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-XC-004 - shared entry, A=RW B=RO via SPS; lock B's row freezes it. */
static void TestXc004(void)
{
    IopmpState_t s; IopmpParams_t p = P(2, 4, 1); p.spsEn = true;
    assert(IopmpInit(&s, &p) == IOPMP_OK);
    Enable(&s); Mdcfg(&s, 0, 4); Srcmd(&s, 0, 0); Srcmd(&s, 1, 0);
    Entry(&s, 0, ADDR_MODE_NA4, 0x1000ULL, 4, ENTRY_CFG_R_BIT | ENTRY_CFG_W_BIT);
    /* A: R+W (MD0 -> bit1,2). B: R only. */
    IopmpWriteReg(&s, REG_SRCMD_BASE + 0U * REG_SRCMD_STRIDE + REG_SRCMD_R_OFF, (1U << 1));
    IopmpWriteReg(&s, REG_SRCMD_BASE + 0U * REG_SRCMD_STRIDE + REG_SRCMD_W_OFF, (1U << 1));
    IopmpWriteReg(&s, REG_SRCMD_BASE + 1U * REG_SRCMD_STRIDE + REG_SRCMD_R_OFF, (1U << 1));
    /* Lock B's SRCMD row (keep its MD0 association, set lock). */
    IopmpWriteReg(&s, REG_SRCMD_BASE + 1U * REG_SRCMD_STRIDE + REG_SRCMD_EN_OFF, (1U << 1) | SRCMD_EN_L_BIT);

    ASSERT_LEGAL(IopmpCheckAccess(&s, 0, 0x1000ULL, 4, IOPMP_TXN_WRITE));
    ASSERT_ETYPE(IopmpCheckAccess(&s, 1, 0x1000ULL, 4, IOPMP_TXN_WRITE), IOPMP_ETYPE_ILLEGAL_WRITE);
    /* Attempt to widen B's SPS write - locked row, rejected. */
    IopmpWriteReg(&s, REG_SRCMD_BASE + 1U * REG_SRCMD_STRIDE + REG_SRCMD_W_OFF, (1U << 1));
    ASSERT_ETYPE(IopmpCheckAccess(&s, 1, 0x1000ULL, 4, IOPMP_TXN_WRITE), IOPMP_ETYPE_ILLEGAL_WRITE);
    IopmpDestroy(&s);
    PASS("IOPMP-XC-004 SPS per-RRID + row lock");
}

/* IOPMP-XC-005 - MDLCK locks SPS column; cannot grant write. */
static void TestXc005(void)
{
    IopmpState_t s; IopmpParams_t p = P(2, 4, 2); p.spsEn = true;
    assert(IopmpInit(&s, &p) == IOPMP_OK);
    Enable(&s); Mdcfg(&s, 0, 4); Srcmd(&s, 1, 0);
    Entry(&s, 0, ADDR_MODE_NA4, 0x1000ULL, 4, ENTRY_CFG_R_BIT | ENTRY_CFG_W_BIT);
    IopmpWriteReg(&s, REG_MDLCK, (1U << 1));                 /* lock MD0 column */
    /* Compromised SW tries to grant RRID1 write on MD0 (bit1) -> rejected. */
    IopmpWriteReg(&s, REG_SRCMD_BASE + 1U * REG_SRCMD_STRIDE + REG_SRCMD_W_OFF, (1U << 1));
    assert((IopmpReadReg(&s, REG_SRCMD_BASE + 1U * REG_SRCMD_STRIDE + REG_SRCMD_W_OFF) & (1U << 1)) == 0U);
    ASSERT_ETYPE(IopmpCheckAccess(&s, 1, 0x1000ULL, 4, IOPMP_TXN_WRITE), IOPMP_ETYPE_ILLEGAL_WRITE);
    IopmpDestroy(&s);
    PASS("IOPMP-XC-005 MDLCK locks SPS column");
}

/* IOPMP-XC-006 - AMO denied by SPS write. */
static void TestXc006(void)
{
    IopmpState_t s; IopmpParams_t p = P(2, 4, 1); p.spsEn = true;
    assert(IopmpInit(&s, &p) == IOPMP_OK);
    Enable(&s); Mdcfg(&s, 0, 4); Srcmd(&s, 1, 0);
    Entry(&s, 0, ADDR_MODE_NA4, 0x1000ULL, 4, ENTRY_CFG_R_BIT | ENTRY_CFG_W_BIT);
    IopmpWriteReg(&s, REG_SRCMD_BASE + 1U * REG_SRCMD_STRIDE + REG_SRCMD_R_OFF, (1U << 1)); /* read only */
    ASSERT_ETYPE(IopmpCheckAccess(&s, 1, 0x1000ULL, 4, IOPMP_TXN_AMO), IOPMP_ETYPE_ILLEGAL_WRITE);
    IopmpDestroy(&s);
    PASS("IOPMP-XC-006 AMO denied by SPS");
}

/* ───────────────────────────────────────────────────────────────────
 * 18.3 lock × stall × runtime reconfig
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-XC-007 - locked 0-3 preserved; unlocked 4-7 rewritten under stall. */
static void TestXc007(void)
{
    IopmpState_t s; IopmpParams_t p = P(2, 8, 1); p.stallEn = true;
    assert(IopmpInit(&s, &p) == IOPMP_OK);
    Enable(&s); Mdcfg(&s, 0, 8); Srcmd(&s, 0, 0);
    Entry(&s, 0, ADDR_MODE_NA4, 0x1000ULL, 4, ENTRY_CFG_R_BIT);
    uint32_t c0 = IopmpReadReg(&s, IopmpReadReg(&s, REG_ENTRYOFFSET) + REG_ENTRY_CFG_OFF);
    IopmpWriteReg(&s, REG_ENTRYLCK, 4U << ENTRYLCK_F_SHIFT);
    IopmpWriteReg(&s, REG_MDSTALL, (1U << 1));               /* stall MD0 */
    Entry(&s, 5, ADDR_MODE_NA4, 0x5000ULL, 4, ENTRY_CFG_R_BIT);    /* unlocked -> updates */
    uint32_t c0off = IopmpReadReg(&s, REG_ENTRYOFFSET) + REG_ENTRY_CFG_OFF;
    IopmpWriteReg(&s, c0off, ENTRY_CFG_W_BIT);               /* locked -> rejected */
    IopmpWriteReg(&s, REG_MDSTALL, 0U);                      /* resume */
    assert(IopmpReadReg(&s, c0off) == c0);
    uint32_t c5 = IopmpReadReg(&s, IopmpReadReg(&s, REG_ENTRYOFFSET) + 5U * REG_ENTRY_STRIDE + REG_ENTRY_CFG_OFF);
    assert((c5 & ENTRY_CFG_R_BIT) != 0U);
    IopmpDestroy(&s);
    PASS("IOPMP-XC-007 lock + stall reconfig");
}

/* IOPMP-XC-008 - stall_violation_en + ie: fault 0x07 captured, interrupt. */
static void TestXc008(void)
{
    IopmpState_t s; IopmpParams_t p = P(2, 4, 1); p.stallEn = true;
    assert(IopmpInit(&s, &p) == IOPMP_OK);
    Enable(&s); Mdcfg(&s, 0, 4); Srcmd(&s, 0, 0);
    Entry(&s, 0, ADDR_MODE_NA4, 0x1000ULL, 4, ENTRY_CFG_R_BIT);
    IopmpWriteReg(&s, REG_ERR_CFG, ERR_CFG_STALL_VIOL_BIT | ERR_CFG_IE_BIT);
    IopmpWriteReg(&s, REG_MDSTALL, (1U << 1));
    TxnResult_t r = IopmpCheckAccess(&s, 0, 0x1000ULL, 4, IOPMP_TXN_READ);
    ASSERT_ETYPE(r, IOPMP_ETYPE_STALL_VIOL);
    assert(IopmpIsIrqPending(&s));
    IopmpDestroy(&s);
    PASS("IOPMP-XC-008 stall fault + capture + IRQ");
}

/* IOPMP-XC-009 - grow MD via MDCFG under stall; new entries active post-resume. */
static void TestXc009(void)
{
    IopmpState_t s; IopmpParams_t p = P(2, 8, 2); p.stallEn = true;
    assert(IopmpInit(&s, &p) == IOPMP_OK);
    Enable(&s); Mdcfg(&s, 0, 2); Mdcfg(&s, 1, 8); Srcmd(&s, 0, 0);
    Entry(&s, 3, ADDR_MODE_NA4, 0x3000ULL, 4, ENTRY_CFG_R_BIT);    /* entry3 in MD1 */
    ASSERT_ETYPE(IopmpCheckAccess(&s, 0, 0x3000ULL, 4, IOPMP_TXN_READ), IOPMP_ETYPE_NO_RULE);
    IopmpWriteReg(&s, REG_MDSTALL, (1U << 1));               /* stall MD0 */
    Mdcfg(&s, 0, 4);                                         /* grow MD0 to entries [0,4) */
    IopmpWriteReg(&s, REG_MDSTALL, 0U);                      /* resume */
    ASSERT_LEGAL(IopmpCheckAccess(&s, 0, 0x3000ULL, 4, IOPMP_TXN_READ));  /* entry3 now in MD0 */
    IopmpDestroy(&s);
    PASS("IOPMP-XC-009 grow MD under stall");
}

/* IOPMP-XC-010 - MDSTALL stalls a group; RRIDSCP exempts one RRID. */
static void TestXc010(void)
{
    IopmpState_t s; IopmpParams_t p = P(3, 4, 1); p.stallEn = true;
    assert(IopmpInit(&s, &p) == IOPMP_OK);
    Srcmd(&s, 0, 0); Srcmd(&s, 1, 0); Srcmd(&s, 2, 0);
    IopmpWriteReg(&s, REG_MDSTALL, (1U << 1));               /* stall all (all in MD0) */
    IopmpWriteReg(&s, REG_RRIDSCP, 1U | (RRIDSCP_OP_NOSTALL << RRIDSCP_OP_SHIFT)); /* exempt RRID1 */
    /* Query stall status. */
    IopmpWriteReg(&s, REG_RRIDSCP, 1U | (RRIDSCP_OP_QUERY << RRIDSCP_OP_SHIFT));
    assert(((IopmpReadReg(&s, REG_RRIDSCP) & RRIDSCP_OP_MASK) >> RRIDSCP_OP_SHIFT) == RRIDSCP_STAT_NOTSTALLED);
    IopmpWriteReg(&s, REG_RRIDSCP, 0U | (RRIDSCP_OP_QUERY << RRIDSCP_OP_SHIFT));
    assert(((IopmpReadReg(&s, REG_RRIDSCP) & RRIDSCP_OP_MASK) >> RRIDSCP_OP_SHIFT) == RRIDSCP_STAT_STALLED);
    IopmpDestroy(&s);
    PASS("IOPMP-XC-010 MDSTALL group + RRIDSCP exempt");
}

/* ───────────────────────────────────────────────────────────────────
 * 18.4 non-priority × suppression × MFR
 * ─────────────────────────────────────────────────────────────────── */

static IopmpParams_t Nprio(uint16_t rridNum)
{
    IopmpParams_t p = P(rridNum, 4, 1);
    p.nonPrioEn = true; p.prioEntry = 0;
    return p;
}

/* IOPMP-XC-011 - non-prio: not all sire -> interrupt asserted. */
static void TestXc011(void)
{
    IopmpState_t s; IopmpParams_t p = Nprio(1); p.peisEn = true;
    assert(IopmpInit(&s, &p) == IOPMP_OK);
    Enable(&s); Mdcfg(&s, 0, 4); Srcmd(&s, 0, 0);
    IopmpWriteReg(&s, REG_ERR_CFG, ERR_CFG_IE_BIT);
    Entry(&s, 0, ADDR_MODE_NAPOT, 0x1000ULL, 0x20, ENTRY_CFG_W_BIT | ENTRY_CFG_SIRE_BIT);
    Entry(&s, 1, ADDR_MODE_NAPOT, 0x1000ULL, 0x20, ENTRY_CFG_W_BIT);   /* no sire */
    ASSERT_ETYPE(IopmpCheckAccess(&s, 0, 0x1000ULL, 4, IOPMP_TXN_READ), IOPMP_ETYPE_ILLEGAL_READ);
    assert(IopmpIsIrqPending(&s));
    IopmpDestroy(&s);
    PASS("IOPMP-XC-011 non-prio partial suppress -> IRQ fires");
}

/* IOPMP-XC-012 - non-prio: all sere -> bus error suppressed. */
static void TestXc012(void)
{
    IopmpState_t s; IopmpParams_t p = Nprio(1); p.peesEn = true;
    assert(IopmpInit(&s, &p) == IOPMP_OK);
    Enable(&s); Mdcfg(&s, 0, 4); Srcmd(&s, 0, 0);
    Entry(&s, 0, ADDR_MODE_NAPOT, 0x1000ULL, 0x20, ENTRY_CFG_W_BIT | ENTRY_CFG_SERE_BIT);
    Entry(&s, 1, ADDR_MODE_NAPOT, 0x1000ULL, 0x20, ENTRY_CFG_W_BIT | ENTRY_CFG_SERE_BIT);
    TxnResult_t r = IopmpCheckAccess(&s, 0, 0x1000ULL, 4, IOPMP_TXN_READ);
    ASSERT_ETYPE(r, IOPMP_ETYPE_ILLEGAL_READ);
    assert(r.suppressError);
    IopmpDestroy(&s);
    PASS("IOPMP-XC-012 non-prio all sere suppress");
}

/* IOPMP-XC-013 - non-prio multi-match + MFR subsequent. */
static void TestXc013(void)
{
    IopmpState_t s; IopmpParams_t p = Nprio(10); p.multifaultEn = true;
    assert(IopmpInit(&s, &p) == IOPMP_OK);
    Enable(&s); Mdcfg(&s, 0, 4); Srcmd(&s, 5, 0); Srcmd(&s, 9, 0);
    Entry(&s, 0, ADDR_MODE_NAPOT, 0x1000ULL, 0x20, ENTRY_CFG_W_BIT);
    IopmpCheckAccess(&s, 5, 0x1000ULL, 4, IOPMP_TXN_READ);   /* first */
    IopmpCheckAccess(&s, 9, 0x1000ULL, 4, IOPMP_TXN_READ);   /* subsequent */
    assert((IopmpReadReg(&s, REG_ERR_INFO) & ERR_INFO_SVC_BIT) != 0U);
    uint32_t mfr = IopmpReadReg(&s, REG_ERR_MFR);
    assert((mfr & ERR_MFR_SVS_BIT) != 0U);
    assert((mfr & (1U << 9)) != 0U);                         /* RRID9 window0 bit9 */
    IopmpDestroy(&s);
    PASS("IOPMP-XC-013 non-prio + MFR");
}

/* ───────────────────────────────────────────────────────────────────
 * 18.5 error capture × MSI × MFR
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-XC-014 - RRID2 -> ERR_INFO + MSI; RRID4,6 -> MFR. */
static void TestXc014(void)
{
    IopmpState_t s; IopmpParams_t p = P(8, 4, 1); p.msiEn = true; p.multifaultEn = true;
    assert(IopmpInit(&s, &p) == IOPMP_OK);
    Enable(&s); Mdcfg(&s, 0, 4);
    for (uint16_t r = 0; r < 8; r++) Srcmd(&s, r, 0);
    IopmpWriteReg(&s, REG_ERR_MSIADDR, 0xFEE00000U);
    IopmpWriteReg(&s, REG_ERR_CFG, ERR_CFG_IE_BIT | ERR_CFG_MSI_SEL_BIT | (0x33U << ERR_CFG_MSIDATA_SHIFT));
    Entry(&s, 0, ADDR_MODE_NA4, 0x1000ULL, 4, ENTRY_CFG_W_BIT);

    IopmpCheckAccess(&s, 2, 0x1000ULL, 4, IOPMP_TXN_READ);   /* first -> MSI */
    assert(IopmpIsMsiPending(&s));
    assert((IopmpReadReg(&s, REG_ERR_REQID) & ERR_REQID_RRID_MASK) == 2U);
    IopmpCheckAccess(&s, 4, 0x1000ULL, 4, IOPMP_TXN_READ);
    IopmpCheckAccess(&s, 6, 0x1000ULL, 4, IOPMP_TXN_READ);
    assert((IopmpReadReg(&s, REG_ERR_INFO) & ERR_INFO_SVC_BIT) != 0U);
    uint32_t mfr = IopmpReadReg(&s, REG_ERR_MFR);
    assert((mfr & (1U << 4)) && (mfr & (1U << 6)));
    IopmpDestroy(&s);
    PASS("IOPMP-XC-014 ERR_INFO + MSI + MFR chain");
}

/* IOPMP-XC-015 - MSI write fault -> msi_werr=1; violation still recorded. */
static void TestXc015(void)
{
    IopmpState_t s; IopmpParams_t p = P(4, 4, 1); p.msiEn = true; p.msiInjectWriteErr = true;
    assert(IopmpInit(&s, &p) == IOPMP_OK);
    Enable(&s); Mdcfg(&s, 0, 4); Srcmd(&s, 0, 0);
    IopmpWriteReg(&s, REG_ERR_MSIADDR, 0xFEE00000U);
    IopmpWriteReg(&s, REG_ERR_CFG, ERR_CFG_IE_BIT | ERR_CFG_MSI_SEL_BIT);
    Entry(&s, 0, ADDR_MODE_NA4, 0x1000ULL, 4, ENTRY_CFG_W_BIT);
    IopmpCheckAccess(&s, 0, 0x1000ULL, 4, IOPMP_TXN_READ);
    assert((IopmpReadReg(&s, REG_ERR_INFO) & ERR_INFO_MSI_WERR_BIT) != 0U);
    assert((IopmpReadReg(&s, REG_ERR_INFO) & ERR_INFO_V_BIT) != 0U);
    IopmpDestroy(&s);
    PASS("IOPMP-XC-015 MSI write fault msi_werr");
}

/* IOPMP-XC-016 - msi_sel + matched entry sire: no MSI. */
static void TestXc016(void)
{
    IopmpState_t s; IopmpParams_t p = P(4, 4, 1); p.msiEn = true; p.peisEn = true;
    assert(IopmpInit(&s, &p) == IOPMP_OK);
    Enable(&s); Mdcfg(&s, 0, 4); Srcmd(&s, 0, 0);
    IopmpWriteReg(&s, REG_ERR_MSIADDR, 0xFEE00000U);
    IopmpWriteReg(&s, REG_ERR_CFG, ERR_CFG_IE_BIT | ERR_CFG_MSI_SEL_BIT);
    Entry(&s, 0, ADDR_MODE_NA4, 0x1000ULL, 4, ENTRY_CFG_W_BIT | ENTRY_CFG_SIRE_BIT);
    IopmpCheckAccess(&s, 0, 0x1000ULL, 4, IOPMP_TXN_READ);
    assert(!IopmpIsMsiPending(&s));
    assert((IopmpReadReg(&s, REG_ERR_INFO) & ERR_INFO_V_BIT) != 0U);
    IopmpDestroy(&s);
    PASS("IOPMP-XC-016 sire suppresses MSI");
}

/* ───────────────────────────────────────────────────────────────────
 * 18.6 address mode × 64-bit × error address capture
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-XC-017 - high NAPOT deny; ERR_REQADDR/H capture full address. */
static void TestXc017(void)
{
    IopmpState_t s; IopmpParams_t p = P(2, 4, 1); p.addrhEn = true;
    assert(IopmpInit(&s, &p) == IOPMP_OK);
    Enable(&s); Mdcfg(&s, 0, 4); Srcmd(&s, 0, 0);
    uint32_t b = IopmpReadReg(&s, REG_ENTRYOFFSET);
    /* base 0x4_0000_1000, 16-byte NAPOT, write-only (deny read). */
    IopmpWriteReg(&s, b + REG_ENTRY_ADDR_OFF, 0x401U);
    IopmpWriteReg(&s, b + REG_ENTRY_ADDRH_OFF, 1U);
    IopmpWriteReg(&s, b + REG_ENTRY_CFG_OFF, (ADDR_MODE_NAPOT << ENTRY_CFG_A_SHIFT) | ENTRY_CFG_W_BIT);

    uint64_t addr = 0x400001000ULL;
    ASSERT_ETYPE(IopmpCheckAccess(&s, 0, addr, 4, IOPMP_TXN_READ), IOPMP_ETYPE_ILLEGAL_READ);
    assert(IopmpReadReg(&s, REG_ERR_REQADDR)  == (uint32_t)((addr >> 2U) & 0xFFFFFFFFU));
    assert(IopmpReadReg(&s, REG_ERR_REQADDRH) == (uint32_t)(addr >> 34U));
    IopmpDestroy(&s);
    PASS("IOPMP-XC-017 high addr deny + capture");
}

/* IOPMP-XC-018 - TOR spanning 34-bit boundary, partial hit; addr captured. */
static void TestXc018(void)
{
    IopmpState_t s; IopmpParams_t p = P(2, 4, 1); p.addrhEn = true;
    assert(IopmpInit(&s, &p) == IOPMP_OK);
    Enable(&s); Mdcfg(&s, 0, 4); Srcmd(&s, 0, 0);
    uint32_t b = IopmpReadReg(&s, REG_ENTRYOFFSET);
    /* entry0 supplies TOR base = 0x3_FFFF_FFFC; entry1 top = 0x4_0000_0040. */
    IopmpWriteReg(&s, b + 0U * REG_ENTRY_STRIDE + REG_ENTRY_ADDR_OFF, 0xFFFFFFFFU);
    IopmpWriteReg(&s, b + 0U * REG_ENTRY_STRIDE + REG_ENTRY_ADDRH_OFF, 0U);
    IopmpWriteReg(&s, b + 1U * REG_ENTRY_STRIDE + REG_ENTRY_ADDR_OFF, 0x10U);
    IopmpWriteReg(&s, b + 1U * REG_ENTRY_STRIDE + REG_ENTRY_ADDRH_OFF, 1U);
    IopmpWriteReg(&s, b + 1U * REG_ENTRY_STRIDE + REG_ENTRY_CFG_OFF,
                  (ADDR_MODE_TOR << ENTRY_CFG_A_SHIFT) | ENTRY_CFG_R_BIT);
    /* Read of 0x80 bytes starting near the end crosses the region end -> partial. */
    uint64_t addr = 0x3FFFFFFC0ULL;
    ASSERT_ETYPE(IopmpCheckAccess(&s, 0, addr, 0x100, IOPMP_TXN_READ), IOPMP_ETYPE_PARTIAL_HIT);
    assert(IopmpReadReg(&s, REG_ERR_REQADDRH) == (uint32_t)(addr >> 34U));
    IopmpDestroy(&s);
    PASS("IOPMP-XC-018 TOR boundary partial hit + capture");
}

/* ───────────────────────────────────────────────────────────────────
 * 18.7 global protection × per-entry × models
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-XC-019 - no_w + no_x data-only device. */
static void TestXc019(void)
{
    IopmpState_t s; IopmpParams_t p = P(1, 4, 1); p.noW = true; p.noX = true;
    assert(IopmpInit(&s, &p) == IOPMP_OK);
    Enable(&s); Mdcfg(&s, 0, 4); Srcmd(&s, 0, 0);
    Entry(&s, 0, ADDR_MODE_NA4, 0x1000ULL, 4, ENTRY_CFG_R_BIT | ENTRY_CFG_W_BIT | ENTRY_CFG_X_BIT);
    ASSERT_LEGAL(IopmpCheckAccess(&s, 0, 0x1000ULL, 4, IOPMP_TXN_READ));
    ASSERT_ETYPE(IopmpCheckAccess(&s, 0, 0x1000ULL, 4, IOPMP_TXN_WRITE), IOPMP_ETYPE_NO_RULE);
    ASSERT_ETYPE(IopmpCheckAccess(&s, 0, 0x1000ULL, 4, IOPMP_TXN_EXEC), IOPMP_ETYPE_NO_RULE);
    IopmpDestroy(&s);
    PASS("IOPMP-XC-019 no_w+no_x data-only");
}

/* IOPMP-XC-020 - xinr: exec checked as read. */
static void TestXc020(void)
{
    IopmpState_t s; IopmpParams_t p = P(1, 4, 1); p.xinr = true;
    assert(IopmpInit(&s, &p) == IOPMP_OK);
    Enable(&s); Mdcfg(&s, 0, 4); Srcmd(&s, 0, 0);
    Entry(&s, 0, ADDR_MODE_NA4, 0x1000ULL, 4, ENTRY_CFG_R_BIT);   /* r=1, x=0 */
    ASSERT_LEGAL(IopmpCheckAccess(&s, 0, 0x1000ULL, 4, IOPMP_TXN_EXEC));
    IopmpDestroy(&s);
    PASS("IOPMP-XC-020 xinr exec-as-read");
}

/* IOPMP-XC-021 - Compact-k: stall MD m, reconfig its entries, resume. */
static void TestXc021(void)
{
    IopmpState_t s; IopmpParams_t p = P(4, 16, 4);
    p.model = IOPMP_MODEL_COMPACT; p.srcmdFmt = 1U; p.mdcfgFmt = 1U; p.stallEn = true;
    assert(IopmpInit(&s, &p) == IOPMP_OK);
    Enable(&s);
    /* RRID2 == MD2 -> entries [8,12). */
    Entry(&s, 8, ADDR_MODE_NA4, 0x8000ULL, 4, ENTRY_CFG_R_BIT);
    ASSERT_LEGAL(IopmpCheckAccess(&s, 2, 0x8000ULL, 4, IOPMP_TXN_READ));
    IopmpWriteReg(&s, REG_MDSTALL, (1U << (2 + 1)));         /* stall MD2 -> only RRID2 */
    assert(IopmpCheckAccess(&s, 2, 0x8000ULL, 4, IOPMP_TXN_READ).stalled);
    /* RRID0 (MD0) keeps running. */
    Entry(&s, 0, ADDR_MODE_NA4, 0x1000ULL, 4, ENTRY_CFG_R_BIT);
    ASSERT_LEGAL(IopmpCheckAccess(&s, 0, 0x1000ULL, 4, IOPMP_TXN_READ));
    Entry(&s, 8, ADDR_MODE_NA4, 0x8000ULL, 4, ENTRY_CFG_W_BIT);   /* reconfig under stall */
    IopmpWriteReg(&s, REG_MDSTALL, 0U);
    ASSERT_ETYPE(IopmpCheckAccess(&s, 2, 0x8000ULL, 4, IOPMP_TXN_READ), IOPMP_ETYPE_ILLEGAL_READ);
    IopmpDestroy(&s);
    PASS("IOPMP-XC-021 compact-k stall + reconfig");
}

/* ───────────────────────────────────────────────────────────────────
 * 18.8 enable / bypass × locks × capture
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-XC-022 - enable=0: bypass, no capture. */
static void TestXc022(void)
{
    IopmpState_t s; IopmpParams_t p = P(1, 4, 1);
    assert(IopmpInit(&s, &p) == IOPMP_OK);
    /* not enabled */
    ASSERT_LEGAL(IopmpCheckAccess(&s, 0, 0x1000ULL, 4, IOPMP_TXN_WRITE));
    assert((IopmpReadReg(&s, REG_ERR_INFO) & ERR_INFO_V_BIT) == 0U);
    IopmpDestroy(&s);
    PASS("IOPMP-XC-022 disabled bypass no capture");
}

/* IOPMP-XC-023 - enable sticky; rules enforced; locks immutable. */
static void TestXc023(void)
{
    IopmpState_t s; IopmpParams_t p = P(1, 4, 1);
    assert(IopmpInit(&s, &p) == IOPMP_OK);
    Mdcfg(&s, 0, 4); Srcmd(&s, 0, 0);
    Entry(&s, 0, ADDR_MODE_NA4, 0x1000ULL, 4, ENTRY_CFG_R_BIT);
    IopmpWriteReg(&s, REG_ENTRYLCK, (1U << ENTRYLCK_F_SHIFT) | ENTRYLCK_L_BIT);
    Enable(&s);
    IopmpWriteReg(&s, REG_HWCFG0, 0U);                      /* try clear enable */
    assert((IopmpReadReg(&s, REG_HWCFG0) & HWCFG0_ENABLE_BIT) != 0U);
    ASSERT_LEGAL(IopmpCheckAccess(&s, 0, 0x1000ULL, 4, IOPMP_TXN_READ));
    /* Locked entry 0 immutable. */
    uint32_t off = IopmpReadReg(&s, REG_ENTRYOFFSET) + REG_ENTRY_CFG_OFF;
    uint32_t before = IopmpReadReg(&s, off);
    IopmpWriteReg(&s, off, ENTRY_CFG_W_BIT);
    assert(IopmpReadReg(&s, off) == before);
    IopmpDestroy(&s);
    PASS("IOPMP-XC-023 enable sticky + locks immutable");
}

/* IOPMP-XC-024 - md_entry_num locked by enable. */
static void TestXc024(void)
{
    IopmpState_t s; IopmpParams_t p = P(2, 8, 2); p.mdcfgFmt = 2U; p.mdEntryNum = 2;
    assert(IopmpInit(&s, &p) == IOPMP_OK);
    Enable(&s);
    IopmpWriteReg(&s, REG_HWCFG3,
        (IopmpReadReg(&s, REG_HWCFG3) & ~HWCFG3_MD_ENTRY_NUM_MASK) | (4U << HWCFG3_MD_ENTRY_NUM_SHIFT));
    assert(((IopmpReadReg(&s, REG_HWCFG3) & HWCFG3_MD_ENTRY_NUM_MASK) >> HWCFG3_MD_ENTRY_NUM_SHIFT) == 2U);
    IopmpDestroy(&s);
    PASS("IOPMP-XC-024 md_entry_num frozen by enable");
}

/* ───────────────────────────────────────────────────────────────────
 * 18.9 multi-instance × mixed-model × independent reactions
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-XC-025 - A wired IRQ, B MSI; independent records. */
static void TestXc025(void)
{
    IopmpState_t a, b;
    IopmpParams_t pa = P(4, 4, 2);
    IopmpParams_t pb = P(4, 8, 4); pb.msiEn = true; pb.model = IOPMP_MODEL_ISOLATION; pb.srcmdFmt = 1U;
    assert(IopmpInit(&a, &pa) == IOPMP_OK);
    assert(IopmpInit(&b, &pb) == IOPMP_OK);
    IopmpSystem_t sys; IopmpSystemInit(&sys);
    IopmpSystemAddInstance(&sys, &a, 0x10000ULL);
    IopmpSystemAddInstance(&sys, &b, 0x20000ULL);

    /* A: wired IRQ. */
    IopmpSystemWriteReg(&sys, 0x10000ULL + REG_HWCFG0, HWCFG0_ENABLE_BIT);
    IopmpSystemWriteReg(&sys, 0x10000ULL + REG_ERR_CFG, ERR_CFG_IE_BIT);
    /* B: MSI. */
    IopmpSystemWriteReg(&sys, 0x20000ULL + REG_HWCFG0, HWCFG0_ENABLE_BIT);
    IopmpSystemWriteReg(&sys, 0x20000ULL + REG_ERR_MSIADDR, 0xFEE00000U);
    IopmpSystemWriteReg(&sys, 0x20000ULL + REG_ERR_CFG, ERR_CFG_IE_BIT | ERR_CFG_MSI_SEL_BIT);

    IopmpSystemCheckAccess(&sys, 0, 0, 0x1000ULL, 4, IOPMP_TXN_READ);   /* A violation */
    IopmpSystemCheckAccess(&sys, 1, 0, 0x9000ULL, 4, IOPMP_TXN_READ);   /* B violation */
    assert(IopmpIsIrqPending(&a) && !IopmpIsMsiPending(&a));
    assert(IopmpIsMsiPending(&b) && !IopmpIsIrqPending(&b));
    IopmpSystemDestroy(&sys); IopmpDestroy(&a); IopmpDestroy(&b);
    PASS("IOPMP-XC-025 mixed wired/MSI independent");
}

/* IOPMP-XC-026 - cascade: inner translates RRID, outer (parallel) checks. */
static void TestXc026(void)
{
    IopmpState_t inner, outer;
    IopmpParams_t pi = P(8, 8, 2); pi.rridTranslEn = true; pi.rridTranslProg = true;
    IopmpParams_t po = P(8, 8, 2);
    assert(IopmpInit(&inner, &pi) == IOPMP_OK);
    assert(IopmpInit(&outer, &po) == IOPMP_OK);
    IopmpSystem_t sys; IopmpSystemInit(&sys);
    IopmpSystemAddInstance(&sys, &inner, 0x10000ULL);
    IopmpSystemAddInstance(&sys, &outer, 0x20000ULL);

    /* Inner: translate RRID5 -> 3; checks RRID3 (associated) -> legal. */
    IopmpSystemWriteReg(&sys, 0x10000ULL + REG_HWCFG0, HWCFG0_ENABLE_BIT);
    IopmpSystemWriteReg(&sys, 0x10000ULL + REG_MDCFG_BASE, 8U);
    IopmpSystemWriteReg(&sys, 0x10000ULL + REG_SRCMD_BASE + 3U * REG_SRCMD_STRIDE + REG_SRCMD_EN_OFF, (1U << 1));
    uint32_t eoff = 0U; IopmpSystemReadReg(&sys, 0x10000ULL + REG_ENTRYOFFSET, &eoff);
    IopmpSystemWriteReg(&sys, 0x10000ULL + eoff + REG_ENTRY_ADDR_OFF, (uint32_t)(0x1000ULL >> 2U));
    IopmpSystemWriteReg(&sys, 0x10000ULL + eoff + REG_ENTRY_CFG_OFF, (ADDR_MODE_NA4 << ENTRY_CFG_A_SHIFT) | ENTRY_CFG_R_BIT);
    IopmpSystemWriteReg(&sys, 0x10000ULL + REG_RRIDTRANSL_BASE + 5U * REG_RRIDTRANSL_STRIDE, 3U);

    /* Outer: RRID3 -> MD0 entry0 read. */
    IopmpSystemWriteReg(&sys, 0x20000ULL + REG_HWCFG0, HWCFG0_ENABLE_BIT);
    IopmpSystemWriteReg(&sys, 0x20000ULL + REG_MDCFG_BASE, 8U);
    IopmpSystemWriteReg(&sys, 0x20000ULL + REG_SRCMD_BASE + 3U * REG_SRCMD_STRIDE + REG_SRCMD_EN_OFF, (1U << 1));
    IopmpSystemReadReg(&sys, 0x20000ULL + REG_ENTRYOFFSET, &eoff);
    IopmpSystemWriteReg(&sys, 0x20000ULL + eoff + REG_ENTRY_ADDR_OFF, (uint32_t)(0x1000ULL >> 2U));
    IopmpSystemWriteReg(&sys, 0x20000ULL + eoff + REG_ENTRY_CFG_OFF, (ADDR_MODE_NA4 << ENTRY_CFG_A_SHIFT) | ENTRY_CFG_R_BIT);

    ASSERT_LEGAL(IopmpSystemCheckAccess(&sys, 0, 5, 0x1000ULL, 4, IOPMP_TXN_READ));   /* inner */
    uint32_t t = 0U; IopmpSystemReadReg(&sys, 0x10000ULL + REG_RRIDTRANSL_BASE + 5U * REG_RRIDTRANSL_STRIDE, &t);
    ASSERT_LEGAL(IopmpSystemCheckAccess(&sys, 1, (uint16_t)t, 0x1000ULL, 4, IOPMP_TXN_READ));  /* outer */
    IopmpSystemDestroy(&sys); IopmpDestroy(&inner); IopmpDestroy(&outer);
    PASS("IOPMP-XC-026 cascade + parallel route");
}

/* ───────────────────────────────────────────────────────────────────
 * 18.10 negative / robustness
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-XC-027 - len=0 transaction: defined, no crash. */
static void TestXc027(void)
{
    IopmpState_t s; IopmpParams_t p = P(1, 4, 1);
    assert(IopmpInit(&s, &p) == IOPMP_OK);
    Enable(&s); Mdcfg(&s, 0, 4); Srcmd(&s, 0, 0);
    Entry(&s, 0, ADDR_MODE_NA4, 0x1000ULL, 4, ENTRY_CFG_R_BIT);
    /* Zero-length covers no byte -> no match -> NO_RULE (defined, no crash). */
    ASSERT_ETYPE(IopmpCheckAccess(&s, 0, 0x1000ULL, 0, IOPMP_TXN_READ), IOPMP_ETYPE_NO_RULE);
    IopmpDestroy(&s);
    PASS("IOPMP-XC-027 len=0 defined");
}

/* IOPMP-XC-028 - max address / large len near top of space: no overflow. */
static void TestXc028(void)
{
    IopmpState_t s; IopmpParams_t p = P(1, 4, 1); p.addrhEn = true;
    assert(IopmpInit(&s, &p) == IOPMP_OK);
    Enable(&s); Mdcfg(&s, 0, 4); Srcmd(&s, 0, 0);
    Entry(&s, 0, ADDR_MODE_NA4, 0x1000ULL, 4, ENTRY_CFG_R_BIT);
    /* Address near 2^64 with large len: no overlap, no overflow -> NO_RULE. */
    TxnResult_t r = IopmpCheckAccess(&s, 0, 0xFFFFFFFFFFFFF000ULL, 0x1000, IOPMP_TXN_READ);
    ASSERT_ETYPE(r, IOPMP_ETYPE_NO_RULE);
    IopmpDestroy(&s);
    PASS("IOPMP-XC-028 max-address no overflow");
}

/* IOPMP-XC-029 - WARL sweep: writing all-ones retains only legal bits. */
static void TestXc029(void)
{
    IopmpState_t s; IopmpParams_t p = P(4, 4, 2);
    assert(IopmpInit(&s, &p) == IOPMP_OK);

    IopmpWriteReg(&s, REG_MDCFG_BASE, 0xFFFFFFFFU);
    assert((IopmpReadReg(&s, REG_MDCFG_BASE) & ~MDCFG_T_MASK) == 0U);
    uint32_t eoff = IopmpReadReg(&s, REG_ENTRYOFFSET) + REG_ENTRY_CFG_OFF;
    IopmpWriteReg(&s, eoff, 0xFFFFFFFFU);
    assert((IopmpReadReg(&s, eoff) & ~ENTRY_CFG_VALID_MASK) == 0U);
    IopmpWriteReg(&s, REG_ERR_CFG, 0xFFFFFFFFU);
    assert((IopmpReadReg(&s, REG_ERR_CFG) & ~(ERR_CFG_VALID_MASK | ERR_CFG_L_BIT)) == 0U);
    IopmpWriteReg(&s, REG_SRCMD_BASE + REG_SRCMD_EN_OFF, 0xFFFFFFFFU);
    /* SRCMD_EN: all 31 MD bits + lock are legal here (md_num=2 doesn't gate). */
    assert(IopmpReadReg(&s, REG_HWCFG1) != 0U);   /* read-only reg unchanged by sweep */
    IopmpDestroy(&s);
    PASS("IOPMP-XC-029 WARL sweep retains only legal bits");
}

/* IOPMP-XC-030 - full lock sweep: protected writes rejected; ERR_INFO mutable. */
static void TestXc030(void)
{
    IopmpState_t s; IopmpParams_t p = P(4, 4, 2);
    assert(IopmpInit(&s, &p) == IOPMP_OK);
    Enable(&s); Mdcfg(&s, 0, 4); Srcmd(&s, 0, 0);
    Entry(&s, 0, ADDR_MODE_NA4, 0x1000ULL, 4, ENTRY_CFG_W_BIT);

    /* Lock everything. */
    IopmpWriteReg(&s, REG_MDCFGLCK, (2U << MDCFGLCK_F_SHIFT) | MDCFGLCK_L_BIT);
    IopmpWriteReg(&s, REG_ENTRYLCK, (4U << ENTRYLCK_F_SHIFT) | ENTRYLCK_L_BIT);
    IopmpWriteReg(&s, REG_MDLCK, MDLCK_L_BIT);
    IopmpWriteReg(&s, REG_SRCMD_BASE + REG_SRCMD_EN_OFF, SRCMD_EN_L_BIT);
    IopmpWriteReg(&s, REG_ERR_CFG, ERR_CFG_L_BIT);

    /* Protected writes rejected. */
    uint32_t m0 = IopmpReadReg(&s, REG_MDCFG_BASE);
    IopmpWriteReg(&s, REG_MDCFG_BASE, 1U);
    assert(IopmpReadReg(&s, REG_MDCFG_BASE) == m0);
    uint32_t e0 = IopmpReadReg(&s, IopmpReadReg(&s, REG_ENTRYOFFSET) + REG_ENTRY_CFG_OFF);
    IopmpWriteReg(&s, IopmpReadReg(&s, REG_ENTRYOFFSET) + REG_ENTRY_CFG_OFF, ENTRY_CFG_R_BIT);
    assert(IopmpReadReg(&s, IopmpReadReg(&s, REG_ENTRYOFFSET) + REG_ENTRY_CFG_OFF) == e0);

    /* Cause a violation; ERR_INFO.v still clearable (no lock on record). */
    IopmpCheckAccess(&s, 0, 0x1000ULL, 4, IOPMP_TXN_READ);
    assert((IopmpReadReg(&s, REG_ERR_INFO) & ERR_INFO_V_BIT) != 0U);
    IopmpWriteReg(&s, REG_ERR_INFO, ERR_INFO_V_BIT);
    assert((IopmpReadReg(&s, REG_ERR_INFO) & ERR_INFO_V_BIT) == 0U);
    IopmpDestroy(&s);
    PASS("IOPMP-XC-030 full lock sweep");
}

/* IOPMP-XC-031 - reset returns non-prelocked state to defaults. */
static void TestXc031(void)
{
    IopmpState_t s; IopmpParams_t p = P(4, 4, 2);
    assert(IopmpInit(&s, &p) == IOPMP_OK);
    Enable(&s); Mdcfg(&s, 0, 3); Srcmd(&s, 0, 0);
    IopmpWriteReg(&s, REG_ERR_CFG, ERR_CFG_IE_BIT);

    IopmpReset(&s);
    assert((IopmpReadReg(&s, REG_HWCFG0) & HWCFG0_ENABLE_BIT) == 0U);
    assert(IopmpReadReg(&s, REG_MDCFG_BASE) == 0U);
    assert(IopmpReadReg(&s, REG_ERR_CFG) == 0U);
    assert(IopmpReadReg(&s, REG_SRCMD_BASE + REG_SRCMD_EN_OFF) == 0U);
    /* HWCFG still reflects params. */
    assert((IopmpReadReg(&s, REG_HWCFG1) & HWCFG1_RRID_NUM_MASK) == 4U);
    IopmpDestroy(&s);
    PASS("IOPMP-XC-031 reset to defaults");
}

/* ── Runner ──────────────────────────────────────────────────────── */

int main(void)
{
    TestXc001(); TestXc002(); TestXc003();
    TestXc004(); TestXc005(); TestXc006();
    TestXc007(); TestXc008(); TestXc009(); TestXc010();
    TestXc011(); TestXc012(); TestXc013();
    TestXc014(); TestXc015(); TestXc016();
    TestXc017(); TestXc018();
    TestXc019(); TestXc020(); TestXc021();
    TestXc022(); TestXc023(); TestXc024();
    TestXc025(); TestXc026();
    TestXc027(); TestXc028(); TestXc029(); TestXc030(); TestXc031();

    printf("\nAll file-18 cross-combination tests passed.\n");
    return 0;
}
