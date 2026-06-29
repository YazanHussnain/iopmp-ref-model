/*
 * test_04_entry_addr_modes.c
 *
 * Test suite for docs/testplan/04-entry-addr-modes.md:
 *   "Entry Array & Address-Mode Decode".
 *
 * Spec: §2.4 (Entry & Entry Array), §4.6.1 (ENTRY_ADDR/ADDRH),
 *       §4.6.2 (ENTRY_CFG r/w/x/a), §4.6.3 (ENTRY_USER_CFG), PMP encoding.
 *
 * Region decode (base/size/active) is verified white-box via the entry
 * helpers; permission and global-override behaviour is verified through the
 * transaction API. NOTE comments mark deliberate deviations from the test
 * plan's ideal.
 *
 * Model NAPOT convention (matches RISC-V PMP): k = number of trailing one
 * bits in the word address, region size = 8 << k bytes. So an 8-byte region
 * has zero trailing ones (bit0=0); a 16-byte region has one (bit0=1).
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include "iopmp.h"
#include "iopmp_internal.h"   /* EntryGetBase/Size/IsActive/CoversAnyByte */
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

/* Write raw ENTRY_ADDR (word address) and ENTRY_CFG for an entry. */
static void SetEntryRaw(IopmpState_t *iopmp, uint32_t idx,
                        uint32_t wordAddr, uint32_t cfg)
{
    uint32_t base = IopmpReadReg(iopmp, REG_ENTRYOFFSET);
    uint32_t slot = base + idx * REG_ENTRY_STRIDE;
    IopmpWriteReg(iopmp, slot + REG_ENTRY_ADDR_OFF, wordAddr);
    IopmpWriteReg(iopmp, slot + REG_ENTRY_CFG_OFF,  cfg);
}

static void SetEntryAddrh(IopmpState_t *iopmp, uint32_t idx, uint32_t addrh)
{
    uint32_t base = IopmpReadReg(iopmp, REG_ENTRYOFFSET);
    uint32_t slot = base + idx * REG_ENTRY_STRIDE;
    IopmpWriteReg(iopmp, slot + REG_ENTRY_ADDRH_OFF, addrh);
}

static uint32_t EntryCfgOff(IopmpState_t *iopmp, uint32_t idx)
{
    return IopmpReadReg(iopmp, REG_ENTRYOFFSET) + idx * REG_ENTRY_STRIDE + REG_ENTRY_CFG_OFF;
}

static uint32_t EntryAddrhOff(IopmpState_t *iopmp, uint32_t idx)
{
    return IopmpReadReg(iopmp, REG_ENTRYOFFSET) + idx * REG_ENTRY_STRIDE + REG_ENTRY_ADDRH_OFF;
}

/* Configure NA4 entry at byteAddr with permission bits. */
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

/* Wire a single-MD instance: RRID0 -> MD0 owning all entries. */
static void Wire1Md(IopmpState_t *iopmp, uint16_t entryNum)
{
    EnableIopmp(iopmp);
    SetupMdcfg(iopmp, 0, entryNum);
    SetupSrcmd(iopmp, 0, 0);
}

/* ───────────────────────────────────────────────────────────────────
 * 4.1 OFF mode
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-ENTRY-001 - a=OFF entry is not a match candidate. */
static void TestEntry001_OffNotCandidate(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    SetEntryRaw(&iopmp, 0, (uint32_t)(0x1000ULL >> 2U), 0x00U);  /* a=OFF */
    assert(!EntryIsActive(&iopmp, 0));
    assert(EntryGetBase(&iopmp, 0) == 0ULL);                     /* OFF: no region */
    assert(EntryGetSize(&iopmp, 0) == 0ULL);

    Wire1Md(&iopmp, 4);
    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ);
    ASSERT_ETYPE(r, IOPMP_ETYPE_NO_RULE);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ENTRY-001 OFF entry not a candidate");
}

/* IOPMP-ENTRY-002 - entry index >= entry_num treated as unavailable (OFF). */
static void TestEntry002_OutOfRangeUnavailable(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 2, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    /* Reading the entry at index == entry_num returns 0 (not implemented). */
    uint32_t off = IopmpReadReg(&iopmp, REG_ENTRYOFFSET)
                   + params.entryNum * REG_ENTRY_STRIDE + REG_ENTRY_CFG_OFF;
    assert(IopmpReadReg(&iopmp, off) == 0U);

    /* The checker only walks indices < entry_num, so no rule beyond it. */
    Wire1Md(&iopmp, 2);
    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ);
    ASSERT_ETYPE(r, IOPMP_ETYPE_NO_RULE);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ENTRY-002 out-of-range entry unavailable");
}

