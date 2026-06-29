/*
 * iopmp_error.c
 *
 * Captures IOPMP fault information into the error registers, maintains the
 * Multi-Fault Record (MFR) of subsequent violations, and delivers the
 * interrupt either as a wired IRQ or as an MSI write.
 *
 * "First capture wins": once ERR_INFO.v is set, the primary record is frozen
 * until software clears it (write 1 to ERR_INFO.v). While frozen, subsequent
 * violations are logged only as a per-RRID bit in the MFR SV bitmap (when the
 * multi-fault extension is present).
 *
 * noErrRec mode: when HWCFG0.no_err_rec is set there are no error-capture
 * registers; violations still block but nothing is recorded.
 */

#include <stdint.h>
#include "iopmp_types.h"
#include "iopmp_reg.h"
#include "iopmp.h"

static uint8_t TtypeForTxnType(TxnType_t txnType)
{
    if (txnType == IOPMP_TXN_READ)  return 1U;
    if (txnType == IOPMP_TXN_WRITE) return 2U;
    if (txnType == IOPMP_TXN_AMO)   return 2U;
    if (txnType == IOPMP_TXN_EXEC)  return 3U;
    return 1U;
}

void ErrorFireIrq(IopmpState_t *iopmp)
{
    /* Only fire when the global interrupt-enable bit is set. */
    if (!(iopmp->regs[REG_ERR_CFG / 4U] & ERR_CFG_IE_BIT)) return;

    /*
     * Delivery mode (spec §5.6): MSI is used only when the instance supports
     * it (msiEn) AND software selected it (ERR_CFG.msi_sel). Otherwise a wired
     * interrupt is asserted.
     */
    bool useMsi = iopmp->params.msiEn
                  && (iopmp->regs[REG_ERR_CFG / 4U] & ERR_CFG_MSI_SEL_BIT) != 0U;

    if (useMsi) {
        iopmp->msiPending = true;
        /* Model an MSI write failure when the test harness requests it. */
        if (iopmp->params.msiInjectWriteErr) {
            iopmp->regs[REG_ERR_INFO / 4U] |= ERR_INFO_MSI_WERR_BIT;
        }
    } else {
        iopmp->irqPending = true;
    }

    if (iopmp->irqCb != NULL) {
        iopmp->irqCb(iopmp, iopmp->irqCbUser);
    }
}

bool IopmpIsMsiPending(const IopmpState_t *iopmp)
{
    return iopmp->msiPending;
}

void IopmpClearMsi(IopmpState_t *iopmp)
{
    iopmp->msiPending = false;
}

/*
 * LogSubsequentViolation - record a fault that arrived while ERR_INFO.v=1.
 *
 * Sets the offending RRID's bit in the SV bitmap and ERR_INFO.svc. The
 * interrupt is NOT re-fired: it is already asserted (and stays so until v is
 * cleared), and an MSI must be issued only once per capture (spec §5.6).
 */
static void LogSubsequentViolation(IopmpState_t *iopmp, uint16_t rrid)
{
    if (!iopmp->params.multifaultEn || iopmp->svWords == NULL) return;
    if (rrid >= iopmp->params.rridNum) return;

    iopmp->svWords[rrid / 32U] |= (1U << (rrid % 32U));
    iopmp->regs[REG_ERR_INFO / 4U] |= ERR_INFO_SVC_BIT;
}

void ErrorRecord(IopmpState_t *iopmp, uint16_t rrid, uint64_t addr,
                 TxnType_t txnType, uint8_t etype, uint32_t entryIdx,
                 bool suppressIrq)
{
    if (iopmp->params.noErrRec) return;

    /* Subsequent violation while a record is already valid. */
    if (iopmp->regs[REG_ERR_INFO / 4U] & ERR_INFO_V_BIT) {
        LogSubsequentViolation(iopmp, rrid);
        return;
    }

    uint8_t  ttype   = TtypeForTxnType(txnType);
    uint32_t errInfo = ERR_INFO_V_BIT
                     | ((uint32_t)ttype << ERR_INFO_TTYPE_SHIFT)
                     | ((uint32_t)etype << ERR_INFO_ETYPE_SHIFT);
    iopmp->regs[REG_ERR_INFO / 4U] = errInfo;

    /*
     * ERR_REQADDR holds the WORD address addr[33:2] (spec §4.3.3); ERR_REQADDRH
     * holds addr[65:34]. Store the byte address shifted right by 2 so the
     * register layout matches ENTRY_ADDR/ENTRY_ADDRH.
     */
    iopmp->regs[REG_ERR_REQADDR / 4U] = (uint32_t)((addr >> 2U) & 0xFFFFFFFFU);
    if (iopmp->params.addrhEn) {
        iopmp->regs[REG_ERR_REQADDRH / 4U] = (uint32_t)(addr >> 34U);
    }

    /*
     * ERR_REQID: RRID in bits 15:0, entry index (eid) in bits 31:16.
     * eid reads 0xffff when there is no matching entry, or when the eid
     * field is not implemented (eidEn=false).
     */
    uint16_t eid;
    if (iopmp->params.eidDisable || entryIdx == UINT32_MAX) {
        eid = 0xFFFFU;
    } else {
        eid = (uint16_t)(entryIdx & 0xFFFFU);
    }
    iopmp->regs[REG_ERR_REQID / 4U] = (uint32_t)rrid
                                    | ((uint32_t)eid << ERR_REQID_EID_SHIFT);

    /* Populate user-defined error registers (storage hook, spec §4.3.5). */
    if (iopmp->entryUserCfg != NULL && entryIdx != UINT32_MAX) {
        iopmp->regs[REG_ERR_USER_BASE / 4U] = iopmp->entryUserCfg[entryIdx];
    }

    /* Fire the interrupt unless per-entry suppression applies. */
    if (!suppressIrq) {
        ErrorFireIrq(iopmp);
    }
}
