/* iopmp_system.h - stub, will be replaced during system layer execution. */
#ifndef IOPMP_SYSTEM_H
#define IOPMP_SYSTEM_H

#include "iopmp.h"
#include "iopmp_config.h"
#include <stdint.h>

typedef enum {
    IOPMP_SYS_OK              = 0,
    IOPMP_SYS_ERR_NULL_PTR    = 1,
    IOPMP_SYS_ERR_FULL        = 2,
    IOPMP_SYS_ERR_NO_INSTANCE = 3,
    IOPMP_SYS_ERR_OVERLAP     = 4,
} IopmpSysError_t;

typedef struct {
    IopmpState_t  *iopmpPtr;
    uint64_t       mmioBase;
    uint64_t       mmioEnd;
} IopmpEntry_t;

typedef struct {
    IopmpEntry_t  instances[IOPMP_MAX_INSTANCES];
    uint8_t       instanceCount;
} IopmpSystem_t;

IopmpSysError_t IopmpSystemInit(IopmpSystem_t *sysPtr);
IopmpSysError_t IopmpSystemAddInstance(IopmpSystem_t *sysPtr, IopmpState_t *iopmpPtr, uint64_t mmioBase);
IopmpSysError_t IopmpSystemReadReg(IopmpSystem_t *sysPtr, uint64_t byteAddr, uint32_t *valueOut);
IopmpSysError_t IopmpSystemWriteReg(IopmpSystem_t *sysPtr, uint64_t byteAddr, uint32_t value);
TxnResult_t     IopmpSystemCheckAccess(IopmpSystem_t *sysPtr, uint8_t instanceIdx, uint16_t rrid, uint64_t addr, uint32_t txnLen, TxnType_t txnType);
uint8_t         IopmpSystemGetInstanceIdx(const IopmpSystem_t *sysPtr, uint64_t mmioBase);
void            IopmpSystemDestroy(IopmpSystem_t *sysPtr);

#endif /* IOPMP_SYSTEM_H */
