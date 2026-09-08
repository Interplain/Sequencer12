#ifndef PATTERN_STORE_TEST_H
#define PATTERN_STORE_TEST_H

/* Temporary, removable Stage 1 diagnostic: writes a known Pattern into ONE
 * upper-FRAM pattern-store slot, reads it back, and verifies header/CRC and
 * a full byte-for-byte match. Only compiled under S12_PATTERNSTORE_TEST_MODE.
 * Touches only its one test slot in the new 0x8000+ region -- never the
 * legacy 0x0000-0x7FFF image. */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PATTERNSTORE_TEST_SLOT_INDEX 12u /* straddles the MB85RC1MT 0x10000 bank boundary */

extern volatile uint8_t g_patternstore_test_write_ok;
extern volatile uint8_t g_patternstore_test_valid_ok;
extern volatile uint8_t g_patternstore_test_read_ok;
extern volatile uint8_t g_patternstore_test_memcmp_ok;
extern volatile uint8_t g_patternstore_test_done;

void PatternStoreTest_Run(void);

#ifdef __cplusplus
}
#endif

#endif
