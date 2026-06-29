/*
 * iopmp_types.h
 *
 * All data structures, enumerations, and type definitions for the
 * IOPMP reference model. Nothing outside this file defines new types.
 */
#ifndef IOPMP_TYPES_H
#define IOPMP_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ── API error codes ──────────────────────────────────────────────── */
typedef enum {
    IOPMP_OK                = 0,  /* operation succeeded */
    IOPMP_ERR_NULL_PTR      = 1,  /* a required pointer argument was NULL */
    IOPMP_ERR_INVALID_PARAM = 2,  /* a parameter value is out of its legal range */
    IOPMP_ERR_NO_MEMORY     = 3,  /* malloc returned NULL - out of memory */
    IOPMP_ERR_NOT_FOUND     = 4,  /* instance not found in the system table */
    IOPMP_ERR_TABLE_FULL    = 5,  /* no room left in the system instance table */
} IopmpError_t;

/* ── Implementation model variants (spec Appendix A.8) ────────────────
 * Each model is the combination of an SRCMD format and an MDCFG format:
 *   Full      (srcmdFmt 0, mdcfgFmt 0)
 *   Rapid-k   (srcmdFmt 0, mdcfgFmt 1)
 *   Dynamic-k (srcmdFmt 0, mdcfgFmt 2)
 *   Isolation (srcmdFmt 1, mdcfgFmt 0) - no SPS
 *   Compact-k (srcmdFmt 1, mdcfgFmt 1) - no SPS
 */
typedef enum {
    IOPMP_MODEL_FULL      = 0,
    IOPMP_MODEL_RAPID_K   = 1,
    IOPMP_MODEL_DYNAMIC_K = 2,
    IOPMP_MODEL_ISOLATION = 3,
    IOPMP_MODEL_COMPACT   = 4,
} IopmpModel_t;

/* ── Transaction access types ─────────────────────────────────────── */
typedef enum {
    IOPMP_TXN_READ  = 1,  /* read transaction */
    IOPMP_TXN_WRITE = 2,  /* write transaction */
    IOPMP_TXN_EXEC  = 3,  /* instruction fetch */
    IOPMP_TXN_AMO   = 4,  /* atomic memory op - needs both read and write permission */
} TxnType_t;

/* ── Error type codes captured in ERR_INFO.etype ─────────────────── */
typedef enum {
    IOPMP_ETYPE_NONE          = 0x00,
    IOPMP_ETYPE_ILLEGAL_READ  = 0x01,
    IOPMP_ETYPE_ILLEGAL_WRITE = 0x02,
    IOPMP_ETYPE_ILLEGAL_EXEC  = 0x03,
    IOPMP_ETYPE_PARTIAL_HIT   = 0x04,  /* entry matched but did not cover all bytes */
    IOPMP_ETYPE_NO_RULE       = 0x05,  /* no entry matched the transaction */
    IOPMP_ETYPE_UNKNOWN_RRID  = 0x06,  /* RRID >= rridNum or implementation-defined illegal */
    IOPMP_ETYPE_STALL_VIOL    = 0x07,  /* stalled txn faulted (ERR_CFG.stall_violation_en) */
    IOPMP_ETYPE_USER_0        = 0x0E,
    IOPMP_ETYPE_USER_1        = 0x0F,
} IopmpEtype_t;

/* ── Result returned by IopmpCheckAccess ──────────────────────────── */
typedef struct {
    bool        legal;          /* true when the transaction is allowed */
    bool        stalled;        /* true when the RRID is stalled - caller should retry */
    bool        suppressError;  /* true when bus error should be suppressed (return dummy
                                   success to initiator); error IS still recorded internally.
                                   Set when ERR_CFG.rs is set OR the matching entry's per-access
                                   bus-error-suppress bit (sere/sewe/sexe) is set with peesEn. */
    uint8_t     etype;          /* error type code; 0 when legal */
    uint32_t    entryIdx;       /* index of the matching entry; UINT32_MAX when none */
} TxnResult_t;

/* ── Snapshot of captured error state ────────────────────────────── */
typedef struct {
    bool        valid;     /* true if an error has been captured */
    uint8_t     ttype;     /* transaction type: 1=read, 2=write/AMO, 3=exec */
    uint8_t     etype;     /* error type code */
    uint64_t    reqAddr;   /* address that caused the fault */
    uint16_t    rrid;      /* RRID that caused the fault */
    uint32_t    entryIdx;  /* entry index involved (0 when not applicable) */
} IopmpErrInfo_t;

