/*
 * test_07_config_protection_locks.c
 *
 * Test suite for docs/testplan/07-config-protection-locks.md:
 *   "Configuration Protection / Locks".
 *
 * Spec: §3 (Configuration Protection), §3.1-3.5, §4.2 (MDLCK/MDLCKH,
 *       MDCFGLCK, ENTRYLCK), §4.5.1 (SRCMD_EN.l), §4.3.1 (ERR_CFG.l).
 *
 * These assertions are SPEC-compliant: they encode the behaviour the spec
 * requires, not whatever the model happens to do. Locks are sticky until
 * reset and prevent the corresponding programmable field from changing.
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
    params.model    = IOPMP_MODEL_FULL;
    return params;
}

static void EnableIopmp(IopmpState_t *iopmp)
{
    IopmpWriteReg(iopmp, REG_HWCFG0, HWCFG0_ENABLE_BIT);
}

static uint32_t SrcmdEnOff(uint16_t rrid)
{
    return REG_SRCMD_BASE + (uint32_t)rrid * REG_SRCMD_STRIDE + REG_SRCMD_EN_OFF;
}
static uint32_t SrcmdEnhOff(uint16_t rrid)
{
    return REG_SRCMD_BASE + (uint32_t)rrid * REG_SRCMD_STRIDE + REG_SRCMD_ENH_OFF;
}
static uint32_t MdcfgOff(uint8_t md)
{
    return REG_MDCFG_BASE + (uint32_t)md * REG_MDCFG_STRIDE;
}
static uint32_t EntryOff(IopmpState_t *iopmp, uint32_t idx, uint32_t field)
{
    return IopmpReadReg(iopmp, REG_ENTRYOFFSET) + idx * REG_ENTRY_STRIDE + field;
}
static void SetNa4(IopmpState_t *iopmp, uint32_t idx, uint64_t byteAddr, uint32_t perm)
{
    IopmpWriteReg(iopmp, EntryOff(iopmp, idx, REG_ENTRY_ADDR_OFF), (uint32_t)(byteAddr >> 2U));
    IopmpWriteReg(iopmp, EntryOff(iopmp, idx, REG_ENTRY_CFG_OFF),
                  (ADDR_MODE_NA4 << ENTRY_CFG_A_SHIFT) | perm);
}

/* MDLCK bit for MD m is (m+1). */
#define MD_BIT(m)   (1U << ((m) + 1U))

