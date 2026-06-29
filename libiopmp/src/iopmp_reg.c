/*
 * iopmp_reg.c
 *
 * MMIO register read and write for the IOPMP instance.
 *
 * All accesses are word-aligned (4 bytes). Reads from unimplemented or
 * out-of-range addresses return 0. Writes to read-only or locked registers
 * are silently dropped. WARL masking is applied before storing writes.
 */

#include <stddef.h>
#include <stdbool.h>
#include "iopmp.h"
#include "iopmp_reg.h"
#include "iopmp_internal.h"

/*
 * IsReadOnlyOffset - return true if 'byteOffset' is a permanently read-only
 * register. HWCFG2 is writable (W1CS prio_ent_prog / programmable prio_entry);
 * HWCFG3 is currently read-only.
 */
static bool IsReadOnlyOffset(uint32_t byteOffset)
{
    switch (byteOffset) {
    case REG_VERSION:
    case REG_IMPLEMENTATION:
    case REG_HWCFG1:
    case REG_ENTRYOFFSET:
    case REG_ERR_REQADDR:
    case REG_ERR_REQADDRH:
    case REG_ERR_REQID:
        return true;
    default:
        return false;
    }
}

static uint32_t ReadFixedReg(const IopmpState_t *iopmp, uint32_t byteOffset)
{
    if ((byteOffset % 4U) != 0U) return 0U;
    uint32_t wordIdx = byteOffset / 4U;
    if (wordIdx >= FIXED_REG_FILE_WORDS) return 0U;
    return iopmp->regs[wordIdx];
}

/*
 * ReadSpsSlotField - return the SPS permission register at slot field offset,
 * or 0 when SPS is absent / the high table is not present.
 */
static uint32_t ReadSpsSlotField(const IopmpState_t *iopmp, uint32_t fieldOff,
                                 uint32_t rridIdx)
{
    if (!iopmp->params.spsEn) return 0U;
    switch (fieldOff) {
    case REG_SRCMD_R_OFF:  return iopmp->srcmdR  ? iopmp->srcmdR[rridIdx]  : 0U;
    case REG_SRCMD_W_OFF:  return iopmp->srcmdW  ? iopmp->srcmdW[rridIdx]  : 0U;
    case REG_SRCMD_X_OFF:  return iopmp->srcmdX  ? iopmp->srcmdX[rridIdx]  : 0U;
    case REG_SRCMD_RH_OFF: return iopmp->srcmdRh ? iopmp->srcmdRh[rridIdx] : 0U;
    case REG_SRCMD_WH_OFF: return iopmp->srcmdWh ? iopmp->srcmdWh[rridIdx] : 0U;
    case REG_SRCMD_XH_OFF: return iopmp->srcmdXh ? iopmp->srcmdXh[rridIdx] : 0U;
    default:               return 0U;
    }
}

/*
 * ReadErrMfr - read the Multi-Fault Record (spec §5.1.6).
 *
 * Scans the per-RRID SV bitmap in 16-RRID windows starting from the programmed
 * svi, wrapping around. Returns the first window with any set bit (svs=1,
 * svw=window content, svi=found index) and clears that window (clear-on-read).
 * When no window is set, returns svs=0 with svi unchanged.
 */
