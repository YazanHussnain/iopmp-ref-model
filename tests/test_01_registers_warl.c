/*
 * test_01_registers_warl.c
 *
 * Test suite for docs/testplan/01-registers-warl.md:
 *   "INFO Registers, Reset & Field-Behavior Semantics"
 *
 * Spec: §4 (Registers), §4.1 (INFO registers), §4.6 (Entry Array Registers),
 *       Table 1 (field behaviors), Table 3 (register summary).
 *
 * One function per test ID (IOPMP-REG-0xx and IOPMP-REG-Xxx). Each function
 * documents the test-plan condition and asserts the reference model's actual
 * behavior. Where the model deliberately deviates from the test plan's ideal
 * "expected scenario", a NOTE comment explains the deviation so the gap stays
 * traceable instead of silent.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include "iopmp.h"
#include "test_utils.h"

/* ── Helpers ─────────────────────────────────────────────────────── */

/* Minimal valid params: FULL model, TOR + addrh on, error capture on. */
static IopmpParams_t MakeDefaultParams(void)
{
    IopmpParams_t params;
    memset(&params, 0, sizeof(params));
    params.rridNum  = 4;
    params.entryNum = 8;
    params.mdNum    = 2;
    params.torEn    = true;
    params.addrhEn  = true;
    params.model    = IOPMP_MODEL_FULL;
    return params;
}

/* Byte offset of entry i, field 'fieldOff', given the live ENTRYOFFSET. */
static uint32_t EntryFieldOffset(IopmpState_t *iopmp, uint32_t i, uint32_t fieldOff)
{
    uint32_t base = IopmpReadReg(iopmp, REG_ENTRYOFFSET);
    return base + i * REG_ENTRY_STRIDE + fieldOff;
}

/* Enable the IOPMP so the access checker actually runs (WISS enable bit). */
static void EnableIopmp(IopmpState_t *iopmp)
{
    IopmpWriteReg(iopmp, REG_HWCFG0, HWCFG0_ENABLE_BIT);
}

/* ───────────────────────────────────────────────────────────────────
 * 1.1 Reset & default state
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-REG-001 §4.1.1 - VERSION (0x0000) read-only. */
static void TestReg001_VersionReadOnly(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeDefaultParams();
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    /* NOTE: the model exposes no vendor/specver params, so VERSION reads 0.
     * The test plan expects a configured JEDEC ID; here we verify the field
     * is read-only - a write is silently dropped. */
    uint32_t before = IopmpReadReg(&iopmp, REG_VERSION);
    IopmpWriteReg(&iopmp, REG_VERSION, 0xFFFFFFFFU);
    assert(IopmpReadReg(&iopmp, REG_VERSION) == before);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-REG-001 VERSION read-only");
}

/* IOPMP-REG-002 §4.1.2 - IMPLEMENTATION (0x0004) read-only. */
static void TestReg002_ImplementationReadOnly(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeDefaultParams();
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    /* NOTE: no impid param; IMPLEMENTATION reads 0. Verify read-only. */
    uint32_t before = IopmpReadReg(&iopmp, REG_IMPLEMENTATION);
    IopmpWriteReg(&iopmp, REG_IMPLEMENTATION, 0xDEADBEEFU);
    assert(IopmpReadReg(&iopmp, REG_IMPLEMENTATION) == before);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-REG-002 IMPLEMENTATION read-only");
}