/* ───────────────────────────────────────────────────────────────────
 * 7.1 SRCMD_EN(s).l - per-RRID SRCMD lock (WISS)
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-LOCK-001 - write md then latch l. */
static void TestLock001_WriteThenLatch(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(4, 4, 4);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    IopmpWriteReg(&iopmp, SrcmdEnOff(2), MD_BIT(1));         /* associate MD1 */
    assert((IopmpReadReg(&iopmp, SrcmdEnOff(2)) & MD_BIT(1)) != 0U);
    IopmpWriteReg(&iopmp, SrcmdEnOff(2), MD_BIT(1) | SRCMD_EN_L_BIT);
    uint32_t v = IopmpReadReg(&iopmp, SrcmdEnOff(2));
    assert((v & SRCMD_EN_L_BIT) != 0U);
    assert((v & MD_BIT(1)) != 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-LOCK-001 SRCMD_EN md write then latch l");
}

/* IOPMP-LOCK-002 - l=1 rejects md write. */
static void TestLock002_LockedRejectsMd(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(4, 4, 4);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    IopmpWriteReg(&iopmp, SrcmdEnOff(2), MD_BIT(1) | SRCMD_EN_L_BIT);
    uint32_t locked = IopmpReadReg(&iopmp, SrcmdEnOff(2));
    IopmpWriteReg(&iopmp, SrcmdEnOff(2), MD_BIT(2) | SRCMD_EN_L_BIT);  /* try add MD2 */
    assert(IopmpReadReg(&iopmp, SrcmdEnOff(2)) == locked);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-LOCK-002 locked SRCMD_EN rejects md");
}

/* IOPMP-LOCK-003 - l=1 also locks SRCMD_ENH. */
static void TestLock003_LockLocksEnh(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(4, 4, 40);             /* md_num>31 -> ENH exists */
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    IopmpWriteReg(&iopmp, SrcmdEnOff(2), SRCMD_EN_L_BIT);    /* lock row */
    IopmpWriteReg(&iopmp, SrcmdEnhOff(2), 0xFFFFFFFFU);      /* try set high MDs */
    assert(IopmpReadReg(&iopmp, SrcmdEnhOff(2)) == 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-LOCK-003 SRCMD_EN.l locks ENH");
}

/* IOPMP-LOCK-004 - l sticky: writing l=0 keeps it. */
static void TestLock004_LockSticky(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(4, 4, 4);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    IopmpWriteReg(&iopmp, SrcmdEnOff(2), SRCMD_EN_L_BIT);
    IopmpWriteReg(&iopmp, SrcmdEnOff(2), 0U);                /* attempt clear */
    assert((IopmpReadReg(&iopmp, SrcmdEnOff(2)) & SRCMD_EN_L_BIT) != 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-LOCK-004 SRCMD_EN.l sticky");
}

/* IOPMP-LOCK-005 - lock is per-RRID: other RRID still writable. */
static void TestLock005_PerRridScope(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(4, 4, 4);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    IopmpWriteReg(&iopmp, SrcmdEnOff(2), SRCMD_EN_L_BIT);    /* lock RRID2 */
    IopmpWriteReg(&iopmp, SrcmdEnOff(3), MD_BIT(1));         /* RRID3 unaffected */
    assert((IopmpReadReg(&iopmp, SrcmdEnOff(3)) & MD_BIT(1)) != 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-LOCK-005 SRCMD lock per-RRID scope");
}

/* ───────────────────────────────────────────────────────────────────
 * 7.2 MDLCK / MDLCKH - per-MD column lock
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-LOCK-006 - MDLCK.md[3]=1: MD3 bit rejected for all RRIDs; others OK. */
static void TestLock006_MdlckColumnLock(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(4, 4, 8);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    IopmpWriteReg(&iopmp, REG_MDLCK, MD_BIT(3));             /* lock MD3 column */
    for (uint16_t s = 0U; s < 4U; s++) {
        IopmpWriteReg(&iopmp, SrcmdEnOff(s), MD_BIT(2) | MD_BIT(3));
        uint32_t v = IopmpReadReg(&iopmp, SrcmdEnOff(s));
        assert((v & MD_BIT(2)) != 0U);                       /* MD2 writable */
        assert((v & MD_BIT(3)) == 0U);                       /* MD3 locked (stays 0) */
    }

    IopmpDestroy(&iopmp);
    PASS("IOPMP-LOCK-006 MDLCK locks MD column for all RRIDs");
}

/* IOPMP-LOCK-007 - MDLCK.l=1 freezes md/mdh. */
static void TestLock007_MdlckFrozen(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(4, 4, 40);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    IopmpWriteReg(&iopmp, REG_MDLCK, MD_BIT(1) | MDLCK_L_BIT);
    uint32_t mdlck = IopmpReadReg(&iopmp, REG_MDLCK);
    IopmpWriteReg(&iopmp, REG_MDLCK, MD_BIT(2));             /* frozen */
    IopmpWriteReg(&iopmp, REG_MDLCKH, 0xFFFFFFFFU);          /* frozen */
    assert(IopmpReadReg(&iopmp, REG_MDLCK)  == mdlck);
    assert(IopmpReadReg(&iopmp, REG_MDLCKH) == 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-LOCK-007 MDLCK.l freezes columns");
}

/* IOPMP-LOCK-008 - MDLCK.l sticky. */
static void TestLock008_MdlckLockSticky(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(4, 4, 4);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    IopmpWriteReg(&iopmp, REG_MDLCK, MDLCK_L_BIT);
    IopmpWriteReg(&iopmp, REG_MDLCK, 0U);
    assert((IopmpReadReg(&iopmp, REG_MDLCK) & MDLCK_L_BIT) != 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-LOCK-008 MDLCK.l sticky");
}

/* IOPMP-LOCK-009 - md_num<=31: MDLCKH not implemented, wired 0. */
static void TestLock009_MdlckhWiredZero(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(4, 4, 8);             /* md_num<=31 */
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    assert(IopmpReadReg(&iopmp, REG_MDLCKH) == 0U);
    IopmpWriteReg(&iopmp, REG_MDLCKH, 0xFFFFFFFFU);         /* must be ignored */
    assert(IopmpReadReg(&iopmp, REG_MDLCKH) == 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-LOCK-009 MDLCKH wired 0 when md_num<=31");
}

/* IOPMP-LOCK-010 - md_num>31; MDLCKH.mdh[5]=1 rejects SRCMD_ENH bit 5 for all s. */
static void TestLock010_MdlckhColumnLock(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(4, 4, 40);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    IopmpWriteReg(&iopmp, REG_MDLCKH, (1U << 5));           /* lock MD 36 (mdh bit 5) */
    for (uint16_t s = 0U; s < 4U; s++) {
        IopmpWriteReg(&iopmp, SrcmdEnhOff(s), 0xFFFFFFFFU);
        uint32_t v = IopmpReadReg(&iopmp, SrcmdEnhOff(s));
        assert((v & (1U << 5)) == 0U);                      /* mdh[5] rejected */
        assert((v & (1U << 4)) != 0U);                      /* mdh[4] writable */
    }

    IopmpDestroy(&iopmp);
    PASS("IOPMP-LOCK-010 MDLCKH locks high-MD column");
}

/* IOPMP-LOCK-011 - MDLCK not implemented: md wired 0, l wired 1. */
static void TestLock011_MdlckNotImplemented(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(4, 4, 4);
    params.mdlckDisable = true;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    uint32_t v = IopmpReadReg(&iopmp, REG_MDLCK);
    assert((v & MDLCK_L_BIT) != 0U);                        /* l wired 1 */
    assert((v & MDLCK_MD_MASK) == 0U);                      /* md wired 0 */
    IopmpWriteReg(&iopmp, REG_MDLCK, MD_BIT(1));            /* ignored */
    assert(IopmpReadReg(&iopmp, REG_MDLCK) == v);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-LOCK-011 MDLCK not implemented: md=0, l=1");
}

/* ───────────────────────────────────────────────────────────────────
 * 7.3 MDCFGLCK - MDCFG table lock (WISS l, WARL incremental f)
 * ─────────────────────────────────────────────────────────────────── */

static uint32_t ReadMdcfglckF(IopmpState_t *iopmp)
{
    return (IopmpReadReg(iopmp, REG_MDCFGLCK) & MDCFGLCK_F_MASK) >> MDCFGLCK_F_SHIFT;
}

/* IOPMP-LOCK-012 - write f=3 -> MDCFG(0,1,2) locked. */
static void TestLock012_MdcfglckSetF(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(4, 16, 6);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    IopmpWriteReg(&iopmp, REG_MDCFGLCK, 3U << MDCFGLCK_F_SHIFT);
    assert(ReadMdcfglckF(&iopmp) == 3U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-LOCK-012 MDCFGLCK.f=3");
}

/* IOPMP-LOCK-013 - MDCFG(2) write rejected when f=3. */
static void TestLock013_LockedMdcfgRejected(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(4, 16, 6);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    IopmpWriteReg(&iopmp, MdcfgOff(2), 5U);                 /* seed */
    IopmpWriteReg(&iopmp, REG_MDCFGLCK, 3U << MDCFGLCK_F_SHIFT);
    IopmpWriteReg(&iopmp, MdcfgOff(2), 9U);                 /* m=2 < f=3 -> rejected */
    assert(IopmpReadReg(&iopmp, MdcfgOff(2)) == 5U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-LOCK-013 locked MDCFG(2) rejected");
}

/* IOPMP-LOCK-014 - MDCFG(3) writable when f=3. */
static void TestLock014_UnlockedMdcfgWritable(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(4, 16, 6);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    IopmpWriteReg(&iopmp, REG_MDCFGLCK, 3U << MDCFGLCK_F_SHIFT);
    IopmpWriteReg(&iopmp, MdcfgOff(3), 9U);                 /* m=3 >= f -> allowed */
    assert(IopmpReadReg(&iopmp, MdcfgOff(3)) == 9U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-LOCK-014 unlocked MDCFG(3) writable");
}

/* IOPMP-LOCK-015 - f is incremental: smaller write rejected. */
static void TestLock015_FIncrementalOnly(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(4, 16, 6);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    IopmpWriteReg(&iopmp, REG_MDCFGLCK, 3U << MDCFGLCK_F_SHIFT);
    IopmpWriteReg(&iopmp, REG_MDCFGLCK, 2U << MDCFGLCK_F_SHIFT);   /* smaller */
    assert(ReadMdcfglckF(&iopmp) == 3U);                   /* unchanged */

    IopmpDestroy(&iopmp);
    PASS("IOPMP-LOCK-015 MDCFGLCK.f incremental only");
}

/* IOPMP-LOCK-016 - larger f accepted. */
static void TestLock016_FGrows(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(4, 16, 6);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    IopmpWriteReg(&iopmp, REG_MDCFGLCK, 3U << MDCFGLCK_F_SHIFT);
    IopmpWriteReg(&iopmp, REG_MDCFGLCK, 5U << MDCFGLCK_F_SHIFT);
    assert(ReadMdcfglckF(&iopmp) == 5U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-LOCK-016 MDCFGLCK.f grows");
}

/* IOPMP-LOCK-017 - f >= md_num locks all MDCFG entries. */
static void TestLock017_FBeyondMdNum(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(4, 16, 4);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    for (uint8_t m = 0U; m < 4U; m++) IopmpWriteReg(&iopmp, MdcfgOff(m), (m + 1U) * 2U);
    IopmpWriteReg(&iopmp, REG_MDCFGLCK, 0xFFU << MDCFGLCK_F_SHIFT);   /* huge f */

    for (uint8_t m = 0U; m < 4U; m++) {
        uint32_t before = IopmpReadReg(&iopmp, MdcfgOff(m));
        IopmpWriteReg(&iopmp, MdcfgOff(m), 15U);
        assert(IopmpReadReg(&iopmp, MdcfgOff(m)) == before);   /* all locked */
    }

    IopmpDestroy(&iopmp);
    PASS("IOPMP-LOCK-017 f>=md_num locks all MDCFG");
}

/* IOPMP-LOCK-018 - MDCFGLCK.l=1 freezes f. */
static void TestLock018_MdcfglckLockFreezesF(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(4, 16, 6);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    IopmpWriteReg(&iopmp, REG_MDCFGLCK, (2U << MDCFGLCK_F_SHIFT) | MDCFGLCK_L_BIT);
    IopmpWriteReg(&iopmp, REG_MDCFGLCK, 5U << MDCFGLCK_F_SHIFT);
    assert(ReadMdcfglckF(&iopmp) == 2U);                   /* frozen */

    IopmpDestroy(&iopmp);
    PASS("IOPMP-LOCK-018 MDCFGLCK.l freezes f");
}

/* IOPMP-LOCK-019 - locking MD m implies MD 0..m-1 locked (incremental f). */
static void TestLock019_PrecedingLocked(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(4, 16, 6);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    for (uint8_t m = 0U; m < 6U; m++) IopmpWriteReg(&iopmp, MdcfgOff(m), (m + 1U) * 2U);
    IopmpWriteReg(&iopmp, REG_MDCFGLCK, 4U << MDCFGLCK_F_SHIFT);   /* lock through MD3 */

    /* MD0..3 all locked (the count model guarantees preceding MDs locked). */
    for (uint8_t m = 0U; m < 4U; m++) {
        uint32_t before = IopmpReadReg(&iopmp, MdcfgOff(m));
        IopmpWriteReg(&iopmp, MdcfgOff(m), 15U);
        assert(IopmpReadReg(&iopmp, MdcfgOff(m)) == before);
    }

    IopmpDestroy(&iopmp);
    PASS("IOPMP-LOCK-019 locking MD m locks all preceding");
}

/* ───────────────────────────────────────────────────────────────────
 * 7.4 ENTRYLCK - entry array lock
 * ─────────────────────────────────────────────────────────────────── */

static uint32_t ReadEntrylckF(IopmpState_t *iopmp)
{
    return (IopmpReadReg(iopmp, REG_ENTRYLCK) & ENTRYLCK_F_MASK) >> ENTRYLCK_F_SHIFT;
}

/* IOPMP-LOCK-020 - write f=4 locks entries 0..3. */
static void TestLock020_EntrylckSetF(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(4, 8, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    IopmpWriteReg(&iopmp, REG_ENTRYLCK, 4U << ENTRYLCK_F_SHIFT);
    assert(ReadEntrylckF(&iopmp) == 4U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-LOCK-020 ENTRYLCK.f=4");
}

/* IOPMP-LOCK-021 - ENTRY_ADDR/ADDRH/CFG(2) all rejected when f=4. */
static void TestLock021_LockedEntryRejected(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(4, 8, 1);
    params.addrhEn = true;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    SetNa4(&iopmp, 2, 0x2000ULL, ENTRY_CFG_R_BIT);
    IopmpWriteReg(&iopmp, EntryOff(&iopmp, 2, REG_ENTRY_ADDRH_OFF), 0U);
    uint32_t a = IopmpReadReg(&iopmp, EntryOff(&iopmp, 2, REG_ENTRY_ADDR_OFF));
    uint32_t h = IopmpReadReg(&iopmp, EntryOff(&iopmp, 2, REG_ENTRY_ADDRH_OFF));
    uint32_t c = IopmpReadReg(&iopmp, EntryOff(&iopmp, 2, REG_ENTRY_CFG_OFF));

    IopmpWriteReg(&iopmp, REG_ENTRYLCK, 4U << ENTRYLCK_F_SHIFT);
    IopmpWriteReg(&iopmp, EntryOff(&iopmp, 2, REG_ENTRY_ADDR_OFF),  0xDEADU);
    IopmpWriteReg(&iopmp, EntryOff(&iopmp, 2, REG_ENTRY_ADDRH_OFF), 0xBEEFU);
    IopmpWriteReg(&iopmp, EntryOff(&iopmp, 2, REG_ENTRY_CFG_OFF),   ENTRY_CFG_W_BIT);
    assert(IopmpReadReg(&iopmp, EntryOff(&iopmp, 2, REG_ENTRY_ADDR_OFF))  == a);
    assert(IopmpReadReg(&iopmp, EntryOff(&iopmp, 2, REG_ENTRY_ADDRH_OFF)) == h);
    assert(IopmpReadReg(&iopmp, EntryOff(&iopmp, 2, REG_ENTRY_CFG_OFF))   == c);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-LOCK-021 locked entry fields rejected");
}

/* IOPMP-LOCK-022 - ENTRY_CFG(4) writable when f=4. */
static void TestLock022_UnlockedEntryWritable(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(4, 8, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    IopmpWriteReg(&iopmp, REG_ENTRYLCK, 4U << ENTRYLCK_F_SHIFT);
    SetNa4(&iopmp, 4, 0x4000ULL, ENTRY_CFG_R_BIT);         /* i=4 >= f -> allowed */
    uint32_t c = IopmpReadReg(&iopmp, EntryOff(&iopmp, 4, REG_ENTRY_CFG_OFF));
    assert((c & ENTRY_CFG_R_BIT) != 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-LOCK-022 unlocked entry writable");
}

/* IOPMP-LOCK-023 - ENTRYLCK.f incremental: smaller rejected. */
static void TestLock023_EntrylckIncremental(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(4, 8, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    IopmpWriteReg(&iopmp, REG_ENTRYLCK, 4U << ENTRYLCK_F_SHIFT);
    IopmpWriteReg(&iopmp, REG_ENTRYLCK, 2U << ENTRYLCK_F_SHIFT);
    assert(ReadEntrylckF(&iopmp) == 4U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-LOCK-023 ENTRYLCK.f incremental only");
}

/* IOPMP-LOCK-024 - f > entry_num locks all entries. */
static void TestLock024_FBeyondEntryNum(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(4, 8, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    SetNa4(&iopmp, 7, 0x7000ULL, ENTRY_CFG_R_BIT);
    uint32_t c = IopmpReadReg(&iopmp, EntryOff(&iopmp, 7, REG_ENTRY_CFG_OFF));
    IopmpWriteReg(&iopmp, REG_ENTRYLCK, 0xFFFFU << ENTRYLCK_F_SHIFT);
    IopmpWriteReg(&iopmp, EntryOff(&iopmp, 7, REG_ENTRY_CFG_OFF), ENTRY_CFG_W_BIT);
    assert(IopmpReadReg(&iopmp, EntryOff(&iopmp, 7, REG_ENTRY_CFG_OFF)) == c);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-LOCK-024 f>entry_num locks all");
}

/* IOPMP-LOCK-025 - ENTRYLCK.l=1 freezes f. */
static void TestLock025_EntrylckLockFreezesF(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(4, 8, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    IopmpWriteReg(&iopmp, REG_ENTRYLCK, (2U << ENTRYLCK_F_SHIFT) | ENTRYLCK_L_BIT);
    IopmpWriteReg(&iopmp, REG_ENTRYLCK, 6U << ENTRYLCK_F_SHIFT);
    assert(ReadEntrylckF(&iopmp) == 2U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-LOCK-025 ENTRYLCK.l freezes f");
}

/* ───────────────────────────────────────────────────────────────────
 * 7.5 ERR_CFG.l - error-config lock (WISS)
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-LOCK-026 - set ie/rs then latch l. */
static void TestLock026_ErrCfgLatch(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(4, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    IopmpWriteReg(&iopmp, REG_ERR_CFG, ERR_CFG_IE_BIT | ERR_CFG_RS_BIT | ERR_CFG_L_BIT);
    uint32_t v = IopmpReadReg(&iopmp, REG_ERR_CFG);
    assert((v & ERR_CFG_IE_BIT) && (v & ERR_CFG_RS_BIT) && (v & ERR_CFG_L_BIT));

    IopmpDestroy(&iopmp);
    PASS("IOPMP-LOCK-026 ERR_CFG ie/rs set + l latched");
}

/* IOPMP-LOCK-027 - l=1 rejects ie/rs writes. */
static void TestLock027_ErrCfgLocked(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(4, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    IopmpWriteReg(&iopmp, REG_ERR_CFG, ERR_CFG_IE_BIT | ERR_CFG_L_BIT);
    uint32_t locked = IopmpReadReg(&iopmp, REG_ERR_CFG);
    IopmpWriteReg(&iopmp, REG_ERR_CFG, ERR_CFG_RS_BIT);    /* try change */
    assert(IopmpReadReg(&iopmp, REG_ERR_CFG) == locked);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-LOCK-027 locked ERR_CFG rejects ie/rs");
}

/* IOPMP-LOCK-028 - error-record regs have no lock: ERR_INFO.v clearable. */
static void TestLock028_ErrInfoNoLock(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(4, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    EnableIopmp(&iopmp);
    IopmpWriteReg(&iopmp, MdcfgOff(0), 4U);
    IopmpWriteReg(&iopmp, SrcmdEnOff(0), MD_BIT(0));
    SetNa4(&iopmp, 0, 0x2000ULL, ENTRY_CFG_W_BIT);

    /* Lock ERR_CFG, then capture a violation. */
    IopmpWriteReg(&iopmp, REG_ERR_CFG, ERR_CFG_L_BIT);
    IopmpCheckAccess(&iopmp, 0, 0x2000ULL, 4, IOPMP_TXN_READ);
    assert((IopmpReadReg(&iopmp, REG_ERR_INFO) & ERR_INFO_V_BIT) != 0U);

    /* ERR_INFO has no lock - v is still clearable despite ERR_CFG.l=1. */
    IopmpWriteReg(&iopmp, REG_ERR_INFO, ERR_INFO_V_BIT);
    assert((IopmpReadReg(&iopmp, REG_ERR_INFO) & ERR_INFO_V_BIT) == 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-LOCK-028 error record regs unaffected by ERR_CFG.l");
}

/* ───────────────────────────────────────────────────────────────────
 * 7.6 Prelocked configurations (§3.5)
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-LOCK-029 - prelocked MDCFGLCK.f=2 at reset rejects MDCFG(1). */
static void TestLock029_PrelockedMdcfg(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(4, 16, 6);
    params.mdcfglckResetF = 2;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    assert(ReadMdcfglckF(&iopmp) == 2U);                   /* reset value */
    IopmpWriteReg(&iopmp, MdcfgOff(1), 9U);                /* m=1 < 2 -> rejected */
    assert(IopmpReadReg(&iopmp, MdcfgOff(1)) == 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-LOCK-029 prelocked MDCFGLCK.f");
}

/* IOPMP-LOCK-030 - prelocked ENTRYLCK.f=4 at reset rejects ENTRY_CFG(0). */
static void TestLock030_PrelockedEntry(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(4, 8, 1);
    params.entrylckResetF = 4;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    assert(ReadEntrylckF(&iopmp) == 4U);
    IopmpWriteReg(&iopmp, EntryOff(&iopmp, 0, REG_ENTRY_CFG_OFF), ENTRY_CFG_R_BIT);
    assert(IopmpReadReg(&iopmp, EntryOff(&iopmp, 0, REG_ENTRY_CFG_OFF)) == 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-LOCK-030 prelocked ENTRYLCK.f");
}

/* IOPMP-LOCK-031 - SRCMD prelocked via MDLCK.md preset: locked md bit rejected. */
static void TestLock031_PrelockedSrcmdViaMdlck(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(4, 8, 4);
    params.mdlckPreset = MD_BIT(2);                        /* MD2 prelocked */
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    assert((IopmpReadReg(&iopmp, REG_MDLCK) & MD_BIT(2)) != 0U);   /* preset at reset */
    IopmpWriteReg(&iopmp, SrcmdEnOff(0), MD_BIT(2) | MD_BIT(1));
    uint32_t v = IopmpReadReg(&iopmp, SrcmdEnOff(0));
    assert((v & MD_BIT(2)) == 0U);                         /* locked md bit rejected */
    assert((v & MD_BIT(1)) != 0U);                         /* unlocked md bit set */

    IopmpDestroy(&iopmp);
    PASS("IOPMP-LOCK-031 prelocked SRCMD via MDLCK preset");
}

/* IOPMP-LOCK-032 - ENTRYLCK hardwired (l wired 1, f fixed): entry write permanently rejected. */
static void TestLock032_EntrylckHardwired(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(4, 8, 1);
    params.entrylckResetF    = 4;
    params.entrylckHardwired = true;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    assert((IopmpReadReg(&iopmp, REG_ENTRYLCK) & ENTRYLCK_L_BIT) != 0U);   /* l wired 1 */
    IopmpWriteReg(&iopmp, EntryOff(&iopmp, 0, REG_ENTRY_CFG_OFF), ENTRY_CFG_R_BIT);
    assert(IopmpReadReg(&iopmp, EntryOff(&iopmp, 0, REG_ENTRY_CFG_OFF)) == 0U);
    /* f cannot be changed either (l frozen). */
    IopmpWriteReg(&iopmp, REG_ENTRYLCK, 6U << ENTRYLCK_F_SHIFT);
    assert(ReadEntrylckF(&iopmp) == 4U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-LOCK-032 ENTRYLCK hardwired permanent");
}

/* ───────────────────────────────────────────────────────────────────
 * Cross-combinations (file-local)
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-LOCK-X01 - locked high-priority deny entry still wins over later allow. */
static void TestLockX01_DefenseInDepth(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(4, 8, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    EnableIopmp(&iopmp);
    IopmpWriteReg(&iopmp, MdcfgOff(0), 8U);
    IopmpWriteReg(&iopmp, SrcmdEnOff(0), MD_BIT(0));

    SetNa4(&iopmp, 0, 0x2000ULL, 0U);                      /* entry 0: deny all */
    IopmpWriteReg(&iopmp, REG_ENTRYLCK, 1U << ENTRYLCK_F_SHIFT);   /* lock entry 0 */

    /* Compromised SW configures entry 5 to allow the same address. */
    SetNa4(&iopmp, 5, 0x2000ULL, ENTRY_CFG_R_BIT);

    /* Highest-priority locked deny entry 0 still wins. */
    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x2000ULL, 4, IOPMP_TXN_READ);
    ASSERT_ILLEGAL(r);
    assert(r.entryIdx == 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-LOCK-X01 locked high-priority deny holds");
}

/* IOPMP-LOCK-X02 - MDLCK-locked association cannot be removed via SRCMD. */
static void TestLockX02_CannotReassociate(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(4, 8, 4);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    EnableIopmp(&iopmp);
    IopmpWriteReg(&iopmp, MdcfgOff(0), 8U);

    SetNa4(&iopmp, 0, 0x2000ULL, 0U);                      /* deny-all entry in MD0 */
    IopmpWriteReg(&iopmp, SrcmdEnOff(0), MD_BIT(0));       /* associate MD0 */
    IopmpWriteReg(&iopmp, REG_MDLCK, MD_BIT(0));           /* lock MD0 column */

    /* Attempt to deassociate by clearing the SRCMD bit -> must be preserved. */
    IopmpWriteReg(&iopmp, SrcmdEnOff(0), 0U);
    assert((IopmpReadReg(&iopmp, SrcmdEnOff(0)) & MD_BIT(0)) != 0U);

    /* Protection holds: the deny entry still applies. */
    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x2000ULL, 4, IOPMP_TXN_READ);
    ASSERT_ILLEGAL(r);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-LOCK-X02 MDLCK-locked association cannot be removed");
}

/* IOPMP-LOCK-X03 - under stall, locked entries untouched while unlocked update. */
static void TestLockX03_StallReconfig(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(4, 8, 1);
    params.stallEn = true;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    EnableIopmp(&iopmp);
    IopmpWriteReg(&iopmp, MdcfgOff(0), 8U);
    IopmpWriteReg(&iopmp, SrcmdEnOff(0), MD_BIT(0));

    SetNa4(&iopmp, 0, 0x2000ULL, ENTRY_CFG_R_BIT);         /* locked entry */
    uint32_t locked0 = IopmpReadReg(&iopmp, EntryOff(&iopmp, 0, REG_ENTRY_CFG_OFF));
    IopmpWriteReg(&iopmp, REG_ENTRYLCK, 1U << ENTRYLCK_F_SHIFT);

    /* Stall RRID0, reconfigure unlocked entry 1, then attempt locked entry 0. */
    IopmpWriteReg(&iopmp, REG_RRIDSCP,
                  (0U & RRIDSCP_RRID_MASK) | (RRIDSCP_OP_STALL << RRIDSCP_OP_SHIFT));
    SetNa4(&iopmp, 1, 0x3000ULL, ENTRY_CFG_W_BIT);                       /* unlocked -> updated */
    IopmpWriteReg(&iopmp, EntryOff(&iopmp, 0, REG_ENTRY_CFG_OFF), ENTRY_CFG_W_BIT);  /* locked -> rejected */
    IopmpWriteReg(&iopmp, REG_RRIDSCP,
                  (0U & RRIDSCP_RRID_MASK) | (RRIDSCP_OP_NOSTALL << RRIDSCP_OP_SHIFT));

    assert(IopmpReadReg(&iopmp, EntryOff(&iopmp, 0, REG_ENTRY_CFG_OFF)) == locked0);
    assert((IopmpReadReg(&iopmp, EntryOff(&iopmp, 1, REG_ENTRY_CFG_OFF)) & ENTRY_CFG_W_BIT) != 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-LOCK-X03 locked entries untouched during stall reconfig");
}

/* IOPMP-LOCK-X04 - MDCFGLCK.f locks MD m and all preceding; cannot grow via MDCFG(m-1). */
static void TestLockX04_CannotGrowLockedMd(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(4, 16, 6);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    IopmpWriteReg(&iopmp, MdcfgOff(1), 5U);
    IopmpWriteReg(&iopmp, REG_MDCFGLCK, 2U << MDCFGLCK_F_SHIFT);   /* MD0,1 locked */

    /* MDCFG(1) (the boundary feeding MD2) is locked -> cannot manipulate. */
    IopmpWriteReg(&iopmp, MdcfgOff(1), 1U);
    assert(IopmpReadReg(&iopmp, MdcfgOff(1)) == 5U);
    /* MDCFG(2) remains writable (MD2 not locked). */
    IopmpWriteReg(&iopmp, MdcfgOff(2), 7U);
    assert(IopmpReadReg(&iopmp, MdcfgOff(2)) == 7U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-LOCK-X04 cannot grow locked MD via preceding boundary");
}

/* IOPMP-LOCK-X05 - Isolation model: fixed RRID->MD mapping; MDLCK.l sticky. */
static void TestLockX05_IsolationModelLocks(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(2, 4, 2);
    params.model    = IOPMP_MODEL_ISOLATION;
    params.srcmdFmt = 1U;                                  /* RRID i -> MD i, no SRCMD table */
    params.mdcfgFmt = 1U;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    EnableIopmp(&iopmp);

    /* MDLCK lock bit is still accessible and sticky. */
    IopmpWriteReg(&iopmp, REG_MDLCK, MDLCK_L_BIT);
    IopmpWriteReg(&iopmp, REG_MDLCK, 0U);
    assert((IopmpReadReg(&iopmp, REG_MDLCK) & MDLCK_L_BIT) != 0U);

    /* Association is hardwired RRID i -> MD i and cannot be redirected via SRCMD. */
    SetNa4(&iopmp, 2, 0x2000ULL, ENTRY_CFG_R_BIT);         /* MD1 owns entries {2,3} */
    ASSERT_LEGAL(IopmpCheckAccess(&iopmp, 1, 0x2000ULL, 4, IOPMP_TXN_READ));
    ASSERT_ETYPE(IopmpCheckAccess(&iopmp, 0, 0x2000ULL, 4, IOPMP_TXN_READ),
                 IOPMP_ETYPE_NO_RULE);                     /* RRID0 cannot reach MD1 */

    IopmpDestroy(&iopmp);
    PASS("IOPMP-LOCK-X05 isolation model fixed mapping + MDLCK sticky");
}

/* ── Runner ──────────────────────────────────────────────────────── */

int main(void)
{
    /* 7.1 SRCMD_EN.l */
    TestLock001_WriteThenLatch();
    TestLock002_LockedRejectsMd();
    TestLock003_LockLocksEnh();
    TestLock004_LockSticky();
    TestLock005_PerRridScope();

    /* 7.2 MDLCK / MDLCKH */
    TestLock006_MdlckColumnLock();
    TestLock007_MdlckFrozen();
    TestLock008_MdlckLockSticky();
    TestLock009_MdlckhWiredZero();
    TestLock010_MdlckhColumnLock();
    TestLock011_MdlckNotImplemented();

    /* 7.3 MDCFGLCK */
    TestLock012_MdcfglckSetF();
    TestLock013_LockedMdcfgRejected();
    TestLock014_UnlockedMdcfgWritable();
    TestLock015_FIncrementalOnly();
    TestLock016_FGrows();
    TestLock017_FBeyondMdNum();
    TestLock018_MdcfglckLockFreezesF();
    TestLock019_PrecedingLocked();

    /* 7.4 ENTRYLCK */
    TestLock020_EntrylckSetF();
    TestLock021_LockedEntryRejected();
    TestLock022_UnlockedEntryWritable();
    TestLock023_EntrylckIncremental();
    TestLock024_FBeyondEntryNum();
    TestLock025_EntrylckLockFreezesF();

    /* 7.5 ERR_CFG.l */
    TestLock026_ErrCfgLatch();
    TestLock027_ErrCfgLocked();
    TestLock028_ErrInfoNoLock();

    /* 7.6 Prelocked */
    TestLock029_PrelockedMdcfg();
    TestLock030_PrelockedEntry();
    TestLock031_PrelockedSrcmdViaMdlck();
    TestLock032_EntrylckHardwired();

    /* Cross-combinations */
    TestLockX01_DefenseInDepth();
    TestLockX02_CannotReassociate();
    TestLockX03_StallReconfig();
    TestLockX04_CannotGrowLockedMd();
    TestLockX05_IsolationModelLocks();

    printf("\nAll file-07 config-protection / lock tests passed.\n");
    return 0;
}
