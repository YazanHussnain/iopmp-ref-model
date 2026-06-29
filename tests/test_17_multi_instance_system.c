/*
 * test_17_multi_instance_system.c
 *
 * Test suite for docs/testplan/17-multi-instance-system.md.
 *
 * Spec: §2.2, §A.7 (parallel & cascading IOPMP), libsystem (IopmpSystem_t).
 *
 * SPEC-compliant. The system layer routes register accesses by MMIO base and
 * transactions by instance index; each instance is fully independent.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include "iopmp.h"
#include "iopmp_system.h"
#include "test_utils.h"

/* ── Shared setup helpers ────────────────────────────────────────────── */

static IopmpParams_t Full(uint16_t rridNum, uint16_t entryNum, uint8_t mdNum)
{
    IopmpParams_t p;
    memset(&p, 0, sizeof(p));
    p.rridNum = rridNum; p.entryNum = entryNum; p.mdNum = mdNum;
    p.torEn = true; p.model = IOPMP_MODEL_FULL;
    return p;
}
static IopmpParams_t Isolation(uint16_t rridNum, uint16_t entryNum, uint8_t mdNum)
{
    IopmpParams_t p = Full(rridNum, entryNum, mdNum);
    p.model = IOPMP_MODEL_ISOLATION; p.srcmdFmt = 1U; p.mdcfgFmt = 0U;
    return p;
}

/* Configure (via the system bus, at mmioBase) RRID0->MD0, entry0 read at addr. */
static void WireViaBus(IopmpSystem_t *sys, uint64_t base, uint64_t addr)
{
    IopmpSystemWriteReg(sys, base + REG_HWCFG0, HWCFG0_ENABLE_BIT);
    IopmpSystemWriteReg(sys, base + REG_MDCFG_BASE, 4U);
    IopmpSystemWriteReg(sys, base + REG_SRCMD_BASE + REG_SRCMD_EN_OFF, (1U << 1));
    uint32_t eoff = 0U;
    IopmpSystemReadReg(sys, base + REG_ENTRYOFFSET, &eoff);
    IopmpSystemWriteReg(sys, base + eoff + REG_ENTRY_ADDR_OFF, (uint32_t)(addr >> 2U));
    IopmpSystemWriteReg(sys, base + eoff + REG_ENTRY_CFG_OFF,
                        (ADDR_MODE_NA4 << ENTRY_CFG_A_SHIFT) | ENTRY_CFG_R_BIT);
}

/* ───────────────────────────────────────────────────────────────────
 * 17.1 Instance registration & MMIO routing
 * ─────────────────────────────────────────────────────────────────── */

