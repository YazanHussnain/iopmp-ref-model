/*
 * iopmp_srcmd.c
 *
 * Reads the SRCMD table to find which Memory Domains (MDs) a given
 * RRID is allowed to access. The result is a 64-bit bitmap where
 * bit N being set means MD N is accessible.
 *
 * Two SRCMD formats are supported, selected by params.srcmdFmt:
 *
 *   srcmdFmt = 0 (baseline bitmap):
 *     SRCMD_EN layout:
 *       bit 0     - SRCMD_EN.l (lock bit, not an MD bit)
 *       bits 31:1 - MD 0..30 (bit N+1 in the register -> MD N in the bitmap)
 *     SRCMD_ENH layout (present when mdNum > 31):
 *       bits 31:0 - MD 31..62 (bit N -> MD N+31 in the bitmap)
 *
 *   srcmdFmt = 1 (compact / direct):
 *     No SRCMD table needed. RRID N is implicitly associated with MD N only.
 *     This is the format used by COMPACT and ISOLATION implementation models.
 */

#include <stdint.h>
#include "iopmp_types.h"
#include "iopmp_reg.h"

/*
 * SrcmdMdIndexedPermits - MD-indexed (srcmd_fmt=2) permission lookup (§A.4.3).
 *
 * Returns whether SRCMD_PERM(md) grants 'txnType' to 'rrid'. bit 2s = read,
 * bit 2s+1 = write; instruction fetch uses the read bit; AMO needs both.
 */
bool SrcmdMdIndexedPermits(const IopmpState_t *iopmp, uint16_t rrid,
                           uint8_t md, TxnType_t txnType)
{
    uint32_t perm; uint32_t rbit;
    if (rrid < 16U) {
        perm = iopmp->srcmdPerm[md];
        rbit = 2U * (uint32_t)rrid;
    } else if (rrid < 32U && iopmp->srcmdPermh != NULL) {
        perm = iopmp->srcmdPermh[md];
        rbit = 2U * ((uint32_t)rrid - 16U);
    } else {
        return false;
    }
    bool rd = ((perm >> rbit) & 1U) != 0U;
    bool wr = ((perm >> (rbit + 1U)) & 1U) != 0U;
    switch (txnType) {
    case IOPMP_TXN_READ:  return rd;
    case IOPMP_TXN_EXEC:  return rd;          /* fetch uses read permission */
    case IOPMP_TXN_WRITE: return wr;
    case IOPMP_TXN_AMO:   return rd && wr;
    default:              return false;
    }
}

uint64_t SrcmdGetMdBitmap(const IopmpState_t *iopmp, uint16_t rrid)
{
    /*
     * srcmdFmt = 1 (compact): each RRID maps directly to the MD with
     * the same index. No register lookup needed.
     */
    if (iopmp->params.srcmdFmt == 1U) {
        /* RRID must be within the number of MDs the instance has. */
        return ((uint32_t)rrid < (uint32_t)iopmp->params.mdNum)
               ? (1ULL << rrid)
               : 0ULL;
    }

    /*
     * srcmdFmt = 2 (MD-indexed PERM):
     * The table is indexed by MD, not by RRID. Each SRCMD_PERM(m) holds a
     * bitmap of RRIDs that can access MD m (bit N+1 -> RRID N for bits 31:1).
     * SRCMD_PERMH(m) covers RRIDs 31-62 (bit N -> RRID N+31).
     *
     * To get the MD bitmap for a given RRID, scan every MD and check
     * whether this RRID's bit is set in that MD's PERM register.
     */
    if (iopmp->params.srcmdFmt == 2U) {
        /*
         * MD-indexed format (spec §A.4.3): SRCMD_PERM(m) holds 2 permission
         * bits per RRID - bit 2s = read, bit 2s+1 = write - for RRIDs 0-15.
         * SRCMD_PERMH(m) covers RRIDs 16-31. An RRID is "associated" with MD m
         * when it has any permission bit set there.
         */
        uint64_t bitmap = 0ULL;
        for (uint8_t mdIdx = 0U; mdIdx < iopmp->params.mdNum; mdIdx++) {
            uint32_t perm; uint32_t rbit;
            if (rrid < 16U) {
                perm = iopmp->srcmdPerm[mdIdx];
                rbit = 2U * (uint32_t)rrid;
            } else if (rrid < 32U && iopmp->srcmdPermh != NULL) {
                perm = iopmp->srcmdPermh[mdIdx];
                rbit = 2U * ((uint32_t)rrid - 16U);
            } else {
                continue;                            /* unsupported RRID in this format */
            }
            if (((perm >> rbit) & 0x3U) != 0U) {
                bitmap |= (1ULL << (uint32_t)mdIdx);
            }
        }
        return bitmap;
    }

    /*
     * srcmdFmt = 0 (baseline bitmap):
     * Extract the MD bits from SRCMD_EN. Bits 31:1 map to MDs 0-30;
     * shift right by 1 to align MD N to bit N of the bitmap.
     */
    uint64_t bitmap = (uint64_t)(iopmp->srcmdEn[rrid] & SRCMD_EN_MD_MASK) >> 1U;

    /*
     * If the instance has more than 31 MDs, SRCMD_ENH holds MDs 31-62.
     * Bit N of SRCMD_ENH maps to MD N+31 in the system-wide bitmap.
     */
    if (iopmp->srcmdEnh != NULL) {
        uint64_t highBits = (uint64_t)iopmp->srcmdEnh[rrid];
        bitmap |= highBits << 31U;
    }

    return bitmap;
}
