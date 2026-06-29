/*
 * test_11_per_entry_suppression.c
 *
 * Test suite for docs/testplan/11-per-entry-suppression.md.
 *
 * Spec: §5.4 (per-entry interrupt/bus-error suppression), §5.1.11
 *       (ENTRY_CFG sire/siwe/sixe/sere/sewe/sexe), §5.1.1 (peis, pees).
 *
 * SPEC-compliant. Suppression changes only the reaction, never legality:
 *   interrupt fires  iff ERR_CFG.ie && !si<r/w/x>e of the matched entry
 *   bus error returns iff !ERR_CFG.rs && !se<r/w/x>e of the matched entry
 * For non-priority multi-matches, suppress only if EVERY matched entry suppresses.
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
    params.hwcfg2En = true;
    params.peisEn   = true;
    params.peesEn   = true;
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
static uint32_t EntryCfgOff(IopmpState_t *iopmp, uint32_t idx)
{
    return IopmpReadReg(iopmp, REG_ENTRYOFFSET) + idx * REG_ENTRY_STRIDE + REG_ENTRY_CFG_OFF;
}
static void SetNa4(IopmpState_t *iopmp, uint32_t idx, uint64_t addr, uint32_t perm)
{
    uint32_t base = IopmpReadReg(iopmp, REG_ENTRYOFFSET) + idx * REG_ENTRY_STRIDE;
    IopmpWriteReg(iopmp, base + REG_ENTRY_ADDR_OFF, (uint32_t)(addr >> 2U));
    IopmpWriteReg(iopmp, base + REG_ENTRY_CFG_OFF, (ADDR_MODE_NA4 << ENTRY_CFG_A_SHIFT) | perm);
}
static void SetNapot(IopmpState_t *iopmp, uint32_t idx, uint64_t addr, uint32_t size, uint32_t perm)
{
    uint32_t k = 0U; while ((8U << k) < size) k++;
    uint32_t base = IopmpReadReg(iopmp, REG_ENTRYOFFSET) + idx * REG_ENTRY_STRIDE;
    IopmpWriteReg(iopmp, base + REG_ENTRY_ADDR_OFF, (uint32_t)(addr >> 2U) | ((1U << k) - 1U));
    IopmpWriteReg(iopmp, base + REG_ENTRY_CFG_OFF, (ADDR_MODE_NAPOT << ENTRY_CFG_A_SHIFT) | perm);
}
/* Wire RRID0 -> MD0 owning all entries, enabled, with given ERR_CFG. */
static void Wire(IopmpState_t *iopmp, uint16_t entryNum, uint32_t errCfg)
{
    EnableIopmp(iopmp);
    SetupMdcfg(iopmp, 0, entryNum);
    SetupSrcmd(iopmp, 0, 0);
    IopmpWriteReg(iopmp, REG_ERR_CFG, errCfg);
}

/* ───────────────────────────────────────────────────────────────────
 * 11.1 Capability
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-SUPP-001 - peis=1: sire/siwe/sixe writable. */
static void TestSupp001_PeisImplemented(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    assert((IopmpReadReg(&iopmp, REG_HWCFG2) & HWCFG2_PEIS_EN_BIT) != 0U);

    uint32_t off = EntryCfgOff(&iopmp, 0);
    IopmpWriteReg(&iopmp, off, ENTRY_CFG_SI_MASK | (ADDR_MODE_NA4 << ENTRY_CFG_A_SHIFT));
    assert((IopmpReadReg(&iopmp, off) & ENTRY_CFG_SI_MASK) == ENTRY_CFG_SI_MASK);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-SUPP-001 peis bits implemented");
}