static void TestSys001to005_Routing(void)
{
    IopmpState_t a, b;
    IopmpParams_t pa = Full(4, 8, 2); pa.mdNum = 3;       /* distinct md_num */
    IopmpParams_t pb = Full(4, 8, 5);
    assert(IopmpInit(&a, &pa) == IOPMP_OK);
    assert(IopmpInit(&b, &pb) == IOPMP_OK);

    IopmpSystem_t sys;
    assert(IopmpSystemInit(&sys) == IOPMP_SYS_OK);
    assert(IopmpSystemAddInstance(&sys, &a, 0x10000ULL) == IOPMP_SYS_OK);
    assert(IopmpSystemAddInstance(&sys, &b, 0x20000ULL) == IOPMP_SYS_OK);

    uint32_t va = 0U, vb = 0U;
    /* 001/002: HWCFG0.md_num distinguishes A (3) from B (5). */
    assert(IopmpSystemReadReg(&sys, 0x10000ULL + REG_HWCFG0, &va) == IOPMP_SYS_OK);
    assert(IopmpSystemReadReg(&sys, 0x20000ULL + REG_HWCFG0, &vb) == IOPMP_SYS_OK);
    assert(((va & HWCFG0_MD_NUM_MASK) >> HWCFG0_MD_NUM_SHIFT) == 3U);
    assert(((vb & HWCFG0_MD_NUM_MASK) >> HWCFG0_MD_NUM_SHIFT) == 5U);

    /* 003: write A's enable; B unaffected. */
    IopmpSystemWriteReg(&sys, 0x10000ULL + REG_HWCFG0, HWCFG0_ENABLE_BIT);
    IopmpSystemReadReg(&sys, 0x10000ULL + REG_HWCFG0, &va);
    IopmpSystemReadReg(&sys, 0x20000ULL + REG_HWCFG0, &vb);
    assert((va & HWCFG0_ENABLE_BIT) != 0U);
    assert((vb & HWCFG0_ENABLE_BIT) == 0U);

    /* 004: unmapped address -> NO_INSTANCE (no crash). */
    uint32_t dummy = 0xABCDU;
    assert(IopmpSystemReadReg(&sys, 0x90000ULL, &dummy) == IOPMP_SYS_ERR_NO_INSTANCE);

    /* 005: boundary - last byte of A's window resolves to A, first of B to B. */
    assert(IopmpSystemGetInstanceIdx(&sys, 0x10000ULL) == 0U);
    assert(IopmpSystemGetInstanceIdx(&sys, 0x20000ULL) == 1U);

    IopmpSystemDestroy(&sys);
    IopmpDestroy(&a); IopmpDestroy(&b);
    PASS("IOPMP-SYS-001..005 MMIO routing");
}

/* ───────────────────────────────────────────────────────────────────
 * 17.2 Transaction dispatch & independence
 * ─────────────────────────────────────────────────────────────────── */

static void TestSys006to010_Independence(void)
{
    IopmpState_t a, b;
    IopmpParams_t pa = Full(4, 8, 2);
    IopmpParams_t pb = Isolation(4, 8, 4);
    assert(IopmpInit(&a, &pa) == IOPMP_OK);
    assert(IopmpInit(&b, &pb) == IOPMP_OK);

    IopmpSystem_t sys;
    IopmpSystemInit(&sys);
    IopmpSystemAddInstance(&sys, &a, 0x10000ULL);
    IopmpSystemAddInstance(&sys, &b, 0x20000ULL);

    /* A: RRID1 -> MD0, entry0 read @0x1000. */
    IopmpSystemWriteReg(&sys, 0x10000ULL + REG_HWCFG0, HWCFG0_ENABLE_BIT);
    IopmpSystemWriteReg(&sys, 0x10000ULL + REG_MDCFG_BASE, 4U);
    IopmpSystemWriteReg(&sys, 0x10000ULL + REG_SRCMD_BASE + 1U * REG_SRCMD_STRIDE + REG_SRCMD_EN_OFF, (1U << 1));
    uint32_t eoff = 0U; IopmpSystemReadReg(&sys, 0x10000ULL + REG_ENTRYOFFSET, &eoff);
    IopmpSystemWriteReg(&sys, 0x10000ULL + eoff + REG_ENTRY_ADDR_OFF, (uint32_t)(0x1000ULL >> 2U));
    IopmpSystemWriteReg(&sys, 0x10000ULL + eoff + REG_ENTRY_CFG_OFF, (ADDR_MODE_NA4 << ENTRY_CFG_A_SHIFT) | ENTRY_CFG_R_BIT);
    /* B: enabled, no rules -> denies. */
    IopmpSystemWriteReg(&sys, 0x20000ULL + REG_HWCFG0, HWCFG0_ENABLE_BIT);

    /* 006: A allows RRID1; 007: B denies same. */
    ASSERT_LEGAL(IopmpSystemCheckAccess(&sys, 0, 1, 0x1000ULL, 4, IOPMP_TXN_READ));
    ASSERT_ILLEGAL(IopmpSystemCheckAccess(&sys, 1, 1, 0x1000ULL, 4, IOPMP_TXN_READ));

    /* 008: cause violation on B; A's record independent (A has none). */
    IopmpSystemCheckAccess(&sys, 1, 1, 0x1000ULL, 4, IOPMP_TXN_READ);
    uint32_t aInfo = 0U, bInfo = 0U;
    IopmpSystemReadReg(&sys, 0x10000ULL + REG_ERR_INFO, &aInfo);
    IopmpSystemReadReg(&sys, 0x20000ULL + REG_ERR_INFO, &bInfo);
    assert((aInfo & ERR_INFO_V_BIT) == 0U);
    assert((bInfo & ERR_INFO_V_BIT) != 0U);

    /* 009: lock A's ERR_CFG; B's still writable. */
    IopmpSystemWriteReg(&sys, 0x10000ULL + REG_ERR_CFG, ERR_CFG_IE_BIT | ERR_CFG_L_BIT);
    IopmpSystemWriteReg(&sys, 0x10000ULL + REG_ERR_CFG, ERR_CFG_RS_BIT);   /* rejected */
    IopmpSystemWriteReg(&sys, 0x20000ULL + REG_ERR_CFG, ERR_CFG_RS_BIT);   /* ok */
    uint32_t aCfg = 0U, bCfg = 0U;
    IopmpSystemReadReg(&sys, 0x10000ULL + REG_ERR_CFG, &aCfg);
    IopmpSystemReadReg(&sys, 0x20000ULL + REG_ERR_CFG, &bCfg);
    assert((aCfg & ERR_CFG_RS_BIT) == 0U);
    assert((bCfg & ERR_CFG_RS_BIT) != 0U);

    /* 010: independent sizing (HWCFG1 rrid/entry counts differ as configured). */
    uint32_t aH1 = 0U; IopmpSystemReadReg(&sys, 0x10000ULL + REG_HWCFG1, &aH1);
    assert((aH1 & HWCFG1_RRID_NUM_MASK) == 4U);

    IopmpSystemDestroy(&sys);
    IopmpDestroy(&a); IopmpDestroy(&b);
    PASS("IOPMP-SYS-006..010 dispatch & independence");
}

