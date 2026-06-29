/*
 * iopmp_error_stub.c
 *
 * Stub implementations of ErrorRecord and ErrorFireIrq for Phase 2.
 * Phase 3 replaces this file with iopmp_error.c which captures errors
 * in the ERR_INFO, ERR_REQADDR, and ERR_REQID registers.
 */
#include "iopmp_types.h"

void ErrorRecord(IopmpState_t *iopmp, uint16_t rrid, uint64_t addr,
                 TxnType_t txnType, uint8_t etype, uint32_t entryIdx)
{
    /* Phase 2 does not capture errors - silently drop. */
    (void)iopmp; (void)rrid; (void)addr; (void)txnType; (void)etype; (void)entryIdx;
}

void ErrorFireIrq(IopmpState_t *iopmp)
{
    (void)iopmp;
}
