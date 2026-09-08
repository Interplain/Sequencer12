#include "platform/fram/fram_newchip_test.h"
#include "platform/fram/mb85rc1mt.h"
#include <string.h>

uint8_t g_newchip_bank0_sample[FRAM_NEWCHIP_TEST_SAMPLE_LEN];
uint8_t g_newchip_bank1_sample[FRAM_NEWCHIP_TEST_SAMPLE_LEN];
volatile uint8_t g_newchip_bank0_read_ok = 0u;
volatile uint8_t g_newchip_bank1_read_ok = 0u;
volatile uint8_t g_newchip_test_done = 0u;

void FramNewchipTest_Run(void)
{
    g_newchip_test_done = 0u;
    memset(g_newchip_bank0_sample, 0, sizeof(g_newchip_bank0_sample));
    memset(g_newchip_bank1_sample, 0, sizeof(g_newchip_bank1_sample));

    /* Bank 0 probe: address 0x00000 -> slave 0x50. */
    g_newchip_bank0_read_ok = MB85RC1MT_Read(0x00000u, g_newchip_bank0_sample, (uint16_t)FRAM_NEWCHIP_TEST_SAMPLE_LEN) ? 1u : 0u;

    /* Bank 1 probe: address 0x10000 -> slave 0x51, exercises the A16 bank-select path. */
    g_newchip_bank1_read_ok = MB85RC1MT_Read(0x10000u, g_newchip_bank1_sample, (uint16_t)FRAM_NEWCHIP_TEST_SAMPLE_LEN) ? 1u : 0u;

    g_newchip_test_done = 1u;
}