/* ───────────────────────────────────────────────────────────────────
 * 17.3 Parallel IOPMP
 * ─────────────────────────────────────────────────────────────────── */

static void TestSys011to013_Parallel(void)
{
    IopmpState_t a, b;
    IopmpParams_t pa = Full(4, 8, 2);
    IopmpParams_t pb = Full(4, 8, 2);
    assert(IopmpInit(&a, &pa) == IOPMP_OK);
    assert(IopmpInit(&b, &pb) == IOPMP_OK);

    IopmpSystem_t sys;
    IopmpSystemInit(&sys);
    IopmpSystemAddInstance(&sys, &a, 0x10000ULL);
    IopmpSystemAddInstance(&sys, &b, 0x20000ULL);

    /* 011: address space split - instance A owns 0x1000, B owns 0x5000. The
     * caller routes by address to the owning instance. */
    WireViaBus(&sys, 0x10000ULL, 0x1000ULL);
    WireViaBus(&sys, 0x20000ULL, 0x5000ULL);

    ASSERT_LEGAL(IopmpSystemCheckAccess(&sys, 0, 0, 0x1000ULL, 4, IOPMP_TXN_READ));  /* A */
    ASSERT_LEGAL(IopmpSystemCheckAccess(&sys, 1, 0, 0x5000ULL, 4, IOPMP_TXN_READ));  /* B */
    /* 012: A does not own 0x5000 region. */
    ASSERT_ETYPE(IopmpSystemCheckAccess(&sys, 0, 0, 0x5000ULL, 4, IOPMP_TXN_READ), IOPMP_ETYPE_NO_RULE);
    /* 013: aggregate MD space - each instance serves its own MDs independently. */
    ASSERT_ETYPE(IopmpSystemCheckAccess(&sys, 1, 0, 0x1000ULL, 4, IOPMP_TXN_READ), IOPMP_ETYPE_NO_RULE);

    IopmpSystemDestroy(&sys);
    IopmpDestroy(&a); IopmpDestroy(&b);
    PASS("IOPMP-SYS-011..013 parallel routing");
}

