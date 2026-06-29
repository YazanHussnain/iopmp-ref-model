/*
 * iopmp_translate.c
 *
 * Main transaction-checking algorithm for the IOPMP reference model.
 *
 * Algorithm (spec §2.7, §5.3):
 *   1. If disabled (HWCFG0.enable == 0), allow everything.
 *   2. Source-enforced (bypass) RRIDs are allowed without checking.
 *   3. If rrid is out of range, or implementation-defined illegal, block
 *      with UNKNOWN_RRID (0x06).
 *   4. Apply RRID translation to get the effective RRID.
 *   5. If the effective RRID is stalled, hold the transaction - or, when
 *      ERR_CFG.stall_violation_en is set, fault it with etype 0x07.
 *   6. Apply global no_w / no_x / xinr.
 *   7. Get the MD bitmap and walk the entries:
 *        a. Priority entries (index < prio_entry): first entry covering any
 *           byte wins; deny / partial-hit / allow per its permission.
 *        b. Non-priority entries (index >= prio_entry): every entry that
 *           covers ALL bytes is a match; legal if ANY match permits. No
 *           partial-hit error is produced for non-priority entries.
 *   8. If nothing matched, block with NO_RULE (0x05).
 */

#include <stdint.h>
#include <stdbool.h>
#include "iopmp.h"
#include "iopmp_reg.h"
#include "iopmp_internal.h"

/* ── Result builders ────────────────────────────────────────────────── */

static TxnResult_t MakeLegalResult(uint32_t entryIdx)
{
    TxnResult_t result = { true, false, false, IOPMP_ETYPE_NONE, entryIdx };
    return result;
}

static TxnResult_t MakeStalledResult(void)
{
    TxnResult_t result = { false, true, false, IOPMP_ETYPE_NONE, UINT32_MAX };
    return result;
}

/*
 * MakeIllegal - build an illegal result and resolve bus-error suppression.
 *
 * The bus-level error response is suppressed (dummy success returned to the
 * initiator) when either the global ERR_CFG.rs bit is set or per-entry
 * bus-error suppression applies for this access type. The violation is still
 * recorded in the error registers regardless.
 */
static TxnResult_t MakeIllegal(const IopmpState_t *iopmp, uint8_t etype,
                               uint32_t entryIdx, bool perEntrySuppressBusErr)
{
    TxnResult_t result = { false, false, false, etype, entryIdx };
    if (perEntrySuppressBusErr
        || (iopmp->regs[REG_ERR_CFG / 4U] & ERR_CFG_RS_BIT) != 0U) {
        result.suppressError = true;
    }
    return result;
}

/* ── Per-access suppression bit selectors (spec §5.1.11) ────────────── */

static uint32_t InterruptSuppressBit(TxnType_t txnType)
{
    if (txnType == IOPMP_TXN_READ) return ENTRY_CFG_SIRE_BIT;
    if (txnType == IOPMP_TXN_EXEC) return ENTRY_CFG_SIXE_BIT;
    return ENTRY_CFG_SIWE_BIT;  /* WRITE and AMO */
}

static uint32_t BusErrorSuppressBit(TxnType_t txnType)
{
    if (txnType == IOPMP_TXN_READ) return ENTRY_CFG_SERE_BIT;
    if (txnType == IOPMP_TXN_EXEC) return ENTRY_CFG_SEXE_BIT;
    return ENTRY_CFG_SEWE_BIT;
}

static uint8_t EtypeForTxnType(TxnType_t txnType)
{
    if (txnType == IOPMP_TXN_READ) return (uint8_t)IOPMP_ETYPE_ILLEGAL_READ;
    if (txnType == IOPMP_TXN_EXEC) return (uint8_t)IOPMP_ETYPE_ILLEGAL_EXEC;
    return (uint8_t)IOPMP_ETYPE_ILLEGAL_WRITE;  /* WRITE and AMO */
}

