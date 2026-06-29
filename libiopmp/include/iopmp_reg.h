/*
 * iopmp_reg.h
 *
 * MMIO register offsets and field bit masks for the IOPMP.
 * All offsets are byte offsets from the instance MMIO base address.
 *
 * Naming pattern:
 *   REG_<NAME>          - byte offset of a fixed register
 *   <REG>_<FIELD>_MASK  - bit mask for a multi-bit field
 *   <REG>_<FIELD>_SHIFT - right-shift needed to isolate the field value
 *   <REG>_<FIELD>_BIT   - single-bit mask (no shift needed)
 */
#ifndef IOPMP_REG_H
#define IOPMP_REG_H

#include <stdint.h>

/* ── Fixed register byte offsets ──────────────────────────────────── */
#define REG_VERSION             0x0000U
#define REG_IMPLEMENTATION      0x0004U
#define REG_HWCFG0              0x0008U
#define REG_HWCFG1              0x000CU
#define REG_HWCFG2              0x0010U  /* present only if HWCFG0.hwcfg2_en */
#define REG_HWCFG3              0x0014U  /* present only if HWCFG0.hwcfg3_en */
#define REG_ENTRYOFFSET         0x002CU
#define REG_MDSTALL             0x0030U  /* present only if stall extension enabled */
#define REG_MDSTALLH            0x0034U
#define REG_RRIDSCP             0x0038U
#define REG_MDLCK               0x0040U
#define REG_MDLCKH              0x0044U
#define REG_MDCFGLCK            0x0048U
#define REG_ENTRYLCK            0x004CU
#define REG_ERR_CFG             0x0060U
#define REG_ERR_INFO            0x0064U
#define REG_ERR_REQADDR         0x0068U
#define REG_ERR_REQADDRH        0x006CU  /* present only if addrh_en */
#define REG_ERR_REQID           0x0070U
#define REG_ERR_MFR             0x0074U  /* multi-fault record; present only if HWCFG2.mfr_en */
/* ── MSI registers (present only when HWCFG2.msi_en = 1; spec §5.1.7).
 * MSI payload is ERR_CFG.msidata; only the target address lives here. */
#define REG_ERR_MSIADDR         0x0078U  /* MSI write address - low part */
#define REG_ERR_MSIADDRH        0x007CU  /* MSI write address - high part (when addrhEn) */
#define REG_ERR_USER_BASE       0x0080U  /* 8 words: 0x0080..0x009C */

/*
 * Size of the fixed register file in 32-bit words.
 * Covers VERSION through the last ERR_USER register.
 */
#define FIXED_REG_FILE_WORDS    ((REG_ERR_USER_BASE + (8U * 4U)) / 4U)

/* ── Dynamic table base offsets and strides ──────────────────────── */
#define REG_MDCFG_BASE          0x0800U  /* MDCFG(m) = base + m*4 */
#define REG_MDCFG_STRIDE        4U

/*
 * SRCMD table: each RRID s owns a 32-byte slot at REG_SRCMD_BASE + s*32.
 * Layout follows spec Table 5 - SPS permission registers (present only when
 * sps_en) live in the same slot as SRCMD_EN/ENH.
 */
#define REG_SRCMD_BASE          0x1000U  /* slot s = base + s*32 */
#define REG_SRCMD_STRIDE        32U
#define REG_SRCMD_EN_OFF        0x00U    /* SRCMD_EN(s)  / SRCMD_PERM(m)  (srcmdFmt 2) */
#define REG_SRCMD_ENH_OFF       0x04U    /* SRCMD_ENH(s) / SRCMD_PERMH(m) (srcmdFmt 2) */
#define REG_SRCMD_R_OFF         0x08U    /* SRCMD_R(s)  : SPS read,  MDs 0-31  (bit m = MD m)   */
#define REG_SRCMD_RH_OFF        0x0CU    /* SRCMD_RH(s) : SPS read,  MDs 32-62 (bit m-32 = MD m) */
#define REG_SRCMD_W_OFF         0x10U    /* SRCMD_W(s)  : SPS write, MDs 0-31  */
#define REG_SRCMD_WH_OFF        0x14U    /* SRCMD_WH(s) : SPS write, MDs 32-62 */
#define REG_SRCMD_X_OFF         0x18U    /* SRCMD_X(s)  : SPS fetch, MDs 0-31  */
#define REG_SRCMD_XH_OFF        0x1CU    /* SRCMD_XH(s) : SPS fetch, MDs 32-62 */