/* IOPMP-SUPP-002 - peis=0: sire/siwe/sixe wired 0. */
static void TestSupp002_PeisAbsent(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    params.peisEn = false;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    uint32_t off = EntryCfgOff(&iopmp, 0);
    IopmpWriteReg(&iopmp, off, ENTRY_CFG_SI_MASK | ENTRY_CFG_R_BIT);
    assert((IopmpReadReg(&iopmp, off) & ENTRY_CFG_SI_MASK) == 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-SUPP-002 peis=0 si bits wired 0");
}

/* IOPMP-SUPP-003 - pees=1: sere/sewe/sexe writable. */
static void TestSupp003_PeesImplemented(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    assert((IopmpReadReg(&iopmp, REG_HWCFG2) & HWCFG2_PEES_EN_BIT) != 0U);

    uint32_t off = EntryCfgOff(&iopmp, 0);
    IopmpWriteReg(&iopmp, off, ENTRY_CFG_SE_MASK | (ADDR_MODE_NA4 << ENTRY_CFG_A_SHIFT));
    assert((IopmpReadReg(&iopmp, off) & ENTRY_CFG_SE_MASK) == ENTRY_CFG_SE_MASK);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-SUPP-003 pees bits implemented");
}

/* IOPMP-SUPP-004 - pees=0: sere/sewe/sexe wired 0. */
static void TestSupp004_PeesAbsent(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    params.peesEn = false;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    uint32_t off = EntryCfgOff(&iopmp, 0);
    IopmpWriteReg(&iopmp, off, ENTRY_CFG_SE_MASK | ENTRY_CFG_R_BIT);
    assert((IopmpReadReg(&iopmp, off) & ENTRY_CFG_SE_MASK) == 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-SUPP-004 pees=0 se bits wired 0");
}

/* ───────────────────────────────────────────────────────────────────
 * 11.2 Interrupt suppression
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-SUPP-005 - sire=1: read deny, no interrupt. */
static void TestSupp005_SireSuppresses(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire(&iopmp, 4, ERR_CFG_IE_BIT);
    SetNa4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_W_BIT | ENTRY_CFG_SIRE_BIT);

    ASSERT_ETYPE(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ),
                 IOPMP_ETYPE_ILLEGAL_READ);
    assert(!IopmpIsIrqPending(&iopmp));

    IopmpDestroy(&iopmp);
    PASS("IOPMP-SUPP-005 sire suppresses interrupt");
}

/* IOPMP-SUPP-006 - sire=0: read deny, interrupt fires. */
static void TestSupp006_NoSireFires(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire(&iopmp, 4, ERR_CFG_IE_BIT);
    SetNa4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_W_BIT);

    IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ);
    assert(IopmpIsIrqPending(&iopmp));

    IopmpDestroy(&iopmp);
    PASS("IOPMP-SUPP-006 no sire -> interrupt fires");
}

/* IOPMP-SUPP-007 - siwe=1: write deny, no interrupt. */
static void TestSupp007_SiweSuppresses(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire(&iopmp, 4, ERR_CFG_IE_BIT);
    SetNa4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_R_BIT | ENTRY_CFG_SIWE_BIT);

    ASSERT_ETYPE(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_WRITE),
                 IOPMP_ETYPE_ILLEGAL_WRITE);
    assert(!IopmpIsIrqPending(&iopmp));

    IopmpDestroy(&iopmp);
    PASS("IOPMP-SUPP-007 siwe suppresses write interrupt");
}

/* IOPMP-SUPP-008 - sixe=1: exec deny, no interrupt. */
static void TestSupp008_SixeSuppresses(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire(&iopmp, 4, ERR_CFG_IE_BIT);
    SetNa4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_R_BIT | ENTRY_CFG_SIXE_BIT);

    ASSERT_ETYPE(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_EXEC),
                 IOPMP_ETYPE_ILLEGAL_EXEC);
    assert(!IopmpIsIrqPending(&iopmp));

    IopmpDestroy(&iopmp);
    PASS("IOPMP-SUPP-008 sixe suppresses exec interrupt");
}

/* IOPMP-SUPP-009 - wrong-type bit (siwe) does not suppress a read. */
static void TestSupp009_WrongTypeBit(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire(&iopmp, 4, ERR_CFG_IE_BIT);
    SetNa4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_W_BIT | ENTRY_CFG_SIWE_BIT);

    IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ);   /* read, siwe set */
    assert(IopmpIsIrqPending(&iopmp));                           /* siwe != sire */

    IopmpDestroy(&iopmp);
    PASS("IOPMP-SUPP-009 wrong-type suppress bit ignored");
}

/* IOPMP-SUPP-010 - ie=0: no interrupt regardless of sire. */
static void TestSupp010_GlobalIeOff(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire(&iopmp, 4, 0U);                                 /* ie=0 */
    SetNa4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_W_BIT);

    IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ);
    assert(!IopmpIsIrqPending(&iopmp));

    IopmpDestroy(&iopmp);
    PASS("IOPMP-SUPP-010 ie=0 no interrupt");
}

