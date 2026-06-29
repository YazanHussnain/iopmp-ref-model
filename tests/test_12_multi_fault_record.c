/*
 * test_12_multi_fault_record.c
 *
 * Test suite for docs/testplan/12-multi-fault-record.md.
 *
 * Spec: §5.5 (Multi-Faults Record), §5.1.6 (ERR_MFR svw/svi/svs),
 *       §5.1.5 (ERR_INFO.svc), §5.1.1 (HWCFG2.mfr_en).
 *
 * SPEC-compliant. The primary record (ERR_INFO) locks on the first violation;
 * subsequent violations set a per-RRID SV bit in the MFR log. ERR_MFR is read
 * as 16-RRID sliding windows; reading clears the returned window. ERR_INFO.svc
 * is set while any SV bit remains and clears once the log is drained or v is
 * re-armed.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include "iopmp.h"
#include "test_utils.h"

/* ── Shared setup helpers ────────────────────────────────────────────── */

static IopmpParams_t MakeMfrParams(uint16_t rridNum)
{
    IopmpParams_t params;
    memset(&params, 0, sizeof(params));
    params.rridNum  = rridNum;
    params.entryNum = 4;
    params.mdNum    = 1;
    params.torEn    = true;
    params.hwcfg2En = true;
    params.multifaultEn = true;
    params.model    = IOPMP_MODEL_FULL;
    return params;
}

static void EnableIopmp(IopmpState_t *iopmp) { IopmpWriteReg(iopmp, REG_HWCFG0, HWCFG0_ENABLE_BIT); }

/* Wire all RRIDs -> MD0; entry 0 denies reads (matched but no permission). */
static void WireDenyAll(IopmpState_t *iopmp, uint16_t rridNum)
{
    EnableIopmp(iopmp);
    IopmpWriteReg(iopmp, REG_MDCFG_BASE, 4U);
    for (uint16_t s = 0U; s < rridNum; s++) {
        uint32_t off = REG_SRCMD_BASE + (uint32_t)s * REG_SRCMD_STRIDE + REG_SRCMD_EN_OFF;
        IopmpWriteReg(iopmp, off, (1U << 1));            /* MD0 */
    }
    uint32_t base = IopmpReadReg(iopmp, REG_ENTRYOFFSET);
    IopmpWriteReg(iopmp, base + REG_ENTRY_ADDR_OFF, (uint32_t)(0x1000ULL >> 2U));
    IopmpWriteReg(iopmp, base + REG_ENTRY_CFG_OFF,
                  (ADDR_MODE_NA4 << ENTRY_CFG_A_SHIFT) | ENTRY_CFG_W_BIT);  /* no read */
}

static void Violate(IopmpState_t *iopmp, uint16_t rrid)
{
    IopmpCheckAccess(iopmp, rrid, 0x1000ULL, 4, IOPMP_TXN_READ);
}
static bool Svc(IopmpState_t *iopmp)
{
    return (IopmpReadReg(iopmp, REG_ERR_INFO) & ERR_INFO_SVC_BIT) != 0U;
}