/* IOPMP-ENTRY-003 - all entries OFF, RRID associated -> NO_RULE. */
static void TestEntry003_AllOffNoRule(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire1Md(&iopmp, 4);
    /* All entry CFGs default to 0 (OFF). */

    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ);
    ASSERT_ETYPE(r, IOPMP_ETYPE_NO_RULE);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ENTRY-003 all OFF -> NO_RULE");
}

/* ───────────────────────────────────────────────────────────────────
 * 4.2 NA4 mode
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-ENTRY-004 - NA4 ENTRY_ADDR=A -> base=A<<2, size=4. */
static void TestEntry004_Na4Decode(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    SetNa4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_R_BIT);
    assert(EntryGetBase(&iopmp, 0) == 0x1000ULL);
    assert(EntryGetSize(&iopmp, 0) == 4ULL);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ENTRY-004 NA4 base/size decode");
}

/* IOPMP-ENTRY-005 - NA4 txn addr=base len=4 fully covered -> LEGAL. */
static void TestEntry005_Na4FullCover(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire1Md(&iopmp, 4);

    SetNa4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_R_BIT);
    ASSERT_LEGAL(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ));

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ENTRY-005 NA4 full cover legal");
}

/* IOPMP-ENTRY-006 - NA4 txn len=8 spans beyond 4 -> partial hit 0x04. */
static void TestEntry006_Na4PartialHit(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire1Md(&iopmp, 4);

    SetNa4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_R_BIT);
    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 8, IOPMP_TXN_READ);
    ASSERT_ETYPE(r, IOPMP_ETYPE_PARTIAL_HIT);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ENTRY-006 NA4 partial hit");
}

/* IOPMP-ENTRY-007 - NA4 txn addr=base+4 just outside -> no match. */
static void TestEntry007_Na4JustOutside(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire1Md(&iopmp, 4);

    SetNa4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_R_BIT);
    assert(!EntryCoversAnyByte(&iopmp, 0, 0x1004ULL, 4));
    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x1004ULL, 4, IOPMP_TXN_READ);
    ASSERT_ETYPE(r, IOPMP_ETYPE_NO_RULE);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ENTRY-007 NA4 just outside -> no match");
}

/* ───────────────────────────────────────────────────────────────────
 * 4.3 NAPOT mode
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-ENTRY-008 - NAPOT 16-byte region decode (k=1). */
static void TestEntry008_Napot16Decode(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    /* base 0x1000, k=1 -> word addr = 0x400 | 1 = 0x401, size 16. */
    SetEntryRaw(&iopmp, 0, 0x401U, (ADDR_MODE_NAPOT << ENTRY_CFG_A_SHIFT) | ENTRY_CFG_R_BIT);
    assert(EntryGetBase(&iopmp, 0) == 0x1000ULL);
    assert(EntryGetSize(&iopmp, 0) == 16ULL);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ENTRY-008 NAPOT 16-byte decode");
}

/* IOPMP-ENTRY-009 - NAPOT 4KB region, txn fully inside -> LEGAL. */
static void TestEntry009_Napot4kFullCover(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire1Md(&iopmp, 4);

    /* base 0x10000, size 4096 -> k=9, word addr = 0x4000 | 0x1FF = 0x41FF. */
    SetEntryRaw(&iopmp, 0, 0x41FFU, (ADDR_MODE_NAPOT << ENTRY_CFG_A_SHIFT) | ENTRY_CFG_R_BIT);
    assert(EntryGetBase(&iopmp, 0) == 0x10000ULL);
    assert(EntryGetSize(&iopmp, 0) == 4096ULL);
    ASSERT_LEGAL(IopmpCheckAccess(&iopmp, 0, 0x10800ULL, 8, IOPMP_TXN_READ));

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ENTRY-009 NAPOT 4KB full cover");
}