/* ───────────────────────────────────────────────────────────────────
 * 11.3 Bus-error suppression
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-SUPP-011 - sere=1, rs=0: read deny, no bus error. */
static void TestSupp011_SereSuppresses(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire(&iopmp, 4, 0U);
    SetNa4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_W_BIT | ENTRY_CFG_SERE_BIT);

    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ);
    ASSERT_ETYPE(r, IOPMP_ETYPE_ILLEGAL_READ);
    assert(r.suppressError);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-SUPP-011 sere suppresses bus error");
}

/* IOPMP-SUPP-012 - sere=0, rs=0: read deny, bus error returned. */
static void TestSupp012_NoSereBusError(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire(&iopmp, 4, 0U);
    SetNa4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_W_BIT);

    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ);
    assert(!r.suppressError);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-SUPP-012 no sere -> bus error");
}

/* IOPMP-SUPP-013 - sewe=1: write deny, no bus error. */
static void TestSupp013_SeweSuppresses(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire(&iopmp, 4, 0U);
    SetNa4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_R_BIT | ENTRY_CFG_SEWE_BIT);

    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_WRITE);
    assert(r.suppressError);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-SUPP-013 sewe suppresses write bus error");
}

/* IOPMP-SUPP-014 - sexe=1: exec deny, no bus error. */
static void TestSupp014_SexeSuppresses(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire(&iopmp, 4, 0U);
    SetNa4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_R_BIT | ENTRY_CFG_SEXE_BIT);

    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_EXEC);
    assert(r.suppressError);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-SUPP-014 sexe suppresses exec bus error");
}

/* IOPMP-SUPP-015 - rs=1 globally suppresses regardless of per-entry bits. */
static void TestSupp015_GlobalRs(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire(&iopmp, 4, ERR_CFG_RS_BIT);
    SetNa4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_W_BIT);       /* no sere */

    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ);
    assert(r.suppressError);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-SUPP-015 global rs suppresses bus error");
}

/* ───────────────────────────────────────────────────────────────────
 * 11.4 Independent interrupt vs bus-error
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-SUPP-016 - sire=1, sere=0: no interrupt, bus error returned. */
static void TestSupp016_IrqSuppNoBusSupp(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire(&iopmp, 4, ERR_CFG_IE_BIT);
    SetNa4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_W_BIT | ENTRY_CFG_SIRE_BIT);

    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ);
    assert(!IopmpIsIrqPending(&iopmp));
    assert(!r.suppressError);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-SUPP-016 interrupt suppressed, bus error not");
}

/* IOPMP-SUPP-017 - sire=0, sere=1: interrupt fires, no bus error. */
static void TestSupp017_BusSuppNoIrqSupp(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire(&iopmp, 4, ERR_CFG_IE_BIT);
    SetNa4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_W_BIT | ENTRY_CFG_SERE_BIT);

    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ);
    assert(IopmpIsIrqPending(&iopmp));
    assert(r.suppressError);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-SUPP-017 bus error suppressed, interrupt not");
}

/* IOPMP-SUPP-018 - guard region: both suppressed, access still blocked. */
static void TestSupp018_GuardRegion(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire(&iopmp, 4, ERR_CFG_IE_BIT);
    SetNa4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_W_BIT | ENTRY_CFG_SIRE_BIT | ENTRY_CFG_SERE_BIT);

    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ);
    ASSERT_ETYPE(r, IOPMP_ETYPE_ILLEGAL_READ);          /* still blocked */
    assert(!IopmpIsIrqPending(&iopmp));
    assert(r.suppressError);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-SUPP-018 guard region: silent block");
}

/* ───────────────────────────────────────────────────────────────────
 * 11.5 Non-priority AND/OR rule
 * ─────────────────────────────────────────────────────────────────── */

static IopmpParams_t MakeNprioParams(void)
{
    IopmpParams_t params = MakeParams(1, 4, 1);
    params.nonPrioEn = true;
    params.prioEntry = 2;                                /* entries 2,3 non-priority */
    return params;
}