/* ── RRID translation table (present only when rridTranslEn = true) ─ */
#define REG_RRIDTRANSL_BASE     0x3000U  /* RRIDTRANSL(s) = base + s*4; bits 15:0 = dest RRID */
#define REG_RRIDTRANSL_STRIDE   4U

/* Entry array offsets - base is read from ENTRYOFFSET register */
#define REG_ENTRY_ADDR_OFF      0U       /* offset within one 16-byte entry slot */
#define REG_ENTRY_ADDRH_OFF     4U
#define REG_ENTRY_CFG_OFF       8U
#define REG_ENTRY_USER_CFG_OFF  12U
#define REG_ENTRY_STRIDE        16U

/* ── HWCFG0 field masks ───────────────────────────────────────────── */
#define HWCFG0_ENABLE_BIT       0x00000001U  /* bit  0: global enable (WISS) */
#define HWCFG0_HWCFG2_EN_BIT    0x00000002U  /* bit  1: HWCFG2 implemented */
#define HWCFG0_HWCFG3_EN_BIT    0x00000004U  /* bit  2: HWCFG3 implemented */
/* bits 22:3 reserved (no_w / no_x live in HWCFG3 per spec Table 8) */
#define HWCFG0_NO_ERR_REC_BIT   0x00800000U  /* bit 23: no error-capture regs */
#define HWCFG0_MD_NUM_MASK      0x3F000000U  /* bits 29:24 */
#define HWCFG0_MD_NUM_SHIFT     24U
#define HWCFG0_ADDRH_EN_BIT     0x40000000U  /* bit 30: 64-bit address support */
#define HWCFG0_TOR_EN_BIT       0x80000000U  /* bit 31: TOR address mode support */

/* ── HWCFG1 field masks ───────────────────────────────────────────── */
#define HWCFG1_RRID_NUM_MASK    0x0000FFFFU  /* bits 15:0 */
#define HWCFG1_RRID_NUM_SHIFT   0U
#define HWCFG1_ENTRY_NUM_MASK   0xFFFF0000U  /* bits 31:16 */
#define HWCFG1_ENTRY_NUM_SHIFT  16U

/* ── HWCFG2 field masks (present when HWCFG0.hwcfg2_en = 1) ──────────
 * Layout follows spec Table 4 (§5.1.1).
 */
#define HWCFG2_PRIO_ENTRY_MASK    0x0000FFFFU  /* bits 15:0 : number of priority entries */
#define HWCFG2_PRIO_ENTRY_SHIFT   0U
#define HWCFG2_PRIO_ENT_PROG_BIT  0x00010000U  /* bit 16: prio_entry programmable (W1CS, reset 1) */
#define HWCFG2_NON_PRIO_EN_BIT    0x00020000U  /* bit 17: non-priority entries supported */
#define HWCFG2_MSI_EN_BIT         0x04000000U  /* bit 26: MSI interrupt delivery */
#define HWCFG2_PEIS_EN_BIT        0x08000000U  /* bit 27: per-entry interrupt suppression */
#define HWCFG2_PEES_EN_BIT        0x10000000U  /* bit 28: per-entry bus-error suppression */
#define HWCFG2_SPS_EN_BIT         0x20000000U  /* bit 29: secondary permission setting */
#define HWCFG2_STALL_EN_BIT       0x40000000U  /* bit 30: stall mechanism present */
#define HWCFG2_MFR_EN_BIT         0x80000000U  /* bit 31: multi-fault record supported */
/* RRID-translation capability is reported in HWCFG3 (rrid_transl_en); kept here
 * only as a model-internal flag for register-presence gating. */

/* ── HWCFG3 field masks (present when HWCFG0.hwcfg3_en = 1; spec Table 8) ── */
#define HWCFG3_MDCFG_FMT_MASK         0x00000003U  /* bits  1:0: MDCFG format */
#define HWCFG3_SRCMD_FMT_MASK         0x0000000CU  /* bits  3:2: SRCMD format */
#define HWCFG3_SRCMD_FMT_SHIFT        2U
#define HWCFG3_MD_ENTRY_NUM_MASK      0x000007F0U  /* bits 10:4: entries per MD (mdcfgFmt=2) */
#define HWCFG3_MD_ENTRY_NUM_SHIFT     4U
#define HWCFG3_XINR_BIT               0x00000800U  /* bit 11: execute fetches treated as data reads */
#define HWCFG3_NO_X_BIT               0x00001000U  /* bit 12: global instruction-fetch disable */
#define HWCFG3_NO_W_BIT               0x00002000U  /* bit 13: global write disable */
#define HWCFG3_RRID_TRANSL_EN_BIT     0x00004000U  /* bit 14: RRID translation supported */
#define HWCFG3_RRID_TRANSL_PROG_BIT   0x00008000U  /* bit 15: RRID translation table is SW-programmable */
#define HWCFG3_RRID_TRANSL_MASK       0xFFFF0000U  /* bits 31:16: tagged outgoing RRID */
#define HWCFG3_RRID_TRANSL_SHIFT      16U