/* IOPMP-ENTRY-010 - NAPOT region, txn crosses upper boundary -> partial hit. */
static void TestEntry010_NapotCrossBoundary(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire1Md(&iopmp, 4);

    /* 16-byte NAPOT at 0x1000 -> [0x1000, 0x1010). Read 8 bytes at 0x100C crosses. */
    SetEntryRaw(&iopmp, 0, 0x401U, (ADDR_MODE_NAPOT << ENTRY_CFG_A_SHIFT) | ENTRY_CFG_R_BIT);
    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x100CULL, 8, IOPMP_TXN_READ);
    ASSERT_ETYPE(r, IOPMP_ETYPE_PARTIAL_HIT);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ENTRY-010 NAPOT crosses boundary -> partial hit");
}

/* IOPMP-ENTRY-011 - NAPOT smallest 8-byte region (k=0). */
static void TestEntry011_Napot8Smallest(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    /* NOTE: model/PMP convention - an 8-byte NAPOT has ZERO trailing ones
     * (word addr bit0=0). The test plan annotated this as "addr=...0b1", which
     * is off by one; the model is PMP-correct. base 0x2000 -> word 0x800. */
    SetEntryRaw(&iopmp, 0, 0x800U, (ADDR_MODE_NAPOT << ENTRY_CFG_A_SHIFT) | ENTRY_CFG_R_BIT);
    assert(EntryGetBase(&iopmp, 0) == 0x2000ULL);
    assert(EntryGetSize(&iopmp, 0) == 8ULL);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ENTRY-011 NAPOT 8-byte smallest");
}

/* IOPMP-ENTRY-012 - NAPOT very large region, no overflow. */
static void TestEntry012_NapotLarge(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    /* 20 trailing ones -> size = 8 << 20 = 8 MiB, base aligned to 0. */
    SetEntryRaw(&iopmp, 0, 0x000FFFFFU, (ADDR_MODE_NAPOT << ENTRY_CFG_A_SHIFT) | ENTRY_CFG_R_BIT);
    assert(EntryGetBase(&iopmp, 0) == 0ULL);
    assert(EntryGetSize(&iopmp, 0) == ((uint64_t)8U << 20));

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ENTRY-012 NAPOT large region no overflow");
}

/* ───────────────────────────────────────────────────────────────────
 * 4.4 TOR mode
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-ENTRY-013 - TOR region [L<<2, H<<2), size=(H-L)<<2. */
static void TestEntry013_TorDecode(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    SetEntryRaw(&iopmp, 0, 0x400U, 0x00U);                  /* prev raw addr = L */
    SetEntryRaw(&iopmp, 1, 0x800U,
                (ADDR_MODE_TOR << ENTRY_CFG_A_SHIFT) | ENTRY_CFG_R_BIT);
    assert(EntryGetBase(&iopmp, 1) == 0x1000ULL);           /* 0x400<<2 */
    assert(EntryGetSize(&iopmp, 1) == 0x1000ULL);           /* (0x800-0x400)<<2 */

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ENTRY-013 TOR decode with predecessor");
}

/* IOPMP-ENTRY-014 - TOR at index 0: lower bound 0, region [0, addr<<2). */
static void TestEntry014_TorIndexZero(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    SetEntryRaw(&iopmp, 0, 0x800U,
                (ADDR_MODE_TOR << ENTRY_CFG_A_SHIFT) | ENTRY_CFG_R_BIT);
    assert(EntryGetBase(&iopmp, 0) == 0ULL);
    assert(EntryGetSize(&iopmp, 0) == 0x2000ULL);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ENTRY-014 TOR at index 0");
}

/* IOPMP-ENTRY-015 - TOR as first entry of an MD derives base from prior MD. */
static void TestEntry015_TorFirstOfMdHazard(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 2);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    /* MD0 owns {0}, MD1 owns {1,2,3}; entry 1 (first of MD1) is TOR. */
    SetupMdcfg(&iopmp, 0, 1);
    SetupMdcfg(&iopmp, 1, 4);
    SetEntryRaw(&iopmp, 0, 0x400U, 0x00U);                  /* in MD0 */
    SetEntryRaw(&iopmp, 1, 0x800U,
                (ADDR_MODE_TOR << ENTRY_CFG_A_SHIFT) | ENTRY_CFG_R_BIT);  /* first of MD1 */

    /* Hazard: the TOR base comes from entry 0, which lives in a different MD. */
    assert(EntryGetBase(&iopmp, 1) == 0x1000ULL);           /* derived from MD0's entry */

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ENTRY-015 TOR-first-of-MD cross-MD base hazard");
}