static uint32_t ReadErrMfr(IopmpState_t *iopmp)
{
    if (!iopmp->params.multifaultEn || iopmp->svWords == NULL) return 0U;

    uint32_t rridNum     = iopmp->params.rridNum;
    uint32_t windowCount = (rridNum + 15U) / 16U;
    if (windowCount == 0U) return 0U;

    uint32_t startSvi = (iopmp->regs[REG_ERR_MFR / 4U] & ERR_MFR_SVI_MASK) >> ERR_MFR_SVI_SHIFT;
    startSvi %= windowCount;

    for (uint32_t k = 0U; k < windowCount; k++) {
        uint32_t w   = (startSvi + k) % windowCount;
        uint32_t svw = 0U;
        for (uint32_t j = 0U; j < 16U; j++) {
            uint32_t rrid = w * 16U + j;
            if (rrid >= rridNum) break;
            if (iopmp->svWords[rrid / 32U] & (1U << (rrid % 32U))) {
                svw |= (1U << j);
            }
        }
        if (svw != 0U) {
            /* Clear-on-read: drop the reported window's SV bits. */
            for (uint32_t j = 0U; j < 16U; j++) {
                uint32_t rrid = w * 16U + j;
                if (rrid >= rridNum) break;
                iopmp->svWords[rrid / 32U] &= ~(1U << (rrid % 32U));
            }
            iopmp->regs[REG_ERR_MFR / 4U] = (w << ERR_MFR_SVI_SHIFT) & ERR_MFR_SVI_MASK;
            /* ERR_INFO.svc tracks whether any SV bit still remains in the log. */
            uint32_t svWordCount = (rridNum + 31U) / 32U;
            uint32_t remaining = 0U;
            for (uint32_t i = 0U; i < svWordCount; i++) remaining |= iopmp->svWords[i];
            if (remaining == 0U) iopmp->regs[REG_ERR_INFO / 4U] &= ~ERR_INFO_SVC_BIT;
            return (svw & ERR_MFR_SVW_MASK)
                 | ((w << ERR_MFR_SVI_SHIFT) & ERR_MFR_SVI_MASK)
                 | ERR_MFR_SVS_BIT;
        }
    }
    /* Nothing found: svs=0, svw=0, svi unchanged. */
    return (startSvi << ERR_MFR_SVI_SHIFT) & ERR_MFR_SVI_MASK;
}

/*
 * ReadRridscp - read RRIDSCP (spec §5.1.3): returns the selected RRID and a
 * status indicating whether it is currently stalled.
 */
static uint32_t ReadRridscp(const IopmpState_t *iopmp)
{
    if (!iopmp->params.stallEn) return 0U;  /* not implemented: stat=0 */

    uint32_t reg  = iopmp->regs[REG_RRIDSCP / 4U];
    uint32_t rrid = (reg & RRIDSCP_RRID_MASK) >> RRIDSCP_RRID_SHIFT;

    uint32_t stat;
    if (rrid >= iopmp->params.rridNum) {
        stat = RRIDSCP_STAT_UNSEL;
    } else {
        stat = StallRridIsStalled(iopmp, (uint16_t)rrid)
             ? RRIDSCP_STAT_STALLED : RRIDSCP_STAT_NOTSTALLED;
    }
    return (rrid & RRIDSCP_RRID_MASK) | (stat << RRIDSCP_OP_SHIFT);
}