/* ── MD lookup helpers ──────────────────────────────────────────────── */

static uint8_t FindEntryMd(const IopmpState_t *iopmp, uint32_t entryIdx)
{
    for (uint8_t mdIdx = 0U; mdIdx < iopmp->params.mdNum; mdIdx++) {
        uint32_t rangeStart = 0U;
        uint32_t rangeEnd   = 0U;
        if (MdcfgGetEntryRange(iopmp, mdIdx, &rangeStart, &rangeEnd)) {
            if (entryIdx >= rangeStart && entryIdx < rangeEnd) return mdIdx;
        }
    }
    return UINT8_MAX;
}

/*
 * SpsCheckPermission - secondary permission filter (spec §5.2).
 *
 * When SPS is enabled a transaction is legal only if BOTH the matching entry
 * and the per-RRID SPS register grant the access. SPS can only restrict.
 * Low table (srcmdR/W/X) covers MDs 0-31 (bit m = MD m); high table
 * (srcmdRh/Wh/Xh) covers MDs 32-62 (bit m-32 = MD m).
 */
static bool SpsCheckPermission(const IopmpState_t *iopmp, uint16_t rrid,
                               uint32_t entryIdx, TxnType_t txnType)
{
    if (!iopmp->params.spsEn) return true;

    uint8_t md = FindEntryMd(iopmp, entryIdx);
    if (md == UINT8_MAX) return false;

    const uint32_t *rTab = iopmp->srcmdR;
    const uint32_t *wTab = iopmp->srcmdW;
    const uint32_t *xTab = iopmp->srcmdX;
    uint32_t bit;

    /*
     * Bit layout mirrors SRCMD_EN/ENH (spec §5.1.8-10):
     *   low  table: MD m (0-30)  at bit m+1 (bit 0 reserved).
     *   high table: MD m (31-62) at bit m-31.
     */
    if (md <= 30U) {
        bit = 1U << ((uint32_t)md + 1U);
    } else {
        rTab = iopmp->srcmdRh;
        wTab = iopmp->srcmdWh;
        xTab = iopmp->srcmdXh;
        if (rTab == NULL) return false;  /* high MDs unsupported on this instance */
        bit = 1U << ((uint32_t)md - 31U);
    }

    switch (txnType) {
    case IOPMP_TXN_READ:  return (rTab[rrid] & bit) != 0U;
    case IOPMP_TXN_WRITE: return (wTab[rrid] & bit) != 0U;
    case IOPMP_TXN_EXEC:  return (xTab[rrid] & bit) != 0U;
    case IOPMP_TXN_AMO:   return ((rTab[rrid] & bit) != 0U) && ((wTab[rrid] & bit) != 0U);
    default:              return false;
    }
}

/*
 * RridEntryPermits - does this RRID's permission layer allow the access on the
 * matched entry?
 *
 *   srcmd_fmt 0/1 : entry permission AND the SPS filter (SPS can only restrict).
 *   srcmd_fmt 2   : entry permission OR the MD-indexed SRCMD_PERM grant - the
 *                   MD-indexed format is permissive ("legal if either allows",
 *                   spec §A.4.3).
 */
static bool RridEntryPermits(const IopmpState_t *iopmp, uint16_t rrid,
                             uint32_t entryIdx, TxnType_t txnType);

static bool EntryBelongsToRrid(const IopmpState_t *iopmp, uint32_t entryIdx,
                               uint64_t mdBitmap)
{
    uint64_t remainingBits = mdBitmap;
    uint8_t  mdIdx = 0U;

    while (remainingBits != 0ULL) {
        if ((remainingBits & 1ULL) != 0ULL) {
            uint32_t rangeStart = 0U;
            uint32_t rangeEnd   = 0U;
            if (MdcfgGetEntryRange(iopmp, mdIdx, &rangeStart, &rangeEnd)) {
                if (entryIdx >= rangeStart && entryIdx < rangeEnd) return true;
            }
        }
        remainingBits >>= 1U;
        mdIdx++;
    }
    return false;
}