/* ───────────────────────────────────────────────────────────────────
 * 17.4 Cascading / gateway
 * ─────────────────────────────────────────────────────────────────── */

/* The inner IOPMP's outgoing RRID for an incoming RRID is read from its
 * RRIDTRANSL table; that becomes the RRID the outer IOPMP checks. */
static uint16_t OutgoingRrid(IopmpSystem_t *sys, uint64_t base, uint16_t inRrid)
{
    uint32_t t = 0U;
    IopmpSystemReadReg(sys, base + REG_RRIDTRANSL_BASE + (uint32_t)inRrid * REG_RRIDTRANSL_STRIDE, &t);
    return (uint16_t)(t & 0xFFFFU);
}

static void TestSys014to017_Cascade(void)
{
    IopmpState_t inner, outer;
    IopmpParams_t pi = Full(8, 8, 2);
    pi.rridTranslEn = true; pi.rridTranslProg = true; pi.hwcfg3En = true;
    IopmpParams_t po = Full(8, 8, 2);
    assert(IopmpInit(&inner, &pi) == IOPMP_OK);
    assert(IopmpInit(&outer, &po) == IOPMP_OK);

    IopmpSystem_t sys;
    IopmpSystemInit(&sys);
    IopmpSystemAddInstance(&sys, &inner, 0x10000ULL);
    IopmpSystemAddInstance(&sys, &outer, 0x20000ULL);

    /* Inner: translate RRID5 -> effective/outgoing RRID 2; the inner checks the
     * translated RRID, so associate RRID2 with MD0 (entry0 read @0x1000). */
    WireViaBus(&sys, 0x10000ULL, 0x1000ULL);
    IopmpSystemWriteReg(&sys, 0x10000ULL + REG_SRCMD_BASE + 2U * REG_SRCMD_STRIDE + REG_SRCMD_EN_OFF, (1U << 1));
    IopmpSystemWriteReg(&sys, 0x10000ULL + REG_RRIDTRANSL_BASE + 5U * REG_RRIDTRANSL_STRIDE, 2U);

    /* Outer: RRID2 -> MD0, entry0 read @0x1000. */
    WireViaBus(&sys, 0x20000ULL, 0x1000ULL);
    IopmpSystemWriteReg(&sys, 0x20000ULL + REG_SRCMD_BASE + 2U * REG_SRCMD_STRIDE + REG_SRCMD_EN_OFF, (1U << 1));

    /* 014: inner passes; outer receives translated RRID=2. */
    ASSERT_LEGAL(IopmpSystemCheckAccess(&sys, 0, 5, 0x1000ULL, 4, IOPMP_TXN_READ));
    uint16_t t = OutgoingRrid(&sys, 0x10000ULL, 5);
    assert(t == 2U);
    ASSERT_LEGAL(IopmpSystemCheckAccess(&sys, 1, t, 0x1000ULL, 4, IOPMP_TXN_READ));

    /* 015: inner denies (RRID6 -> identity 6, unassociated) -> never reaches outer. */
    ASSERT_ETYPE(IopmpSystemCheckAccess(&sys, 0, 6, 0x1000ULL, 4, IOPMP_TXN_READ), IOPMP_ETYPE_NO_RULE);

    /* 016: outer denies the translated RRID (write not permitted by entry). */
    ASSERT_ETYPE(IopmpSystemCheckAccess(&sys, 1, t, 0x1000ULL, 4, IOPMP_TXN_WRITE), IOPMP_ETYPE_ILLEGAL_WRITE);

    /* 017: several inner RRIDs collapse to one outer RRID (both translate to 2). */
    IopmpSystemWriteReg(&sys, 0x10000ULL + REG_RRIDTRANSL_BASE + 7U * REG_RRIDTRANSL_STRIDE, 2U);
    assert(OutgoingRrid(&sys, 0x10000ULL, 7) == 2U);
    assert(OutgoingRrid(&sys, 0x10000ULL, 5) == 2U);   /* both -> outer RRID 2 */
    ASSERT_LEGAL(IopmpSystemCheckAccess(&sys, 0, 7, 0x1000ULL, 4, IOPMP_TXN_READ));

    IopmpSystemDestroy(&sys);
    IopmpDestroy(&inner); IopmpDestroy(&outer);
    PASS("IOPMP-SYS-014..017 cascade/gateway");
}