uint32_t IopmpReadReg(IopmpState_t *iopmp, uint32_t byteOffset)
{
    uint32_t entryBase = iopmp->params.entryOffset;

    /* ── Entry array (highest priority - above the SRCMD range) ──── */
    if (byteOffset >= entryBase) {
        uint32_t entryOff = byteOffset - entryBase;
        uint32_t entryIdx = entryOff / REG_ENTRY_STRIDE;
        uint32_t fieldOff = entryOff % REG_ENTRY_STRIDE;

        if (entryIdx >= iopmp->params.entryNum) return 0U;

        if (fieldOff == REG_ENTRY_ADDR_OFF)     return iopmp->entryAddr[entryIdx];
        if (fieldOff == REG_ENTRY_CFG_OFF)      return iopmp->entryCfg[entryIdx];
        if (fieldOff == REG_ENTRY_ADDRH_OFF)    return (iopmp->entryAddrh   != NULL) ? iopmp->entryAddrh[entryIdx]   : 0U;
        if (fieldOff == REG_ENTRY_USER_CFG_OFF) return (iopmp->entryUserCfg != NULL) ? iopmp->entryUserCfg[entryIdx] : 0U;
        return 0U;
    }

    /* ── RRID translation table (0x3000+) ───────────────────────────── */
    if (iopmp->params.rridTranslEn && (iopmp->rridTransl != NULL)) {
        uint32_t end = REG_RRIDTRANSL_BASE + (uint32_t)iopmp->params.rridNum * REG_RRIDTRANSL_STRIDE;
        if (byteOffset >= REG_RRIDTRANSL_BASE && byteOffset < end) {
            uint32_t rridIdx = (byteOffset - REG_RRIDTRANSL_BASE) / REG_RRIDTRANSL_STRIDE;
            return iopmp->rridTransl[rridIdx] & 0xFFFFU;
        }
    }

    /* ── SRCMD table - 32-byte slot per RRID (SRCMD_EN/ENH + SPS regs) ── */
    if (byteOffset >= REG_SRCMD_BASE && byteOffset < entryBase) {
        uint32_t srcmdOff = byteOffset - REG_SRCMD_BASE;
        uint32_t slotIdx  = srcmdOff / REG_SRCMD_STRIDE;
        uint32_t fieldOff = srcmdOff % REG_SRCMD_STRIDE;

        if (iopmp->params.srcmdFmt == 2U) {
            if (slotIdx >= iopmp->params.mdNum) return 0U;
            if (fieldOff == REG_SRCMD_EN_OFF)  return iopmp->srcmdPerm[slotIdx];
            if (fieldOff == REG_SRCMD_ENH_OFF) return (iopmp->srcmdPermh != NULL) ? iopmp->srcmdPermh[slotIdx] : 0U;
            return 0U;
        }

        /* Exclusive format (srcmd_fmt=1): no SRCMD table - RRID i maps to MD i. */
        if (iopmp->params.srcmdFmt == 1U) return 0U;
        if (slotIdx >= iopmp->params.rridNum) return 0U;
        if (fieldOff == REG_SRCMD_EN_OFF)  return iopmp->srcmdEn[slotIdx];
        if (fieldOff == REG_SRCMD_ENH_OFF) return (iopmp->srcmdEnh != NULL) ? iopmp->srcmdEnh[slotIdx] : 0U;
        return ReadSpsSlotField(iopmp, fieldOff, slotIdx);
    }

    /* ── MDCFG table - absent (reads 0) when mdcfgFmt != 0 ────────── */
    if (byteOffset >= REG_MDCFG_BASE && byteOffset < REG_SRCMD_BASE) {
        if (iopmp->params.mdcfgFmt != 0U) return 0U;
        uint32_t mdIdx = (byteOffset - REG_MDCFG_BASE) / REG_MDCFG_STRIDE;
        if (mdIdx >= iopmp->params.mdNum) return 0U;
        return iopmp->mdcfg[mdIdx] & MDCFG_T_MASK;
    }

    /* ── ERR_MFR has a special windowed read ──────────────────────── */
    if (byteOffset == REG_ERR_MFR) return ReadErrMfr(iopmp);

    /* ── RRIDSCP read reports stall status of the selected RRID ────── */
    if (byteOffset == REG_RRIDSCP) return ReadRridscp(iopmp);

    /* ── Fixed register area ──────────────────────────────────────── */
    return ReadFixedReg(iopmp, byteOffset);
}

/*
 * SpsApplyMdlck - keep MD bits whose MDLCK lock is set unchanged.
 *
 * Bit layout mirrors SRCMD_EN/ENH (spec §5.1.8-10):
 *   low  table (SRCMD_R/W/X)   : MD m (0-30)  at bit m+1; bit 0 reserved.
 *   high table (SRCMD_RH/WH/XH): MD m (31-62) at bit m-31.
 */
static uint32_t SpsApplyMdlck(const IopmpState_t *iopmp, uint32_t oldVal,
                              uint32_t newVal, bool high)
{
    uint32_t result = newVal;
    for (uint32_t bit = 0U; bit < 32U; bit++) {
        uint32_t md;
        if (high) {
            md = bit + 31U;
        } else {
            if (bit == 0U) continue;          /* reserved */
            md = bit - 1U;
        }
        if (md < iopmp->params.mdNum && LockMdIsLocked(iopmp, (uint8_t)md)) {
            result = (result & ~(1U << bit)) | (oldVal & (1U << bit));
        }
    }
    return result;
}

/*
 * WriteSpsSlotField - write an SPS permission register, honouring SRCMD_EN.l
 * (per-RRID lock) and MDLCK (per-MD lock). Returns true if consumed.
 */