/* IOPMP-ENTRY-016 - TOR region shifts when the predecessor's address changes. */
static void TestEntry016_TorShiftsWithPrev(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    SetEntryRaw(&iopmp, 0, 0x400U, 0x00U);
    SetEntryRaw(&iopmp, 1, 0x800U,
                (ADDR_MODE_TOR << ENTRY_CFG_A_SHIFT) | ENTRY_CFG_R_BIT);
    assert(EntryGetBase(&iopmp, 1) == 0x1000ULL);

    /* Change the predecessor's address - the TOR base moves with it. */
    SetEntryRaw(&iopmp, 0, 0x600U, 0x00U);
    assert(EntryGetBase(&iopmp, 1) == 0x1800ULL);
    assert(EntryGetSize(&iopmp, 1) == 0x800ULL);            /* (0x800-0x600)<<2 */

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ENTRY-016 TOR region tracks predecessor address");
}

/* IOPMP-ENTRY-017 - TOR with H <= L: empty region, covers nothing. */
static void TestEntry017_TorEmptyRange(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    SetEntryRaw(&iopmp, 0, 0x800U, 0x00U);                  /* L = 0x2000 */
    SetEntryRaw(&iopmp, 1, 0x400U,                          /* H = 0x1000 < L */
                (ADDR_MODE_TOR << ENTRY_CFG_A_SHIFT) | ENTRY_CFG_R_BIT);
    assert(EntryGetSize(&iopmp, 1) == 0ULL);
    assert(!EntryCoversAnyByte(&iopmp, 1, 0x1800ULL, 4));

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ENTRY-017 TOR empty range covers nothing");
}

/* IOPMP-ENTRY-018 - tor_en=0: TOR-mode entry is inactive (treated as OFF). */
static void TestEntry018_TorWhenDisabled(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    params.torEn = false;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    SetEntryRaw(&iopmp, 0, 0x800U,
                (ADDR_MODE_TOR << ENTRY_CFG_A_SHIFT) | ENTRY_CFG_R_BIT);

    /* WARL: with tor_en=0 the illegal TOR encoding reads back a legal value
     * (coerced to OFF), so the entry is inactive. */
    uint32_t a = (IopmpReadReg(&iopmp, EntryCfgOff(&iopmp, 0)) & ENTRY_CFG_A_MASK)
                 >> ENTRY_CFG_A_SHIFT;
    assert(a == ADDR_MODE_OFF);                     /* coerced to legal */
    assert(!EntryIsActive(&iopmp, 0));              /* and inactive */

    Wire1Md(&iopmp, 4);
    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ);
    ASSERT_ETYPE(r, IOPMP_ETYPE_NO_RULE);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ENTRY-018 TOR inactive when tor_en=0");
}

/* ───────────────────────────────────────────────────────────────────
 * 4.5 64-bit addresses (ENTRY_ADDRH)
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-ENTRY-019 - - addrh_en=1: NAPOT region above the 34-bit boundary. */
static void TestEntry019_HighNapotMatch(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    params.addrhEn = true;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire1Md(&iopmp, 4);

    /* word addr = (addrh<<32) | 0x401 = 0x1_0000_0401, k=1 -> size 16.
     * base = (wordAddr & ~1)<<2 = 0x1_0000_0400 << 2 = 0x4_0000_1000. */
    SetEntryRaw(&iopmp, 0, 0x401U, (ADDR_MODE_NAPOT << ENTRY_CFG_A_SHIFT) | ENTRY_CFG_R_BIT);
    SetEntryAddrh(&iopmp, 0, 1U);
    assert(EntryGetBase(&iopmp, 0) == 0x400001000ULL);
    assert(EntryGetSize(&iopmp, 0) == 16ULL);

    ASSERT_LEGAL(IopmpCheckAccess(&iopmp, 0, 0x400001000ULL, 4, IOPMP_TXN_READ));

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ENTRY-019 high NAPOT region matches");
}

/* IOPMP-ENTRY-020 - addrh_en=0: ENTRY_ADDRH not implemented, reads 0. */
static void TestEntry020_AddrhAbsent(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);        /* addrhEn = false */
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    assert(IopmpReadReg(&iopmp, EntryAddrhOff(&iopmp, 0)) == 0U);
    IopmpWriteReg(&iopmp, EntryAddrhOff(&iopmp, 0), 0xFFFFFFFFU);
    assert(IopmpReadReg(&iopmp, EntryAddrhOff(&iopmp, 0)) == 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ENTRY-020 ENTRY_ADDRH absent reads 0");
}

