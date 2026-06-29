# The IOPMP reference model - a single static library with the complete
# implementation (registers, lookup, errors, locks, stall, and extensions).
add_library(iopmp STATIC
    src/iopmp_init.c
    src/iopmp_reg.c
    src/iopmp_srcmd.c
    src/iopmp_mdcfg.c
    src/iopmp_entry.c
    src/iopmp_translate.c
    src/iopmp_error.c
    src/iopmp_lock.c
    src/iopmp_stall.c
)
target_include_directories(iopmp PUBLIC include)