static bool WriteSpsSlotField(IopmpState_t *iopmp, uint32_t fieldOff,
                              uint32_t rridIdx, uint32_t value)
{
    if (!iopmp->params.spsEn) return false;

    /* The whole SPS row is locked when SRCMD_EN(s).l is set. */
    if (iopmp->srcmdEn[rridIdx] & SRCMD_EN_L_BIT) return true;

    uint32_t **tabHi = NULL;
    uint32_t **tabLo = NULL;
    bool high = false;
    switch (fieldOff) {
    case REG_SRCMD_R_OFF:  tabLo = &iopmp->srcmdR;  break;
    case REG_SRCMD_W_OFF:  tabLo = &iopmp->srcmdW;  break;
    case REG_SRCMD_X_OFF:  tabLo = &iopmp->srcmdX;  break;
    case REG_SRCMD_RH_OFF: tabHi = &iopmp->srcmdRh; high = true; break;
    case REG_SRCMD_WH_OFF: tabHi = &iopmp->srcmdWh; high = true; break;
    case REG_SRCMD_XH_OFF: tabHi = &iopmp->srcmdXh; high = true; break;
    default: return false;
    }

    uint32_t *tab = high ? *tabHi : *tabLo;
    if (tab == NULL) return true;  /* high table not present: drop */
    if (!high) value &= ~1U;       /* bit 0 reserved in the low SPS registers */
    tab[rridIdx] = SpsApplyMdlck(iopmp, tab[rridIdx], value, high);
    return true;
}