/* IOPMP-ENTRY-021 - - addrh_en=1 with MSBs hardwired (WARL mask). */
static void TestEntry021_AddrhWarlMask(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    params.addrhEn       = true;
    params.entryAddrhMask = 0x0000FFFFU;               /* only low 16 bits writable */
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    IopmpWriteReg(&iopmp, EntryAddrhOff(&iopmp, 0), 0xFFFFFFFFU);
    /* Bits outside the mask are hardwired (read 0 here). */
    assert(IopmpReadReg(&iopmp, EntryAddrhOff(&iopmp, 0)) == 0x0000FFFFU);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ENTRY-021 ENTRY_ADDRH WARL mask");
}

/* IOPMP-ENTRY-022 - - 64-bit TOR spanning the 34-bit boundary. */
static void TestEntry022_TorAcrossBoundary(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    params.addrhEn = true;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    /* prev word addr = 0xFFFFFFFF (addrh 0) -> L = 0x3_FFFF_FFFC.
     * this word addr = (1<<32) | 0x10        -> H = 0x4_0000_0040. */
    SetEntryRaw(&iopmp, 0, 0xFFFFFFFFU, 0x00U);
    SetEntryAddrh(&iopmp, 0, 0U);
    SetEntryRaw(&iopmp, 1, 0x00000010U,
                (ADDR_MODE_TOR << ENTRY_CFG_A_SHIFT) | ENTRY_CFG_R_BIT);
    SetEntryAddrh(&iopmp, 1, 1U);

    assert(EntryGetBase(&iopmp, 1) == 0x3FFFFFFFCULL);
    assert(EntryGetSize(&iopmp, 1) == (0x400000040ULL - 0x3FFFFFFFCULL));
    /* The 34-bit boundary (0x4_0000_0000) lies inside the region. */
    assert(EntryCoversAnyByte(&iopmp, 1, 0x400000000ULL, 4));

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ENTRY-022 64-bit TOR across boundary");
}

/* ───────────────────────────────────────────────────────────────────
 * 4.6 Permission bits (r/w/x) WARL
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-ENTRY-023 - r=1,w=0,x=0: read legal, write 0x02, exec 0x03. */
static void TestEntry023_ReadOnly(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire1Md(&iopmp, 4);

    SetNa4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_R_BIT);
    ASSERT_LEGAL(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ));
    ASSERT_ETYPE(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_WRITE),
                 IOPMP_ETYPE_ILLEGAL_WRITE);
    ASSERT_ETYPE(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_EXEC),
                 IOPMP_ETYPE_ILLEGAL_EXEC);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ENTRY-023 read-only permission bits");
}

/* IOPMP-ENTRY-024 - r=0,w=1: read -> 0x01 illegal read. */
static void TestEntry024_WriteOnlyReadIllegal(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire1Md(&iopmp, 4);

    SetNa4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_W_BIT);
    ASSERT_ETYPE(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ),
                 IOPMP_ETYPE_ILLEGAL_READ);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ENTRY-024 write-only -> read illegal");
}

/* IOPMP-ENTRY-025 - r=0,w=0,x=1: exec legal. */
static void TestEntry025_ExecOnly(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire1Md(&iopmp, 4);

    SetNa4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_X_BIT);
    ASSERT_LEGAL(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_EXEC));

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ENTRY-025 exec-only legal");
}

/* IOPMP-ENTRY-026 - active entry with no perms: illegal per access type. */
static void TestEntry026_NoPerms(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire1Md(&iopmp, 4);

    SetNa4(&iopmp, 0, 0x1000ULL, 0U);               /* active (NA4), no r/w/x */
    ASSERT_ETYPE(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ),
                 IOPMP_ETYPE_ILLEGAL_READ);
    ASSERT_ETYPE(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_WRITE),
                 IOPMP_ETYPE_ILLEGAL_WRITE);
    ASSERT_ETYPE(IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_EXEC),
                 IOPMP_ETYPE_ILLEGAL_EXEC);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ENTRY-026 active no-perm entry illegal");
}

