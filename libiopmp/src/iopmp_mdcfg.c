/*
 * iopmp_mdcfg.c
 *
 * Reads the MDCFG table to find which entry indices belong to a given
 * Memory Domain.
 *
 * Two MDCFG formats are supported, selected by params.mdcfgFmt:
 *
 *   mdcfgFmt = 0 (standard):
 *     MDCFG(m).t is the exclusive upper bound for MD m.
 *     MD m covers entries [ MDCFG(m-1).t, MDCFG(m).t ).
 *     MD 0 starts at entry 0 (there is no MDCFG(-1)).
 *     Software programs the MDCFG registers to partition the entry array.
 *
 *   mdcfgFmt = 1 (fixed / equal partition):
 *     The entry array is divided equally among all MDs.
 *     Each MD gets exactly floor(entryNum / mdNum) entries.
 *     The software-programmed MDCFG registers are ignored.
 *     This is the format used by the ISOLATION implementation model.
 */

#include <stdint.h>
#include <stdbool.h>
#include "iopmp_types.h"
#include "iopmp_reg.h"

bool MdcfgGetEntryRange(const IopmpState_t *iopmp, uint8_t mdIdx,
                         uint32_t *startOut, uint32_t *endOut)
{
    /*
     * mdcfgFmt = 1 (fixed equal partition):
     * Each MD owns floor(entryNum / mdNum) consecutive entries.
     * The partition size is derived from the counts; software cannot change it.
     */
    if (iopmp->params.mdcfgFmt == 1U) {
        uint32_t entriesPerMd = (uint32_t)iopmp->params.entryNum
                                / (uint32_t)iopmp->params.mdNum;
        if (entriesPerMd == 0U) return false;

        *startOut = (uint32_t)mdIdx * entriesPerMd;
        *endOut   = *startOut + entriesPerMd;

        /* Clamp the last MD to the actual entry count. */
        if (*endOut > (uint32_t)iopmp->params.entryNum) {
            *endOut = (uint32_t)iopmp->params.entryNum;
        }
        return (*endOut > *startOut);
    }

    /*
     * mdcfgFmt = 2 (programmable fixed partition):
     * Same layout as format 1, but the entries-per-MD count comes from
     * params.mdEntryNum (stored in HWCFG3.md_entry_num) rather than
     * being derived from entryNum / mdNum. This lets hardware expose a
     * smaller, implementation-defined slot size without restricting
     * entryNum or mdNum independently.
     */
    if (iopmp->params.mdcfgFmt == 2U) {
        uint32_t entriesPerMd = (uint32_t)iopmp->params.mdEntryNum;
        if (entriesPerMd == 0U) return false;

        *startOut = (uint32_t)mdIdx * entriesPerMd;
        *endOut   = *startOut + entriesPerMd;

        if (*endOut > (uint32_t)iopmp->params.entryNum) {
            *endOut = (uint32_t)iopmp->params.entryNum;
        }
        return (*endOut > *startOut);
    }

    /*
     * mdcfgFmt = 0 (standard): software-programmed upper bounds.
     * The lower bound is the upper bound of the previous MD, or 0 for MD 0.
     */
    uint32_t lowerBound = (mdIdx == 0U)
                          ? 0U
                          : (iopmp->mdcfg[mdIdx - 1U] & MDCFG_T_MASK);
    uint32_t upperBound = iopmp->mdcfg[mdIdx] & MDCFG_T_MASK;

    /* An empty range means this MD has no entries assigned. */
    if (upperBound <= lowerBound) return false;

    *startOut = lowerBound;
    *endOut   = upperBound;
    return true;
}