/* ───────────────────────────────────────────────────────────────────
 * 12.1 Capability
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-MFR-001 - mfr_en=1: ERR_MFR / svc implemented. */
static void TestMfr001_Implemented(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeMfrParams(8);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    assert((IopmpReadReg(&iopmp, REG_HWCFG2) & HWCFG2_MFR_EN_BIT) != 0U);

    WireDenyAll(&iopmp, 8);
    Violate(&iopmp, 3);                                  /* first */
    Violate(&iopmp, 7);                                  /* subsequent */
    assert(Svc(&iopmp));
    assert((IopmpReadReg(&iopmp, REG_ERR_MFR) & ERR_MFR_SVS_BIT) != 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MFR-001 MFR implemented");
}

/* IOPMP-MFR-002 - mfr_en=0: svc wired 0, ERR_MFR not implemented. */
static void TestMfr002_Absent(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeMfrParams(8);
    params.multifaultEn = false;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    WireDenyAll(&iopmp, 8);
    Violate(&iopmp, 3);
    Violate(&iopmp, 7);
    assert(!Svc(&iopmp));
    assert(IopmpReadReg(&iopmp, REG_ERR_MFR) == 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MFR-002 MFR absent");
}

/* ───────────────────────────────────────────────────────────────────
 * 12.2 Subsequent-violation logging
 * ─────────────────────────────────────────────────────────────────── */

/* Read ERR_MFR windows and OR together the RRIDs found (drains the log). */
static uint64_t DrainMfr(IopmpState_t *iopmp)
{
    uint64_t found = 0ULL;
    for (int i = 0; i < 8; i++) {
        uint32_t mfr = IopmpReadReg(iopmp, REG_ERR_MFR);
        if (!(mfr & ERR_MFR_SVS_BIT)) break;
        uint32_t svi = (mfr & ERR_MFR_SVI_MASK) >> ERR_MFR_SVI_SHIFT;
        uint32_t svw = (mfr & ERR_MFR_SVW_MASK);
        for (uint32_t j = 0U; j < 16U; j++)
            if (svw & (1U << j)) found |= (1ULL << (svi * 16U + j));
    }
    return found;
}

/* IOPMP-MFR-003 - first RRID3, subsequent RRID7: ERR_INFO unchanged, SV[7], svc. */
static void TestMfr003_Subsequent(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeMfrParams(8);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    WireDenyAll(&iopmp, 8);

    Violate(&iopmp, 3);
    uint32_t reqid = IopmpReadReg(&iopmp, REG_ERR_REQID);
    Violate(&iopmp, 7);
    assert(IopmpReadReg(&iopmp, REG_ERR_REQID) == reqid);   /* primary unchanged */
    assert(Svc(&iopmp));
    assert(DrainMfr(&iopmp) == (1ULL << 7));

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MFR-003 subsequent logs SV[7]");
}

/* IOPMP-MFR-004 - same RRID twice: single SV bit. */
static void TestMfr004_SameRridOnce(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeMfrParams(8);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    WireDenyAll(&iopmp, 8);

    Violate(&iopmp, 3);
    Violate(&iopmp, 7);
    Violate(&iopmp, 7);
    assert(DrainMfr(&iopmp) == (1ULL << 7));

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MFR-004 same RRID single bit");
}

/* IOPMP-MFR-005 - multiple RRIDs 7,20,35. */
static void TestMfr005_MultipleRrids(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeMfrParams(40);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    WireDenyAll(&iopmp, 40);

    Violate(&iopmp, 3);                                  /* first */
    Violate(&iopmp, 7);
    Violate(&iopmp, 20);
    Violate(&iopmp, 35);
    assert(Svc(&iopmp));
    uint64_t found = DrainMfr(&iopmp);
    assert(found == ((1ULL << 7) | (1ULL << 20) | (1ULL << 35)));

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MFR-005 multiple RRIDs logged");
}

/* IOPMP-MFR-006 - v=0: violation goes to ERR_INFO (first); SV not set, svc=0. */
static void TestMfr006_FirstNoSv(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeMfrParams(8);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    WireDenyAll(&iopmp, 8);

    Violate(&iopmp, 3);
    assert((IopmpReadReg(&iopmp, REG_ERR_INFO) & ERR_INFO_V_BIT) != 0U);
    assert(!Svc(&iopmp));
    assert((IopmpReadReg(&iopmp, REG_ERR_MFR) & ERR_MFR_SVS_BIT) == 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MFR-006 first violation no SV");
}

/* IOPMP-MFR-007 - first-capturing RRID violates again: its SV bit set. */
static void TestMfr007_OwnSubsequent(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeMfrParams(8);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    WireDenyAll(&iopmp, 8);

    Violate(&iopmp, 3);                                  /* first */
    Violate(&iopmp, 3);                                  /* subsequent, same RRID */
    assert(Svc(&iopmp));
    assert(DrainMfr(&iopmp) == (1ULL << 3));

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MFR-007 own subsequent logged");
}

/* ───────────────────────────────────────────────────────────────────
 * 12.3 ERR_MFR window read & scan
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-MFR-008 - SV[7] only: window 0, bit 7, svs=1, svi=0. */
static void TestMfr008_Window0(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeMfrParams(16);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    WireDenyAll(&iopmp, 16);

    Violate(&iopmp, 3);
    Violate(&iopmp, 7);
    IopmpWriteReg(&iopmp, REG_ERR_MFR, 0U << ERR_MFR_SVI_SHIFT);   /* svi=0 */
    uint32_t mfr = IopmpReadReg(&iopmp, REG_ERR_MFR);
    assert((mfr & ERR_MFR_SVS_BIT) != 0U);
    assert(((mfr & ERR_MFR_SVI_MASK) >> ERR_MFR_SVI_SHIFT) == 0U);
    assert((mfr & ERR_MFR_SVW_MASK) == (1U << 7));

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MFR-008 window 0 read");
}

/* IOPMP-MFR-009 - SV[35]: scan to window 2, bit 3. */
static void TestMfr009_Window2(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeMfrParams(40);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    WireDenyAll(&iopmp, 40);

    Violate(&iopmp, 3);
    Violate(&iopmp, 35);                                 /* window 2 (32-47), bit 3 */
    IopmpWriteReg(&iopmp, REG_ERR_MFR, 0U);
    uint32_t mfr = IopmpReadReg(&iopmp, REG_ERR_MFR);
    assert((mfr & ERR_MFR_SVS_BIT) != 0U);
    assert(((mfr & ERR_MFR_SVI_MASK) >> ERR_MFR_SVI_SHIFT) == 2U);
    assert((mfr & ERR_MFR_SVW_MASK) == (1U << 3));

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MFR-009 scan to window 2");
}

/* IOPMP-MFR-010 - no SV bits: svs=0, svw=0. */
static void TestMfr010_Empty(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeMfrParams(8);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    WireDenyAll(&iopmp, 8);
    Violate(&iopmp, 3);                                  /* only first capture */

    uint32_t mfr = IopmpReadReg(&iopmp, REG_ERR_MFR);
    assert((mfr & ERR_MFR_SVS_BIT) == 0U);
    assert((mfr & ERR_MFR_SVW_MASK) == 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MFR-010 empty log svs=0");
}

/* IOPMP-MFR-011 - clear-on-read: second read of same window is 0. */
static void TestMfr011_ClearOnRead(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeMfrParams(16);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    WireDenyAll(&iopmp, 16);
    Violate(&iopmp, 3);
    Violate(&iopmp, 7);

    IopmpWriteReg(&iopmp, REG_ERR_MFR, 0U);
    uint32_t first = IopmpReadReg(&iopmp, REG_ERR_MFR);
    assert((first & ERR_MFR_SVS_BIT) != 0U);
    IopmpWriteReg(&iopmp, REG_ERR_MFR, 0U);              /* rescan from window 0 */
    uint32_t second = IopmpReadReg(&iopmp, REG_ERR_MFR);
    assert((second & ERR_MFR_SVS_BIT) == 0U);            /* window cleared */

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MFR-011 clear-on-read");
}

/* IOPMP-MFR-012 - windows 1 and 3, svi=2: scan from 2 forward finds window 3. */
static void TestMfr012_ScanForward(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeMfrParams(64);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    WireDenyAll(&iopmp, 64);

    Violate(&iopmp, 0);                                  /* first */
    Violate(&iopmp, 18);                                 /* window 1, bit 2 */
    Violate(&iopmp, 50);                                 /* window 3, bit 2 */
    IopmpWriteReg(&iopmp, REG_ERR_MFR, 2U << ERR_MFR_SVI_SHIFT);   /* start window 2 */
    uint32_t mfr = IopmpReadReg(&iopmp, REG_ERR_MFR);
    assert((mfr & ERR_MFR_SVS_BIT) != 0U);
    assert(((mfr & ERR_MFR_SVI_MASK) >> ERR_MFR_SVI_SHIFT) == 3U);  /* window 3 */

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MFR-012 scan forward from svi");
}

/* IOPMP-MFR-013 - wrap-around: from window 2, the set window 1 is found via wrap. */
static void TestMfr013_Wrap(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeMfrParams(64);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    WireDenyAll(&iopmp, 64);

    Violate(&iopmp, 0);                                  /* first */
    Violate(&iopmp, 18);                                 /* window 1 only */
    IopmpWriteReg(&iopmp, REG_ERR_MFR, 2U << ERR_MFR_SVI_SHIFT);   /* start past it */
    uint32_t mfr = IopmpReadReg(&iopmp, REG_ERR_MFR);
    assert((mfr & ERR_MFR_SVS_BIT) != 0U);
    assert(((mfr & ERR_MFR_SVI_MASK) >> ERR_MFR_SVI_SHIFT) == 1U);  /* wrapped to 1 */

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MFR-013 wrap-around scan");
}

/* IOPMP-MFR-014 - svi WARL [27:16]: write 5, read back (no SV -> unchanged). */
static void TestMfr014_SviWarl(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeMfrParams(128);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    IopmpWriteReg(&iopmp, REG_ERR_MFR, 5U << ERR_MFR_SVI_SHIFT);
    uint32_t mfr = IopmpReadReg(&iopmp, REG_ERR_MFR);
    assert(((mfr & ERR_MFR_SVI_MASK) >> ERR_MFR_SVI_SHIFT) == 5U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MFR-014 svi WARL");
}

/* ───────────────────────────────────────────────────────────────────
 * 12.4 svc interaction
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-MFR-015 - svc=1 while subsequent violations exist. */
static void TestMfr015_SvcSet(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeMfrParams(8);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    WireDenyAll(&iopmp, 8);
    Violate(&iopmp, 3);
    Violate(&iopmp, 7);
    assert(Svc(&iopmp));

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MFR-015 svc=1 with subsequent");
}

/* IOPMP-MFR-016 - svc clears once the log is fully drained. */
static void TestMfr016_SvcClearsOnDrain(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeMfrParams(8);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    WireDenyAll(&iopmp, 8);
    Violate(&iopmp, 3);
    Violate(&iopmp, 7);
    assert(Svc(&iopmp));

    (void)DrainMfr(&iopmp);                              /* read out all windows */
    assert(!Svc(&iopmp));                                /* svc reflects empty log */

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MFR-016 svc clears when log drained");
}

/* IOPMP-MFR-017 - clearing v re-arms; the MFR log resets per capture cycle. */
static void TestMfr017_ResetPerCycle(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeMfrParams(8);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    WireDenyAll(&iopmp, 8);

    Violate(&iopmp, 3);
    Violate(&iopmp, 7);                                  /* SV[7] logged */
    IopmpWriteReg(&iopmp, REG_ERR_INFO, ERR_INFO_V_BIT); /* clear v (re-arm) */

    /* New capture cycle: stale SV[7] must be gone. */
    Violate(&iopmp, 5);                                  /* new first capture */
    assert(!Svc(&iopmp));
    assert((IopmpReadReg(&iopmp, REG_ERR_MFR) & ERR_MFR_SVS_BIT) == 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MFR-017 log resets per capture cycle");
}

/* ───────────────────────────────────────────────────────────────────
 * Cross-combinations
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-MFR-X01 - ERR_INFO=A; MFR={B,C}. */
static void TestMfrX01_FullPicture(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeMfrParams(8);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    WireDenyAll(&iopmp, 8);

    Violate(&iopmp, 2);                                  /* A */
    Violate(&iopmp, 4);                                  /* B */
    Violate(&iopmp, 6);                                  /* C */
    assert((IopmpReadReg(&iopmp, REG_ERR_REQID) & ERR_REQID_RRID_MASK) == 2U);
    assert(DrainMfr(&iopmp) == ((1ULL << 4) | (1ULL << 6)));

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MFR-X01 ERR_INFO=A, MFR={B,C}");
}

/* IOPMP-MFR-X02 - non-priority multi-match illegal from several RRIDs. */
static void TestMfrX02_NonPrio(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeMfrParams(8);
    params.nonPrioEn = true;
    params.prioEntry = 0;                                /* all non-priority */
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    EnableIopmp(&iopmp);
    IopmpWriteReg(&iopmp, REG_MDCFG_BASE, 4U);
    for (uint16_t s = 0U; s < 8U; s++)
        IopmpWriteReg(&iopmp, REG_SRCMD_BASE + (uint32_t)s * REG_SRCMD_STRIDE + REG_SRCMD_EN_OFF, (1U << 1));
    uint32_t base = IopmpReadReg(&iopmp, REG_ENTRYOFFSET);
    /* NAPOT 16 bytes, write-only (denies read), covers all. */
    IopmpWriteReg(&iopmp, base + REG_ENTRY_ADDR_OFF, (uint32_t)(0x1000ULL >> 2U) | 1U);
    IopmpWriteReg(&iopmp, base + REG_ENTRY_CFG_OFF, (ADDR_MODE_NAPOT << ENTRY_CFG_A_SHIFT) | ENTRY_CFG_W_BIT);

    Violate(&iopmp, 1);                                  /* first */
    Violate(&iopmp, 5);                                  /* subsequent */
    assert(DrainMfr(&iopmp) == (1ULL << 5));

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MFR-X02 non-priority subsequent logged");
}

/* IOPMP-MFR-X03 - fully suppressed subsequent violation still records its SV bit. */
static void TestMfrX03_SuppressedStillLogs(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeMfrParams(8);
    params.peisEn = true;
    params.peesEn = true;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    EnableIopmp(&iopmp);
    IopmpWriteReg(&iopmp, REG_MDCFG_BASE, 4U);
    for (uint16_t s = 0U; s < 8U; s++)
        IopmpWriteReg(&iopmp, REG_SRCMD_BASE + (uint32_t)s * REG_SRCMD_STRIDE + REG_SRCMD_EN_OFF, (1U << 1));
    IopmpWriteReg(&iopmp, REG_ERR_CFG, ERR_CFG_IE_BIT);
    uint32_t base = IopmpReadReg(&iopmp, REG_ENTRYOFFSET);
    IopmpWriteReg(&iopmp, base + REG_ENTRY_ADDR_OFF, (uint32_t)(0x1000ULL >> 2U));
    IopmpWriteReg(&iopmp, base + REG_ENTRY_CFG_OFF,
                  (ADDR_MODE_NA4 << ENTRY_CFG_A_SHIFT) | ENTRY_CFG_W_BIT
                  | ENTRY_CFG_SIRE_BIT | ENTRY_CFG_SERE_BIT);

    Violate(&iopmp, 3);                                  /* first */
    Violate(&iopmp, 7);                                  /* subsequent, suppressed */
    /* The model records the subsequent RRID regardless of reaction suppression. */
    assert(DrainMfr(&iopmp) == (1ULL << 7));

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MFR-X03 suppressed subsequent still logged");
}

/* IOPMP-MFR-X04 - MSI mode: first triggers MSI, subsequent logged in MFR. */
static void TestMfrX04_MsiPlusMfr(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeMfrParams(8);
    params.msiEn = true;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    EnableIopmp(&iopmp);
    IopmpWriteReg(&iopmp, REG_MDCFG_BASE, 4U);
    for (uint16_t s = 0U; s < 8U; s++)
        IopmpWriteReg(&iopmp, REG_SRCMD_BASE + (uint32_t)s * REG_SRCMD_STRIDE + REG_SRCMD_EN_OFF, (1U << 1));
    IopmpWriteReg(&iopmp, REG_ERR_MSIADDR, 0xFEE00000U);
    IopmpWriteReg(&iopmp, REG_ERR_CFG, ERR_CFG_IE_BIT | ERR_CFG_MSI_SEL_BIT);
    uint32_t base = IopmpReadReg(&iopmp, REG_ENTRYOFFSET);
    IopmpWriteReg(&iopmp, base + REG_ENTRY_ADDR_OFF, (uint32_t)(0x1000ULL >> 2U));
    IopmpWriteReg(&iopmp, base + REG_ENTRY_CFG_OFF, (ADDR_MODE_NA4 << ENTRY_CFG_A_SHIFT) | ENTRY_CFG_W_BIT);

    Violate(&iopmp, 3);                                  /* first -> MSI */
    assert(IopmpIsMsiPending(&iopmp));
    Violate(&iopmp, 7);                                  /* subsequent -> MFR */
    assert(DrainMfr(&iopmp) == (1ULL << 7));

    IopmpDestroy(&iopmp);
    PASS("IOPMP-MFR-X04 MSI first + MFR subsequent");
}

/* ── Runner ──────────────────────────────────────────────────────── */

int main(void)
{
    TestMfr001_Implemented();
    TestMfr002_Absent();

    TestMfr003_Subsequent();
    TestMfr004_SameRridOnce();
    TestMfr005_MultipleRrids();
    TestMfr006_FirstNoSv();
    TestMfr007_OwnSubsequent();

    TestMfr008_Window0();
    TestMfr009_Window2();
    TestMfr010_Empty();
    TestMfr011_ClearOnRead();
    TestMfr012_ScanForward();
    TestMfr013_Wrap();
    TestMfr014_SviWarl();

    TestMfr015_SvcSet();
    TestMfr016_SvcClearsOnDrain();
    TestMfr017_ResetPerCycle();

    TestMfrX01_FullPicture();
    TestMfrX02_NonPrio();
    TestMfrX03_SuppressedStillLogs();
    TestMfrX04_MsiPlusMfr();

    printf("\nAll file-12 multi-fault record tests passed.\n");
    return 0;
}
