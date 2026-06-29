/*
 * iopmp_lock.c
 *
 * Lock checks for MDLCK, MDCFGLCK, and ENTRYLCK registers.
 *
 * Lock semantics (per the spec):
 *   MDLCK:    bit N+1 set -> MD N is locked. Writes to SRCMD_EN for any
 *             RRID that references a locked MD are blocked.
 *   MDCFGLCK: .f field = number of MDs whose MDCFG is frozen. MDs 0..f-1
 *             are locked. .l bit permanently freezes .f (can only grow).
 *   ENTRYLCK: .f field = number of entries locked. Entries 0..f-1 are
 *             locked. .l bit permanently freezes .f (can only grow).
 *   ERR_CFG:  .l bit locks ERR_CFG from further writes once set.
 *
 * Note: MDLCKH extends MDLCK to cover MDs 31-62 (bits 31:0 of MDLCKH
 * map to MDs 31-62). This implementation checks both registers.
 */

#include <stdint.h>
#include <stdbool.h>
#include "iopmp_types.h"
#include "iopmp_reg.h"

bool LockMdIsLocked(const IopmpState_t *iopmp, uint8_t mdIdx)
{
    if (mdIdx < 31U) {
        /* MDLCK bits 31:1 cover MDs 0-30. MD N is at bit N+1. */
        uint32_t mdlck = iopmp->regs[REG_MDLCK / 4U];
        return (mdlck & (1U << ((uint32_t)mdIdx + 1U))) != 0U;
    } else {
        /* MDLCKH bits 31:0 cover MDs 31-62. MD N is at bit N-31. */
        uint32_t mdlckh = iopmp->regs[REG_MDLCKH / 4U];
        return (mdlckh & (1U << ((uint32_t)mdIdx - 31U))) != 0U;
    }
}

bool LockMdcfgIsLocked(const IopmpState_t *iopmp, uint8_t mdIdx)
{
    /*
     * MDCFGLCK.f is the number of locked MDs.
     * MDs 0 through f-1 are locked.
     */
    uint32_t mdcfglck   = iopmp->regs[REG_MDCFGLCK / 4U];
    uint32_t lockedCount = (mdcfglck & MDCFGLCK_F_MASK) >> MDCFGLCK_F_SHIFT;
    return (uint32_t)mdIdx < lockedCount;
}

bool LockEntryIsLocked(const IopmpState_t *iopmp, uint32_t entryIdx)
{
    /*
     * ENTRYLCK.f is the number of locked entries.
     * Entries 0 through f-1 are locked.
     */
    uint32_t entrylck    = iopmp->regs[REG_ENTRYLCK / 4U];
    uint32_t lockedCount = (entrylck & ENTRYLCK_F_MASK) >> ENTRYLCK_F_SHIFT;
    return entryIdx < lockedCount;
}