/*
 * PriorityBoundary - the first non-priority entry index.
 *
 * Entries with index < boundary are priority entries; index >= boundary are
 * non-priority. When the non-priority extension is absent, every entry is a
 * priority entry. The live value of prio_entry is read from HWCFG2 so a
 * programmed (prio_ent_prog) boundary is honoured.
 */
static uint32_t PriorityBoundary(const IopmpState_t *iopmp)
{
    if (!iopmp->params.nonPrioEn) return iopmp->params.entryNum;
    uint32_t hwcfg2 = iopmp->regs[REG_HWCFG2 / 4U];
    return (hwcfg2 & HWCFG2_PRIO_ENTRY_MASK) >> HWCFG2_PRIO_ENTRY_SHIFT;
}

static bool RridEntryPermits(const IopmpState_t *iopmp, uint16_t rrid,
                             uint32_t entryIdx, TxnType_t txnType)
{
    bool entryOk = EntryHasPermission(iopmp, entryIdx, txnType);

    if (iopmp->params.srcmdFmt == 2U) {
        uint8_t md = FindEntryMd(iopmp, entryIdx);
        if (md == UINT8_MAX) return entryOk;
        return entryOk || SrcmdMdIndexedPermits(iopmp, rrid, md, txnType);
    }

    return entryOk && SpsCheckPermission(iopmp, rrid, entryIdx, txnType);
}