void IopmpWriteReg(IopmpState_t *iopmp, uint32_t byteOffset, uint32_t value)
{
    if ((byteOffset % 4U) != 0U) return;
    if (IsReadOnlyOffset(byteOffset)) return;

    uint32_t entryBase = iopmp->params.entryOffset;

    /* ── Entry array ──────────────────────────────────────────────── */
    if (byteOffset >= entryBase) {
        uint32_t entryOff = byteOffset - entryBase;
        uint32_t entryIdx = entryOff / REG_ENTRY_STRIDE;
        uint32_t fieldOff = entryOff % REG_ENTRY_STRIDE;

        if (entryIdx >= iopmp->params.entryNum) return;
        if (LockEntryIsLocked(iopmp, entryIdx)) return;

        if (fieldOff == REG_ENTRY_ADDR_OFF) {
            iopmp->entryAddr[entryIdx] = value;
        } else if (fieldOff == REG_ENTRY_CFG_OFF) {
            /*
             * The per-access suppression bits exist only when their capability
             * is present: sire/siwe/sixe require HWCFG2.peis, sere/sewe/sexe
             * require HWCFG2.pees (spec §5.1.11). Otherwise they are wired 0.
             */
            uint32_t mask = ENTRY_CFG_R_BIT | ENTRY_CFG_W_BIT | ENTRY_CFG_X_BIT
                          | ENTRY_CFG_A_MASK;
            if (iopmp->params.peisEn) mask |= ENTRY_CFG_SI_MASK;
            if (iopmp->params.peesEn) mask |= ENTRY_CFG_SE_MASK;
            uint32_t cfg = value & mask;
            /*
             * WARL: TOR is an illegal address mode when tor_en=0, so the field
             * reads back a legal value - coerce it to OFF (spec §4.6.2).
             */
            if (!iopmp->params.torEn
                && ((cfg & ENTRY_CFG_A_MASK) >> ENTRY_CFG_A_SHIFT) == ADDR_MODE_TOR) {
                cfg &= ~ENTRY_CFG_A_MASK;     /* -> OFF */
            }
            /* WARL constraint: writing w=1 forces r=1 when configured. */
            if (iopmp->params.entryPermWImpliesR && (cfg & ENTRY_CFG_W_BIT)) {
                cfg |= ENTRY_CFG_R_BIT;
            }
            iopmp->entryCfg[entryIdx] = cfg;
        } else if (fieldOff == REG_ENTRY_ADDRH_OFF && iopmp->entryAddrh != NULL) {
            uint32_t mask = (iopmp->params.entryAddrhMask != 0U)
                            ? iopmp->params.entryAddrhMask : 0xFFFFFFFFU;
            iopmp->entryAddrh[entryIdx] = value & mask;
        } else if (fieldOff == REG_ENTRY_USER_CFG_OFF && iopmp->entryUserCfg != NULL) {
            iopmp->entryUserCfg[entryIdx] = value;
        }
        return;
    }

    /* ── RRID translation table (0x3000+) ───────────────────────────── */
    if (iopmp->params.rridTranslEn && (iopmp->rridTransl != NULL)) {
        uint32_t end = REG_RRIDTRANSL_BASE + (uint32_t)iopmp->params.rridNum * REG_RRIDTRANSL_STRIDE;
        if (byteOffset >= REG_RRIDTRANSL_BASE && byteOffset < end) {
            if (!iopmp->params.rridTranslProg) return;  /* hardware-fixed */
            uint32_t rridIdx = (byteOffset - REG_RRIDTRANSL_BASE) / REG_RRIDTRANSL_STRIDE;
            iopmp->rridTransl[rridIdx] = value & 0xFFFFU;
            return;
        }
    }

    /* ── SRCMD table - 32-byte slot per RRID ──────────────────────── */
    if (byteOffset >= REG_SRCMD_BASE && byteOffset < entryBase) {
        uint32_t srcmdOff = byteOffset - REG_SRCMD_BASE;
        uint32_t slotIdx  = srcmdOff / REG_SRCMD_STRIDE;
        uint32_t fieldOff = srcmdOff % REG_SRCMD_STRIDE;

        if (iopmp->params.srcmdFmt == 2U) {
            /*
             * MD-indexed format: SRCMD_PERM(m)/PERMH(m) are 2-bit-per-RRID
             * permission bitmaps (no in-register lock bit). Per-MD locking is
             * via MDLCK on MD m.
             */
            if (slotIdx >= iopmp->params.mdNum) return;
            if (LockMdIsLocked(iopmp, (uint8_t)slotIdx)) return;
            if (fieldOff == REG_SRCMD_EN_OFF) {
                iopmp->srcmdPerm[slotIdx] = value;
            } else if (fieldOff == REG_SRCMD_ENH_OFF && iopmp->srcmdPermh != NULL) {
                iopmp->srcmdPermh[slotIdx] = value;
            }
            return;
        }

        /* Exclusive format (srcmd_fmt=1): no SRCMD table - writes dropped. */
        if (iopmp->params.srcmdFmt == 1U) return;
        if (slotIdx >= iopmp->params.rridNum) return;

        if (fieldOff == REG_SRCMD_EN_OFF) {
            if (iopmp->srcmdEn[slotIdx] & SRCMD_EN_L_BIT) return;
            /*
             * MDLCK locks individual MD columns: a locked MD's association bit
             * must keep its current value (spec §3.1) - it can be neither set
             * nor cleared. Unlocked MD bits take the new value.
             */
            uint32_t lockedMdMask = 0U;
            for (uint8_t mdBitIdx = 0U; mdBitIdx < 31U; mdBitIdx++) {
                if (LockMdIsLocked(iopmp, mdBitIdx)) {
                    lockedMdMask |= (1U << ((uint32_t)mdBitIdx + 1U));
                }
            }
            uint32_t old = iopmp->srcmdEn[slotIdx];
            uint32_t md  = (value & SRCMD_EN_MD_MASK & ~lockedMdMask)
                         | (old   & SRCMD_EN_MD_MASK &  lockedMdMask);
            uint32_t l   = (old | value) & SRCMD_EN_L_BIT;   /* WISS */
            iopmp->srcmdEn[slotIdx] = md | l;
        } else if (fieldOff == REG_SRCMD_ENH_OFF && iopmp->srcmdEnh != NULL) {
            if (!(iopmp->srcmdEn[slotIdx] & SRCMD_EN_L_BIT)) {
                uint32_t lockedMask = 0U;
                for (uint8_t mdBitIdx = 0U; mdBitIdx < 32U; mdBitIdx++) {
                    if (LockMdIsLocked(iopmp, mdBitIdx + 31U)) {
                        lockedMask |= (1U << mdBitIdx);
                    }
                }
                uint32_t old = iopmp->srcmdEnh[slotIdx];
                iopmp->srcmdEnh[slotIdx] = (value & ~lockedMask) | (old & lockedMask);
            }
        } else {
            WriteSpsSlotField(iopmp, fieldOff, slotIdx, value);
        }
        return;
    }

    /* ── MDCFG table ──────────────────────────────────────────────── */
    if (byteOffset >= REG_MDCFG_BASE && byteOffset < REG_SRCMD_BASE) {
        uint32_t mdIdx = (byteOffset - REG_MDCFG_BASE) / REG_MDCFG_STRIDE;
        if (mdIdx >= iopmp->params.mdNum) return;
        if (LockMdcfgIsLocked(iopmp, (uint8_t)mdIdx)) return;
        /*
         * Improper settings (MDCFG(m).t < MDCFG(m-1).t) are accepted as written;
         * the lookup (MdcfgGetEntryRange) then reports MD m as owning no entries
         * - the "isolate MD m" reference behaviour (spec §A.5).
         */
        iopmp->mdcfg[mdIdx] = value & MDCFG_T_MASK;
        return;
    }

    /* ── HWCFG0 - only the enable bit is software-writable (WISS) ──── */
    if (byteOffset == REG_HWCFG0) {
        if (value & HWCFG0_ENABLE_BIT) {
            iopmp->regs[REG_HWCFG0 / 4U] |= HWCFG0_ENABLE_BIT;
        }
        return;
    }

    /* ── HWCFG2 - prio_ent_prog (W1CS) and programmable prio_entry ── */
    if (byteOffset == REG_HWCFG2) {
        uint32_t cur    = iopmp->regs[REG_HWCFG2 / 4U];
        uint32_t newVal = cur;
        /* prio_entry is writable only while prio_ent_prog is still set. */
        if (cur & HWCFG2_PRIO_ENT_PROG_BIT) {
            newVal = (newVal & ~HWCFG2_PRIO_ENTRY_MASK) | (value & HWCFG2_PRIO_ENTRY_MASK);
        }
        /* prio_ent_prog is W1CS: writing 1 clears it (sticky to 0). */
        if ((value & HWCFG2_PRIO_ENT_PROG_BIT) && (cur & HWCFG2_PRIO_ENT_PROG_BIT)) {
            newVal &= ~HWCFG2_PRIO_ENT_PROG_BIT;
        }
        iopmp->regs[REG_HWCFG2 / 4U] = newVal;
        return;
    }

    /* ── HWCFG3 - programmable md_entry_num (until enable) + rrid_transl_prog W1CS ── */
    if (byteOffset == REG_HWCFG3) {
        uint32_t cur    = iopmp->regs[REG_HWCFG3 / 4U];
        uint32_t newVal = cur;

        /*
         * md_entry_num (the Dynamic-k slot size) is WARL and writable only
         * while the IOPMP is disabled; HWCFG0.enable locks it (spec App. A.6).
         */
        bool enabled = (iopmp->regs[REG_HWCFG0 / 4U] & HWCFG0_ENABLE_BIT) != 0U;
        if (!enabled && iopmp->params.mdcfgFmt == 2U) {
            newVal = (newVal & ~HWCFG3_MD_ENTRY_NUM_MASK)
                   | (value & HWCFG3_MD_ENTRY_NUM_MASK);
            /* Keep the live lookup parameter in step with the register. */
            iopmp->params.mdEntryNum =
                (uint16_t)((newVal & HWCFG3_MD_ENTRY_NUM_MASK) >> HWCFG3_MD_ENTRY_NUM_SHIFT);
        }

        /* rrid_transl_prog is W1CS: writing 1 clears it (locks rrid_transl). */
        if ((value & HWCFG3_RRID_TRANSL_PROG_BIT) && (cur & HWCFG3_RRID_TRANSL_PROG_BIT)) {
            newVal &= ~HWCFG3_RRID_TRANSL_PROG_BIT;
            iopmp->params.rridTranslProg = false;
        }

        iopmp->regs[REG_HWCFG3 / 4U] = newVal;
        return;
    }

    /* ── ERR_INFO - RW1C on v and msi_werr ────────────────────────── */
    if (byteOffset == REG_ERR_INFO) {
        if (value & ERR_INFO_V_BIT) {
            /* Clearing v re-arms capture: also clears the svc status bit and
             * drains the multi-fault log so each capture cycle starts fresh. */
            iopmp->regs[REG_ERR_INFO / 4U] &= ~(ERR_INFO_V_BIT | ERR_INFO_SVC_BIT);
            iopmp->irqPending = false;
            if (iopmp->svWords != NULL) {
                uint32_t svWordCount = ((uint32_t)iopmp->params.rridNum + 31U) / 32U;
                for (uint32_t i = 0U; i < svWordCount; i++) iopmp->svWords[i] = 0U;
            }
        }
        if (value & ERR_INFO_MSI_WERR_BIT) {
            iopmp->regs[REG_ERR_INFO / 4U] &= ~ERR_INFO_MSI_WERR_BIT;
        }
        return;
    }

    /* ── ERR_MFR - write sets the svi scan index (WARL) ───────────── */
    if (byteOffset == REG_ERR_MFR) {
        iopmp->regs[REG_ERR_MFR / 4U] = (value & ERR_MFR_SVI_MASK);
        return;
    }

    /* ── RRIDSCP - cherry-pick stall control (spec §5.1.3) ────────── */
    if (byteOffset == REG_RRIDSCP) {
        if (!iopmp->params.stallEn) return;
        uint32_t rrid = (value & RRIDSCP_RRID_MASK) >> RRIDSCP_RRID_SHIFT;
        uint32_t op   = (value & RRIDSCP_OP_MASK)   >> RRIDSCP_OP_SHIFT;
        /* Remember the selected RRID for a subsequent query read. */
        iopmp->regs[REG_RRIDSCP / 4U] = (rrid & RRIDSCP_RRID_MASK);
        if (rrid < iopmp->params.rridNum && iopmp->rridStalled != NULL) {
            if (op == RRIDSCP_OP_STALL)   iopmp->rridStalled[rrid] = true;
            if (op == RRIDSCP_OP_NOSTALL) iopmp->rridStalled[rrid] = false;
        }
        return;
    }

    /* ── ERR_CFG - ie/rs/msi_sel/stall_violation_en/msidata; l is WISS ── */
    if (byteOffset == REG_ERR_CFG) {
        uint32_t curVal = iopmp->regs[REG_ERR_CFG / 4U];
        if (curVal & ERR_CFG_L_BIT) return;  /* frozen once locked */
        /*
         * Only the fields whose feature is present are writable; the rest are
         * wired 0 (spec §5.1.4): msi_sel/msidata require MSI delivery,
         * stall_violation_en requires the stall extension.
         */
        uint32_t validMask = ERR_CFG_IE_BIT | ERR_CFG_RS_BIT;
        if (iopmp->params.msiEn)   validMask |= ERR_CFG_MSI_SEL_BIT | ERR_CFG_MSIDATA_MASK;
        if (iopmp->params.stallEn) validMask |= ERR_CFG_STALL_VIOL_BIT;
        uint32_t newLock = (value & ERR_CFG_L_BIT) | (curVal & ERR_CFG_L_BIT);
        iopmp->regs[REG_ERR_CFG / 4U] = newLock | (value & validMask);
        return;
    }

    /* ── MSI address registers - present only with MSI; locked by ERR_CFG.l ── */
    if (byteOffset == REG_ERR_MSIADDR || byteOffset == REG_ERR_MSIADDRH) {
        if (!iopmp->params.msiEn) return;                 /* not implemented */
        if (byteOffset == REG_ERR_MSIADDRH && !iopmp->params.addrhEn) return;
        if (iopmp->regs[REG_ERR_CFG / 4U] & ERR_CFG_L_BIT) return;
        iopmp->regs[byteOffset / 4U] = value;
        return;
    }

    /* ── ENTRYLCK - .f sticky/incremental; .l WISS ────────────────── */
    if (byteOffset == REG_ENTRYLCK) {
        uint32_t curVal = iopmp->regs[REG_ENTRYLCK / 4U];
        if (curVal & ENTRYLCK_L_BIT) return;
        uint32_t curCount  = (curVal & ENTRYLCK_F_MASK) >> ENTRYLCK_F_SHIFT;
        uint32_t newCount  = (value  & ENTRYLCK_F_MASK) >> ENTRYLCK_F_SHIFT;
        uint32_t finalCount = (newCount > curCount) ? newCount : curCount;
        uint32_t newLock    = (value & ENTRYLCK_L_BIT) | (curVal & ENTRYLCK_L_BIT);
        iopmp->regs[REG_ENTRYLCK / 4U] = newLock | (finalCount << ENTRYLCK_F_SHIFT);
        return;
    }

    /* ── MDCFGLCK - .f sticky/incremental; .l WISS ────────────────── */
    if (byteOffset == REG_MDCFGLCK) {
        uint32_t curVal = iopmp->regs[REG_MDCFGLCK / 4U];
        if (curVal & MDCFGLCK_L_BIT) return;
        uint32_t curCount  = (curVal & MDCFGLCK_F_MASK) >> MDCFGLCK_F_SHIFT;
        uint32_t newCount  = (value  & MDCFGLCK_F_MASK) >> MDCFGLCK_F_SHIFT;
        uint32_t finalCount = (newCount > curCount) ? newCount : curCount;
        uint32_t newLock    = (value & MDCFGLCK_L_BIT) | (curVal & MDCFGLCK_L_BIT);
        iopmp->regs[REG_MDCFGLCK / 4U] = newLock | (finalCount << MDCFGLCK_F_SHIFT);
        return;
    }

    /* ── MDLCK / MDLCKH - sticky per-MD lock bits + WISS l ────────── */
    if (byteOffset == REG_MDLCK) {
        if (iopmp->params.mdlckDisable) return;            /* MDLCK not implemented */
        uint32_t curVal = iopmp->regs[REG_MDLCK / 4U];
        if (curVal & MDLCK_L_BIT) return;                   /* frozen */
        /* md bits are sticky-set (cannot be cleared); l is WISS. */
        uint32_t md = (curVal | value) & MDLCK_MD_MASK;
        uint32_t l  = (curVal | value) & MDLCK_L_BIT;
        iopmp->regs[REG_MDLCK / 4U] = md | l;
        return;
    }
    if (byteOffset == REG_MDLCKH) {
        if (iopmp->params.mdlckDisable) return;
        if (iopmp->params.mdNum <= 31U) return;             /* not implemented: wired 0 */
        if (iopmp->regs[REG_MDLCK / 4U] & MDLCK_L_BIT) return;
        iopmp->regs[REG_MDLCKH / 4U] |= value;              /* sticky-set */
        return;
    }

    /* ── MDSTALL / MDSTALLH - delegated to the stall module ────────── */
    if (StallHandleWrite(iopmp, byteOffset, value)) return;

    /* ── All other fixed registers - write directly ───────────────── */
    uint32_t wordIdx = byteOffset / 4U;
    if (wordIdx < FIXED_REG_FILE_WORDS) {
        iopmp->regs[wordIdx] = value;
    }
}

/* ── Remaining public API ────────────────────────────────────────── */

const IopmpHwCfg_t *IopmpGetHwCfg(const IopmpState_t *iopmp)
{
    return &iopmp->hwCfg;
}

void IopmpSetIrqCb(IopmpState_t *iopmp, IopmpIrqCb_t cb, void *userData)
{
    iopmp->irqCb     = cb;
    iopmp->irqCbUser = userData;
}

bool IopmpIsIrqPending(const IopmpState_t *iopmp)
{
    return iopmp->irqPending;
}

void IopmpClearIrq(IopmpState_t *iopmp)
{
    iopmp->irqPending = false;
}
