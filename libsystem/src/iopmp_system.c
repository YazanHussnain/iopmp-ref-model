/*
 * iopmp_system.c
 *
 * Multi-instance IOPMP router - System Layer.
 *
 * An IopmpSystem_t holds a flat table of up to IOPMP_MAX_INSTANCES IOPMP
 * instances. Each entry records:
 *   - the IopmpState_t pointer (caller-managed; this layer does not own it)
 *   - the MMIO base address of that instance's register window
 *   - the MMIO end address (base + per-instance register file size)
 *
 * Register accesses from the system bus (IopmpSystemReadReg/WriteReg) are
 * dispatched to the correct instance by looking up which instance's MMIO
 * window contains the requested address, then computing the offset within
 * that window.
 *
 * Transaction checks (IopmpSystemCheckAccess) are routed by instance index,
 * which the caller knows from its topology description.
 */

#include <string.h>
#include <stddef.h>
#include "iopmp_system.h"
#include "iopmp.h"
#include "iopmp_config.h"

/*
 * Each IOPMP instance occupies a contiguous MMIO window. The window size
 * covers the fixed registers plus the entry array at its maximum offset.
 * We use a generous 64 KB window that covers the full register map.
 */
#define IOPMP_MMIO_WINDOW_SIZE  0x10000ULL

/* ── Public API ──────────────────────────────────────────────────────── */

IopmpSysError_t IopmpSystemInit(IopmpSystem_t *sysPtr)
{
    if (sysPtr == NULL) return IOPMP_SYS_ERR_NULL_PTR;
    memset(sysPtr, 0, sizeof(*sysPtr));
    return IOPMP_SYS_OK;
}

IopmpSysError_t IopmpSystemAddInstance(IopmpSystem_t *sysPtr,
                                         IopmpState_t  *iopmpPtr,
                                         uint64_t       mmioBase)
{
    if (sysPtr == NULL || iopmpPtr == NULL) return IOPMP_SYS_ERR_NULL_PTR;
    if (sysPtr->instanceCount >= IOPMP_MAX_INSTANCES) return IOPMP_SYS_ERR_FULL;

    uint64_t newEnd = mmioBase + IOPMP_MMIO_WINDOW_SIZE;

    /* Reject any new instance whose MMIO window overlaps an existing one. */
    for (uint8_t idx = 0U; idx < sysPtr->instanceCount; idx++) {
        uint64_t existBase = sysPtr->instances[idx].mmioBase;
        uint64_t existEnd  = sysPtr->instances[idx].mmioEnd;

        bool overlaps = (mmioBase < existEnd) && (newEnd > existBase);
        if (overlaps) return IOPMP_SYS_ERR_OVERLAP;
    }

    IopmpEntry_t *slot    = &sysPtr->instances[sysPtr->instanceCount];
    slot->iopmpPtr        = iopmpPtr;
    slot->mmioBase        = mmioBase;
    slot->mmioEnd         = newEnd;
    sysPtr->instanceCount++;

    return IOPMP_SYS_OK;
}

/*
 * FindInstance - find the instance whose MMIO window contains 'byteAddr'.
 *
 * Returns a pointer to the IopmpEntry_t, or NULL when no instance matches.
 * Sets *offsetOut to the byte offset within that instance's register window.
 */
static const IopmpEntry_t *FindInstance(const IopmpSystem_t *sysPtr,
                                          uint64_t byteAddr,
                                          uint32_t *offsetOut)
{
    for (uint8_t idx = 0U; idx < sysPtr->instanceCount; idx++) {
        const IopmpEntry_t *entry = &sysPtr->instances[idx];

        if (byteAddr >= entry->mmioBase && byteAddr < entry->mmioEnd) {
            *offsetOut = (uint32_t)(byteAddr - entry->mmioBase);
            return entry;
        }
    }
    return NULL;
}

IopmpSysError_t IopmpSystemReadReg(IopmpSystem_t *sysPtr,
                                     uint64_t byteAddr,
                                     uint32_t *valueOut)
{
    if (sysPtr == NULL || valueOut == NULL) return IOPMP_SYS_ERR_NULL_PTR;

    uint32_t offset = 0U;
    const IopmpEntry_t *entry = FindInstance(sysPtr, byteAddr, &offset);
    if (entry == NULL) return IOPMP_SYS_ERR_NO_INSTANCE;

    *valueOut = IopmpReadReg(entry->iopmpPtr, offset);
    return IOPMP_SYS_OK;
}

IopmpSysError_t IopmpSystemWriteReg(IopmpSystem_t *sysPtr,
                                      uint64_t byteAddr,
                                      uint32_t value)
{
    if (sysPtr == NULL) return IOPMP_SYS_ERR_NULL_PTR;

    uint32_t offset = 0U;
    const IopmpEntry_t *entry = FindInstance(sysPtr, byteAddr, &offset);
    if (entry == NULL) return IOPMP_SYS_ERR_NO_INSTANCE;

    IopmpWriteReg(entry->iopmpPtr, offset, value);
    return IOPMP_SYS_OK;
}

TxnResult_t IopmpSystemCheckAccess(IopmpSystem_t *sysPtr,
                                     uint8_t instanceIdx,
                                     uint16_t rrid,
                                     uint64_t addr,
                                     uint32_t txnLen,
                                     TxnType_t txnType)
{
    TxnResult_t illegalResult = {
        .legal         = false,
        .stalled       = false,
        .suppressError = false,
        .etype         = (uint8_t)IOPMP_ETYPE_NO_RULE,
        .entryIdx      = UINT32_MAX,
    };

    if (sysPtr == NULL || instanceIdx >= sysPtr->instanceCount) {
        return illegalResult;
    }

    return IopmpCheckAccess(sysPtr->instances[instanceIdx].iopmpPtr,
                             rrid, addr, txnLen, txnType);
}

uint8_t IopmpSystemGetInstanceIdx(const IopmpSystem_t *sysPtr, uint64_t mmioBase)
{
    if (sysPtr == NULL) return UINT8_MAX;

    for (uint8_t idx = 0U; idx < sysPtr->instanceCount; idx++) {
        if (sysPtr->instances[idx].mmioBase == mmioBase) return idx;
    }
    return UINT8_MAX;
}

void IopmpSystemDestroy(IopmpSystem_t *sysPtr)
{
    /*
     * The system layer does not own the IopmpState_t objects - they are
     * caller-managed. Just zero the system table.
     */
    if (sysPtr == NULL) return;
    memset(sysPtr, 0, sizeof(*sysPtr));
}