/* IOPMP-SUPP-019 - all matched non-prio have sire: interrupt suppressed. */
static void TestSupp019_AllSire(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeNprioParams();
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire(&iopmp, 4, ERR_CFG_IE_BIT);
    SetNapot(&iopmp, 2, 0x1000ULL, 0x20U, ENTRY_CFG_W_BIT | ENTRY_CFG_SIRE_BIT);
    SetNapot(&iopmp, 3, 0x1000ULL, 0x20U, ENTRY_CFG_W_BIT | ENTRY_CFG_SIRE_BIT);

    IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ);
    assert(!IopmpIsIrqPending(&iopmp));

    IopmpDestroy(&iopmp);
    PASS("IOPMP-SUPP-019 all matched sire -> suppressed");
}

/* IOPMP-SUPP-020 - one matched non-prio lacks sire: interrupt fires. */
static void TestSupp020_OneMissingSire(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeNprioParams();
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire(&iopmp, 4, ERR_CFG_IE_BIT);
    SetNapot(&iopmp, 2, 0x1000ULL, 0x20U, ENTRY_CFG_W_BIT | ENTRY_CFG_SIRE_BIT);
    SetNapot(&iopmp, 3, 0x1000ULL, 0x20U, ENTRY_CFG_W_BIT);   /* no sire */

    IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ);
    assert(IopmpIsIrqPending(&iopmp));

    IopmpDestroy(&iopmp);
    PASS("IOPMP-SUPP-020 one missing sire -> interrupt fires");
}

/* IOPMP-SUPP-021 - all matched non-prio have sere: bus error suppressed. */
static void TestSupp021_AllSere(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeNprioParams();
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire(&iopmp, 4, 0U);
    SetNapot(&iopmp, 2, 0x1000ULL, 0x20U, ENTRY_CFG_W_BIT | ENTRY_CFG_SERE_BIT);
    SetNapot(&iopmp, 3, 0x1000ULL, 0x20U, ENTRY_CFG_W_BIT | ENTRY_CFG_SERE_BIT);

    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ);
    assert(r.suppressError);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-SUPP-021 all matched sere -> suppressed");
}

/* IOPMP-SUPP-022 - one matched non-prio lacks sere: bus error returned. */
static void TestSupp022_OneMissingSere(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeNprioParams();
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire(&iopmp, 4, 0U);
    SetNapot(&iopmp, 2, 0x1000ULL, 0x20U, ENTRY_CFG_W_BIT | ENTRY_CFG_SERE_BIT);
    SetNapot(&iopmp, 3, 0x1000ULL, 0x20U, ENTRY_CFG_W_BIT);

    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ);
    assert(!r.suppressError);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-SUPP-022 one missing sere -> bus error");
}

/* IOPMP-SUPP-023 - write: all siwe set (suppress irq), one sewe clear (bus error). */
static void TestSupp023_IndependentNonPrio(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeNprioParams();
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire(&iopmp, 4, ERR_CFG_IE_BIT);
    SetNapot(&iopmp, 2, 0x1000ULL, 0x20U, ENTRY_CFG_R_BIT | ENTRY_CFG_SIWE_BIT | ENTRY_CFG_SEWE_BIT);
    SetNapot(&iopmp, 3, 0x1000ULL, 0x20U, ENTRY_CFG_R_BIT | ENTRY_CFG_SIWE_BIT);  /* no sewe */

    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_WRITE);
    assert(!IopmpIsIrqPending(&iopmp));                 /* all siwe -> irq suppressed */
    assert(!r.suppressError);                           /* not all sewe -> bus error */

    IopmpDestroy(&iopmp);
    PASS("IOPMP-SUPP-023 independent irq/bus suppression (non-prio)");
}

/* ───────────────────────────────────────────────────────────────────
 * Cross-combinations
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-SUPP-X01 - fully suppressed: no irq, no bus error; still captured. */
static void TestSuppX01_FullySuppressed(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire(&iopmp, 4, ERR_CFG_IE_BIT);
    SetNa4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_W_BIT | ENTRY_CFG_SIRE_BIT | ENTRY_CFG_SERE_BIT);

    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ);
    assert(!IopmpIsIrqPending(&iopmp));
    assert(r.suppressError);
    assert((IopmpReadReg(&iopmp, REG_ERR_INFO) & ERR_INFO_V_BIT) != 0U);  /* captured */

    IopmpDestroy(&iopmp);
    PASS("IOPMP-SUPP-X01 fully suppressed but captured");
}

