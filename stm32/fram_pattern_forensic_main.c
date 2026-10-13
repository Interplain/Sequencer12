#include "stm32f4xx_hal.h"
#include "stm32/hw/hw_init.h"
#include "platform/fram/mb85rc1mt.h"

#define FORENSIC_PATTERN1_ADDRESS 0x08000u
#define FORENSIC_PATTERN1_SIZE    0x00A40u
#define FORENSIC_JOURNAL_ADDRESS  0x1C800u
#define FORENSIC_JOURNAL_SIZE    0x00A44u

_Static_assert(FORENSIC_PATTERN1_SIZE == 20u + 2604u,
               "Pattern 1 capture must contain its 20-byte header and full Pattern payload");
_Static_assert(FORENSIC_JOURNAL_SIZE == 24u + 2604u,
               "journal capture must contain its 24-byte header and full Pattern payload");

uint8_t g_forensic_pattern1_raw[FORENSIC_PATTERN1_SIZE];
uint8_t g_forensic_journal_raw[FORENSIC_JOURNAL_SIZE];
volatile uint8_t g_forensic_fram_ready;
volatile uint8_t g_forensic_pattern1_read_ok;
volatile uint8_t g_forensic_journal_read_ok;
volatile uint8_t g_forensic_done;

void SysTick_Handler(void)
{
    HAL_IncTick();
}

int main(void)
{
    HW_Init();
    MB85RC1MT_Init(&hi2c3);

    g_forensic_fram_ready = MB85RC1MT_IsReady();
    if (g_forensic_fram_ready)
    {
        g_forensic_pattern1_read_ok = MB85RC1MT_Read(
            FORENSIC_PATTERN1_ADDRESS, g_forensic_pattern1_raw,
            (uint16_t)sizeof(g_forensic_pattern1_raw));
        g_forensic_journal_read_ok = MB85RC1MT_Read(
            FORENSIC_JOURNAL_ADDRESS, g_forensic_journal_raw,
            (uint16_t)sizeof(g_forensic_journal_raw));
    }

    g_forensic_done = 1u;
    while (1) __NOP();
}