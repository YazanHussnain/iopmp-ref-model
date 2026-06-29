/*
 * test_utils.h
 *
 * Shared macros for all IOPMP tests.
 * These wrap assert() with descriptive failure messages.
 */
#ifndef TEST_UTILS_H
#define TEST_UTILS_H

#include <assert.h>
#include <stdio.h>
#include "iopmp.h"

/*
 * ASSERT_LEGAL - fail the test if 'result' says the transaction was blocked.
 */
#define ASSERT_LEGAL(result) \
    do { \
        if (!(result).legal) { \
            fprintf(stderr, "[FAIL] %s:%d - expected LEGAL, got etype=0x%02X\n", \
                    __FILE__, __LINE__, (result).etype); \
            assert(0); \
        } \
    } while (0)

/*
 * ASSERT_ILLEGAL - fail the test if 'result' says the transaction was allowed.
 */
#define ASSERT_ILLEGAL(result) \
    do { \
        if ((result).legal) { \
            fprintf(stderr, "[FAIL] %s:%d - expected ILLEGAL, got LEGAL\n", \
                    __FILE__, __LINE__); \
            assert(0); \
        } \
    } while (0)

/*
 * ASSERT_ETYPE - fail the test unless 'result' has the expected error type.
 */
#define ASSERT_ETYPE(result, expectedEtype) \
    do { \
        ASSERT_ILLEGAL(result); \
        if ((result).etype != (uint8_t)(expectedEtype)) { \
            fprintf(stderr, "[FAIL] %s:%d - expected etype=0x%02X, got 0x%02X\n", \
                    __FILE__, __LINE__, (uint8_t)(expectedEtype), (result).etype); \
            assert(0); \
        } \
    } while (0)

/*
 * ASSERT_REG - fail if the register at 'byteOffset' does not equal 'expected'.
 */
#define ASSERT_REG(iopmpPtr, byteOffset, expected) \
    do { \
        uint32_t regVal = IopmpReadReg((iopmpPtr), (byteOffset)); \
        if (regVal != (uint32_t)(expected)) { \
            fprintf(stderr, "[FAIL] %s:%d - reg 0x%04X: expected 0x%08X, got 0x%08X\n", \
                    __FILE__, __LINE__, (byteOffset), (uint32_t)(expected), regVal); \
            assert(0); \
        } \
    } while (0)

/*
 * PASS - print a passing test name.
 */
#define PASS(testName) printf("[PASS] %s\n", (testName))

#endif /* TEST_UTILS_H */