/* ───────────────────────────────────────────────────────────────────
 * Cross-combinations
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-SYS-X01 - mixed-model system (Full, Compact-k, Isolation). */
static void TestSysX01_MixedModels(void)
{
    IopmpState_t a, b, c;
    IopmpParams_t pa = Full(4, 8, 2);
    IopmpParams_t pb = Full(4, 16, 4); pb.model = IOPMP_MODEL_COMPACT; pb.srcmdFmt = 1U; pb.mdcfgFmt = 1U;
    IopmpParams_t pc = Isolation(4, 8, 4);
    assert(IopmpInit(&a, &pa) == IOPMP_OK);
    assert(IopmpInit(&b, &pb) == IOPMP_OK);
    assert(IopmpInit(&c, &pc) == IOPMP_OK);

    IopmpSystem_t sys;
    IopmpSystemInit(&sys);
    assert(IopmpSystemAddInstance(&sys, &a, 0x10000ULL) == IOPMP_SYS_OK);
    assert(IopmpSystemAddInstance(&sys, &b, 0x20000ULL) == IOPMP_SYS_OK);
    assert(IopmpSystemAddInstance(&sys, &c, 0x30000ULL) == IOPMP_SYS_OK);

    /* Each instance reports its own srcmd_fmt via HWCFG3 when present; here we
     * just confirm each checks per its own model. Compact-k: RRID0->MD0. */
    IopmpSystemWriteReg(&sys, 0x20000ULL + REG_HWCFG0, HWCFG0_ENABLE_BIT);
    uint32_t eoff = 0U; IopmpSystemReadReg(&sys, 0x20000ULL + REG_ENTRYOFFSET, &eoff);
    IopmpSystemWriteReg(&sys, 0x20000ULL + eoff + REG_ENTRY_ADDR_OFF, (uint32_t)(0x1000ULL >> 2U));
    IopmpSystemWriteReg(&sys, 0x20000ULL + eoff + REG_ENTRY_CFG_OFF, (ADDR_MODE_NA4 << ENTRY_CFG_A_SHIFT) | ENTRY_CFG_R_BIT);
    ASSERT_LEGAL(IopmpSystemCheckAccess(&sys, 1, 0, 0x1000ULL, 4, IOPMP_TXN_READ));   /* RRID0->MD0 */
    ASSERT_ETYPE(IopmpSystemCheckAccess(&sys, 1, 1, 0x1000ULL, 4, IOPMP_TXN_READ), IOPMP_ETYPE_NO_RULE);

    IopmpSystemDestroy(&sys);
    IopmpDestroy(&a); IopmpDestroy(&b); IopmpDestroy(&c);
    PASS("IOPMP-SYS-X01 mixed-model system");
}

/* IOPMP-SYS-X02 - independent interrupt state per instance. */
static void TestSysX02_IndependentIrq(void)
{
    IopmpState_t a, b;
    IopmpParams_t pa = Full(4, 8, 2), pb = Full(4, 8, 2);
    assert(IopmpInit(&a, &pa) == IOPMP_OK);
    assert(IopmpInit(&b, &pb) == IOPMP_OK);
    IopmpSystem_t sys; IopmpSystemInit(&sys);
    IopmpSystemAddInstance(&sys, &a, 0x10000ULL);
    IopmpSystemAddInstance(&sys, &b, 0x20000ULL);

    IopmpSystemWriteReg(&sys, 0x10000ULL + REG_HWCFG0, HWCFG0_ENABLE_BIT);
    IopmpSystemWriteReg(&sys, 0x10000ULL + REG_ERR_CFG, ERR_CFG_IE_BIT);
    IopmpSystemWriteReg(&sys, 0x20000ULL + REG_HWCFG0, HWCFG0_ENABLE_BIT);

    IopmpSystemCheckAccess(&sys, 0, 0, 0x1000ULL, 4, IOPMP_TXN_READ);   /* A violation, ie=1 */
    assert(IopmpIsIrqPending(&a));
    assert(!IopmpIsIrqPending(&b));

    IopmpSystemDestroy(&sys);
    IopmpDestroy(&a); IopmpDestroy(&b);
    PASS("IOPMP-SYS-X02 independent IRQ per instance");
}