/* IOPMP-ENTRY-027 - - implementation constrains w⇒r (WARL). */
static void TestEntry027_WImpliesR(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    params.entryPermWImpliesR = true;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    /* Write w=1, r=0 -> WARL forces r=1. */
    SetNa4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_W_BIT);
    uint32_t cfg = IopmpReadReg(&iopmp, EntryCfgOff(&iopmp, 0));
    assert((cfg & ENTRY_CFG_W_BIT) != 0U);
    assert((cfg & ENTRY_CFG_R_BIT) != 0U);          /* r forced on */

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ENTRY-027 WARL w implies r");
}

/* IOPMP-ENTRY-028 - - ENTRY_USER_CFG storage + error-record hook. */
static void TestEntry028_UserCfg(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    params.entryUserCfgEn = true;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire1Md(&iopmp, 4);

    uint32_t userOff = IopmpReadReg(&iopmp, REG_ENTRYOFFSET) + REG_ENTRY_USER_CFG_OFF;
    IopmpWriteReg(&iopmp, userOff, 0x0000ABCDU);
    assert(IopmpReadReg(&iopmp, userOff) == 0x0000ABCDU);   /* storage works */

    /* NOTE: the model applies no permission-altering rule from ENTRY_USER_CFG.
     * Its IMP-defined behaviour is the §4.3.5 hook: on a matched violation the
     * entry's user attribute is copied into ERR_USER. */
    SetNa4(&iopmp, 0, 0x1000ULL, 0U);               /* active, no perms */
    IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_READ);   /* violation, entry 0 */
    assert(IopmpReadReg(&iopmp, REG_ERR_USER_BASE) == 0x0000ABCDU);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ENTRY-028 ENTRY_USER_CFG storage + hook");
}

/* ───────────────────────────────────────────────────────────────────
 * Cross-combinations (file-local)
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-ENTRY-X01 - TOR honored with priority-by-index against predecessor. */
static void TestEntryX01_TorPriority(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire1Md(&iopmp, 4);

    /* Entry 0: NA4 read-only at 0x1000. Entry 1: TOR [0x1000,0x2000) r+w.
     * Both cover 0x1000 -> lower index (entry 0) wins, write blocked. */
    SetNa4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_R_BIT);
    SetEntryRaw(&iopmp, 1, (uint32_t)(0x2000ULL >> 2U),
                (ADDR_MODE_TOR << ENTRY_CFG_A_SHIFT) | ENTRY_CFG_R_BIT | ENTRY_CFG_W_BIT);

    TxnResult_t w = IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_WRITE);
    ASSERT_ETYPE(w, IOPMP_ETYPE_ILLEGAL_WRITE);
    assert(w.entryIdx == 0U);

    /* In the TOR-only region (0x1800), entry 1 matches and permits the read. */
    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x1800ULL, 4, IOPMP_TXN_READ);
    ASSERT_LEGAL(r);
    assert(r.entryIdx == 1U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ENTRY-X01 TOR honored, priority by index");
}

/* IOPMP-ENTRY-X02 - ENTRYLCK.f locks entry: ADDR/CFG writes rejected. */
static void TestEntryX02_EntrylckRejectsWrite(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    SetNa4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_R_BIT);
    uint32_t addrOff = IopmpReadReg(&iopmp, REG_ENTRYOFFSET) + REG_ENTRY_ADDR_OFF;
    uint32_t cfgOff  = EntryCfgOff(&iopmp, 0);
    uint32_t addrBefore = IopmpReadReg(&iopmp, addrOff);
    uint32_t cfgBefore  = IopmpReadReg(&iopmp, cfgOff);

    /* Lock entry 0: ENTRYLCK.f = 1. */
    IopmpWriteReg(&iopmp, REG_ENTRYLCK, 1U << ENTRYLCK_F_SHIFT);

    IopmpWriteReg(&iopmp, addrOff, 0xDEADU);
    IopmpWriteReg(&iopmp, cfgOff, ENTRY_CFG_W_BIT);
    assert(IopmpReadReg(&iopmp, addrOff) == addrBefore);   /* unchanged */
    assert(IopmpReadReg(&iopmp, cfgOff)  == cfgBefore);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ENTRY-X02 ENTRYLCK rejects entry writes");
}