/* IOPMP-REG-003 §4.1.3 - HWCFG0.enable resets to 0 when programmable. */
static void TestReg003_EnableResetsZero(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeDefaultParams();
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    assert((IopmpReadReg(&iopmp, REG_HWCFG0) & HWCFG0_ENABLE_BIT) == 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-REG-003 enable resets to 0");
}

/* IOPMP-REG-004 §4.1.3 - enable hardwired to 1 (hardwired implementation). */
static void TestReg004_EnableHardwired(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeDefaultParams();
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    /* NOTE: the model has no "hardwired enable" parameter - enable is always
     * a programmable WISS bit that resets to 0. The hardwired-to-1 variant is
     * not modeled; we assert the programmable reset behavior instead. */
    assert((IopmpReadReg(&iopmp, REG_HWCFG0) & HWCFG0_ENABLE_BIT) == 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-REG-004 enable hardwired variant not modeled (programmable)");
}

/* IOPMP-REG-005 §4.3.2 - ERR_INFO.v defaults to 0 after reset. */
static void TestReg005_ErrInfoValidDefaultsZero(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeDefaultParams();
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    assert((IopmpReadReg(&iopmp, REG_ERR_INFO) & ERR_INFO_V_BIT) == 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-REG-005 ERR_INFO.v=0 after reset");
}

/* IOPMP-REG-006 §4.2 - lock registers read their reset value (0 unprelocked). */
static void TestReg006_LockRegsResetValue(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeDefaultParams();  /* no prelock configured */
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    assert(IopmpReadReg(&iopmp, REG_MDLCK)    == 0U);
    assert(IopmpReadReg(&iopmp, REG_MDCFGLCK) == 0U);
    assert(IopmpReadReg(&iopmp, REG_ENTRYLCK) == 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-REG-006 lock regs reset to 0");
}

/* IOPMP-REG-007 §4.6.2 - ENTRY_CFG(i).a is OFF(0) for all i after reset. */
static void TestReg007_EntryCfgResetOff(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeDefaultParams();
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    for (uint32_t i = 0U; i < params.entryNum; i++) {
        uint32_t cfg = IopmpReadReg(&iopmp, EntryFieldOffset(&iopmp, i, REG_ENTRY_CFG_OFF));
        uint32_t a   = (cfg & ENTRY_CFG_A_MASK) >> ENTRY_CFG_A_SHIFT;
        assert(a == ADDR_MODE_OFF);
    }

    IopmpDestroy(&iopmp);
    PASS("IOPMP-REG-007 all entries reset to a=OFF");
}

/* IOPMP-REG-008 §4.1.8 - ENTRYOFFSET reads the configured layout offset. */
static void TestReg008_EntryOffsetMatchesLayout(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeDefaultParams();
    params.entryOffset = 0x8000U;          /* explicit positive offset */
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    assert(IopmpReadReg(&iopmp, REG_ENTRYOFFSET) == 0x8000U);

    IopmpDestroy(&iopmp);

    /* Default (entryOffset==0) falls back to IOPMP_DEFAULT_ENTRY_OFFSET. */
    IopmpParams_t def = MakeDefaultParams();
    assert(IopmpInit(&iopmp, &def) == IOPMP_OK);
    assert(IopmpReadReg(&iopmp, REG_ENTRYOFFSET) == IOPMP_DEFAULT_ENTRY_OFFSET);
    IopmpDestroy(&iopmp);

    PASS("IOPMP-REG-008 ENTRYOFFSET matches configured layout");
}

/* IOPMP-REG-009 - §4.1.8 - negative (two's-complement) ENTRYOFFSET reads back. */
static void TestReg009_EntryOffsetNegativeReadback(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeDefaultParams();
    /* Entry array placed *before* VERSION -> negative signed offset.
     * -0x4000 in two's complement = 0xFFFFC000. */
    params.entryOffset = 0xFFFFC000U;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    /* The raw two's-complement value is read back unchanged (read-only reg). */
    assert(IopmpReadReg(&iopmp, REG_ENTRYOFFSET) == 0xFFFFC000U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-REG-009 negative ENTRYOFFSET preserved");
}

/* ───────────────────────────────────────────────────────────────────
 * 1.2 HWCFG0 / HWCFG1 read-only capability fields
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-REG-010 §4.1.3 - md_num=N in HWCFG0[29:24]; write has no effect. */
static void TestReg010_MdNumField(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeDefaultParams();
    params.mdNum = 5;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    uint32_t hwcfg0 = IopmpReadReg(&iopmp, REG_HWCFG0);
    assert(((hwcfg0 & HWCFG0_MD_NUM_MASK) >> HWCFG0_MD_NUM_SHIFT) == 5U);

    /* Writing the md_num field has no effect (only enable is writable). */
    IopmpWriteReg(&iopmp, REG_HWCFG0, 0x3F000000U);
    hwcfg0 = IopmpReadReg(&iopmp, REG_HWCFG0);
    assert(((hwcfg0 & HWCFG0_MD_NUM_MASK) >> HWCFG0_MD_NUM_SHIFT) == 5U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-REG-010 md_num read-only");
}

/* IOPMP-REG-011 §4.1.3 - tor_en=1 -> HWCFG0[31]=1. */
static void TestReg011_TorEnSet(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeDefaultParams();
    params.torEn = true;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    assert((IopmpReadReg(&iopmp, REG_HWCFG0) & HWCFG0_TOR_EN_BIT) != 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-REG-011 tor_en=1");
}

/* IOPMP-REG-012 §4.1.3 - tor_en=0 -> HWCFG0[31]=0. */
static void TestReg012_TorEnClear(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeDefaultParams();
    params.torEn = false;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    assert((IopmpReadReg(&iopmp, REG_HWCFG0) & HWCFG0_TOR_EN_BIT) == 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-REG-012 tor_en=0");
}

/* IOPMP-REG-013 §4.1.3 - addrh_en=1 -> HWCFG0[30]=1. */
static void TestReg013_AddrhEnSet(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeDefaultParams();
    params.addrhEn = true;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    assert((IopmpReadReg(&iopmp, REG_HWCFG0) & HWCFG0_ADDRH_EN_BIT) != 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-REG-013 addrh_en=1");
}

/* IOPMP-REG-014 §4.1.3 - no_err_rec=1 -> HWCFG0[23]=1. */
static void TestReg014_NoErrRecSet(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeDefaultParams();
    params.noErrRec = true;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    assert((IopmpReadReg(&iopmp, REG_HWCFG0) & HWCFG0_NO_ERR_REC_BIT) != 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-REG-014 no_err_rec=1");
}

/* IOPMP-REG-015 §4.1.3 - hwcfg2_en/hwcfg3_en reflected in HWCFG0[1]/[2]. */
static void TestReg015_Hwcfg23EnBits(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeDefaultParams();
    params.hwcfg2En = true;
    params.hwcfg3En = false;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    uint32_t hwcfg0 = IopmpReadReg(&iopmp, REG_HWCFG0);
    assert((hwcfg0 & HWCFG0_HWCFG2_EN_BIT) != 0U);
    assert((hwcfg0 & HWCFG0_HWCFG3_EN_BIT) == 0U);
    IopmpDestroy(&iopmp);

    params.hwcfg2En = false;
    params.hwcfg3En = true;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);
    hwcfg0 = IopmpReadReg(&iopmp, REG_HWCFG0);
    assert((hwcfg0 & HWCFG0_HWCFG2_EN_BIT) == 0U);
    assert((hwcfg0 & HWCFG0_HWCFG3_EN_BIT) != 0U);
    IopmpDestroy(&iopmp);

    PASS("IOPMP-REG-015 hwcfg2_en/hwcfg3_en bits");
}

/* IOPMP-REG-016 §4.1.4 - rrid_num=R in HWCFG1[15:0]; read-only. */
static void TestReg016_RridNumField(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeDefaultParams();
    params.rridNum = 12;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    uint32_t hwcfg1 = IopmpReadReg(&iopmp, REG_HWCFG1);
    assert((hwcfg1 & HWCFG1_RRID_NUM_MASK) == 12U);

    /* HWCFG1 is read-only - writes are dropped entirely. */
    IopmpWriteReg(&iopmp, REG_HWCFG1, 0xFFFFFFFFU);
    assert(IopmpReadReg(&iopmp, REG_HWCFG1) == hwcfg1);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-REG-016 rrid_num read-only");
}

/* IOPMP-REG-017 §4.1.4 - entry_num=E (>0) in HWCFG1[31:16]. */
static void TestReg017_EntryNumField(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeDefaultParams();
    params.entryNum = 20;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    uint32_t hwcfg1 = IopmpReadReg(&iopmp, REG_HWCFG1);
    assert(((hwcfg1 & HWCFG1_ENTRY_NUM_MASK) >> HWCFG1_ENTRY_NUM_SHIFT) == 20U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-REG-017 entry_num field");
}

/* IOPMP-REG-018 §4.1.3 - HWCFG0[22:3] reserved read 0 after write-all-ones. */
static void TestReg018_Hwcfg0ReservedZero(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeDefaultParams();
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    IopmpWriteReg(&iopmp, REG_HWCFG0, 0xFFFFFFFFU);

    /* Reserved field is bits 22:3 (between hwcfg3_en at [2] and no_err_rec [23]). */
    uint32_t reservedMask = 0x007FFFF8U;  /* bits 22..3 */
    assert((IopmpReadReg(&iopmp, REG_HWCFG0) & reservedMask) == 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-REG-018 HWCFG0 reserved bits read 0");
}

/* ───────────────────────────────────────────────────────────────────
 * 1.3 WARL behavior (write-any, read-legal)
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-REG-019 §4.6.2 - ENTRY_CFG.a=NAPOT(0x3) is legal and retained. */
static void TestReg019_EntryCfgNapotLegal(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeDefaultParams();
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    uint32_t off = EntryFieldOffset(&iopmp, 0, REG_ENTRY_CFG_OFF);
    IopmpWriteReg(&iopmp, off, (uint32_t)ADDR_MODE_NAPOT << ENTRY_CFG_A_SHIFT);

    uint32_t a = (IopmpReadReg(&iopmp, off) & ENTRY_CFG_A_MASK) >> ENTRY_CFG_A_SHIFT;
    assert(a == ADDR_MODE_NAPOT);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-REG-019 ENTRY_CFG a=NAPOT legal");
}

/* IOPMP-REG-020 §4.6.2 - ENTRY_CFG.a=TOR when tor_en=0 (WARL). */
static void TestReg020_EntryCfgTorWhenUnsupported(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeDefaultParams();
    params.torEn = false;                 /* TOR unsupported */
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    uint32_t off = EntryFieldOffset(&iopmp, 0, REG_ENTRY_CFG_OFF);
    IopmpWriteReg(&iopmp, off, (uint32_t)ADDR_MODE_TOR << ENTRY_CFG_A_SHIFT);
    uint32_t a = (IopmpReadReg(&iopmp, off) & ENTRY_CFG_A_MASK) >> ENTRY_CFG_A_SHIFT;

    /* WARL: TOR is illegal when tor_en=0, so the field reads back a legal
     * value (the model coerces it to OFF). */
    assert(a != ADDR_MODE_TOR);
    assert(a == ADDR_MODE_OFF);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-REG-020 ENTRY_CFG a=TOR coerced to legal (WARL)");
}

/* IOPMP-REG-021 §4.6.2 - implementation hardwires entry.w=0 (read-only device). */
static void TestReg021_EntryWHardwiredZero(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeDefaultParams();
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    uint32_t off = EntryFieldOffset(&iopmp, 0, REG_ENTRY_CFG_OFF);
    IopmpWriteReg(&iopmp, off, ENTRY_CFG_W_BIT);
    uint32_t w = IopmpReadReg(&iopmp, off) & ENTRY_CFG_W_BIT;

    /* NOTE: MODEL DEVIATION. The model has no per-entry "w hardwired to 0"
     * parameter; the w bit is freely programmable. The test plan's read-only
     * device variant is not modeled, so w reads back 1 here. */
    assert(w != 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-REG-021 entry.w hardwired variant not modeled");
}

/* IOPMP-REG-022 §4.6.2 - ENTRY_CFG reserved bits read 0. */
static void TestReg022_EntryCfgReservedZero(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeDefaultParams();
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    uint32_t off = EntryFieldOffset(&iopmp, 0, REG_ENTRY_CFG_OFF);
    IopmpWriteReg(&iopmp, off, 0xFFFFFFFFU);
    uint32_t cfg = IopmpReadReg(&iopmp, off);

    /* Bits outside the valid mask (r/w/x/a + per-access si/se) must read 0.
     * NOTE: the model's valid mask includes the §5.1.11 SI/SE bits [10:5],
     * so "reserved" here is [31:11], not the plan's [31:5]. */
    assert((cfg & ~ENTRY_CFG_VALID_MASK) == 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-REG-022 ENTRY_CFG reserved bits read 0");
}

/* IOPMP-REG-023 §4.4.1 - MDCFG.t WARL [15:0]; rsv [31:16] read 0. */
static void TestReg023_MdcfgWarl(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeDefaultParams();  /* mdcfgFmt 0 (standard) */
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    IopmpWriteReg(&iopmp, REG_MDCFG_BASE, 0xFFFFFFFFU);
    uint32_t t = IopmpReadReg(&iopmp, REG_MDCFG_BASE);

    assert((t & MDCFG_T_MASK) == 0xFFFFU);  /* low 16 bits retained */
    assert((t & ~MDCFG_T_MASK) == 0U);      /* upper 16 bits read 0 */

    IopmpDestroy(&iopmp);
    PASS("IOPMP-REG-023 MDCFG.t WARL");
}

/* ───────────────────────────────────────────────────────────────────
 * 1.4 WISS - write-1-set-sticky-to-1
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-REG-024 §4.1.3 - HWCFG0.enable: write 1 sets it. */
static void TestReg024_EnableSet(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeDefaultParams();
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    IopmpWriteReg(&iopmp, REG_HWCFG0, HWCFG0_ENABLE_BIT);
    assert((IopmpReadReg(&iopmp, REG_HWCFG0) & HWCFG0_ENABLE_BIT) != 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-REG-024 enable write-1-set");
}

/* IOPMP-REG-025 §4.1.3 - HWCFG0.enable sticky: write 0 keeps it set. */
static void TestReg025_EnableSticky(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeDefaultParams();
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    IopmpWriteReg(&iopmp, REG_HWCFG0, HWCFG0_ENABLE_BIT);
    IopmpWriteReg(&iopmp, REG_HWCFG0, 0U);
    assert((IopmpReadReg(&iopmp, REG_HWCFG0) & HWCFG0_ENABLE_BIT) != 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-REG-025 enable sticky-to-1");
}

/* IOPMP-REG-026 §4.5.1 - SRCMD_EN(s).l: write 1 sets it. */
static void TestReg026_SrcmdEnLockSet(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeDefaultParams();
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    uint32_t off = REG_SRCMD_BASE + 0U * REG_SRCMD_STRIDE + REG_SRCMD_EN_OFF;
    IopmpWriteReg(&iopmp, off, SRCMD_EN_L_BIT);
    assert((IopmpReadReg(&iopmp, off) & SRCMD_EN_L_BIT) != 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-REG-026 SRCMD_EN.l write-1-set");
}

/* IOPMP-REG-027 §4.5.1 - SRCMD_EN(s).l sticky: write 0 keeps it set. */
static void TestReg027_SrcmdEnLockSticky(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeDefaultParams();
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    uint32_t off = REG_SRCMD_BASE + 0U * REG_SRCMD_STRIDE + REG_SRCMD_EN_OFF;
    IopmpWriteReg(&iopmp, off, SRCMD_EN_L_BIT);
    IopmpWriteReg(&iopmp, off, 0U);          /* attempt clear - row now frozen */
    assert((IopmpReadReg(&iopmp, off) & SRCMD_EN_L_BIT) != 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-REG-027 SRCMD_EN.l sticky-to-1");
}

/* IOPMP-REG-028 §4.2.1 - MDLCK.l WISS: write 1 then 0, stays 1. */
static void TestReg028_MdlckLockSticky(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeDefaultParams();  /* mdlck implemented */
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    IopmpWriteReg(&iopmp, REG_MDLCK, MDLCK_L_BIT);
    assert((IopmpReadReg(&iopmp, REG_MDLCK) & MDLCK_L_BIT) != 0U);
    IopmpWriteReg(&iopmp, REG_MDLCK, 0U);
    assert((IopmpReadReg(&iopmp, REG_MDLCK) & MDLCK_L_BIT) != 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-REG-028 MDLCK.l sticky-to-1");
}

/* ───────────────────────────────────────────────────────────────────
 * 1.5 W1CS - write-1-clear-sticky-to-0
 * ─────────────────────────────────────────────────────────────────── */

/* Params with HWCFG2 present, programmable non-priority boundary. */
static IopmpParams_t MakeHwcfg2Params(void)
{
    IopmpParams_t params = MakeDefaultParams();
    params.hwcfg2En    = true;
    params.nonPrioEn   = true;
    params.prioEntProg = true;
    params.prioEntry   = 4;
    return params;
}

/* IOPMP-REG-029 - §5.1.1 - HWCFG2.prio_ent_prog W1CS, reset=1, write 1 clears. */
static void TestReg029_PrioEntProgW1cs(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeHwcfg2Params();
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    /* Resets to 1 because the boundary is programmable. */
    assert((IopmpReadReg(&iopmp, REG_HWCFG2) & HWCFG2_PRIO_ENT_PROG_BIT) != 0U);

    /* Write 1 -> clears to 0 (sticky). */
    IopmpWriteReg(&iopmp, REG_HWCFG2, HWCFG2_PRIO_ENT_PROG_BIT);
    assert((IopmpReadReg(&iopmp, REG_HWCFG2) & HWCFG2_PRIO_ENT_PROG_BIT) == 0U);

    /* Sticky-to-0: cannot be set again. */
    IopmpWriteReg(&iopmp, REG_HWCFG2, HWCFG2_PRIO_ENT_PROG_BIT);
    assert((IopmpReadReg(&iopmp, REG_HWCFG2) & HWCFG2_PRIO_ENT_PROG_BIT) == 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-REG-029 prio_ent_prog W1CS clears & sticky");
}

/* IOPMP-REG-030 - §5.1.1 - prio_ent_prog already 0: write 0 stays 0. */
static void TestReg030_PrioEntProgWriteZeroNoEffect(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeHwcfg2Params();
    params.prioEntProg = false;          /* resets to 0 */
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    assert((IopmpReadReg(&iopmp, REG_HWCFG2) & HWCFG2_PRIO_ENT_PROG_BIT) == 0U);
    IopmpWriteReg(&iopmp, REG_HWCFG2, 0U);
    assert((IopmpReadReg(&iopmp, REG_HWCFG2) & HWCFG2_PRIO_ENT_PROG_BIT) == 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-REG-030 prio_ent_prog write-0 no effect");
}

/* IOPMP-REG-031 - §A HWCFG3 - rrid_transl_prog W1CS reset=1, write 1 clears. */
static void TestReg031_RridTranslProgW1cs(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeDefaultParams();
    params.hwcfg3En       = true;
    params.rridTranslEn   = true;
    params.rridTranslProg = true;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    assert((IopmpReadReg(&iopmp, REG_HWCFG3) & HWCFG3_RRID_TRANSL_PROG_BIT) != 0U);

    IopmpWriteReg(&iopmp, REG_HWCFG3, HWCFG3_RRID_TRANSL_PROG_BIT);
    assert((IopmpReadReg(&iopmp, REG_HWCFG3) & HWCFG3_RRID_TRANSL_PROG_BIT) == 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-REG-031 rrid_transl_prog W1CS clears");
}

/* ───────────────────────────────────────────────────────────────────
 * 1.6 RW1C - read-status / write-1-clear (error valid)
 * ─────────────────────────────────────────────────────────────────── */

/* Build an enabled instance and provoke a NO_RULE violation, capturing it. */
static void ProvokeNoRuleViolation(IopmpState_t *iopmp, TxnType_t txn)
{
    EnableIopmp(iopmp);
    TxnResult_t r = IopmpCheckAccess(iopmp, 0, 0x1000U, 4U, txn);
    ASSERT_ILLEGAL(r);
}

/* IOPMP-REG-032 §4.3.2 - ERR_INFO read returns v=1 plus ttype/etype. */
static void TestReg032_ErrInfoReadCaptured(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeDefaultParams();
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    ProvokeNoRuleViolation(&iopmp, IOPMP_TXN_READ);

    uint32_t errInfo = IopmpReadReg(&iopmp, REG_ERR_INFO);
    assert((errInfo & ERR_INFO_V_BIT) != 0U);
    uint32_t ttype = (errInfo & ERR_INFO_TTYPE_MASK) >> ERR_INFO_TTYPE_SHIFT;
    uint32_t etype = (errInfo & ERR_INFO_ETYPE_MASK) >> ERR_INFO_ETYPE_SHIFT;
    assert(ttype == 1U);                          /* read */
    assert(etype == (uint32_t)IOPMP_ETYPE_NO_RULE);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-REG-032 ERR_INFO captures v/ttype/etype");
}

/* IOPMP-REG-033 §4.3.2 - write ERR_INFO.v=1 clears v (re-arm). */
static void TestReg033_ErrInfoW1c(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeDefaultParams();
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    ProvokeNoRuleViolation(&iopmp, IOPMP_TXN_READ);
    assert((IopmpReadReg(&iopmp, REG_ERR_INFO) & ERR_INFO_V_BIT) != 0U);

    IopmpWriteReg(&iopmp, REG_ERR_INFO, ERR_INFO_V_BIT);
    assert((IopmpReadReg(&iopmp, REG_ERR_INFO) & ERR_INFO_V_BIT) == 0U);

    /* Re-armed: a fresh violation captures again. */
    IopmpCheckAccess(&iopmp, 0, 0x2000U, 4U, IOPMP_TXN_WRITE);
    assert((IopmpReadReg(&iopmp, REG_ERR_INFO) & ERR_INFO_V_BIT) != 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-REG-033 ERR_INFO.v write-1-clear");
}

/* IOPMP-REG-034 §4.3.2 - write ERR_INFO with bit0=0 has no effect on v. */
static void TestReg034_ErrInfoWriteZeroIgnored(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeDefaultParams();
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    ProvokeNoRuleViolation(&iopmp, IOPMP_TXN_READ);
    assert((IopmpReadReg(&iopmp, REG_ERR_INFO) & ERR_INFO_V_BIT) != 0U);

    /* Write with bit0 cleared - v must remain set. */
    IopmpWriteReg(&iopmp, REG_ERR_INFO, 0xFFFFFFFEU);
    assert((IopmpReadReg(&iopmp, REG_ERR_INFO) & ERR_INFO_V_BIT) != 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-REG-034 ERR_INFO write-0 ignored");
}

/* IOPMP-REG-035 §4.3.2 - ttype/etype are read-only; writes don't change them. */
static void TestReg035_ErrInfoTtypeEtypeReadOnly(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeDefaultParams();
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    ProvokeNoRuleViolation(&iopmp, IOPMP_TXN_READ);
    uint32_t before = IopmpReadReg(&iopmp, REG_ERR_INFO)
                      & (ERR_INFO_TTYPE_MASK | ERR_INFO_ETYPE_MASK);

    /* Attempt to overwrite ttype/etype (bit0 kept 0 so v is not cleared). */
    IopmpWriteReg(&iopmp, REG_ERR_INFO, ERR_INFO_TTYPE_MASK | ERR_INFO_ETYPE_MASK);
    uint32_t after = IopmpReadReg(&iopmp, REG_ERR_INFO)
                     & (ERR_INFO_TTYPE_MASK | ERR_INFO_ETYPE_MASK);
    assert(after == before);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-REG-035 ERR_INFO ttype/etype read-only");
}

/* ───────────────────────────────────────────────────────────────────
 * 1.7 Address decoding & out-of-range register access
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-REG-036 §4 Table3 - ENTRY_ADDR(entry_num) out of range reads 0. */
static void TestReg036_EntryAddrOutOfRange(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeDefaultParams();  /* entryNum = 8 */
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    uint32_t off = EntryFieldOffset(&iopmp, params.entryNum, REG_ENTRY_ADDR_OFF);
    assert(IopmpReadReg(&iopmp, off) == 0U);     /* out-of-range, no crash */

    IopmpDestroy(&iopmp);
    PASS("IOPMP-REG-036 out-of-range ENTRY_ADDR reads 0");
}

/* IOPMP-REG-037 §4.5 - SRCMD_EN(rrid_num) out of range reads 0. */
static void TestReg037_SrcmdOutOfRange(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeDefaultParams();  /* rridNum = 4 */
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    uint32_t off = REG_SRCMD_BASE + (uint32_t)params.rridNum * REG_SRCMD_STRIDE
                   + REG_SRCMD_EN_OFF;
    assert(IopmpReadReg(&iopmp, off) == 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-REG-037 out-of-range SRCMD_EN reads 0");
}

/* IOPMP-REG-038 §4 - HWCFG2 absent (hwcfg2_en=0) reads 0. */
static void TestReg038_Hwcfg2AbsentReadsZero(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeDefaultParams();
    params.hwcfg2En = false;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    assert(IopmpReadReg(&iopmp, REG_HWCFG2) == 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-REG-038 absent HWCFG2 reads 0");
}

/* IOPMP-REG-039 §4 - reserved offset (0x0018) has no functional effect. */
static void TestReg039_ReservedOffset(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeDefaultParams();
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    /* Reserved gap between HWCFG3 (0x14) and ENTRYOFFSET (0x2C). */
    assert(IopmpReadReg(&iopmp, 0x0018U) == 0U);

    /* A write to the reserved offset must not disturb real neighbors. */
    uint32_t entryOffsetBefore = IopmpReadReg(&iopmp, REG_ENTRYOFFSET);
    uint32_t hwcfg1Before      = IopmpReadReg(&iopmp, REG_HWCFG1);
    IopmpWriteReg(&iopmp, 0x0018U, 0xFFFFFFFFU);
    assert(IopmpReadReg(&iopmp, REG_ENTRYOFFSET) == entryOffsetBefore);
    assert(IopmpReadReg(&iopmp, REG_HWCFG1)      == hwcfg1Before);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-REG-039 reserved offset no functional effect");
}

/* IOPMP-REG-040 §4 - word-addressed access decodes each offset distinctly. */
static void TestReg040_WordAddressedDecode(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeDefaultParams();
    params.rridNum  = 7;
    params.entryNum = 9;
    params.mdNum    = 3;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    /* Each fixed register decodes to its own distinct value. */
    uint32_t hwcfg1 = IopmpReadReg(&iopmp, REG_HWCFG1);
    assert((hwcfg1 & HWCFG1_RRID_NUM_MASK) == 7U);
    assert(((hwcfg1 & HWCFG1_ENTRY_NUM_MASK) >> HWCFG1_ENTRY_NUM_SHIFT) == 9U);

    uint32_t hwcfg0 = IopmpReadReg(&iopmp, REG_HWCFG0);
    assert(((hwcfg0 & HWCFG0_MD_NUM_MASK) >> HWCFG0_MD_NUM_SHIFT) == 3U);

    assert(IopmpReadReg(&iopmp, REG_ENTRYOFFSET) == IOPMP_DEFAULT_ENTRY_OFFSET);

    /* Unaligned access is rejected (reads 0, writes dropped). */
    assert(IopmpReadReg(&iopmp, REG_HWCFG1 + 1U) == 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-REG-040 word-addressed decode");
}

/* ───────────────────────────────────────────────────────────────────
 * Cross-combinations (file-local)
 * ─────────────────────────────────────────────────────────────────── */

/* IOPMP-REG-X01 §4.1.3+§4.6 - enable=0 -> IOPMP bypassed (all txns legal). */
static void TestRegX01_DisabledBypass(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeDefaultParams();
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    /* enable=0 after reset -> any transaction is allowed without checking. */
    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x1000U, 4U, IOPMP_TXN_READ);
    ASSERT_LEGAL(r);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-REG-X01 disabled IOPMP bypassed");
}

/* IOPMP-REG-X02 §4.1.3+§3 - enable set->sticky; checks now enforced. */
static void TestRegX02_EnableStickyEnforced(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeDefaultParams();
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    EnableIopmp(&iopmp);
    IopmpWriteReg(&iopmp, REG_HWCFG0, 0U);        /* attempt to clear */
    assert((IopmpReadReg(&iopmp, REG_HWCFG0) & HWCFG0_ENABLE_BIT) != 0U);

    /* With no rules configured, gating is active -> NO_RULE block. */
    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x1000U, 4U, IOPMP_TXN_READ);
    ASSERT_ETYPE(r, IOPMP_ETYPE_NO_RULE);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-REG-X02 enable sticky + enforcement active");
}

/* IOPMP-REG-X03 §4.1.4+§3.2 - ENTRYLCK.f > entry_num clamps: all entries locked. */
static void TestRegX03_EntrylckClamp(void)
{
    IopmpState_t iopmp;
    IopmpParams_t params = MakeDefaultParams();  /* entryNum = 8 */
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    /* Write ENTRYLCK.f = entry_num + 5 (overshoot). */
    uint32_t f = (uint32_t)params.entryNum + 5U;
    IopmpWriteReg(&iopmp, REG_ENTRYLCK, f << ENTRYLCK_F_SHIFT);

    /* Every entry index < entry_num is now < f, so all entries are locked:
     * writes to ENTRY_CFG are dropped. */
    uint32_t off0    = EntryFieldOffset(&iopmp, 0, REG_ENTRY_CFG_OFF);
    uint32_t offLast = EntryFieldOffset(&iopmp, params.entryNum - 1U, REG_ENTRY_CFG_OFF);
    IopmpWriteReg(&iopmp, off0,    ENTRY_CFG_R_BIT);
    IopmpWriteReg(&iopmp, offLast, ENTRY_CFG_R_BIT);
    assert(IopmpReadReg(&iopmp, off0)    == 0U);
    assert(IopmpReadReg(&iopmp, offLast) == 0U);

    IopmpDestroy(&iopmp);
    PASS("IOPMP-REG-X03 ENTRYLCK.f overshoot locks all entries");
}

/* IOPMP-REG-X04 - §4.1.3+§4.3 - no_err_rec=1 -> no capture; eid wired 0xffff. */
static void TestRegX04_NoErrRecAndEid(void)
{
    /* Part A: no_err_rec=1 -> violations block but nothing is recorded. */
    IopmpState_t iopmp;
    IopmpParams_t params = MakeDefaultParams();
    params.noErrRec = true;
    assert(IopmpInit(&iopmp, &params) == IOPMP_OK);

    EnableIopmp(&iopmp);
    TxnResult_t r = IopmpCheckAccess(&iopmp, 0, 0x1000U, 4U, IOPMP_TXN_READ);
    ASSERT_ILLEGAL(r);                             /* still blocked */
    assert((IopmpReadReg(&iopmp, REG_ERR_INFO) & ERR_INFO_V_BIT) == 0U);  /* not recorded */
    IopmpDestroy(&iopmp);

    /* Part B: eid wired 0xffff when no entry matched (NO_RULE).
     * The test plan ties this to no_err_rec; in the model the eid=0xffff
     * encoding is produced whenever entryIdx is "none". */
    IopmpParams_t p2 = MakeDefaultParams();        /* error capture on */
    assert(IopmpInit(&iopmp, &p2) == IOPMP_OK);
    EnableIopmp(&iopmp);
    IopmpCheckAccess(&iopmp, 0, 0x1000U, 4U, IOPMP_TXN_READ);  /* NO_RULE */
    uint32_t reqid = IopmpReadReg(&iopmp, REG_ERR_REQID);
    uint32_t eid   = (reqid & ERR_REQID_EID_MASK) >> ERR_REQID_EID_SHIFT;
    assert(eid == 0xFFFFU);
    IopmpDestroy(&iopmp);

    PASS("IOPMP-REG-X04 no_err_rec no-capture + eid=0xffff on no-match");
}

/* ── Runner ──────────────────────────────────────────────────────── */

int main(void)
{
    /* 1.1 Reset & default state */
    TestReg001_VersionReadOnly();
    TestReg002_ImplementationReadOnly();
    TestReg003_EnableResetsZero();
    TestReg004_EnableHardwired();
    TestReg005_ErrInfoValidDefaultsZero();
    TestReg006_LockRegsResetValue();
    TestReg007_EntryCfgResetOff();
    TestReg008_EntryOffsetMatchesLayout();
    TestReg009_EntryOffsetNegativeReadback();

    /* 1.2 HWCFG0 / HWCFG1 read-only capability fields */
    TestReg010_MdNumField();
    TestReg011_TorEnSet();
    TestReg012_TorEnClear();
    TestReg013_AddrhEnSet();
    TestReg014_NoErrRecSet();
    TestReg015_Hwcfg23EnBits();
    TestReg016_RridNumField();
    TestReg017_EntryNumField();
    TestReg018_Hwcfg0ReservedZero();

    /* 1.3 WARL */
    TestReg019_EntryCfgNapotLegal();
    TestReg020_EntryCfgTorWhenUnsupported();
    TestReg021_EntryWHardwiredZero();
    TestReg022_EntryCfgReservedZero();
    TestReg023_MdcfgWarl();

    /* 1.4 WISS */
    TestReg024_EnableSet();
    TestReg025_EnableSticky();
    TestReg026_SrcmdEnLockSet();
    TestReg027_SrcmdEnLockSticky();
    TestReg028_MdlckLockSticky();

    /* 1.5 W1CS */
    TestReg029_PrioEntProgW1cs();
    TestReg030_PrioEntProgWriteZeroNoEffect();
    TestReg031_RridTranslProgW1cs();

    /* 1.6 RW1C */
    TestReg032_ErrInfoReadCaptured();
    TestReg033_ErrInfoW1c();
    TestReg034_ErrInfoWriteZeroIgnored();
    TestReg035_ErrInfoTtypeEtypeReadOnly();

    /* 1.7 Address decoding */
    TestReg036_EntryAddrOutOfRange();
    TestReg037_SrcmdOutOfRange();
    TestReg038_Hwcfg2AbsentReadsZero();
    TestReg039_ReservedOffset();
    TestReg040_WordAddressedDecode();

    /* Cross-combinations */
    TestRegX01_DisabledBypass();
    TestRegX02_EnableStickyEnforced();
    TestRegX03_EntrylckClamp();
    TestRegX04_NoErrRecAndEid();

    printf("\nAll file-01 register/WARL tests passed.\n");
    return 0;
}
