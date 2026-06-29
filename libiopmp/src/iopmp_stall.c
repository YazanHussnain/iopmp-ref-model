/*
 * iopmp_stall.c
 *
 * RRID stall mechanism.
 *
 * The stall feature lets software temporarily block transactions from
 * selected memory domains (MDs). This is useful when reprogramming
 * entries belonging to a domain that is currently in use.
 *
 * How it works:
 *   1. Software writes MDSTALL/MDSTALLH to mark certain MDs as stalled.
 *   2. StallRecomputeRridMask walks every RRID, checks its MD membership
 *      bitmap (from SRCMD_EN/SRCMD_ENH), and sets rridStalled[rrid] = true
 *      whenever at least one of the RRID's MDs is stalled.
 *   3. IopmpCheckAccess checks StallRridIsStalled before doing the priority
 *      walk; stalled transactions are returned to the caller as
 *      {legal=false, stalled=true} so the bus can retry later.
 */

#include <stdint.h>
#include <stdbool.h>
#include "iopmp_types.h"
#include "iopmp_reg.h"
#include "iopmp_internal.h"

/*
 * StallHandleWrite - consume a write to MDSTALL or MDSTALLH.
 *
 * Returns true when byteOffset is a stall register and the stall extension
 * is active; the caller (IopmpWriteReg) should then skip further processing.
 * Returns false for any other offset.
 *
 * MDSTALL write semantics:
 *   bit 0 = EXEMPT command: 0 = stall the selected MDs, 1 = unstall them.
 *   bits 31:1 = MD selection bitmap (bit n+1 selects MD n, for n = 0..30).
 * MDSTALLH covers MDs 31-62 with its own exempt bit 0.
 *
 * On read-back, bit 0 (IS_BUSY) is always 0 since the reference model is
 * fully synchronous.
 */
/*
 * ImplementedMdMask - bitmap of MDs (bit m = MD m) that physically exist.
 * Used to drop writes to unimplemented MDSTALL.md bits (spec §5.7.8).
 */
static uint64_t ImplementedMdMask(const IopmpState_t *iopmp)
{
    uint32_t n = iopmp->params.mdNum;
    return (n >= 64U) ? ~0ULL : ((1ULL << n) - 1ULL);
}

/*
 * StallSnapshot - latch rrid_stall from the current MDSTALL/MDSTALLH selection.
 *
 * Spec §5.7.3: at the moment MDSTALL is written,
 *   rrid_stall[s] = MDSTALL.exempt XOR Reduction_OR(SRCMD(s).md & stall_by_md)
 * where stall_by_md is the selected-MD bitmap. The SRCMD association is sampled
 * here (snapshot) and not recomputed on later SRCMD changes.
 */
static void StallSnapshot(IopmpState_t *iopmp, uint64_t selectedMds, bool exempt)
{
    if (iopmp->rridStalled == NULL) return;
    for (uint16_t s = 0U; s < iopmp->params.rridNum; s++) {
        uint64_t md      = SrcmdGetMdBitmap(iopmp, s);
        bool     touched = (md & selectedMds) != 0ULL;
        iopmp->rridStalled[s] = exempt ^ touched;
    }
}

bool StallHandleWrite(IopmpState_t *iopmp, uint32_t byteOffset, uint32_t value)
{
    /* MDSTALL/MDSTALLH are not implemented unless the stall extension exists.
     * Consume the write (so it never falls through to generic storage) but
     * leave the registers wired 0. */
    if (byteOffset != REG_MDSTALL && byteOffset != REG_MDSTALLH) return false;
    if (!iopmp->params.stallEn) return true;

    if (byteOffset == REG_MDSTALLH) {
        /*
         * MDSTALLH only stages the high-MD selection (spec §5.7.7 Step 1.1).
         * It does NOT recompute rrid_stall - only the subsequent MDSTALL write
         * (Step 1.2, carrying MDSTALL.exempt) commits the update.
         */
        if (iopmp->params.mdNum <= 31U) return true;        /* wired 0 */
        uint64_t implHigh = ImplementedMdMask(iopmp) >> 31U; /* MDs 31-62 -> bits */
        iopmp->regs[REG_MDSTALLH / 4U] = value & (uint32_t)implHigh;
        return true;
    }

    /* REG_MDSTALL - the committing write (§5.7.7 Step 1.2). */
    bool     exempt   = (value & MDSTALL_EXEMPT_BIT) != 0U;
    uint64_t implMask = ImplementedMdMask(iopmp);

    /* Selected MDs: MDSTALL.md (bits 31:1 -> MD n) plus staged MDSTALLH. */
    uint64_t selected = (uint64_t)(value & MDSTALL_MD_MASK) >> 1U;
    if (iopmp->params.mdNum > 31U) {
        selected |= (uint64_t)iopmp->regs[REG_MDSTALLH / 4U] << 31U;
    }
    selected &= implMask;                                   /* drop unimplemented MDs */

    StallSnapshot(iopmp, selected, exempt);

    /* Readback: MDSTALL.md returns the (implemented) selection; is_busy=0. */
    iopmp->regs[REG_MDSTALL / 4U] =
        (uint32_t)((selected & 0x7FFFFFFFULL) << 1U);
    return true;
}

void StallRecomputeRridMask(IopmpState_t *iopmp)
{
    /*
     * Retained for API compatibility. rrid_stall is now latched as a snapshot
     * at MDSTALL write time (StallSnapshot); there is nothing to recompute from
     * register state alone, since the exempt polarity is not retained.
     */
    (void)iopmp;
}

bool StallRridIsStalled(const IopmpState_t *iopmp, uint16_t rrid)
{
    if (!iopmp->params.stallEn || iopmp->rridStalled == NULL) return false;
    return iopmp->rridStalled[rrid];
}