/* ── ENTRY_CFG field masks ────────────────────────────────────────── */
#define ENTRY_CFG_R_BIT         0x00000001U  /* read permission */
#define ENTRY_CFG_W_BIT         0x00000002U  /* write permission */
#define ENTRY_CFG_X_BIT         0x00000004U  /* execute / instruction-fetch permission */
#define ENTRY_CFG_A_MASK        0x00000018U  /* address mode bits 4:3 */
#define ENTRY_CFG_A_SHIFT       3U
/*
 * Per-entry suppression bits (spec §5.1.11). Interrupt-suppress bits gated by
 * HWCFG2.peis; bus-error-suppress bits gated by HWCFG2.pees. One bit per access
 * type so a guard region can suppress, e.g., illegal reads but not writes.
 */
#define ENTRY_CFG_SIRE_BIT      0x00000020U  /* bit  5: suppress interrupt on illegal read   */
#define ENTRY_CFG_SIWE_BIT      0x00000040U  /* bit  6: suppress interrupt on illegal write/AMO */
#define ENTRY_CFG_SIXE_BIT      0x00000080U  /* bit  7: suppress interrupt on illegal fetch   */
#define ENTRY_CFG_SERE_BIT      0x00000100U  /* bit  8: suppress bus error on illegal read    */
#define ENTRY_CFG_SEWE_BIT      0x00000200U  /* bit  9: suppress bus error on illegal write/AMO */
#define ENTRY_CFG_SEXE_BIT      0x00000400U  /* bit 10: suppress bus error on illegal fetch    */
#define ENTRY_CFG_SI_MASK      (ENTRY_CFG_SIRE_BIT | ENTRY_CFG_SIWE_BIT | ENTRY_CFG_SIXE_BIT)
#define ENTRY_CFG_SE_MASK      (ENTRY_CFG_SERE_BIT | ENTRY_CFG_SEWE_BIT | ENTRY_CFG_SEXE_BIT)
#define ENTRY_CFG_VALID_MASK   (ENTRY_CFG_R_BIT | ENTRY_CFG_W_BIT \
                               | ENTRY_CFG_X_BIT | ENTRY_CFG_A_MASK \
                               | ENTRY_CFG_SI_MASK | ENTRY_CFG_SE_MASK)

/* ── Address mode encodings (ENTRY_CFG.a field) ──────────────────── */
#define ADDR_MODE_OFF           0U  /* entry disabled */
#define ADDR_MODE_TOR           1U  /* top-of-range */
#define ADDR_MODE_NA4           2U  /* naturally aligned 4-byte region */
#define ADDR_MODE_NAPOT         3U  /* naturally aligned power-of-two region */

/* ── ERR_CFG field masks (spec §4.3.1 + extensions §5.1.4) ────────── */
#define ERR_CFG_L_BIT           0x00000001U  /* bit 0: sticky lock bit (WISS) */
#define ERR_CFG_IE_BIT          0x00000002U  /* bit 1: interrupt enable */
#define ERR_CFG_RS_BIT          0x00000004U  /* bit 2: suppress bus error (return fake success) */
#define ERR_CFG_MSI_SEL_BIT     0x00000008U  /* bit 3: 1 = deliver interrupt via MSI (msi_en only) */
#define ERR_CFG_STALL_VIOL_BIT  0x00000010U  /* bit 4: fault stalled txns (etype 0x07) instead of holding */
#define ERR_CFG_MSIDATA_MASK    0x0007FF00U  /* bits 18:8: MSI payload data */
#define ERR_CFG_MSIDATA_SHIFT   8U
#define ERR_CFG_VALID_MASK     (ERR_CFG_IE_BIT | ERR_CFG_RS_BIT \
                               | ERR_CFG_MSI_SEL_BIT | ERR_CFG_STALL_VIOL_BIT \
                               | ERR_CFG_MSIDATA_MASK)