/* ── Read-only mirror of hardware capability fields ───────────────── */
typedef struct {
    uint16_t    rridNum;   /* number of RRIDs this instance supports */
    uint16_t    entryNum;  /* number of protection entries */
    uint8_t     mdNum;     /* number of memory domains */
    bool        torEn;     /* TOR address mode is available */
    bool        addrhEn;   /* 64-bit addresses are available */
    bool        noErrRec;  /* error-capture registers are absent */
    bool        stallEn;   /* MDSTALL mechanism is present */
    bool        hwcfg2En;  /* HWCFG2 register is present */
    bool        hwcfg3En;  /* HWCFG3 register is present */
} IopmpHwCfg_t;

/* ── Runtime configuration supplied to IopmpInit ─────────────────── */
typedef struct {
    uint16_t        rridNum;      /* 1..65535 */
    uint16_t        entryNum;     /* 1..65535 */
    uint8_t         mdNum;        /* 1..64 */
    bool            torEn;
    bool            addrhEn;
    bool            noErrRec;
    bool            stallEn;
    bool            hwcfg2En;
    bool            hwcfg3En;
    bool            noW;          /* global write disable (all writes -> NO_RULE) */
    bool            noX;          /* global execute disable (all fetches -> NO_RULE) */
    bool            multifaultEn; /* multi-fault record (ERR_MFR) present */
    bool            peisEn;       /* per-entry interrupt suppression (ENTRY_CFG[6]) */
    bool            peesEn;       /* per-entry bus-error suppression (ENTRY_CFG[7]) */
    bool            xinr;         /* treat execute fetches as data reads */
    uint8_t         mdcfgFmt;     /* 0=standard, 1=fixed equal, 2=programmable fixed */
    uint8_t         srcmdFmt;     /* 0=baseline bitmap, 1=compact RRID==MD, 2=MD-indexed PERM */
    uint16_t        mdEntryNum;   /* entries per MD when mdcfgFmt=2; ignored otherwise */

    /* ── Non-priority entries (spec §5.3) ── */
    bool            nonPrioEn;    /* HWCFG2.non_prio_en: non-priority entries supported */
    bool            prioEntProg;  /* HWCFG2.prio_ent_prog: prio_entry boundary is programmable */
    uint16_t        prioEntry;    /* HWCFG2.prio_entry: entries with index < prioEntry are priority,
                                     index >= prioEntry are non-priority. Reset value of the boundary. */

    bool            msiEn;        /* MSI interrupt delivery (write to msi addr instead of wired IRQ) */
    bool            msiInjectWriteErr; /* test hook: when set, an MSI write "fails" (sets ERR_INFO.msi_werr) */
    bool            spsEn;        /* secondary permission setting (per-RRID per-MD r/w/x filter) */
    bool            rridTranslEn;   /* RRID translation supported */
    bool            rridTranslProg; /* RRID translation table is SW-programmable; false = hardware-fixed */

    /* ── Implementation-defined behaviours, configurable (spec §3.5, §4, App. A) ── */
    uint8_t         mdcfglckResetF;   /* prelocked MDCFGLCK.f reset value (0 = none) */
    uint16_t        entrylckResetF;   /* prelocked ENTRYLCK.f reset value (0 = none) */
    bool            entrylckHardwired;/* ENTRYLCK.l wired to 1 at reset (entries 0..f permanently locked) */
    bool            mdlckDisable;     /* when true, MDLCK not implemented: MDLCK.l wired 1, .md wired 0 */
    bool            eidDisable;       /* when true, ERR_REQID.eid not implemented: eid reads 0xffff */
    uint32_t        mdlckPreset;      /* prelocked MDLCK.md bits (MDs 0-30, bit n+1) */
    uint32_t        mdlckhPreset;     /* prelocked MDLCKH.mdh bits (MDs 31-62) */
    uint32_t        entryAddrhMask;   /* WARL mask for ENTRY_ADDRH writes; 0 = all bits writable */
    bool            entryPermWImpliesR; /* WARL constraint: writing w=1 forces r=1 */
    bool            entryUserCfgEn;   /* ENTRY_USER_CFG(i) registers present (storage + hook) */
    const bool     *rridIllegalVec;   /* optional [rridNum] vector: RRIDs deemed illegal (etype 0x06); NULL = none */
    const bool     *rridBypassVec;    /* optional [rridNum] vector: source-enforced RRIDs not checked; NULL = none */

    IopmpModel_t    model;
    uint32_t        entryOffset;  /* 0 means use IOPMP_DEFAULT_ENTRY_OFFSET */
} IopmpParams_t;

/* ── Interrupt callback type ──────────────────────────────────────── */
/* Forward-declare the struct so the callback can receive a pointer to it. */
typedef struct IopmpState IopmpState_t;
typedef void (*IopmpIrqCb_t)(IopmpState_t *iopmp, void *userData);

