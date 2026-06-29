/*
 * iopmp_init.c
 *
 * Initializes, resets, and tears down an IOPMP instance.
 * All other source files assume IopmpInit() has already run successfully.
 */

#include <stdlib.h>
#include <string.h>
#include "iopmp.h"
#include "iopmp_reg.h"
#include "iopmp_config.h"

/*
 * BuildHwcfg0 - assemble the HWCFG0 register value from params.
 *
 * HWCFG0 is read-only after init. It encodes the hardware capabilities
 * of this instance so software can discover what it supports.
 */
static uint32_t BuildHwcfg0(const IopmpParams_t *params)
{
    uint32_t hwcfg0 = 0U;

    if (params->hwcfg2En)  hwcfg0 |= HWCFG0_HWCFG2_EN_BIT;
    if (params->hwcfg3En)  hwcfg0 |= HWCFG0_HWCFG3_EN_BIT;
    if (params->noErrRec)  hwcfg0 |= HWCFG0_NO_ERR_REC_BIT;
    if (params->addrhEn)   hwcfg0 |= HWCFG0_ADDRH_EN_BIT;
    if (params->torEn)     hwcfg0 |= HWCFG0_TOR_EN_BIT;

    hwcfg0 |= (uint32_t)(params->mdNum & 0x3FU) << HWCFG0_MD_NUM_SHIFT;

    return hwcfg0;
}

/*
 * BuildHwcfg1 - assemble the HWCFG1 register value from params.
 */
static uint32_t BuildHwcfg1(const IopmpParams_t *params)
{
    uint32_t hwcfg1 = (uint32_t)params->rridNum;
    hwcfg1 |= (uint32_t)params->entryNum << HWCFG1_ENTRY_NUM_SHIFT;
    return hwcfg1;
}

/*
 * PopulateHwCfg - fill the hwCfg mirror struct from params.
 *
 * The hwCfg struct lets callers read capability info without decoding
 * register bit fields every time.
 */
static void PopulateHwCfg(IopmpState_t *iopmp)
{
    const IopmpParams_t *params = &iopmp->params;

    iopmp->hwCfg.rridNum   = params->rridNum;
    iopmp->hwCfg.entryNum  = params->entryNum;
    iopmp->hwCfg.mdNum     = params->mdNum;
    iopmp->hwCfg.torEn     = params->torEn;
    iopmp->hwCfg.addrhEn   = params->addrhEn;
    iopmp->hwCfg.noErrRec  = params->noErrRec;
    iopmp->hwCfg.stallEn   = params->stallEn;
    iopmp->hwCfg.hwcfg2En  = params->hwcfg2En;
    iopmp->hwCfg.hwcfg3En  = params->hwcfg3En;
}

/*
 * BuildHwcfg2 - assemble the HWCFG2 register value from params.
 *
 * HWCFG2 is present only when HWCFG0.hwcfg2_en = 1.
 * It reports optional extensions: non-priority entries, stall, etc.
 */
static uint32_t BuildHwcfg2(const IopmpParams_t *params)
{
    uint32_t hwcfg2 = 0U;

    /* prio_entry boundary (bits 15:0) - meaningful only when non_prio_en. */
    if (params->nonPrioEn) {
        hwcfg2 |= ((uint32_t)params->prioEntry << HWCFG2_PRIO_ENTRY_SHIFT)
                  & HWCFG2_PRIO_ENTRY_MASK;
        hwcfg2 |= HWCFG2_NON_PRIO_EN_BIT;
        /* prio_ent_prog is W1CS reset 1 when the boundary is programmable. */
        if (params->prioEntProg) hwcfg2 |= HWCFG2_PRIO_ENT_PROG_BIT;
    }

    if (params->msiEn)         hwcfg2 |= HWCFG2_MSI_EN_BIT;
    if (params->peisEn)        hwcfg2 |= HWCFG2_PEIS_EN_BIT;
    if (params->peesEn)        hwcfg2 |= HWCFG2_PEES_EN_BIT;
    if (params->spsEn)         hwcfg2 |= HWCFG2_SPS_EN_BIT;
    if (params->stallEn)       hwcfg2 |= HWCFG2_STALL_EN_BIT;
    if (params->multifaultEn)  hwcfg2 |= HWCFG2_MFR_EN_BIT;

    return hwcfg2;
}