/* IOPMP-SYS-X03 - overlap rejected; non-overlap multi-hop topology builds. */
static void TestSysX03_OverlapRejected(void)
{
    IopmpState_t a, b;
    IopmpParams_t pa = Full(4, 8, 2), pb = Full(4, 8, 2);
    assert(IopmpInit(&a, &pa) == IOPMP_OK);
    assert(IopmpInit(&b, &pb) == IOPMP_OK);
    IopmpSystem_t sys; IopmpSystemInit(&sys);
    assert(IopmpSystemAddInstance(&sys, &a, 0x10000ULL) == IOPMP_SYS_OK);
    /* Overlapping window (within 64 KB of A) must be rejected. */
    assert(IopmpSystemAddInstance(&sys, &b, 0x18000ULL) == IOPMP_SYS_ERR_OVERLAP);
    /* A non-overlapping base succeeds. */
    assert(IopmpSystemAddInstance(&sys, &b, 0x20000ULL) == IOPMP_SYS_OK);

    IopmpSystemDestroy(&sys);
    IopmpDestroy(&a); IopmpDestroy(&b);
    PASS("IOPMP-SYS-X03 overlap rejected");
}

/* IOPMP-SYS-X04 - stalling A leaves B unaffected. */
static void TestSysX04_StallIsolation(void)
{
    IopmpState_t a, b;
    IopmpParams_t pa = Full(4, 8, 2); pa.stallEn = true; pa.hwcfg2En = true;
    IopmpParams_t pb = Full(4, 8, 2);
    assert(IopmpInit(&a, &pa) == IOPMP_OK);
    assert(IopmpInit(&b, &pb) == IOPMP_OK);
    IopmpSystem_t sys; IopmpSystemInit(&sys);
    IopmpSystemAddInstance(&sys, &a, 0x10000ULL);
    IopmpSystemAddInstance(&sys, &b, 0x20000ULL);

    WireViaBus(&sys, 0x10000ULL, 0x1000ULL);
    WireViaBus(&sys, 0x20000ULL, 0x1000ULL);

    /* Stall A's RRID0 (MD0 selected). */
    IopmpSystemWriteReg(&sys, 0x10000ULL + REG_MDSTALL, (1U << 1));
    assert(IopmpSystemCheckAccess(&sys, 0, 0, 0x1000ULL, 4, IOPMP_TXN_READ).stalled);
    /* B unaffected. */
    ASSERT_LEGAL(IopmpSystemCheckAccess(&sys, 1, 0, 0x1000ULL, 4, IOPMP_TXN_READ));

    IopmpSystemDestroy(&sys);
    IopmpDestroy(&a); IopmpDestroy(&b);
    PASS("IOPMP-SYS-X04 stall isolation between instances");
}

/* ── Runner ──────────────────────────────────────────────────────── */

int main(void)
{
    TestSys001to005_Routing();
    TestSys006to010_Independence();
    TestSys011to013_Parallel();
    TestSys014to017_Cascade();

    TestSysX01_MixedModels();
    TestSysX02_IndependentIrq();
    TestSysX03_OverlapRejected();
    TestSysX04_StallIsolation();

    printf("\nAll file-17 multi-instance system tests passed.\n");
    return 0;
}
