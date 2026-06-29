/*
 * iopmp_config.h
 *
 * Compile-time upper limits for the IOPMP reference model.
 * These cap memory allocation regardless of what the runtime params say.
 * Raise them if you need to model very large instances.
 */
#ifndef IOPMP_CONFIG_H
#define IOPMP_CONFIG_H

/* Highest number of requestor role IDs any instance can have. */
#define IOPMP_MAX_RRID_NUM          65535U

/* Highest number of protection entries any instance can have. */
#define IOPMP_MAX_ENTRY_NUM         65535U

/* Highest number of memory domains any instance can have. */
#define IOPMP_MAX_MD_NUM            64U

/* Maximum IOPMP instances that can be registered in one system. */
#define IOPMP_MAX_INSTANCES         16U

/*
 * Default byte offset of the entry array from the IOPMP MMIO base.
 * Software reads the actual value from the ENTRYOFFSET register.
 */
#define IOPMP_DEFAULT_ENTRY_OFFSET  0x4000U

#endif /* IOPMP_CONFIG_H */