/* ── Main IOPMP instance - one per physical IOPMP in the system ───── */
struct IopmpState {
    IopmpParams_t    params;       /* immutable after IopmpInit() returns */
    IopmpHwCfg_t     hwCfg;        /* convenience mirror of HWCFG register fields */

    /*
     * Fixed MMIO register file. Word-addressed: regs[byteOffset/4].
     * Covers VERSION through the last ERR_USER register.
     */
    uint32_t        *regs;

    /* Dynamic tables - sizes come from params at init time. */
    uint32_t        *mdcfg;        /* [mdNum]    MDCFG(m).t fields */

    /*
     * SRCMD format 0 and 1 tables (indexed by RRID):
     *   srcmdEn[s]  - SRCMD_EN(s): lock bit + MD bitmap bits 0-30
     *   srcmdEnh[s] - SRCMD_ENH(s): MD bitmap bits 31-62; NULL when mdNum <= 31
     * NULL for both when srcmdFmt == 2.
     */
    uint32_t        *srcmdEn;      /* [rridNum]  SRCMD_EN(s); NULL when srcmdFmt==2 */
    uint32_t        *srcmdEnh;     /* [rridNum]  SRCMD_ENH(s); NULL when mdNum<=31 or srcmdFmt==2 */

    /*
     * SRCMD format 2 tables (indexed by MD):
     *   srcmdPerm[m]  - SRCMD_PERM(m): lock bit + RRID bitmap for RRIDs 0-30
     *   srcmdPermh[m] - SRCMD_PERMH(m): RRID bitmap for RRIDs 31-62; NULL when rridNum <= 31
     * NULL for both when srcmdFmt != 2.
     */
    uint32_t        *srcmdPerm;    /* [mdNum]    SRCMD_PERM(m); NULL when srcmdFmt!=2 */
    uint32_t        *srcmdPermh;   /* [mdNum]    SRCMD_PERMH(m); NULL when rridNum<=31 or srcmdFmt!=2 */

    uint32_t        *entryAddr;    /* [entryNum] ENTRY_ADDR(i) */
    uint32_t        *entryAddrh;   /* [entryNum] ENTRY_ADDRH(i); NULL when !addrhEn */
    uint32_t        *entryCfg;     /* [entryNum] ENTRY_CFG(i) */
    uint32_t        *entryUserCfg; /* [entryNum] ENTRY_USER_CFG(i); NULL when absent */

    /* Interrupt and error state */
    bool             irqPending;
    bool             msiPending;   /* set when an MSI write would be issued; only when msiEn */
    IopmpIrqCb_t     irqCb;
    void            *irqCbUser;

    /*
     * Per-RRID stall flags - only allocated when params.stallEn is true.
     * NULL otherwise. Used by the stall mechanism.
     */
    bool            *rridStalled;  /* [rridNum] */

    /*
     * SPS secondary-permission tables - only allocated when params.spsEn is true.
     * Each array has rridNum entries. Bit N in srcmdR[s] = "RRID s may read MD N."
     * Supports up to 32 MDs (one 32-bit word per RRID).
     */
    uint32_t        *srcmdR;       /* [rridNum] secondary R perm, MDs 0-30 in bits 31:1; NULL when !spsEn */
    uint32_t        *srcmdW;       /* [rridNum] secondary W perm, MDs 0-30; NULL when !spsEn */
    uint32_t        *srcmdX;       /* [rridNum] secondary X perm, MDs 0-30; NULL when !spsEn */
    uint32_t        *srcmdRh;      /* [rridNum] secondary R perm, MDs 31-62; NULL when !spsEn or mdNum<=31 */
    uint32_t        *srcmdWh;      /* [rridNum] secondary W perm, MDs 31-62; NULL when !spsEn or mdNum<=31 */
    uint32_t        *srcmdXh;      /* [rridNum] secondary X perm, MDs 31-62; NULL when !spsEn or mdNum<=31 */

    /*
     * Multi-Fault Record subsequent-violation bitmap (spec §5.5).
     * One bit per RRID: svWords[s/32] bit (s%32) = "RRID s issued >=1 subsequent
     * violation while ERR_INFO.v was already set". NULL when !multifaultEn.
     */
    uint32_t        *svWords;      /* [(rridNum+31)/32] */

    /*
     * RRID translation table - only allocated when params.rridTranslEn is true.
     * rridTransl[s] holds the destination RRID (bits 15:0) for incoming RRID s.
     * When rridTranslProg is false the table is hardware-fixed and read-only to SW.
     */
    uint32_t        *rridTransl;   /* [rridNum]; NULL when !rridTranslEn */
};

#endif /* IOPMP_TYPES_H */
