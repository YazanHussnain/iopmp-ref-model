/*
 * test_06_error_capture_reactions.c
 *
 * Test suite for docs/testplan/06-error-capture-reactions.md:
 *   "Error Capture & Reactions".
 *
 * Spec: §2.8 (Error Reactions), §4.3 (ERR_CFG/ERR_INFO/ERR_REQADDR[H]/
 *       ERR_REQID/ERR_USER), Table 2/7 (error types), §5.1.4-5 extensions.
 *
 * Verified through the public transaction + MMIO API plus the IRQ/MSI query
 * helpers. NOTE comments mark deliberate deviations from the test plan's ideal.
 *
 * ERR_REQADDR holds the word address addr[33:2] and ERR_REQADDRH holds
 * addr[65:34] (spec §4.3.3); address-field asserts below use those word-address
 * values.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include "iopmp.h"
#include "test_utils.h"

/* ── IRQ callback instrumentation ────────────────────────────────────── */

static int g_cbCount;
static void IrqCb(IopmpState_t *iopmp, void *user)
{
    (void)iopmp; (void)user;
    g_cbCount++;
}

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

static void SetEntryRaw(IopmpState_t *iopmp, uint32_t idx, uint32_t wordAddr, uint32_t cfg)
{
    uint32_t base = IopmpReadReg(iopmp, REG_ENTRYOFFSET);
    uint32_t slot = base + idx * REG_ENTRY_STRIDE;
    IopmpWriteReg(iopmp, slot + REG_ENTRY_ADDR_OFF, wordAddr);
    IopmpWriteReg(iopmp, slot + REG_ENTRY_CFG_OFF,  cfg);
}

