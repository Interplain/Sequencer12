#ifndef FRAM_NEWCHIP_TEST_H
#define FRAM_NEWCHIP_TEST_H

/* Temporary, removable, read-only MB85RC1MT device test.
 * Only compiled when S12_FRAM_NEWCHIP_TEST_MODE is defined. Issues only
 * MB85RC1MT_Read calls — never writes/erases/formats the new chip. */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FRAM_NEWCHIP_TEST_SAMPLE_LEN 32u

extern uint8_t g_newchip_bank0_sample[FRAM_NEWCHIP_TEST_SAMPLE_LEN]; /* read from address 0x00000 */
extern uint8_t g_newchip_bank1_sample[FRAM_NEWCHIP_TEST_SAMPLE_LEN]; /* read from address 0x10000 */

extern volatile uint8_t g_newchip_bank0_read_ok; /* 1 if the 0x50 (bank 0) read succeeded */
extern volatile uint8_t g_newchip_bank1_read_ok; /* 1 if the 0x51 (bank 1) read succeeded */
extern volatile uint8_t g_newchip_test_done;

/* Reads FRAM_NEWCHIP_TEST_SAMPLE_LEN bytes from address 0x00000 (bank 0,
 * slave 0x50) and from 0x10000 (bank 1, slave 0x51). Read-only. */
void FramNewchipTest_Run(void);

#ifdef __cplusplus
}
#endif

#endif
