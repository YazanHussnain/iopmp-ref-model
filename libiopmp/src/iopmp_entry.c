/*
 * iopmp_entry.c
 *
 * Decodes individual IOPMP entry fields and answers questions about
 * whether an entry covers a given address range.
 *
 * Address encoding (per the spec):
 *   ENTRY_ADDR stores bits 33:2 of the byte address (i.e. the word address).
 *   ENTRY_ADDRH stores bits 65:34 when 64-bit addresses are enabled.
 *
 *   NA4:   base = wordAddr << 2, size = 4 bytes.
 *
 *   NAPOT: count trailing 1-bits in the word address to get k.
 *          size = 8 << k bytes.  base = (wordAddr & ~sizeMask) << 2.
 *          Example: wordAddr = 0b...011 -> k=2, size=32, base clears bits 1:0.
 *
 *   TOR:   base = previous entry's raw word address << 2 (0 for entry 0).
 *          top  = this entry's raw word address << 2.
 *          Covers bytes in [base, top).
 *
 *   OFF:   entry is disabled; covers nothing.
 */

#include <stdint.h>
#include <stdbool.h>
#include "iopmp_types.h"
#include "iopmp_reg.h"

/*
 * GetWordAddr - return the 34-bit word address of entry 'entryIdx'.
 *
 * ENTRY_ADDR holds bits 33:2, so it is already the word address in
 * 32-bit units. ENTRY_ADDRH extends this to 66-bit byte address space
 * when addrhEn is true (stored in the upper word address bits 65:34).
 */
static uint64_t GetWordAddr(const IopmpState_t *iopmp, uint32_t entryIdx)
{
    uint64_t wordAddr = (uint64_t)iopmp->entryAddr[entryIdx];

    if (iopmp->entryAddrh != NULL) {
        wordAddr |= (uint64_t)iopmp->entryAddrh[entryIdx] << 32U;
    }

    return wordAddr;
}

/*
 * GetAddrMode - return the ENTRY_CFG.a field for the entry.
 */
static uint32_t GetAddrMode(const IopmpState_t *iopmp, uint32_t entryIdx)
{
    return (iopmp->entryCfg[entryIdx] & ENTRY_CFG_A_MASK) >> ENTRY_CFG_A_SHIFT;
}

bool EntryIsActive(const IopmpState_t *iopmp, uint32_t entryIdx)
{
    uint32_t mode = GetAddrMode(iopmp, entryIdx);

    if (mode == ADDR_MODE_OFF) return false;

    /*
     * TOR mode is an optional feature. When the instance is configured
     * without TOR support (torEn = false), TOR-mode entries behave as
     * if they were OFF - they protect nothing.
     */
    if (mode == ADDR_MODE_TOR && !iopmp->params.torEn) return false;

    return true;
}

/*
 * CountTrailingOnes - count how many consecutive 1-bits are at the
 * low end of 'value'. Used for NAPOT size decoding.
 */
static uint32_t CountTrailingOnes(uint64_t value)
{
    uint32_t count = 0U;
    while ((value & 1ULL) != 0ULL) {
        count++;
        value >>= 1U;
    }
    return count;
}

uint64_t EntryGetBase(const IopmpState_t *iopmp, uint32_t entryIdx)
{
    uint32_t mode    = GetAddrMode(iopmp, entryIdx);
    uint64_t wordAddr = GetWordAddr(iopmp, entryIdx);

    if (mode == ADDR_MODE_TOR) {
        /* TOR: base is the raw word address of the *previous* entry, or 0. */
        if (entryIdx == 0U) return 0ULL;
        return GetWordAddr(iopmp, entryIdx - 1U) << 2U;
    }

    if (mode == ADDR_MODE_NA4) {
        return wordAddr << 2U;
    }

    if (mode == ADDR_MODE_NAPOT) {
        /*
         * Clear the trailing one-bits to get the aligned base.
         * k trailing ones -> size = 8<<k bytes, so sizeMask = (8<<k)/4 - 1 in word units.
         */
        uint32_t trailingOnes = CountTrailingOnes(wordAddr);
        uint64_t wordSizeMask = ((uint64_t)1U << trailingOnes) - 1U;
        uint64_t baseWord     = wordAddr & ~wordSizeMask;
        return baseWord << 2U;
    }

    return 0ULL;  /* OFF - no valid base */
}

uint64_t EntryGetSize(const IopmpState_t *iopmp, uint32_t entryIdx)
{
    uint32_t mode     = GetAddrMode(iopmp, entryIdx);
    uint64_t wordAddr  = GetWordAddr(iopmp, entryIdx);

    if (mode == ADDR_MODE_NA4) {
        return 4ULL;
    }

    if (mode == ADDR_MODE_TOR) {
        /* TOR size = top - base, where top = this entry's raw word address << 2. */
        uint64_t base = EntryGetBase(iopmp, entryIdx);
        uint64_t top  = wordAddr << 2U;
        return (top > base) ? (top - base) : 0ULL;
    }

    if (mode == ADDR_MODE_NAPOT) {
        uint32_t trailingOnes = CountTrailingOnes(wordAddr);
        /* k trailing ones -> size = 8 << k bytes. */
        return (uint64_t)8U << trailingOnes;
    }

    return 0ULL;  /* OFF */
}

bool EntryCoversAnyByte(const IopmpState_t *iopmp, uint32_t entryIdx,
                          uint64_t txnAddr, uint32_t txnLen)
{
    uint64_t entryBase = EntryGetBase(iopmp, entryIdx);
    uint64_t entryEnd  = entryBase + EntryGetSize(iopmp, entryIdx);
    uint64_t txnEnd    = txnAddr + (uint64_t)txnLen;

    /* Two ranges overlap when neither is completely before the other. */
    return (txnAddr < entryEnd) && (entryBase < txnEnd);
}

bool EntryCoversAllBytes(const IopmpState_t *iopmp, uint32_t entryIdx,
                           uint64_t txnAddr, uint32_t txnLen)
{
    uint64_t entryBase = EntryGetBase(iopmp, entryIdx);
    uint64_t entryEnd  = entryBase + EntryGetSize(iopmp, entryIdx);

    return (txnAddr >= entryBase) && ((txnAddr + (uint64_t)txnLen) <= entryEnd);
}

bool EntryHasPermission(const IopmpState_t *iopmp, uint32_t entryIdx,
                          TxnType_t txnType)
{
    uint32_t cfg = iopmp->entryCfg[entryIdx];

    if (txnType == IOPMP_TXN_READ) {
        return (cfg & ENTRY_CFG_R_BIT) != 0U;
    }
    if (txnType == IOPMP_TXN_WRITE) {
        return (cfg & ENTRY_CFG_W_BIT) != 0U;
    }
    if (txnType == IOPMP_TXN_EXEC) {
        return (cfg & ENTRY_CFG_X_BIT) != 0U;
    }
    if (txnType == IOPMP_TXN_AMO) {
        /* AMO needs both read and write permission. */
        return ((cfg & ENTRY_CFG_R_BIT) != 0U) && ((cfg & ENTRY_CFG_W_BIT) != 0U);
    }

    return false;
}
