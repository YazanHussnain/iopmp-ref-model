/*
 * iopmp_internal.h
 *
 * Internal function declarations shared between source files inside
 * libiopmp. External code must not include this header.
 */
#ifndef IOPMP_INTERNAL_H
#define IOPMP_INTERNAL_H

#include "iopmp_types.h"

/* ── From iopmp_srcmd.c ────────────────────────────────── */

/*
 * SrcmdGetMdBitmap - return a 64-bit bitmap of MDs associated with 'rrid'.
 *
 * Bit m is set when MD m is accessible to this RRID.
 * MDs 0-30 come from SRCMD_EN bits 31:1.
 * MDs 31-62 come from SRCMD_ENH bits 31:0 (when present).
 */
uint64_t SrcmdGetMdBitmap(const IopmpState_t *iopmp, uint16_t rrid);

/*
 * SrcmdMdIndexedPermits - MD-indexed (srcmd_fmt=2) 2-bit permission lookup.
 * Returns whether SRCMD_PERM(md) grants txnType to rrid (§A.4.3).
 */
bool SrcmdMdIndexedPermits(const IopmpState_t *iopmp, uint16_t rrid,
                           uint8_t md, TxnType_t txnType);

/* ── From iopmp_mdcfg.c ────────────────────────────────── */

/*
 * MdcfgGetEntryRange - get the entry index range owned by MD 'mdIdx'.
 *
 * startOut : first entry index belonging to this MD (inclusive)
 * endOut   : one past the last entry index (exclusive)
 *
 * Returns false when the MD has no entries (empty range).
 */
bool MdcfgGetEntryRange(const IopmpState_t *iopmp, uint8_t mdIdx,
                         uint32_t *startOut, uint32_t *endOut);

/* ── From iopmp_entry.c ────────────────────────────────── */
bool     EntryIsActive(const IopmpState_t *iopmp, uint32_t entryIdx);
uint64_t EntryGetBase(const IopmpState_t *iopmp, uint32_t entryIdx);
uint64_t EntryGetSize(const IopmpState_t *iopmp, uint32_t entryIdx);
bool     EntryCoversAnyByte(const IopmpState_t *iopmp, uint32_t entryIdx,
                              uint64_t txnAddr, uint32_t txnLen);
bool     EntryCoversAllBytes(const IopmpState_t *iopmp, uint32_t entryIdx,
                               uint64_t txnAddr, uint32_t txnLen);
bool     EntryHasPermission(const IopmpState_t *iopmp, uint32_t entryIdx,
                              TxnType_t txnType);

/* ── From iopmp_error.c ────────────────────────────────── */

/*
 * ErrorRecord - capture the first violation in the error registers.
 *
 * On the first violation (ERR_INFO.v == 0) the full record is written and,
 * unless suppressIrq is set or ERR_CFG.ie is clear, the interrupt fires.
 * On a subsequent violation (ERR_INFO.v == 1) the primary record is kept and,
 * when the multi-fault extension is enabled, the offending RRID's bit is set
 * in the SV bitmap and ERR_INFO.svc is set.
 *
 * suppressIrq : caller-computed per-entry interrupt suppression for this
 *               access type (sire/siwe/sixe, AND-reduced over matched
 *               non-priority entries). The interrupt is fired only when
 *               ERR_CFG.ie is set AND suppressIrq is false.
 */
void ErrorRecord(IopmpState_t *iopmp, uint16_t rrid, uint64_t addr,
                 TxnType_t txnType, uint8_t etype, uint32_t entryIdx,
                 bool suppressIrq);

/*
 * ErrorFireIrq - assert the interrupt if ERR_CFG.ie is set.
 */
void ErrorFireIrq(IopmpState_t *iopmp);

/* ── From iopmp_lock.c ─────────────────────────────────── */
bool LockMdIsLocked(const IopmpState_t *iopmp, uint8_t mdIdx);
bool LockMdcfgIsLocked(const IopmpState_t *iopmp, uint8_t mdIdx);
bool LockEntryIsLocked(const IopmpState_t *iopmp, uint32_t entryIdx);

/* ── From iopmp_stall.c ────────────────────────────────── */

/*
 * StallHandleWrite - process a write to the MDSTALL or MDSTALLH register.
 *
 * If byteOffset matches MDSTALL or MDSTALLH and the stall extension is
 * enabled, this function updates the stalled-MD state and recomputes the
 * per-RRID stall flags, then returns true (write was consumed).
 * Returns false for any other offset or when stallEn = false.
 */
bool StallHandleWrite(IopmpState_t *iopmp, uint32_t byteOffset, uint32_t value);

void StallRecomputeRridMask(IopmpState_t *iopmp);
bool StallRridIsStalled(const IopmpState_t *iopmp, uint16_t rrid);

#endif /* IOPMP_INTERNAL_H */
