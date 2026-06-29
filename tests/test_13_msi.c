/*
 * test_13_msi.c
 *
 * Test suite for docs/testplan/13-msi.md.
 *
 * Spec: §5.6 (MSI), §5.1.7 (ERR_MSIADDR/H), §5.1.4 (msi_sel, msidata),
 *       §5.1.5 (ERR_INFO.msi_werr), §5.1.1 (HWCFG2.msi_en).
 *
 * SPEC-compliant. When msi_en=1 and ERR_CFG.msi_sel=1, a violation delivers
 * the interrupt by writing msidata to ERR_MSIADDR(/H) rather than asserting a
 * wired interrupt. The model exposes the pending MSI via IopmpIsMsiPending and
 * the target/payload via ERR_MSIADDR(/H) and ERR_CFG.msidata.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include "iopmp.h"
#include "test_utils.h"

/* ── Shared setup helpers ────────────────────────────────────────────── */

static IopmpParams_t MakeMsiParams(void)
{
    IopmpParams_t params;
    memset(&params, 0, sizeof(params));
    params.rridNum  = 4;
    params.entryNum = 4;
    params.mdNum    = 1;
    params.torEn    = true;
    params.hwcfg2En = true;
    params.msiEn    = true;
    params.model    = IOPMP_MODEL_FULL;
    return params;
}

static void EnableIopmp(IopmpState_t *iopmp) { IopmpWriteReg(iopmp, REG_HWCFG0, HWCFG0_ENABLE_BIT); }

/* Wire RRID0 -> MD0; entry 0 denies reads. Returns with given ERR_CFG written. */
static void WireDeny(IopmpState_t *iopmp, uint32_t errCfgExtra)
{
    EnableIopmp(iopmp);
    IopmpWriteReg(iopmp, REG_MDCFG_BASE, 4U);
    IopmpWriteReg(iopmp, REG_SRCMD_BASE + REG_SRCMD_EN_OFF, (1U << 1));
    uint32_t base = IopmpReadReg(iopmp, REG_ENTRYOFFSET);
    IopmpWriteReg(iopmp, base + REG_ENTRY_ADDR_OFF, (uint32_t)(0x1000ULL >> 2U));
    IopmpWriteReg(iopmp, base + REG_ENTRY_CFG_OFF,
                  (ADDR_MODE_NA4 << ENTRY_CFG_A_SHIFT) | ENTRY_CFG_W_BIT);
    IopmpWriteReg(iopmp, REG_ERR_CFG, errCfgExtra);
}
static void Violate(IopmpState_t *iopmp)
{
    IopmpCheckAccess(iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ);
}