TxnResult_t IopmpCheckAccess(IopmpState_t *iopmp, uint16_t rrid,
                             uint64_t addr, uint32_t txnLen,
                             TxnType_t txnType)
{
    /* Disabled IOPMP lets everything through. */
    if (!(iopmp->regs[REG_HWCFG0 / 4U] & HWCFG0_ENABLE_BIT)) {
        return MakeLegalResult(UINT32_MAX);
    }

    /* Source-enforced requesters are checked upstream (spec §A.4.1). */
    if (iopmp->params.rridBypassVec != NULL
        && rrid < iopmp->params.rridNum
        && iopmp->params.rridBypassVec[rrid]) {
        return MakeLegalResult(UINT32_MAX);
    }

    /* RRID legality: out of range, or implementation-defined illegal. */
    bool illegalRrid = (rrid >= iopmp->params.rridNum)
        || (iopmp->params.rridIllegalVec != NULL && iopmp->params.rridIllegalVec[rrid]);
    if (illegalRrid) {
        ErrorRecord(iopmp, rrid, addr, txnType,
                    (uint8_t)IOPMP_ETYPE_UNKNOWN_RRID, UINT32_MAX, false);
        return MakeIllegal(iopmp, (uint8_t)IOPMP_ETYPE_UNKNOWN_RRID, UINT32_MAX, false);
    }

    /* RRID translation: original RRID is kept for error recording. */
    uint16_t origRrid      = rrid;
    uint16_t effectiveRrid = rrid;
    if (iopmp->params.rridTranslEn && (iopmp->rridTransl != NULL)) {
        effectiveRrid = (uint16_t)(iopmp->rridTransl[rrid] & 0xFFFFU);
        if (effectiveRrid >= iopmp->params.rridNum) {
            ErrorRecord(iopmp, origRrid, addr, txnType,
                        (uint8_t)IOPMP_ETYPE_UNKNOWN_RRID, UINT32_MAX, false);
            return MakeIllegal(iopmp, (uint8_t)IOPMP_ETYPE_UNKNOWN_RRID, UINT32_MAX, false);
        }
    }

    /* Stall handling. */
    if (iopmp->params.stallEn && StallRridIsStalled(iopmp, effectiveRrid)) {
        if (iopmp->regs[REG_ERR_CFG / 4U] & ERR_CFG_STALL_VIOL_BIT) {
            ErrorRecord(iopmp, origRrid, addr, txnType,
                        (uint8_t)IOPMP_ETYPE_STALL_VIOL, UINT32_MAX, false);
            return MakeIllegal(iopmp, (uint8_t)IOPMP_ETYPE_STALL_VIOL, UINT32_MAX, false);
        }
        return MakeStalledResult();
    }

    /* Global write disable. */
    if (iopmp->params.noW && (txnType == IOPMP_TXN_WRITE || txnType == IOPMP_TXN_AMO)) {
        ErrorRecord(iopmp, origRrid, addr, txnType,
                    (uint8_t)IOPMP_ETYPE_NO_RULE, UINT32_MAX, false);
        return MakeIllegal(iopmp, (uint8_t)IOPMP_ETYPE_NO_RULE, UINT32_MAX, false);
    }

    /* Global execute disable (takes precedence over xinr). */
    if (iopmp->params.noX && txnType == IOPMP_TXN_EXEC) {
        ErrorRecord(iopmp, origRrid, addr, txnType,
                    (uint8_t)IOPMP_ETYPE_NO_RULE, UINT32_MAX, false);
        return MakeIllegal(iopmp, (uint8_t)IOPMP_ETYPE_NO_RULE, UINT32_MAX, false);
    }

    /* xinr: treat instruction fetch as a data read for the rest of the check. */
    if (iopmp->params.xinr && txnType == IOPMP_TXN_EXEC) {
        txnType = IOPMP_TXN_READ;
    }

    uint64_t mdBitmap   = SrcmdGetMdBitmap(iopmp, effectiveRrid);
    bool     rapidK     = (iopmp->params.model == IOPMP_MODEL_RAPID_K);
    uint32_t boundary   = PriorityBoundary(iopmp);
    uint32_t entryNum   = iopmp->params.entryNum;

    /* ── Priority pass: first covering entry decides ──────────────────── */
    for (uint32_t entryIdx = 0U; entryIdx < boundary && entryIdx < entryNum; entryIdx++) {
        if (!EntryBelongsToRrid(iopmp, entryIdx, mdBitmap)) continue;
        if (!EntryIsActive(iopmp, entryIdx)) continue;
        if (!EntryCoversAnyByte(iopmp, entryIdx, addr, txnLen)) continue;

        /*
         * Partial-hit is evaluated BEFORE permission: per spec §2.7 a
         * highest-priority entry that covers some but not all of the
         * transaction's bytes raises a partial-hit error (0x04)
         * "irrespective of its permission".
         */
        if (!rapidK && !EntryCoversAllBytes(iopmp, entryIdx, addr, txnLen)) {
            bool si = iopmp->params.peisEn
                      && (iopmp->entryCfg[entryIdx] & InterruptSuppressBit(txnType)) != 0U;
            bool se = iopmp->params.peesEn
                      && (iopmp->entryCfg[entryIdx] & BusErrorSuppressBit(txnType)) != 0U;
            ErrorRecord(iopmp, origRrid, addr, txnType,
                        (uint8_t)IOPMP_ETYPE_PARTIAL_HIT, entryIdx, si);
            return MakeIllegal(iopmp, (uint8_t)IOPMP_ETYPE_PARTIAL_HIT, entryIdx, se);
        }

        bool permitted = RridEntryPermits(iopmp, effectiveRrid, entryIdx, txnType);

        if (!permitted) {
            uint8_t  etype = EtypeForTxnType(txnType);
            bool si = iopmp->params.peisEn
                      && (iopmp->entryCfg[entryIdx] & InterruptSuppressBit(txnType)) != 0U;
            bool se = iopmp->params.peesEn
                      && (iopmp->entryCfg[entryIdx] & BusErrorSuppressBit(txnType)) != 0U;
            ErrorRecord(iopmp, origRrid, addr, txnType, etype, entryIdx, si);
            return MakeIllegal(iopmp, etype, entryIdx, se);
        }

        return MakeLegalResult(entryIdx);
    }

    /* ── Non-priority pass: all full-cover matches considered equally ─── */
    if (iopmp->params.nonPrioEn) {
        bool     anyMatch     = false;
        uint32_t firstMatch   = UINT32_MAX;
        /* AND across all matched entries: suppress only if every match suppresses. */
        bool     allSuppressIrq = true;
        bool     allSuppressErr = true;

        for (uint32_t entryIdx = boundary; entryIdx < entryNum; entryIdx++) {
            if (!EntryBelongsToRrid(iopmp, entryIdx, mdBitmap)) continue;
            if (!EntryIsActive(iopmp, entryIdx)) continue;
            /* Non-priority entries must cover ALL bytes; partial overlap is skipped. */
            if (!EntryCoversAllBytes(iopmp, entryIdx, addr, txnLen)) continue;

            /* A matching entry that permits the access makes the txn legal. */
            if (RridEntryPermits(iopmp, effectiveRrid, entryIdx, txnType)) {
                return MakeLegalResult(entryIdx);
            }

            anyMatch = true;
            if (firstMatch == UINT32_MAX) firstMatch = entryIdx;
            if (!(iopmp->entryCfg[entryIdx] & InterruptSuppressBit(txnType))) allSuppressIrq = false;
            if (!(iopmp->entryCfg[entryIdx] & BusErrorSuppressBit(txnType)))  allSuppressErr = false;
        }

        if (anyMatch) {
            uint8_t etype = EtypeForTxnType(txnType);
            bool si = iopmp->params.peisEn && allSuppressIrq;
            bool se = iopmp->params.peesEn && allSuppressErr;
            ErrorRecord(iopmp, origRrid, addr, txnType, etype, firstMatch, si);
            return MakeIllegal(iopmp, etype, firstMatch, se);
        }
    }

    /* Nothing matched. */
    ErrorRecord(iopmp, origRrid, addr, txnType,
                (uint8_t)IOPMP_ETYPE_NO_RULE, UINT32_MAX, false);
    return MakeIllegal(iopmp, (uint8_t)IOPMP_ETYPE_NO_RULE, UINT32_MAX, false);
}

