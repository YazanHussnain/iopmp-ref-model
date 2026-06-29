/*
 * iopmp_ext.c
 *
 * Extension hooks (64-bit addresses, HWCFG2/HWCFG3, non-priority entries).
 *
 * HWCFG2 and HWCFG3 register setup is handled directly in iopmp_init.c
 * (BuildHwcfg2/BuildHwcfg3 inside WriteHardwareRegisters).
 *
 * NP (non-priority) entry support is integrated into iopmp_translate.c
 * and gated on params.prientProg at runtime.
 *
 * This file serves as the compilation unit for the extension features in the
 * CMake target; additional extension utilities can be added here as needed.
 */

#include "iopmp_types.h"

/*
 * IopmpExtPopulateHwcfg23 - placeholder; real setup happens in
 * WriteHardwareRegisters() inside iopmp_init.c.
 */
void IopmpExtPopulateHwcfg23(IopmpState_t *iopmp) { (void)iopmp; }