/* ───────────────────────────────────────────────────────────────────
 * 13.1 Capability & register presence
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-MSI-001 - msi_en=1: ERR_MSIADDR + msi_sel/msidata implemented. */
static void TestMsi001_Implemented(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeMsiParams();
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    assert((IopmpReadReg(&iopmp, REG_HWCFG2) & HWCFG2_MSI_EN_BIT) != 0U);

    IopmpWriteReg(&iopmp, REG_ERR_MSIADDR, 0xFEE00000U);
    assert(IopmpReadReg(&iopmp, REG_ERR_MSIADDR) == 0xFEE00000U);
    IopmpWriteReg(&iopmp, REG_ERR_CFG, ERR_CFG_MSI_SEL_BIT | (0x123U << ERR_CFG_MSIDATA_SHIFT));
    uint32_t cfg = IopmpReadReg(&iopmp, REG_ERR_CFG);
    assert((cfg & ERR_CFG_MSI_SEL_BIT) != 0U);
    assert(((cfg & ERR_CFG_MSIDATA_MASK) >> ERR_CFG_MSIDATA_SHIFT) == 0x123U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MSI-001 MSI implemented");
}

/* IOPMP-MSI-002 - msi_en=0: MSI regs not implemented; msi_sel stays 0. */
static void TestMsi002_Absent(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeMsiParams();
    params.msiEn = false;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    IopmpWriteReg(&iopmp, REG_ERR_MSIADDR, 0xFEE00000U);
    assert(IopmpReadReg(&iopmp, REG_ERR_MSIADDR) == 0U);
    IopmpWriteReg(&iopmp, REG_ERR_CFG, ERR_CFG_MSI_SEL_BIT);
    assert((IopmpReadReg(&iopmp, REG_ERR_CFG) & ERR_CFG_MSI_SEL_BIT) == 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MSI-002 MSI absent");
}

/* IOPMP-MSI-003 - addrh_en=1 & msi_en=1: ERR_MSIADDRH implemented. */
static void TestMsi003_AddrhImplemented(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeMsiParams();
    params.addrhEn = true;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    IopmpWriteReg(&iopmp, REG_ERR_MSIADDRH, 0x2U);
    assert(IopmpReadReg(&iopmp, REG_ERR_MSIADDRH) == 0x2U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MSI-003 ERR_MSIADDRH implemented");
}

/* IOPMP-MSI-004 - addrh_en=0: ERR_MSIADDRH not implemented. */
static void TestMsi004_AddrhAbsent(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeMsiParams();        /* addrhEn=false */
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    IopmpWriteReg(&iopmp, REG_ERR_MSIADDRH, 0xFFFFFFFFU);
    assert(IopmpReadReg(&iopmp, REG_ERR_MSIADDRH) == 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MSI-004 ERR_MSIADDRH absent (addrh_en=0)");
}

/* ───────────────────────────────────────────────────────────────────
 * 13.2 Interrupt delivery mode select
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-MSI-005 - msi_sel=0, ie=1: wired interrupt, no MSI. */
static void TestMsi005_WiredWhenSelClear(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeMsiParams();
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    WireDeny(&iopmp, ERR_CFG_IE_BIT);                /* msi_sel=0 */

    Violate(&iopmp);
    assert(IopmpIsIrqPending(&iopmp));
    assert(!IopmpIsMsiPending(&iopmp));

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MSI-005 wired interrupt when msi_sel=0");
}

/* IOPMP-MSI-006 - msi_sel=1, ie=1: MSI write of msidata to ERR_MSIADDR. */
static void TestMsi006_MsiIssued(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeMsiParams();
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    EnableIopmp(&iopmp);
    IopmpWriteReg(&iopmp, REG_MDCFG_BASE, 4U);
    IopmpWriteReg(&iopmp, REG_SRCMD_BASE + REG_SRCMD_EN_OFF, (1U << 1));
    uint32_t base = IopmpReadReg(&iopmp, REG_ENTRYOFFSET);
    IopmpWriteReg(&iopmp, base + REG_ENTRY_ADDR_OFF, (uint32_t)(0x1000ULL >> 2U));
    IopmpWriteReg(&iopmp, base + REG_ENTRY_CFG_OFF, (ADDR_MODE_NA4 << ENTRY_CFG_A_SHIFT) | ENTRY_CFG_W_BIT);
    IopmpWriteReg(&iopmp, REG_ERR_MSIADDR, 0xFEE01000U);
    IopmpWriteReg(&iopmp, REG_ERR_CFG, ERR_CFG_IE_BIT | ERR_CFG_MSI_SEL_BIT | (0x55U << ERR_CFG_MSIDATA_SHIFT));

    Violate(&iopmp);
    assert(IopmpIsMsiPending(&iopmp));
    assert(!IopmpIsIrqPending(&iopmp));
    assert(IopmpReadReg(&iopmp, REG_ERR_MSIADDR) == 0xFEE01000U);   /* target */
    assert(((IopmpReadReg(&iopmp, REG_ERR_CFG) & ERR_CFG_MSIDATA_MASK) >> ERR_CFG_MSIDATA_SHIFT)
           == 0x55U);                                                /* payload */

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MSI-006 MSI issued with data");
}

/* IOPMP-MSI-007 - msi_sel WARL programmable (write 1 then 0). */
static void TestMsi007_MsiSelWarl(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeMsiParams();
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    IopmpWriteReg(&iopmp, REG_ERR_CFG, ERR_CFG_MSI_SEL_BIT);
    assert((IopmpReadReg(&iopmp, REG_ERR_CFG) & ERR_CFG_MSI_SEL_BIT) != 0U);
    IopmpWriteReg(&iopmp, REG_ERR_CFG, 0U);
    assert((IopmpReadReg(&iopmp, REG_ERR_CFG) & ERR_CFG_MSI_SEL_BIT) == 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MSI-007 msi_sel WARL");
}

/* IOPMP-MSI-008 - msi_sel=1, ie=0: no MSI (interrupt globally disabled). */
static void TestMsi008_NoMsiWhenIeClear(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeMsiParams();
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    WireDeny(&iopmp, ERR_CFG_MSI_SEL_BIT);           /* ie=0 */

    Violate(&iopmp);
    assert(!IopmpIsMsiPending(&iopmp));

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MSI-008 no MSI when ie=0");
}

/* ───────────────────────────────────────────────────────────────────
 * 13.3 MSI address composition
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-MSI-009 - addrh_en=0: ERR_MSIADDR holds the (low) target. */
static void TestMsi009_LowTarget(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeMsiParams();
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    IopmpWriteReg(&iopmp, REG_ERR_MSIADDR, 0x8000ABCDU);
    assert(IopmpReadReg(&iopmp, REG_ERR_MSIADDR) == 0x8000ABCDU);
    assert(IopmpReadReg(&iopmp, REG_ERR_MSIADDRH) == 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MSI-009 low target only");
}

/* IOPMP-MSI-010 - addrh_en=1: low + high target combine. */
static void TestMsi010_HighTarget(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeMsiParams();
    params.addrhEn = true;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    IopmpWriteReg(&iopmp, REG_ERR_MSIADDR, 0x00001000U);
    IopmpWriteReg(&iopmp, REG_ERR_MSIADDRH, 0x00000002U);
    uint64_t target = (uint64_t)IopmpReadReg(&iopmp, REG_ERR_MSIADDR)
                    | ((uint64_t)IopmpReadReg(&iopmp, REG_ERR_MSIADDRH) << 32U);
    assert(target == 0x200001000ULL);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MSI-010 high+low target combine");
}

/* IOPMP-MSI-011 - ERR_MSIADDR WARL read-back. */
static void TestMsi011_AddrWarl(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeMsiParams();
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    IopmpWriteReg(&iopmp, REG_ERR_MSIADDR, 0x12345678U);
    assert(IopmpReadReg(&iopmp, REG_ERR_MSIADDR) == 0x12345678U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MSI-011 ERR_MSIADDR WARL");
}

/* ───────────────────────────────────────────────────────────────────
 * 13.4 msidata & msi_werr
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-MSI-012 - msidata WARL [18:8]. */
static void TestMsi012_MsidataWarl(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeMsiParams();
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    IopmpWriteReg(&iopmp, REG_ERR_CFG, 0x7FFU << ERR_CFG_MSIDATA_SHIFT);   /* 11 bits */
    assert(((IopmpReadReg(&iopmp, REG_ERR_CFG) & ERR_CFG_MSIDATA_MASK) >> ERR_CFG_MSIDATA_SHIFT)
           == 0x7FFU);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MSI-012 msidata WARL");
}

/* IOPMP-MSI-013 - successful MSI: msi_werr=0. */
static void TestMsi013_WerrClearOnSuccess(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeMsiParams();           /* no injected failure */
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    WireDeny(&iopmp, ERR_CFG_IE_BIT | ERR_CFG_MSI_SEL_BIT);
    IopmpWriteReg(&iopmp, REG_ERR_MSIADDR, 0xFEE00000U);

    Violate(&iopmp);
    assert(IopmpIsMsiPending(&iopmp));
    assert((IopmpReadReg(&iopmp, REG_ERR_INFO) & ERR_INFO_MSI_WERR_BIT) == 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MSI-013 msi_werr=0 on success");
}

/* IOPMP-MSI-014 - failed MSI write: msi_werr=1. */
static void TestMsi014_WerrSetOnFailure(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeMsiParams();
    params.msiInjectWriteErr = true;                  /* model the MSI write failing */
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    WireDeny(&iopmp, ERR_CFG_IE_BIT | ERR_CFG_MSI_SEL_BIT);
    IopmpWriteReg(&iopmp, REG_ERR_MSIADDR, 0xFEE00000U);

    Violate(&iopmp);
    assert((IopmpReadReg(&iopmp, REG_ERR_INFO) & ERR_INFO_MSI_WERR_BIT) != 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MSI-014 msi_werr=1 on failure");
}

/* IOPMP-MSI-015 - msi_werr RW1C. */
static void TestMsi015_WerrRw1c(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeMsiParams();
    params.msiInjectWriteErr = true;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    WireDeny(&iopmp, ERR_CFG_IE_BIT | ERR_CFG_MSI_SEL_BIT);
    IopmpWriteReg(&iopmp, REG_ERR_MSIADDR, 0xFEE00000U);
    Violate(&iopmp);
    assert((IopmpReadReg(&iopmp, REG_ERR_INFO) & ERR_INFO_MSI_WERR_BIT) != 0U);

    IopmpWriteReg(&iopmp, REG_ERR_INFO, ERR_INFO_MSI_WERR_BIT);   /* write 1 -> clear */
    assert((IopmpReadReg(&iopmp, REG_ERR_INFO) & ERR_INFO_MSI_WERR_BIT) == 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MSI-015 msi_werr RW1C");
}

/* IOPMP-MSI-016 - MSI not available: msi_werr wired 0. */
static void TestMsi016_WerrWiredZero(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeMsiParams();
    params.msiEn = false;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    WireDeny(&iopmp, ERR_CFG_IE_BIT);

    Violate(&iopmp);
    assert((IopmpReadReg(&iopmp, REG_ERR_INFO) & ERR_INFO_MSI_WERR_BIT) == 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MSI-016 msi_werr wired 0 without MSI");
}

/* ───────────────────────────────────────────────────────────────────
 * 13.5 Lock interaction
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-MSI-017 - ERR_CFG.l locks MSI address/data/sel. */
static void TestMsi017_LockedByErrCfgL(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeMsiParams();
    params.addrhEn = true;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    IopmpWriteReg(&iopmp, REG_ERR_MSIADDR, 0xFEE00000U);
    IopmpWriteReg(&iopmp, REG_ERR_CFG, ERR_CFG_MSI_SEL_BIT | ERR_CFG_L_BIT);
    uint32_t cfg = IopmpReadReg(&iopmp, REG_ERR_CFG);

    IopmpWriteReg(&iopmp, REG_ERR_MSIADDR, 0xDEADBEEFU);
    IopmpWriteReg(&iopmp, REG_ERR_MSIADDRH, 0x9U);
    IopmpWriteReg(&iopmp, REG_ERR_CFG, 0U);
    assert(IopmpReadReg(&iopmp, REG_ERR_MSIADDR) == 0xFEE00000U);
    assert(IopmpReadReg(&iopmp, REG_ERR_MSIADDRH) == 0U);
    assert(IopmpReadReg(&iopmp, REG_ERR_CFG) == cfg);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MSI-017 MSI regs locked by ERR_CFG.l");
}

/* ───────────────────────────────────────────────────────────────────
 * Cross-combinations
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-MSI-X01 - MSI fires once per capture; clearing v re-arms. */
static void TestMsiX01_OncePerCapture(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeMsiParams();
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    WireDeny(&iopmp, ERR_CFG_IE_BIT | ERR_CFG_MSI_SEL_BIT);
    IopmpWriteReg(&iopmp, REG_ERR_MSIADDR, 0xFEE00000U);

    Violate(&iopmp);
    assert(IopmpIsMsiPending(&iopmp));
    IopmpClearMsi(&iopmp);
    Violate(&iopmp);                                 /* subsequent while v=1: no new MSI */
    assert(!IopmpIsMsiPending(&iopmp));

    IopmpWriteReg(&iopmp, REG_ERR_INFO, ERR_INFO_V_BIT);   /* re-arm */
    Violate(&iopmp);
    assert(IopmpIsMsiPending(&iopmp));               /* fresh capture -> MSI again */

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MSI-X01 MSI once per capture");
}

/* IOPMP-MSI-X02 - matched entry sire=1: no MSI (interrupt suppressed). */
static void TestMsiX02_SuppressedNoMsi(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeMsiParams();
    params.peisEn = true;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    EnableIopmp(&iopmp);
    IopmpWriteReg(&iopmp, REG_MDCFG_BASE, 4U);
    IopmpWriteReg(&iopmp, REG_SRCMD_BASE + REG_SRCMD_EN_OFF, (1U << 1));
    uint32_t base = IopmpReadReg(&iopmp, REG_ENTRYOFFSET);
    IopmpWriteReg(&iopmp, base + REG_ENTRY_ADDR_OFF, (uint32_t)(0x1000ULL >> 2U));
    IopmpWriteReg(&iopmp, base + REG_ENTRY_CFG_OFF,
                  (ADDR_MODE_NA4 << ENTRY_CFG_A_SHIFT) | ENTRY_CFG_W_BIT | ENTRY_CFG_SIRE_BIT);
    IopmpWriteReg(&iopmp, REG_ERR_MSIADDR, 0xFEE00000U);
    IopmpWriteReg(&iopmp, REG_ERR_CFG, ERR_CFG_IE_BIT | ERR_CFG_MSI_SEL_BIT);

    Violate(&iopmp);
    assert(!IopmpIsMsiPending(&iopmp));

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MSI-X02 suppressed -> no MSI");
}

/* IOPMP-MSI-X03 - MSI on first capture; subsequent RRIDs logged in MFR. */
static void TestMsiX03_MsiPlusMfr(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeMsiParams();
    params.multifaultEn = true;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    EnableIopmp(&iopmp);
    IopmpWriteReg(&iopmp, REG_MDCFG_BASE, 4U);
    IopmpWriteReg(&iopmp, REG_SRCMD_BASE + REG_SRCMD_EN_OFF, (1U << 1));
    IopmpWriteReg(&iopmp, REG_SRCMD_BASE + REG_SRCMD_STRIDE + REG_SRCMD_EN_OFF, (1U << 1));
    uint32_t base = IopmpReadReg(&iopmp, REG_ENTRYOFFSET);
    IopmpWriteReg(&iopmp, base + REG_ENTRY_ADDR_OFF, (uint32_t)(0x1000ULL >> 2U));
    IopmpWriteReg(&iopmp, base + REG_ENTRY_CFG_OFF, (ADDR_MODE_NA4 << ENTRY_CFG_A_SHIFT) | ENTRY_CFG_W_BIT);
    IopmpWriteReg(&iopmp, REG_ERR_MSIADDR, 0xFEE00000U);
    IopmpWriteReg(&iopmp, REG_ERR_CFG, ERR_CFG_IE_BIT | ERR_CFG_MSI_SEL_BIT);

    IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ);   /* first -> MSI */
    assert(IopmpIsMsiPending(&iopmp));
    IopmpCheckAccess(&iopmp, 1, 0x1000ULL, 4, IOPMP_TXN_READ);   /* subsequent -> MFR */
    assert((IopmpReadReg(&iopmp, REG_ERR_INFO) & ERR_INFO_SVC_BIT) != 0U);
    assert((IopmpReadReg(&iopmp, REG_ERR_MFR) & ERR_MFR_SVS_BIT) != 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MSI-X03 MSI first + MFR subsequent");
}

/* IOPMP-MSI-X04 - addrh target > 4 GiB uses the high register. */
static void TestMsiX04_HighAddrTarget(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeMsiParams();
    params.addrhEn = true;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    WireDeny(&iopmp, ERR_CFG_IE_BIT | ERR_CFG_MSI_SEL_BIT);
    IopmpWriteReg(&iopmp, REG_ERR_MSIADDR, 0x00000000U);
    IopmpWriteReg(&iopmp, REG_ERR_MSIADDRH, 0x00000002U);   /* target 0x2_0000_0000 */

    Violate(&iopmp);
    assert(IopmpIsMsiPending(&iopmp));
    uint64_t target = (uint64_t)IopmpReadReg(&iopmp, REG_ERR_MSIADDR)
                    | ((uint64_t)IopmpReadReg(&iopmp, REG_ERR_MSIADDRH) << 32U);
    assert(target == 0x200000000ULL);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MSI-X04 high-address MSI target");
}

/* ── Runner ──────────────────────────────────────────────────────── */

int main(void)
{
    TestMsi001_Implemented();
    TestMsi002_Absent();
    TestMsi003_AddrhImplemented();
    TestMsi004_AddrhAbsent();

    TestMsi005_WiredWhenSelClear();
    TestMsi006_MsiIssued();
    TestMsi007_MsiSelWarl();
    TestMsi008_NoMsiWhenIeClear();

    TestMsi009_LowTarget();
    TestMsi010_HighTarget();
    TestMsi011_AddrWarl();

    TestMsi012_MsidataWarl();
    TestMsi013_WerrClearOnSuccess();
    TestMsi014_WerrSetOnFailure();
    TestMsi015_WerrRw1c();
    TestMsi016_WerrWiredZero();

    TestMsi017_LockedByErrCfgL();

    TestMsiX01_OncePerCapture();
    TestMsiX02_SuppressedNoMsi();
    TestMsiX03_MsiPlusMfr();
    TestMsiX04_HighAddrTarget();

    printf("\nAll file-13 MSI tests passed.\n");
    return 0;
}