static void SetNa4(IopmpState_t *iopmp, uint32_t idx, uint64_t byteAddr, uint32_t perm)
{
    SetEntryRaw(iopmp, idx, (uint32_t)(byteAddr >> 2U),
                (ADDR_MODE_NA4 << ENTRY_CFG_A_SHIFT) | perm);
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

static uint32_t ErrTtype(IopmpState_t *iopmp)
{
    return (IopmpReadReg(iopmp, REG_ERR_INFO) & ERR_INFO_TTYPE_MASK) >> ERR_INFO_TTYPE_SHIFT;
}

static uint32_t ErrEtype(IopmpState_t *iopmp)
{
    return (IopmpReadReg(iopmp, REG_ERR_INFO) & ERR_INFO_ETYPE_MASK) >> ERR_INFO_ETYPE_SHIFT;
}

static bool ErrValid(IopmpState_t *iopmp)
{
    return (IopmpReadReg(iopmp, REG_ERR_INFO) & ERR_INFO_V_BIT) != 0U;
}

/* ───────────────────────────────────────────────────────────────────
 * 6.1 First-capture-wins
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-ERR-001 - first capture records all fields. */
static void TestErr001_FirstCapture(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(4, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    EnableIopmp(&iopmp);
    SetupMdcfg(&iopmp, 0, 4);
    SetupSrcmd(&iopmp, 2, 0);                         /* RRID 2 -> MD0 */
    SetNa4(&iopmp, 0, 0x2000ULL, ENTRY_CFG_W_BIT);    /* read denied (no r) */

    TxnResult_t r = IopmpCheckAccess(&iopmp, 2, 0x2000ULL, 4, IOPMP_TXN_READ);
    ASSERT_ETYPE(r, IOPMP_ETYPE_ILLEGAL_READ);

    assert(ErrValid(&iopmp));
    assert(ErrTtype(&iopmp) == 1U);                   /* read */
    assert(ErrEtype(&iopmp) == (uint32_t)IOPMP_ETYPE_ILLEGAL_READ);

    uint32_t reqid = IopmpReadReg(&iopmp, REG_ERR_REQID);
    assert((reqid & ERR_REQID_RRID_MASK) == 2U);
    assert(((reqid & ERR_REQID_EID_MASK) >> ERR_REQID_EID_SHIFT) == 0U);  /* matched idx 0 */

    /* ERR_REQADDR = addr[33:2] = 0x2000 >> 2. */
    assert(IopmpReadReg(&iopmp, REG_ERR_REQADDR) == (0x2000U >> 2));

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ERR-001 first capture records fields");
}

/* IOPMP-ERR-002 - second violation while v=1 leaves the record unchanged. */
static void TestErr002_FirstWins(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(4, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    EnableIopmp(&iopmp);
    SetupMdcfg(&iopmp, 0, 4);
    SetupSrcmd(&iopmp, 1, 0);
    SetNa4(&iopmp, 0, 0x2000ULL, ENTRY_CFG_W_BIT);    /* read denied */

    IopmpCheckAccess(&iopmp, 1, 0x2000ULL, 4, IOPMP_TXN_READ);  /* first: read */
    uint32_t firstInfo  = IopmpReadReg(&iopmp, REG_ERR_INFO);
    uint32_t firstReqId = IopmpReadReg(&iopmp, REG_ERR_REQID);

    /* A different (write) violation must not overwrite the captured record. */
    IopmpCheckAccess(&iopmp, 1, 0x2000ULL, 4, IOPMP_TXN_WRITE);
    assert(IopmpReadReg(&iopmp, REG_ERR_INFO)  == firstInfo);
    assert(IopmpReadReg(&iopmp, REG_ERR_REQID) == firstReqId);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ERR-002 first-capture-wins");
}

/* IOPMP-ERR-003 - write ERR_INFO.v=1 clears v (RW1C), re-arms. */
static void TestErr003_ClearReArms(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(4, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    EnableIopmp(&iopmp);
    SetupMdcfg(&iopmp, 0, 4);
    SetupSrcmd(&iopmp, 0, 0);
    SetNa4(&iopmp, 0, 0x2000ULL, ENTRY_CFG_W_BIT);

    IopmpCheckAccess(&iopmp, 0, 0x2000ULL, 4, IOPMP_TXN_READ);
    assert(ErrValid(&iopmp));
    IopmpWriteReg(&iopmp, REG_ERR_INFO, ERR_INFO_V_BIT);
    assert(!ErrValid(&iopmp));

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ERR-003 RW1C clears v");
}

/* IOPMP-ERR-004 - new violation after clear is captured fresh. */
static void TestErr004_FreshAfterClear(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(4, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    EnableIopmp(&iopmp);
    SetupMdcfg(&iopmp, 0, 4);
    SetupSrcmd(&iopmp, 0, 0);
    SetNa4(&iopmp, 0, 0x2000ULL, ENTRY_CFG_R_BIT);    /* read OK, write denied */

    IopmpCheckAccess(&iopmp, 0, 0x2000ULL, 4, IOPMP_TXN_WRITE);   /* first: write */
    assert(ErrEtype(&iopmp) == (uint32_t)IOPMP_ETYPE_ILLEGAL_WRITE);
    IopmpWriteReg(&iopmp, REG_ERR_INFO, ERR_INFO_V_BIT);          /* clear */

    /* Exec violation now captured fresh. */
    IopmpCheckAccess(&iopmp, 0, 0x2000ULL, 4, IOPMP_TXN_EXEC);
    assert(ErrValid(&iopmp));
    assert(ErrEtype(&iopmp) == (uint32_t)IOPMP_ETYPE_ILLEGAL_EXEC);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ERR-004 fresh capture after clear");
}

/* IOPMP-ERR-005 - - fully suppressed (ie=0, rs=1): model still captures. */
static void TestErr005_FullySuppressedStillCaptures(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(4, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    EnableIopmp(&iopmp);
    SetupMdcfg(&iopmp, 0, 4);
    SetupSrcmd(&iopmp, 0, 0);
    IopmpWriteReg(&iopmp, REG_ERR_CFG, ERR_CFG_RS_BIT);   /* ie=0, rs=1 */
    SetNa4(&iopmp, 0, 0x2000ULL, ENTRY_CFG_W_BIT);

    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x2000ULL, 4, IOPMP_TXN_READ);
    assert(r.suppressError);                          /* bus error faked-success */
    assert(!IopmpIsIrqPending(&iopmp));               /* no interrupt */

    /* NOTE: §2.8 says the capture record "need not" update when fully
     * suppressed. The model chooses to capture anyway (a permitted option). */
    assert(ErrValid(&iopmp));

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ERR-005 captures even when fully suppressed");
}

/* ───────────────────────────────────────────────────────────────────
 * 6.2 Error type encoding
 * ─────────────────────────────────────────────────────────────────── */

/* Helper: wire RRID0->MD0 with all entries, enabled. */
static void Wire(IopmpState_t *iopmp, uint16_t entryNum)
{
    EnableIopmp(iopmp);
    SetupMdcfg(iopmp, 0, entryNum);
    SetupSrcmd(iopmp, 0, 0);
}

/* IOPMP-ERR-006..009 - illegal read/write/AMO/exec etype + ttype. */
static void TestErr006to009_AccessEtypes(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);

    /* 006 read on r=0 */
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire(&iopmp, 4);
    SetNa4(&iopmp, 0, 0x2000ULL, ENTRY_CFG_W_BIT);
    IopmpCheckAccess(&iopmp, 0, 0x2000ULL, 4, IOPMP_TXN_READ);
    assert(ErrEtype(&iopmp) == 0x01U && ErrTtype(&iopmp) == 0x01U);
    IopmpDestroy(&iopmp);

    /* 007 write on w=0 */
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire(&iopmp, 4);
    SetNa4(&iopmp, 0, 0x2000ULL, ENTRY_CFG_R_BIT);
    IopmpCheckAccess(&iopmp, 0, 0x2000ULL, 4, IOPMP_TXN_WRITE);
    assert(ErrEtype(&iopmp) == 0x02U && ErrTtype(&iopmp) == 0x02U);
    IopmpDestroy(&iopmp);

    /* 008 AMO without r&w */
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire(&iopmp, 4);
    SetNa4(&iopmp, 0, 0x2000ULL, ENTRY_CFG_R_BIT);    /* missing w */
    IopmpCheckAccess(&iopmp, 0, 0x2000ULL, 4, IOPMP_TXN_AMO);
    assert(ErrEtype(&iopmp) == 0x02U && ErrTtype(&iopmp) == 0x02U);
    IopmpDestroy(&iopmp);

    /* 009 exec on x=0 */
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire(&iopmp, 4);
    SetNa4(&iopmp, 0, 0x2000ULL, ENTRY_CFG_R_BIT);
    IopmpCheckAccess(&iopmp, 0, 0x2000ULL, 4, IOPMP_TXN_EXEC);
    assert(ErrEtype(&iopmp) == 0x03U && ErrTtype(&iopmp) == 0x03U);
    IopmpDestroy(&iopmp);

    PASS("IOPMP-ERR-006..009 access-type etypes");
}

/* IOPMP-ERR-010 - partial hit -> 0x04. */
static void TestErr010_PartialHit(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire(&iopmp, 4);
    SetNa4(&iopmp, 0, 0x2000ULL, ENTRY_CFG_R_BIT);
    IopmpCheckAccess(&iopmp, 0, 0x2000ULL, 8, IOPMP_TXN_READ);
    assert(ErrEtype(&iopmp) == 0x04U);
    IopmpDestroy(&iopmp);
    PASS("IOPMP-ERR-010 partial-hit etype 0x04");
}

/* IOPMP-ERR-011 - no rule -> 0x05, eid invalid. */
static void TestErr011_NoRule(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire(&iopmp, 4);
    IopmpCheckAccess(&iopmp, 0, 0x2000ULL, 4, IOPMP_TXN_READ);
    assert(ErrEtype(&iopmp) == 0x05U);
    uint32_t eid = (IopmpReadReg(&iopmp, REG_ERR_REQID) & ERR_REQID_EID_MASK) >> ERR_REQID_EID_SHIFT;
    assert(eid == 0xFFFFU);
    IopmpDestroy(&iopmp);
    PASS("IOPMP-ERR-011 no-rule etype 0x05, eid invalid");
}

/* IOPMP-ERR-012 - unknown RRID -> 0x06, eid invalid. */
static void TestErr012_UnknownRrid(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(2, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    EnableIopmp(&iopmp);
    IopmpCheckAccess(&iopmp, 2, 0x2000ULL, 4, IOPMP_TXN_READ);
    assert(ErrEtype(&iopmp) == 0x06U);
    uint32_t eid = (IopmpReadReg(&iopmp, REG_ERR_REQID) & ERR_REQID_EID_MASK) >> ERR_REQID_EID_SHIFT;
    assert(eid == 0xFFFFU);
    IopmpDestroy(&iopmp);
    PASS("IOPMP-ERR-012 unknown-RRID etype 0x06");
}

/* IOPMP-ERR-013 - - fetch treated as read (xinr): never reports 0x03. */
static void TestErr013_NoFetchSignal(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    params.xinr     = true;                           /* exec treated as data read */
    params.hwcfg3En = true;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire(&iopmp, 4);

    /* Entry grants exec but not read; with xinr the fetch becomes a read and
     * is denied as an illegal READ (0x01) - never 0x03. */
    SetNa4(&iopmp, 0, 0x2000ULL, ENTRY_CFG_X_BIT);
    IopmpCheckAccess(&iopmp, 0, 0x2000ULL, 4, IOPMP_TXN_EXEC);
    assert(ErrEtype(&iopmp) == 0x01U);
    assert(ErrTtype(&iopmp) != 0x03U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ERR-013 xinr: fetch never reports 0x03");
}

/* IOPMP-ERR-014 - - model never emits user etypes 0x0E/0x0F. */
static void TestErr014_NoUserEtype(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(2, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire(&iopmp, 4);

    /* NOTE: the enum defines IOPMP_ETYPE_USER_0/1 (0x0E/0x0F) but the reference
     * checker has no IMP rule path that produces them. Confirm a range of
     * violations never yields a user etype. */
    SetNa4(&iopmp, 0, 0x2000ULL, ENTRY_CFG_W_BIT);
    IopmpCheckAccess(&iopmp, 0, 0x2000ULL, 4, IOPMP_TXN_READ);
    assert(ErrEtype(&iopmp) != 0x0EU && ErrEtype(&iopmp) != 0x0FU);
    IopmpWriteReg(&iopmp, REG_ERR_INFO, ERR_INFO_V_BIT);

    IopmpCheckAccess(&iopmp, 0, 0x9000ULL, 4, IOPMP_TXN_READ);   /* no rule */
    assert(ErrEtype(&iopmp) != 0x0EU && ErrEtype(&iopmp) != 0x0FU);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ERR-014 user etypes never emitted by model");
}

/* ───────────────────────────────────────────────────────────────────
 * 6.3 ERR_REQADDR / ERR_REQADDRH / ERR_REQID
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-ERR-015 - addrh_en=0: REQADDR holds low addr; REQADDRH not implemented. */
static void TestErr015_ReqAddrLowOnly(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);        /* addrhEn = false */
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire(&iopmp, 4);
    SetNa4(&iopmp, 0, 0x2000ULL, ENTRY_CFG_W_BIT);

    IopmpCheckAccess(&iopmp, 0, 0x2000ULL, 4, IOPMP_TXN_READ);
    /* ERR_REQADDR = addr[33:2]; ERR_REQADDRH not implemented when addrh_en=0. */
    assert(IopmpReadReg(&iopmp, REG_ERR_REQADDR)  == (0x2000U >> 2));
    assert(IopmpReadReg(&iopmp, REG_ERR_REQADDRH) == 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ERR-015 REQADDR low only (addrh_en=0)");
}

/* IOPMP-ERR-016 - - addrh_en=1: high-addr violation fills both halves. */
static void TestErr016_ReqAddrHigh(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    params.addrhEn = true;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire(&iopmp, 4);
    /* No matching entry -> NO_RULE; the high address must still be captured. */

    uint64_t addr = 0x400002000ULL;
    IopmpCheckAccess(&iopmp, 0, addr, 4, IOPMP_TXN_READ);
    /* ERR_REQADDR = addr[33:2]; ERR_REQADDRH = addr[65:34] (spec §4.3.3). */
    assert(IopmpReadReg(&iopmp, REG_ERR_REQADDR)  == (uint32_t)((addr >> 2U) & 0xFFFFFFFFU));
    assert(IopmpReadReg(&iopmp, REG_ERR_REQADDRH) == (uint32_t)(addr >> 34U));

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ERR-016 REQADDR + REQADDRH populated");
}

/* IOPMP-ERR-017 - matched entry index recorded in ERR_REQID.eid. */
static void TestErr017_EidMatched(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 8, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire(&iopmp, 8);
    SetNa4(&iopmp, 3, 0x3000ULL, ENTRY_CFG_W_BIT);    /* deny read at entry 3 */

    IopmpCheckAccess(&iopmp, 0, 0x3000ULL, 4, IOPMP_TXN_READ);
    uint32_t eid = (IopmpReadReg(&iopmp, REG_ERR_REQID) & ERR_REQID_EID_MASK) >> ERR_REQID_EID_SHIFT;
    assert(eid == 3U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ERR-017 eid = matched entry index");
}

/* IOPMP-ERR-018 - no-entry-hit (0x05/0x06): eid invalid 0xffff. */
static void TestErr018_EidInvalid(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire(&iopmp, 4);

    IopmpCheckAccess(&iopmp, 0, 0x2000ULL, 4, IOPMP_TXN_READ);   /* NO_RULE */
    uint32_t eid = (IopmpReadReg(&iopmp, REG_ERR_REQID) & ERR_REQID_EID_MASK) >> ERR_REQID_EID_SHIFT;
    assert(eid == 0xFFFFU);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ERR-018 eid invalid on no-hit");
}

/* IOPMP-ERR-019 - - eid field not implemented: wired 0xffff even on a hit. */
static void TestErr019_EidDisabled(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 8, 1);
    params.eidDisable = true;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire(&iopmp, 8);
    SetNa4(&iopmp, 3, 0x3000ULL, ENTRY_CFG_W_BIT);    /* matched entry 3, denied */

    IopmpCheckAccess(&iopmp, 0, 0x3000ULL, 4, IOPMP_TXN_READ);
    uint32_t eid = (IopmpReadReg(&iopmp, REG_ERR_REQID) & ERR_REQID_EID_MASK) >> ERR_REQID_EID_SHIFT;
    assert(eid == 0xFFFFU);                           /* wired despite real match */

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ERR-019 eid wired 0xffff when not implemented");
}

/* IOPMP-ERR-020 - - no_err_rec=1: nothing captured. */
static void TestErr020_NoErrRec(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    params.noErrRec = true;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire(&iopmp, 4);
    SetNa4(&iopmp, 0, 0x2000ULL, ENTRY_CFG_W_BIT);

    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x2000ULL, 4, IOPMP_TXN_READ);
    ASSERT_ILLEGAL(r);                                /* still blocked */
    assert(!ErrValid(&iopmp));                        /* but nothing recorded */
    assert(IopmpReadReg(&iopmp, REG_ERR_REQADDR) == 0U);
    assert(IopmpReadReg(&iopmp, REG_ERR_REQID)   == 0U);

    /* NOTE: the test plan ties "eid wired 0xffff" to no_err_rec; in the model
     * eid=0xffff is governed by the separate eidDisable param (see ERR-019). */

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ERR-020 no_err_rec captures nothing");
}

/* ───────────────────────────────────────────────────────────────────
 * 6.4 Interrupt reaction (ERR_CFG.ie)
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-ERR-021 - ie=1: interrupt asserted + callback fired, stays until clear. */
static void TestErr021_IrqAsserted(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire(&iopmp, 4);
    g_cbCount = 0;
    IopmpSetIrqCb(&iopmp, IrqCb, NULL);
    IopmpWriteReg(&iopmp, REG_ERR_CFG, ERR_CFG_IE_BIT);
    SetNa4(&iopmp, 0, 0x2000ULL, ENTRY_CFG_W_BIT);

    IopmpCheckAccess(&iopmp, 0, 0x2000ULL, 4, IOPMP_TXN_READ);
    assert(IopmpIsIrqPending(&iopmp));
    assert(g_cbCount == 1);
    /* Stays asserted while v is still set. */
    assert(IopmpIsIrqPending(&iopmp));

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ERR-021 interrupt asserted with ie=1");
}

/* IOPMP-ERR-022 - ie=0: no interrupt, record still captured. */
static void TestErr022_NoIrqWhenIeClear(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire(&iopmp, 4);
    g_cbCount = 0;
    IopmpSetIrqCb(&iopmp, IrqCb, NULL);
    SetNa4(&iopmp, 0, 0x2000ULL, ENTRY_CFG_W_BIT);    /* ie left 0 */

    IopmpCheckAccess(&iopmp, 0, 0x2000ULL, 4, IOPMP_TXN_READ);
    assert(!IopmpIsIrqPending(&iopmp));
    assert(g_cbCount == 0);
    assert(ErrValid(&iopmp));                          /* still captured */

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ERR-022 no interrupt when ie=0");
}

/* IOPMP-ERR-023 - clearing v deasserts the interrupt. */
static void TestErr023_ClearDeasserts(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire(&iopmp, 4);
    IopmpWriteReg(&iopmp, REG_ERR_CFG, ERR_CFG_IE_BIT);
    SetNa4(&iopmp, 0, 0x2000ULL, ENTRY_CFG_W_BIT);

    IopmpCheckAccess(&iopmp, 0, 0x2000ULL, 4, IOPMP_TXN_READ);
    assert(IopmpIsIrqPending(&iopmp));
    IopmpWriteReg(&iopmp, REG_ERR_INFO, ERR_INFO_V_BIT);   /* clear v */
    assert(!IopmpIsIrqPending(&iopmp));

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ERR-023 interrupt deasserts on clear");
}

/* IOPMP-ERR-024 - interrupt re-asserts per fresh capture. */
static void TestErr024_ReAssert(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire(&iopmp, 4);
    g_cbCount = 0;
    IopmpSetIrqCb(&iopmp, IrqCb, NULL);
    IopmpWriteReg(&iopmp, REG_ERR_CFG, ERR_CFG_IE_BIT);
    SetNa4(&iopmp, 0, 0x2000ULL, ENTRY_CFG_W_BIT);

    IopmpCheckAccess(&iopmp, 0, 0x2000ULL, 4, IOPMP_TXN_READ);
    assert(g_cbCount == 1);
    IopmpWriteReg(&iopmp, REG_ERR_INFO, ERR_INFO_V_BIT);   /* clear */
    IopmpCheckAccess(&iopmp, 0, 0x2000ULL, 4, IOPMP_TXN_READ);
    assert(g_cbCount == 2);                            /* fired again */

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ERR-024 interrupt re-asserts per capture");
}

/* ───────────────────────────────────────────────────────────────────
 * 6.5 Bus-error reaction (ERR_CFG.rs)
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-ERR-025 - rs=0: bus error returned (not suppressed). */
static void TestErr025_BusError(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire(&iopmp, 4);
    SetNa4(&iopmp, 0, 0x2000ULL, ENTRY_CFG_W_BIT);

    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x2000ULL, 4, IOPMP_TXN_READ);
    ASSERT_ILLEGAL(r);
    assert(!r.suppressError);                          /* bus error returned */

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ERR-025 rs=0 returns bus error");
}

/* IOPMP-ERR-026 - - rs=1: bus error suppressed (faked success). */
static void TestErr026_SuppressBusError(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire(&iopmp, 4);
    IopmpWriteReg(&iopmp, REG_ERR_CFG, ERR_CFG_RS_BIT);
    SetNa4(&iopmp, 0, 0x2000ULL, ENTRY_CFG_W_BIT);

    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x2000ULL, 4, IOPMP_TXN_READ);
    ASSERT_ILLEGAL(r);
    assert(r.suppressError);                           /* faked success */

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ERR-026 rs=1 suppresses bus error");
}

/* IOPMP-ERR-027 - - rs=1 and ie=1: interrupt fires, bus error suppressed. */
static void TestErr027_IndependentReactions(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire(&iopmp, 4);
    IopmpWriteReg(&iopmp, REG_ERR_CFG, ERR_CFG_RS_BIT | ERR_CFG_IE_BIT);
    SetNa4(&iopmp, 0, 0x2000ULL, ENTRY_CFG_W_BIT);

    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x2000ULL, 4, IOPMP_TXN_READ);
    assert(r.suppressError);                           /* bus error suppressed */
    assert(IopmpIsIrqPending(&iopmp));                 /* interrupt still fires */

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ERR-027 ie and rs are independent");
}

/* ───────────────────────────────────────────────────────────────────
 * 6.6 ERR_CFG register semantics
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-ERR-028 - ERR_CFG.l WISS: set 1 sticky, ie/rs frozen. */
static void TestErr028_LockSticky(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    IopmpWriteReg(&iopmp, REG_ERR_CFG, ERR_CFG_IE_BIT | ERR_CFG_L_BIT);
    uint32_t locked = IopmpReadReg(&iopmp, REG_ERR_CFG);
    assert((locked & ERR_CFG_L_BIT) != 0U);
    assert((locked & ERR_CFG_IE_BIT) != 0U);

    /* Frozen: attempt to set rs / clear ie -> ignored. */
    IopmpWriteReg(&iopmp, REG_ERR_CFG, ERR_CFG_RS_BIT);
    assert(IopmpReadReg(&iopmp, REG_ERR_CFG) == locked);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ERR-028 ERR_CFG.l sticky + freezes ie/rs");
}

/* IOPMP-ERR-029 - ie/rs WARL: write both -> read back. */
static void TestErr029_IeRsWarl(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    IopmpWriteReg(&iopmp, REG_ERR_CFG, ERR_CFG_IE_BIT | ERR_CFG_RS_BIT);
    uint32_t v = IopmpReadReg(&iopmp, REG_ERR_CFG);
    assert((v & ERR_CFG_IE_BIT) != 0U);
    assert((v & ERR_CFG_RS_BIT) != 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ERR-029 ie/rs WARL read back");
}

/* IOPMP-ERR-030 - reserved bits read 0 after write-all-ones. */
static void TestErr030_ReservedZero(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    IopmpWriteReg(&iopmp, REG_ERR_CFG, 0xFFFFFFFFU);
    uint32_t v = IopmpReadReg(&iopmp, REG_ERR_CFG);
    /* Only l + the defined valid fields may be set; everything else reads 0.
     * NOTE: the model's valid mask includes the §5.1.4 extension fields
     * (msi_sel/stall_viol/msidata), so "reserved" is wider than the base
     * spec's [31:3]. */
    assert((v & ~(ERR_CFG_VALID_MASK | ERR_CFG_L_BIT)) == 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ERR-030 ERR_CFG reserved bits read 0");
}

/* IOPMP-ERR-031 - - ERR_USER(0..7) read/write storage. */
static void TestErr031_ErrUser(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    for (uint32_t i = 0U; i < 8U; i++) {
        uint32_t off = REG_ERR_USER_BASE + i * 4U;
        IopmpWriteReg(&iopmp, off, 0x1000U + i);
        assert(IopmpReadReg(&iopmp, off) == 0x1000U + i);
    }

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ERR-031 ERR_USER read/write storage");
}

/* ───────────────────────────────────────────────────────────────────
 * Cross-combinations (file-local)
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-ERR-X01 - ERR_CFG.l=1: ie/rs writes rejected (immutable). */
static void TestErrX01_LockedImmutable(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    IopmpWriteReg(&iopmp, REG_ERR_CFG, ERR_CFG_IE_BIT | ERR_CFG_RS_BIT | ERR_CFG_L_BIT);
    uint32_t locked = IopmpReadReg(&iopmp, REG_ERR_CFG);

    IopmpWriteReg(&iopmp, REG_ERR_CFG, 0U);            /* try to clear everything */
    assert(IopmpReadReg(&iopmp, REG_ERR_CFG) == locked);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ERR-X01 locked ERR_CFG immutable");
}

/* IOPMP-ERR-X02 - - per-entry sire suppresses the interrupt; record kept. */
static void TestErrX02_SireSuppressesIrq(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    params.hwcfg2En = true;
    params.peisEn   = true;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire(&iopmp, 4);
    IopmpWriteReg(&iopmp, REG_ERR_CFG, ERR_CFG_IE_BIT);

    SetNa4(&iopmp, 0, 0x2000ULL, ENTRY_CFG_W_BIT | ENTRY_CFG_SIRE_BIT);
    IopmpCheckAccess(&iopmp, 0, 0x2000ULL, 4, IOPMP_TXN_READ);
    assert(ErrValid(&iopmp));                          /* captured */
    assert(!IopmpIsIrqPending(&iopmp));                /* interrupt suppressed */

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ERR-X02 per-entry sire suppresses interrupt");
}

/* IOPMP-ERR-X03 - - per-entry sere suppresses the bus error (rs=0). */
static void TestErrX03_SereSuppressesBusError(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    params.hwcfg2En = true;
    params.peesEn   = true;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire(&iopmp, 4);
    /* ERR_CFG.rs left 0; suppression comes from the per-entry sere bit. */

    SetNa4(&iopmp, 0, 0x2000ULL, ENTRY_CFG_W_BIT | ENTRY_CFG_SERE_BIT);
    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x2000ULL, 4, IOPMP_TXN_READ);
    ASSERT_ILLEGAL(r);
    assert(r.suppressError);                           /* per-entry bus-error suppress */

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ERR-X03 per-entry sere suppresses bus error");
}

/* IOPMP-ERR-X04 - - subsequent violations logged in ERR_MFR with svc=1. */
static void TestErrX04_MultiFault(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(4, 4, 1);
    params.hwcfg2En     = true;
    params.multifaultEn = true;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    EnableIopmp(&iopmp);
    SetupMdcfg(&iopmp, 0, 4);
    SetupSrcmd(&iopmp, 0, 0);
    SetupSrcmd(&iopmp, 1, 0);
    SetNa4(&iopmp, 0, 0x2000ULL, ENTRY_CFG_W_BIT);     /* read denied */

    IopmpCheckAccess(&iopmp, 0, 0x2000ULL, 4, IOPMP_TXN_READ);   /* first: RRID0 */
    assert(ErrValid(&iopmp));
    assert((IopmpReadReg(&iopmp, REG_ERR_INFO) & ERR_INFO_SVC_BIT) == 0U);

    IopmpCheckAccess(&iopmp, 1, 0x2000ULL, 4, IOPMP_TXN_READ);   /* subsequent: RRID1 */
    assert((IopmpReadReg(&iopmp, REG_ERR_INFO) & ERR_INFO_SVC_BIT) != 0U);

    uint32_t mfr = IopmpReadReg(&iopmp, REG_ERR_MFR);
    assert((mfr & ERR_MFR_SVS_BIT) != 0U);             /* a window had set bits */
    assert((mfr & (1U << 1)) != 0U);                   /* RRID1 bit in window 0 */

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ERR-X04 subsequent violations in ERR_MFR, svc=1");
}

/* IOPMP-ERR-X05 - - msi_sel + ie: interrupt delivered via MSI. */
static void TestErrX05_MsiDelivery(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    params.hwcfg2En = true;
    params.msiEn    = true;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire(&iopmp, 4);
    IopmpWriteReg(&iopmp, REG_ERR_MSIADDR, 0xFEE00000U);
    IopmpWriteReg(&iopmp, REG_ERR_CFG, ERR_CFG_IE_BIT | ERR_CFG_MSI_SEL_BIT);
    SetNa4(&iopmp, 0, 0x2000ULL, ENTRY_CFG_W_BIT);

    IopmpCheckAccess(&iopmp, 0, 0x2000ULL, 4, IOPMP_TXN_READ);
    assert(IopmpIsMsiPending(&iopmp));                 /* MSI write would issue */
    assert(!IopmpIsIrqPending(&iopmp));                /* not the wired IRQ */

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ERR-X05 interrupt delivered via MSI");
}

/* IOPMP-ERR-X06 - - stall_violation_en: stalled txn faulted with etype 0x07. */
static void TestErrX06_StallViolation(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(2, 4, 1);
    params.hwcfg2En = true;
    params.stallEn  = true;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    EnableIopmp(&iopmp);
    SetupMdcfg(&iopmp, 0, 4);
    SetupSrcmd(&iopmp, 0, 0);
    SetNa4(&iopmp, 0, 0x2000ULL, ENTRY_CFG_R_BIT);

    /* Fault stalled transactions rather than holding them. */
    IopmpWriteReg(&iopmp, REG_ERR_CFG, ERR_CFG_STALL_VIOL_BIT);
    /* Stall RRID0 via RRIDSCP. */
    IopmpWriteReg(&iopmp, REG_RRIDSCP,
                  (0U & RRIDSCP_RRID_MASK) | (RRIDSCP_OP_STALL << RRIDSCP_OP_SHIFT));

    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x2000ULL, 4, IOPMP_TXN_READ);
    ASSERT_ETYPE(r, IOPMP_ETYPE_STALL_VIOL);
    assert(ErrEtype(&iopmp) == 0x07U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ERR-X06 stall violation etype 0x07");
}

/* ── Runner ──────────────────────────────────────────────────────── */

int main(void)
{
    /* 6.1 First-capture-wins */
    TestErr001_FirstCapture();
    TestErr002_FirstWins();
    TestErr003_ClearReArms();
    TestErr004_FreshAfterClear();
    TestErr005_FullySuppressedStillCaptures();

    /* 6.2 Error type encoding */
    TestErr006to009_AccessEtypes();
    TestErr010_PartialHit();
    TestErr011_NoRule();
    TestErr012_UnknownRrid();
    TestErr013_NoFetchSignal();
    TestErr014_NoUserEtype();

    /* 6.3 REQADDR / REQADDRH / REQID */
    TestErr015_ReqAddrLowOnly();
    TestErr016_ReqAddrHigh();
    TestErr017_EidMatched();
    TestErr018_EidInvalid();
    TestErr019_EidDisabled();
    TestErr020_NoErrRec();

    /* 6.4 Interrupt reaction */
    TestErr021_IrqAsserted();
    TestErr022_NoIrqWhenIeClear();
    TestErr023_ClearDeasserts();
    TestErr024_ReAssert();

    /* 6.5 Bus-error reaction */
    TestErr025_BusError();
    TestErr026_SuppressBusError();
    TestErr027_IndependentReactions();

    /* 6.6 ERR_CFG semantics */
    TestErr028_LockSticky();
    TestErr029_IeRsWarl();
    TestErr030_ReservedZero();
    TestErr031_ErrUser();

    /* Cross-combinations */
    TestErrX01_LockedImmutable();
    TestErrX02_SireSuppressesIrq();
    TestErrX03_SereSuppressesBusError();
    TestErrX04_MultiFault();
    TestErrX05_MsiDelivery();
    TestErrX06_StallViolation();

    printf("\nAll file-06 error-capture tests passed.\n");
    return 0;
}