/*
 * BuildHwcfg3 - assemble the HWCFG3 register value from params.
 *
 * HWCFG3 reports the MDCFG and SRCMD format variants.
 * When mdcfgFmt=2, bits 13:4 carry the entries-per-MD count.
 */
static uint32_t BuildHwcfg3(const IopmpParams_t *params)
{
    uint32_t hwcfg3 = (uint32_t)(params->mdcfgFmt & 0x03U);
    hwcfg3 |= (uint32_t)((params->srcmdFmt & 0x03U) << HWCFG3_SRCMD_FMT_SHIFT);
    if (params->mdcfgFmt == 2U) {
        hwcfg3 |= ((uint32_t)params->mdEntryNum << HWCFG3_MD_ENTRY_NUM_SHIFT)
                  & HWCFG3_MD_ENTRY_NUM_MASK;
    }
    if (params->xinr)            hwcfg3 |= HWCFG3_XINR_BIT;
    if (params->noX)             hwcfg3 |= HWCFG3_NO_X_BIT;
    if (params->noW)             hwcfg3 |= HWCFG3_NO_W_BIT;
    if (params->rridTranslEn)    hwcfg3 |= HWCFG3_RRID_TRANSL_EN_BIT;
    if (params->rridTranslProg)  hwcfg3 |= HWCFG3_RRID_TRANSL_PROG_BIT;
    return hwcfg3;
}

/*
 * WriteHardwareRegisters - write the read-only capability registers.
 *
 * Called at init time and again after reset to restore them.
 */
static void WriteHardwareRegisters(IopmpState_t *iopmp)
{
    iopmp->regs[REG_HWCFG0      / 4U] = BuildHwcfg0(&iopmp->params);
    iopmp->regs[REG_HWCFG1      / 4U] = BuildHwcfg1(&iopmp->params);
    iopmp->regs[REG_ENTRYOFFSET / 4U] = iopmp->params.entryOffset;

    if (iopmp->params.hwcfg2En) {
        iopmp->regs[REG_HWCFG2 / 4U] = BuildHwcfg2(&iopmp->params);
    }
    if (iopmp->params.hwcfg3En) {
        iopmp->regs[REG_HWCFG3 / 4U] = BuildHwcfg3(&iopmp->params);
    }
}

/*
 * WriteResetState - apply the reset values of writable lock registers.
 *
 * These are "prelocked configurations" (spec §3.5): a register may come out
 * of reset already (partially) locked. Re-applied on every reset.
 */
static void WriteResetState(IopmpState_t *iopmp)
{
    const IopmpParams_t *params = &iopmp->params;

    /* Prelocked MDCFGLCK.f (and lock bit when fully prelocked). */
    if (params->mdcfglckResetF != 0U) {
        uint32_t v = ((uint32_t)params->mdcfglckResetF << MDCFGLCK_F_SHIFT) & MDCFGLCK_F_MASK;
        iopmp->regs[REG_MDCFGLCK / 4U] = v;
    }

    /* Prelocked ENTRYLCK.f; ENTRYLCK.l wired 1 when the array is hardwired. */
    if (params->entrylckResetF != 0U || params->entrylckHardwired) {
        uint32_t v = ((uint32_t)params->entrylckResetF << ENTRYLCK_F_SHIFT) & ENTRYLCK_F_MASK;
        if (params->entrylckHardwired) v |= ENTRYLCK_L_BIT;
        iopmp->regs[REG_ENTRYLCK / 4U] = v;
    }

    /*
     * MDLCK preset: prelocked per-MD lock bits. When MDLCK is not implemented
     * (mdlckEn=false) the spec requires MDLCK.l wired 1 and .md wired 0.
     */
    if (params->mdlckDisable) {
        iopmp->regs[REG_MDLCK / 4U] = MDLCK_L_BIT;
    } else if (params->mdlckPreset != 0U) {
        iopmp->regs[REG_MDLCK / 4U] = params->mdlckPreset & MDLCK_MD_MASK;
    }
    if (!params->mdlckDisable && params->mdNum > 31U && params->mdlckhPreset != 0U) {
        iopmp->regs[REG_MDLCKH / 4U] = params->mdlckhPreset;
    }

    /*
     * SRCMD rows may be prelocked through MDLCK presets: an RRID row that has
     * all its lock bits preset is effectively locked. Here we additionally set
     * each SRCMD_EN(s).l for rows the integrator marked prelocked via mdlckPreset
     * being non-zero is NOT automatic - SRCMD prelock is modelled by the SRCMD_EN
     * lock bit, which software can set; integrators wanting a hard prelock wire
     * ENTRYLCK/MDCFGLCK above. (See test plan IOPMP-LOCK-031.)
     */
}