/* IOPMP-ENTRY-X03 - - addrh_en=1 violation at high addr fills ERR_REQADDRH. */
static void TestEntryX03_ErrReqAddrh(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    params.addrhEn = true;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire1Md(&iopmp, 4);
    /* No entries -> NO_RULE; the high address must still be captured. */

    uint64_t highAddr = 0x400001000ULL;
    IopmpCheckAccess(&iopmp, 0, highAddr, 4, IOPMP_TXN_READ);

    /* ERR_REQADDR = addr[33:2], ERR_REQADDRH = addr[65:34] (spec §4.3.3). */
    assert(IopmpReadReg(&iopmp, REG_ERR_REQADDR)  == (uint32_t)((highAddr >> 2U) & 0xFFFFFFFFU));
    assert(IopmpReadReg(&iopmp, REG_ERR_REQADDRH) == (uint32_t)(highAddr >> 34U));

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ENTRY-X03 ERR_REQADDRH captures addr[65:34]");
}

/* IOPMP-ENTRY-X04 - - no_x=1 global fetch deny overrides entry x=1. */
static void TestEntryX04_GlobalNoX(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    params.noX      = true;
    params.hwcfg3En = true;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire1Md(&iopmp, 4);

    /* Entry grants execute, but the global no_x deny fires first -> NO_RULE. */
    SetNa4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_X_BIT);
    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 4, IOPMP_TXN_EXEC);
    ASSERT_ETYPE(r, IOPMP_ETYPE_NO_RULE);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ENTRY-X04 global no_x overrides entry x=1");
}

/* IOPMP-ENTRY-X05 - - non-priority entry: partial cover never matches (no 0x04). */
static void TestEntryX05_NonPriorityPartial(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeParams(1, 4, 1);
    params.hwcfg2En  = true;
    params.nonPrioEn = true;
    params.prioEntry = 0;                            /* entry 0 is non-priority */
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    Wire1Md(&iopmp, 4);

    /* NA4 (4 bytes) at 0x1000; txn of 8 bytes only partially covers it.
     * A non-priority entry must cover ALL bytes to match -> no partial-hit,
     * falls through to NO_RULE. */
    SetNa4(&iopmp, 0, 0x1000ULL, ENTRY_CFG_R_BIT);
    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x1000ULL, 8, IOPMP_TXN_READ);
    ASSERT_ETYPE(r, IOPMP_ETYPE_NO_RULE);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-ENTRY-X05 non-priority partial cover -> NO_RULE (no 0x04)");
}

/* ── Runner ──────────────────────────────────────────────────────── */

int main(void)
{
    /* 4.1 OFF */
    TestEntry001_OffNotCandidate();
    TestEntry002_OutOfRangeUnavailable();
    TestEntry003_AllOffNoRule();

    /* 4.2 NA4 */
    TestEntry004_Na4Decode();
    TestEntry005_Na4FullCover();
    TestEntry006_Na4PartialHit();
    TestEntry007_Na4JustOutside();

    /* 4.3 NAPOT */
    TestEntry008_Napot16Decode();
    TestEntry009_Napot4kFullCover();
    TestEntry010_NapotCrossBoundary();
    TestEntry011_Napot8Smallest();
    TestEntry012_NapotLarge();

    /* 4.4 TOR */
    TestEntry013_TorDecode();
    TestEntry014_TorIndexZero();
    TestEntry015_TorFirstOfMdHazard();
    TestEntry016_TorShiftsWithPrev();
    TestEntry017_TorEmptyRange();
    TestEntry018_TorWhenDisabled();

    /* 4.5 64-bit addresses */
    TestEntry019_HighNapotMatch();
    TestEntry020_AddrhAbsent();
    TestEntry021_AddrhWarlMask();
    TestEntry022_TorAcrossBoundary();

    /* 4.6 Permission bits */
    TestEntry023_ReadOnly();
    TestEntry024_WriteOnlyReadIllegal();
    TestEntry025_ExecOnly();
    TestEntry026_NoPerms();
    TestEntry027_WImpliesR();
    TestEntry028_UserCfg();

    /* Cross-combinations */
    TestEntryX01_TorPriority();
    TestEntryX02_EntrylckRejectsWrite();
    TestEntryX03_ErrReqAddrh();
    TestEntryX04_GlobalNoX();
    TestEntryX05_NonPriorityPartial();

    printf("\nAll file-04 entry address-mode tests passed.\n");
    return 0;
}