/* ── ERR_INFO field masks (spec §4.3.2 + extensions §5.1.5) ──────── */
#define ERR_INFO_V_BIT          0x00000001U  /* bit 0: error captured and valid (RW1C) */
#define ERR_INFO_TTYPE_MASK     0x00000006U  /* bits 2:1: transaction type */
#define ERR_INFO_TTYPE_SHIFT    1U
#define ERR_INFO_MSI_WERR_BIT   0x00000008U  /* bit 3: MSI write failed (RW1C) */
#define ERR_INFO_ETYPE_MASK     0x000000F0U  /* bits 7:4: error type */
#define ERR_INFO_ETYPE_SHIFT    4U
#define ERR_INFO_SVC_BIT        0x00000100U  /* bit 8: subsequent violation(s) logged in ERR_MFR */

/* ── ERR_MFR field masks (present when HWCFG2.mfr_en = 1; spec §5.1.6) */
#define ERR_MFR_SVW_MASK        0x0000FFFFU  /* bits 15:0 : 16-bit SV window content */
#define ERR_MFR_SVW_SHIFT       0U
#define ERR_MFR_SVI_MASK        0x0FFF0000U  /* bits 27:16: window index to scan from */
#define ERR_MFR_SVI_SHIFT       16U
#define ERR_MFR_SVS_BIT         0x80000000U  /* bit 31: 1 = a window with set bits was found */

/* ── RRIDSCP field masks (present when stall_en = 1; spec §5.1.3) ── */
#define RRIDSCP_RRID_MASK       0x0000FFFFU  /* bits 15:0 : RRID to select */
#define RRIDSCP_RRID_SHIFT      0U
#define RRIDSCP_OP_MASK         0xC0000000U  /* bits 31:30: op (write) / stat (read) */
#define RRIDSCP_OP_SHIFT        30U
#define RRIDSCP_OP_QUERY        0U
#define RRIDSCP_OP_STALL        1U
#define RRIDSCP_OP_NOSTALL      2U
#define RRIDSCP_STAT_NOIMPL     0U  /* read: RRIDSCP not implemented */
#define RRIDSCP_STAT_STALLED    1U  /* read: selected RRID is stalled */
#define RRIDSCP_STAT_NOTSTALLED 2U  /* read: selected RRID is not stalled */
#define RRIDSCP_STAT_UNSEL      3U  /* read: unimplemented / unselectable RRID */

/* ── SRCMD_EN field masks ─────────────────────────────────────────── */
#define SRCMD_EN_L_BIT          0x00000001U  /* sticky lock for this RRID row */
#define SRCMD_EN_MD_MASK        0xFFFFFFFEU  /* MD bitmap in bits 31:1 */

/* ── MDCFG field masks ────────────────────────────────────────────── */
#define MDCFG_T_MASK            0x0000FFFFU  /* upper entry index bound for this MD */

/* ── MDLCK field masks ────────────────────────────────────────────── */
#define MDLCK_L_BIT             0x00000001U  /* lock bit 0 */
#define MDLCK_MD_MASK           0xFFFFFFFEU  /* per-MD lock bits 31:1 */

/* ── MDCFGLCK field masks ─────────────────────────────────────────── */
#define MDCFGLCK_L_BIT          0x00000001U
#define MDCFGLCK_F_MASK         0x0000007EU  /* number of locked MDs, bits 6:1 */
#define MDCFGLCK_F_SHIFT        1U

/* ── ENTRYLCK field masks ─────────────────────────────────────────── */
#define ENTRYLCK_L_BIT          0x00000001U
#define ENTRYLCK_F_MASK         0xFFFFFFFEU  /* number of locked entries, bits 31:1 */
#define ENTRYLCK_F_SHIFT        1U

/* ── ERR_REQID field masks ────────────────────────────────────────── */
#define ERR_REQID_RRID_MASK     0x0000FFFFU  /* bits 15:0: RRID that caused the fault */
#define ERR_REQID_EID_MASK      0xFFFF0000U  /* bits 31:16: entry index that matched */
#define ERR_REQID_EID_SHIFT     16U

/* ── MDSTALL field masks ──────────────────────────────────────────── */
#define MDSTALL_EXEMPT_BIT      0x00000001U  /* 0 = stall, 1 = exempt from stall */
#define MDSTALL_IS_BUSY_BIT     0x00000001U  /* read-back: 1 = stall update pending */
#define MDSTALL_MD_MASK         0xFFFFFFFEU  /* MD selection bitmap bits 31:1 */

#endif /* IOPMP_REG_H */