IopmpError_t IopmpInit(IopmpState_t *iopmp, const IopmpParams_t *params)
{
    if (iopmp == NULL || params == NULL) {
        return IOPMP_ERR_NULL_PTR;
    }

    /* rridNum and entryNum are uint16_t, so their max is already bounded by the type. */
    if (params->rridNum  == 0)                              return IOPMP_ERR_INVALID_PARAM;
    if (params->entryNum == 0)                              return IOPMP_ERR_INVALID_PARAM;
    if (params->mdNum    == 0 || params->mdNum > IOPMP_MAX_MD_NUM) return IOPMP_ERR_INVALID_PARAM;

    /* Formats 0-2 are defined. Format 2 requires mdEntryNum > 0. */
    if (params->mdcfgFmt > 2U) return IOPMP_ERR_INVALID_PARAM;
    if (params->srcmdFmt > 2U) return IOPMP_ERR_INVALID_PARAM;
    if (params->mdcfgFmt == 2U && params->mdEntryNum == 0U) return IOPMP_ERR_INVALID_PARAM;

    /*
     * SPS is not supported with the Exclusive (srcmdFmt 1) or MD-indexed
     * (srcmdFmt 2) SRCMD formats - that covers the Isolation and Compact-k
     * models (spec §A.4.3, §A.8). Reject the inconsistent combination.
     */
    if (params->spsEn && params->srcmdFmt != 0U) return IOPMP_ERR_INVALID_PARAM;

    /* Copy params. Fill in the default entry offset when caller left it zero. */
    iopmp->params = *params;
    if (iopmp->params.entryOffset == 0U) {
        iopmp->params.entryOffset = IOPMP_DEFAULT_ENTRY_OFFSET;
    }

    /* Zero all pointers before any allocation so IopmpDestroy() is safe on error. */
    iopmp->regs          = NULL;
    iopmp->mdcfg         = NULL;
    iopmp->srcmdEn       = NULL;
    iopmp->srcmdEnh      = NULL;
    iopmp->srcmdPerm     = NULL;
    iopmp->srcmdPermh    = NULL;
    iopmp->entryAddr     = NULL;
    iopmp->entryAddrh    = NULL;
    iopmp->entryCfg      = NULL;
    iopmp->entryUserCfg  = NULL;
    iopmp->rridStalled   = NULL;
    iopmp->srcmdR        = NULL;
    iopmp->srcmdW        = NULL;
    iopmp->srcmdX        = NULL;
    iopmp->srcmdRh       = NULL;
    iopmp->srcmdWh       = NULL;
    iopmp->srcmdXh       = NULL;
    iopmp->svWords       = NULL;
    iopmp->rridTransl    = NULL;
    iopmp->irqCb         = NULL;
    iopmp->irqCbUser     = NULL;
    iopmp->irqPending    = false;
    iopmp->msiPending    = false;

    /* Allocate the fixed register file. calloc zeros it for us. */
    iopmp->regs = calloc(FIXED_REG_FILE_WORDS, sizeof(uint32_t));
    if (iopmp->regs == NULL) goto alloc_fail;

    iopmp->mdcfg = calloc(params->mdNum, sizeof(uint32_t));
    if (iopmp->mdcfg == NULL) goto alloc_fail;

    /*
     * srcmdFmt=2 uses an MD-indexed PERM table instead of the RRID-indexed EN table.
     * The two sets of arrays are mutually exclusive.
     */
    if (params->srcmdFmt == 2U) {
        iopmp->srcmdPerm = calloc(params->mdNum, sizeof(uint32_t));
        if (iopmp->srcmdPerm == NULL) goto alloc_fail;
        /* MD-indexed PERM holds 2 bits/RRID: PERM covers RRIDs 0-15,
         * PERMH covers RRIDs 16-31 (needed when rridNum > 16). */
        if (params->rridNum > 16U) {
            iopmp->srcmdPermh = calloc(params->mdNum, sizeof(uint32_t));
            if (iopmp->srcmdPermh == NULL) goto alloc_fail;
        }
    } else {
        iopmp->srcmdEn = calloc(params->rridNum, sizeof(uint32_t));
        if (iopmp->srcmdEn == NULL) goto alloc_fail;
        /* Only allocate SRCMD_ENH when we have more than 31 memory domains. */
        if (params->mdNum > 31U) {
            iopmp->srcmdEnh = calloc(params->rridNum, sizeof(uint32_t));
            if (iopmp->srcmdEnh == NULL) goto alloc_fail;
        }
    }

    iopmp->entryAddr = calloc(params->entryNum, sizeof(uint32_t));
    if (iopmp->entryAddr == NULL) goto alloc_fail;

    iopmp->entryCfg = calloc(params->entryNum, sizeof(uint32_t));
    if (iopmp->entryCfg == NULL) goto alloc_fail;

    /* ENTRY_ADDRH is only needed when the instance supports 64-bit addresses. */
    if (params->addrhEn) {
        iopmp->entryAddrh = calloc(params->entryNum, sizeof(uint32_t));
        if (iopmp->entryAddrh == NULL) goto alloc_fail;
    }

    /* Per-RRID stall flags are only needed when the stall extension is enabled. */
    if (params->stallEn) {
        iopmp->rridStalled = calloc(params->rridNum, sizeof(bool));
        if (iopmp->rridStalled == NULL) goto alloc_fail;
    }

    /* SPS secondary permission tables - one 32-bit MD bitmap per RRID. */
    if (params->spsEn) {
        iopmp->srcmdR = calloc(params->rridNum, sizeof(uint32_t));
        if (iopmp->srcmdR == NULL) goto alloc_fail;
        iopmp->srcmdW = calloc(params->rridNum, sizeof(uint32_t));
        if (iopmp->srcmdW == NULL) goto alloc_fail;
        iopmp->srcmdX = calloc(params->rridNum, sizeof(uint32_t));
        if (iopmp->srcmdX == NULL) goto alloc_fail;
        /* High-MD SPS tables (MDs 31-62) only when more than 31 MDs exist. */
        if (params->mdNum > 31U) {
            iopmp->srcmdRh = calloc(params->rridNum, sizeof(uint32_t));
            if (iopmp->srcmdRh == NULL) goto alloc_fail;
            iopmp->srcmdWh = calloc(params->rridNum, sizeof(uint32_t));
            if (iopmp->srcmdWh == NULL) goto alloc_fail;
            iopmp->srcmdXh = calloc(params->rridNum, sizeof(uint32_t));
            if (iopmp->srcmdXh == NULL) goto alloc_fail;
        }
    }

    /* ENTRY_USER_CFG storage - one word per entry when the feature is present. */
    if (params->entryUserCfgEn) {
        iopmp->entryUserCfg = calloc(params->entryNum, sizeof(uint32_t));
        if (iopmp->entryUserCfg == NULL) goto alloc_fail;
    }

    /* Multi-Fault Record subsequent-violation bitmap - one bit per RRID. */
    if (params->multifaultEn) {
        uint32_t svWordCount = ((uint32_t)params->rridNum + 31U) / 32U;
        iopmp->svWords = calloc(svWordCount, sizeof(uint32_t));
        if (iopmp->svWords == NULL) goto alloc_fail;
    }

    /* RRID translation table - one target RRID per source RRID.
     * Default is identity: each RRID maps to itself (no translation effect). */
    if (params->rridTranslEn) {
        iopmp->rridTransl = malloc((size_t)params->rridNum * sizeof(uint32_t));
        if (iopmp->rridTransl == NULL) goto alloc_fail;
        for (uint16_t i = 0U; i < params->rridNum; i++) {
            iopmp->rridTransl[i] = (uint32_t)i;
        }
    }

    PopulateHwCfg(iopmp);
    WriteHardwareRegisters(iopmp);
    WriteResetState(iopmp);

    return IOPMP_OK;

alloc_fail:
    /* Free whatever was allocated before the failure. */
    IopmpDestroy(iopmp);
    return IOPMP_ERR_NO_MEMORY;
}