/* IOPMP-SUPP-X02 - higher-priority matched entry's sire governs suppression. */
static void TestSuppX02_PriorityEntryGoverns(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire(&iopmp, 4, ERR_CFG_IE_BIT);
    /* Entry 0 (higher priority) denies with sire; entry 1 also covers w/o sire. */
    SetNapot(&iopmp, 0, 0x1000ULL, 0x20U, ENTRY_CFG_W_BIT | ENTRY_CFG_SIRE_BIT);
    SetNapot(&iopmp, 1, 0x1000ULL, 0x20U, ENTRY_CFG_W_BIT);

    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ);
    assert(r.entryIdx == 0U);
    assert(!IopmpIsIrqPending(&iopmp));                 /* entry 0's sire governs */

    IopmpDestroy(&iopmp);
    PASS("IOPMP-SUPP-X02 matched priority entry's sire governs");
}

/* IOPMP-SUPP-X03 - SPS-induced denial uses matched entry's suppress bit. */
static void TestSuppX03_SpsDenialSuppressed(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    params.spsEn = true;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire(&iopmp, 4, 0U);
    SetNa4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_R_BIT | ENTRY_CFG_W_BIT | ENTRY_CFG_SEWE_BIT);
    /* SPS write not granted -> write denied; sewe suppresses its bus error. */

    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_WRITE);
    ASSERT_ETYPE(r, IOPMP_ETYPE_ILLEGAL_WRITE);
    assert(r.suppressError);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-SUPP-X03 SPS denial honours matched-entry suppress");
}

/* IOPMP-SUPP-X04 - MSI mode + sire: no MSI sent when interrupt suppressed. */
static void TestSuppX04_MsiSuppressed(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    params.msiEn = true;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire(&iopmp, 4, ERR_CFG_IE_BIT | ERR_CFG_MSI_SEL_BIT);
    SetNa4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_W_BIT | ENTRY_CFG_SIRE_BIT);

    IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ);
    assert(!IopmpIsMsiPending(&iopmp));                 /* suppressed -> no MSI */

    IopmpDestroy(&iopmp);
    PASS("IOPMP-SUPP-X04 MSI not sent when interrupt suppressed");
}

/* IOPMP-SUPP-X05 - global no_x deny (no matched entry): per-entry bits N/A. */
static void TestSuppX05_GlobalNoXNoEntry(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    params.noX      = true;
    params.hwcfg3En = true;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire(&iopmp, 4, ERR_CFG_IE_BIT);
    SetNa4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_X_BIT | ENTRY_CFG_SIXE_BIT);  /* sixe ignored */

    /* Global no_x denies before any entry match -> NO_RULE; reaction per ie. */
    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_EXEC);
    ASSERT_ETYPE(r, IOPMP_ETYPE_NO_RULE);
    assert(IopmpIsIrqPending(&iopmp));                  /* no matched entry to suppress */

    IopmpDestroy(&iopmp);
    PASS("IOPMP-SUPP-X05 global no_x: per-entry suppress N/A");
}

/* ── Runner ──────────────────────────────────────────────────────── */

int main(void)
{
    TestSupp001_PeisImplemented();
    TestSupp002_PeisAbsent();
    TestSupp003_PeesImplemented();
    TestSupp004_PeesAbsent();

    TestSupp005_SireSuppresses();
    TestSupp006_NoSireFires();
    TestSupp007_SiweSuppresses();
    TestSupp008_SixeSuppresses();
    TestSupp009_WrongTypeBit();
    TestSupp010_GlobalIeOff();

    TestSupp011_SereSuppresses();
    TestSupp012_NoSereBusError();
    TestSupp013_SeweSuppresses();
    TestSupp014_SexeSuppresses();
    TestSupp015_GlobalRs();

    TestSupp016_IrqSuppNoBusSupp();
    TestSupp017_BusSuppNoIrqSupp();
    TestSupp018_GuardRegion();

    TestSupp019_AllSire();
    TestSupp020_OneMissingSire();
    TestSupp021_AllSere();
    TestSupp022_OneMissingSere();
    TestSupp023_IndependentNonPrio();

    TestSuppX01_FullySuppressed();
    TestSuppX02_PriorityEntryGoverns();
    TestSuppX03_SpsDenialSuppressed();
    TestSuppX04_MsiSuppressed();
    TestSuppX05_GlobalNoXNoEntry();

    printf("\nAll file-11 per-entry suppression tests passed.\n");
    return 0;
}