/* ── Error info retrieval ────────────────────────────────────────────── */

IopmpErrInfo_t IopmpGetErrInfo(const IopmpState_t *iopmp)
{
    IopmpErrInfo_t info;
    uint32_t errInfo  = iopmp->regs[REG_ERR_INFO     / 4U];
    uint32_t reqAddrL = iopmp->regs[REG_ERR_REQADDR  / 4U];
    uint32_t reqAddrH = iopmp->regs[REG_ERR_REQADDRH / 4U];
    uint32_t reqId    = iopmp->regs[REG_ERR_REQID    / 4U];

    info.valid    = (errInfo & ERR_INFO_V_BIT) != 0U;
    info.ttype    = (uint8_t)((errInfo & ERR_INFO_TTYPE_MASK) >> ERR_INFO_TTYPE_SHIFT);
    info.etype    = (uint8_t)((errInfo & ERR_INFO_ETYPE_MASK) >> ERR_INFO_ETYPE_SHIFT);
    /* The registers hold the word address (addr[65:2]); rebuild the byte address. */
    info.reqAddr  = ((uint64_t)reqAddrL | ((uint64_t)reqAddrH << 32U)) << 2U;
    info.rrid     = (uint16_t)(reqId & ERR_REQID_RRID_MASK);
    info.entryIdx = (reqId & ERR_REQID_EID_MASK) >> ERR_REQID_EID_SHIFT;

    return info;
}

void IopmpClearError(IopmpState_t *iopmp)
{
    iopmp->regs[REG_ERR_INFO / 4U] &= ~(ERR_INFO_V_BIT | ERR_INFO_SVC_BIT);
    iopmp->irqPending = false;
}