void IopmpReset(IopmpState_t *iopmp)
{
    /* Save params - they survive the reset. */
    IopmpParams_t savedParams = iopmp->params;

    /* Zero the fixed register file. */
    memset(iopmp->regs, 0, FIXED_REG_FILE_WORDS * sizeof(uint32_t));

    /* Zero each dynamic table. */
    memset(iopmp->mdcfg,     0, savedParams.mdNum    * sizeof(uint32_t));
    memset(iopmp->entryAddr, 0, savedParams.entryNum * sizeof(uint32_t));
    memset(iopmp->entryCfg,  0, savedParams.entryNum * sizeof(uint32_t));

    if (iopmp->srcmdEn     != NULL) memset(iopmp->srcmdEn,     0, savedParams.rridNum * sizeof(uint32_t));
    if (iopmp->srcmdEnh    != NULL) memset(iopmp->srcmdEnh,    0, savedParams.rridNum * sizeof(uint32_t));
    if (iopmp->srcmdPerm   != NULL) memset(iopmp->srcmdPerm,   0, savedParams.mdNum   * sizeof(uint32_t));
    if (iopmp->srcmdPermh  != NULL) memset(iopmp->srcmdPermh,  0, savedParams.mdNum   * sizeof(uint32_t));
    if (iopmp->entryAddrh  != NULL) memset(iopmp->entryAddrh,  0, savedParams.entryNum * sizeof(uint32_t));
    if (iopmp->rridStalled != NULL) memset(iopmp->rridStalled,  0, savedParams.rridNum  * sizeof(bool));
    if (iopmp->srcmdR      != NULL) memset(iopmp->srcmdR,       0, savedParams.rridNum  * sizeof(uint32_t));
    if (iopmp->srcmdW      != NULL) memset(iopmp->srcmdW,       0, savedParams.rridNum  * sizeof(uint32_t));
    if (iopmp->srcmdX      != NULL) memset(iopmp->srcmdX,       0, savedParams.rridNum  * sizeof(uint32_t));
    if (iopmp->srcmdRh     != NULL) memset(iopmp->srcmdRh,      0, savedParams.rridNum  * sizeof(uint32_t));
    if (iopmp->srcmdWh     != NULL) memset(iopmp->srcmdWh,      0, savedParams.rridNum  * sizeof(uint32_t));
    if (iopmp->srcmdXh     != NULL) memset(iopmp->srcmdXh,      0, savedParams.rridNum  * sizeof(uint32_t));
    if (iopmp->entryUserCfg!= NULL) memset(iopmp->entryUserCfg, 0, savedParams.entryNum * sizeof(uint32_t));
    if (iopmp->svWords     != NULL) memset(iopmp->svWords,      0, (((uint32_t)savedParams.rridNum + 31U) / 32U) * sizeof(uint32_t));
    /* rridTransl is NOT zeroed on reset: translation is a hardware parameter. */

    iopmp->irqPending  = false;
    iopmp->msiPending  = false;

    /* Restore params and write hardware registers back. */
    iopmp->params = savedParams;
    WriteHardwareRegisters(iopmp);
    WriteResetState(iopmp);
}

void IopmpDestroy(IopmpState_t *iopmp)
{
    if (iopmp == NULL) return;

    free(iopmp->regs);
    free(iopmp->mdcfg);
    free(iopmp->srcmdEn);
    free(iopmp->srcmdEnh);
    free(iopmp->srcmdPerm);
    free(iopmp->srcmdPermh);
    free(iopmp->entryAddr);
    free(iopmp->entryAddrh);
    free(iopmp->entryCfg);
    free(iopmp->entryUserCfg);
    free(iopmp->rridStalled);
    free(iopmp->srcmdR);
    free(iopmp->srcmdW);
    free(iopmp->srcmdX);
    free(iopmp->srcmdRh);
    free(iopmp->srcmdWh);
    free(iopmp->srcmdXh);
    free(iopmp->svWords);
    free(iopmp->rridTransl);

    /* Zero the struct so any accidental use after destroy crashes fast. */
    memset(iopmp, 0, sizeof(IopmpState_t));
}
