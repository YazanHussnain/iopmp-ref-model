/*
 * iopmp.h
 *
 * Public API for the IOPMP reference model. This is the only header
 * that code outside libiopmp should include. Everything else is internal.
 */
#ifndef IOPMP_H
#define IOPMP_H

#include "iopmp_config.h"
#include "iopmp_types.h"
#include "iopmp_reg.h"

/* ── Instance lifecycle ───────────────────────────────────────────── */

/*
 * IopmpInit - set up a new IOPMP instance.
 *
 * Allocates all internal tables based on 'params' and writes the
 * read-only HWCFG registers to match the hardware description.
 * The caller owns the IopmpState_t storage - either stack or heap.
 * Call IopmpDestroy() to release the internal allocations when done.
 *
 * iopmp  : pre-allocated IopmpState_t; its contents are overwritten
 * params : hardware parameters describing this IOPMP instance
 *
 * Returns IOPMP_OK on success.
 */
IopmpError_t IopmpInit(IopmpState_t *iopmp, const IopmpParams_t *params);

/*
 * IopmpReset - return the instance to its power-on state.
 *
 * Clears all writable registers and tables. The params from IopmpInit()
 * are preserved and the read-only HWCFG registers are restored.
 */
void IopmpReset(IopmpState_t *iopmp);

/*
 * IopmpDestroy - free memory allocated by IopmpInit().
 *
 * After this call the IopmpState_t is zeroed and must not be used
 * again without another IopmpInit().
 */
void IopmpDestroy(IopmpState_t *iopmp);

/* ── MMIO register access ─────────────────────────────────────────── */

/*
 * IopmpReadReg - read a 32-bit register at 'byteOffset'.
 *
 * Returns 0 for unknown, unaligned, or unimplemented offsets.
 */
uint32_t IopmpReadReg(IopmpState_t *iopmp, uint32_t byteOffset);

/*
 * IopmpWriteReg - write a 32-bit value to the register at 'byteOffset'.
 *
 * Silently ignores writes to read-only, locked, or unimplemented registers.
 * Applies WARL masking so only legal bits are stored.
 */
void IopmpWriteReg(IopmpState_t *iopmp, uint32_t byteOffset, uint32_t value);

/* ── Transaction checking ─────────────────────────────────────────── */

/*
 * IopmpCheckAccess - decide whether a transaction is allowed.
 *
 * This is the main function of the reference model. It performs the
 * full IOPMP lookup: RRID validation -> SRCMD bitmap -> MDCFG ranges ->
 * entry matching -> permission check.
 *
 * iopmp   : the instance to check against
 * rrid    : requestor role ID of the bus master issuing the transaction
 * addr    : start byte address
 * txnLen  : number of bytes in the transaction
 * txnType : IOPMP_TXN_READ, WRITE, EXEC, or AMO
 */
TxnResult_t IopmpCheckAccess(IopmpState_t *iopmp, uint16_t rrid,
                               uint64_t addr, uint32_t txnLen,
                               TxnType_t txnType);

/* ── Interrupt interface ──────────────────────────────────────────── */
bool IopmpIsIrqPending(const IopmpState_t *iopmp);
void IopmpClearIrq(IopmpState_t *iopmp);

/*
 * IopmpIsMsiPending - true when an MSI write would have been issued.
 *
 * Only meaningful when params.msiEn is set. The caller models the actual
 * bus write using REG_ERR_MSIADDR / REG_ERR_MSIADDRH (target address) and
 * ERR_CFG.msidata (payload).
 */
bool IopmpIsMsiPending(const IopmpState_t *iopmp);
void IopmpClearMsi(IopmpState_t *iopmp);

/* ── Error information ────────────────────────────────────────────── */
IopmpErrInfo_t       IopmpGetErrInfo(const IopmpState_t *iopmp);
void                 IopmpClearError(IopmpState_t *iopmp);

/* ── Hardware capability query ────────────────────────────────────── */
const IopmpHwCfg_t  *IopmpGetHwCfg(const IopmpState_t *iopmp);

/* ── Interrupt callback registration ─────────────────────────────── */

/*
 * IopmpSetIrqCb - register a function to call when an interrupt fires.
 *
 * The callback receives the instance pointer and 'userData' so the
 * caller can identify which IOPMP raised the interrupt.
 * Pass NULL to remove an existing callback.
 */
void IopmpSetIrqCb(IopmpState_t *iopmp, IopmpIrqCb_t cb, void *userData);

#endif /* IOPMP_H */
