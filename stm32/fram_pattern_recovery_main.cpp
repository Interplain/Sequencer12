#include "stm32f4xx_hal.h"
#include "stm32/hw/hw_init.h"
#include "platform/fram/mb85rc1mt.h"
#include "platform/fram/pattern_store.h"
#include <cstdint>

#define FORENSIC_PATTERN1_ADDRESS 0x08000u
#define FORENSIC_PATTERN1_SIZE (FRAM_PATTERNSTORE_SLOT_STRIDE)
#define FORENSIC_JOURNAL_ADDRESS FRAM_PATTERNSTORE_JOURNAL_ADDR
#define FORENSIC_JOURNAL_SIZE (sizeof(uint32_t) + sizeof(uint16_t) + sizeof(uint16_t) + \
                               sizeof(uint8_t) + 3u + 3u * sizeof(uint32_t) + \
                               sizeof(sequencer::Pattern))

static_assert(FORENSIC_PATTERN1_SIZE == sizeof(uint32_t) + sizeof(uint16_t) +
              sizeof(uint16_t) + 3u * sizeof(uint32_t) + sizeof(sequencer::Pattern),
              "Pattern 1 capture size must match the current V2 slot layout");
static_assert(FORENSIC_JOURNAL_SIZE == 24u + sizeof(sequencer::Pattern),
              "Journal capture size must match current V2 journal layout");

uint8_t g_recovery_pattern1_raw[FORENSIC_PATTERN1_SIZE];
uint8_t g_recovery_journal_raw[FORENSIC_JOURNAL_SIZE];
sequencer::Pattern g_recovery_validated_pattern{};
volatile uint32_t g_recovery_generation = 0u;
volatile uint8_t g_recovery_fram_ready = 0u;
volatile uint8_t g_recovery_result = 0xFFu;
volatile uint8_t g_recovery_pattern_valid = 0u;
volatile uint8_t g_recovery_pattern_raw_read_ok = 0u;
volatile uint8_t g_recovery_journal_raw_read_ok = 0u;
volatile uint8_t g_recovery_done = 0u;

extern "C" void SysTick_Handler(void)
{
    HAL_IncTick();
}

int main(void)
{
    HW_Init();
    MB85RC1MT_Init(&hi2c3);

    g_recovery_fram_ready = MB85RC1MT_IsReady();
    if (g_recovery_fram_ready)
    {
        g_recovery_result = (uint8_t)PatternStore_RecoverJournal();
        uint32_t generation = 0u;
        g_recovery_pattern_valid = PatternStore_ReadWithGeneration(
            0u, &g_recovery_validated_pattern, &generation);
        g_recovery_generation = generation;
        g_recovery_pattern_raw_read_ok = MB85RC1MT_Read(
            FORENSIC_PATTERN1_ADDRESS, g_recovery_pattern1_raw,
            (uint16_t)sizeof(g_recovery_pattern1_raw));
        g_recovery_journal_raw_read_ok = MB85RC1MT_Read(
            FORENSIC_JOURNAL_ADDRESS, g_recovery_journal_raw,
            (uint16_t)sizeof(g_recovery_journal_raw));
    }

    g_recovery_done = 1u;
    while (1) __NOP();
}