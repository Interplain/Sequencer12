#include "stm32/timing_probe.h"
#include "stm32f4xx_hal.h"

volatile uint32_t g_timing_max_ui_update_us = 0u;
volatile uint32_t g_timing_max_on_draw_us = 0u;
volatile uint32_t g_timing_max_draw_rgb565_us = 0u;
volatile uint32_t g_timing_max_service_persistence_us = 0u;
volatile uint32_t g_timing_max_save_song_us = 0u;
volatile uint32_t g_timing_max_pattern_write_us = 0u;

#if defined(S12_TIMING_PROBE_ENABLE)

void TimingProbe_Init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0u;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

uint32_t TimingProbe_Begin(void)
{
    return DWT->CYCCNT;
}

void TimingProbe_End(volatile uint32_t* max_us_slot, uint32_t start_cycles)
{
    uint32_t elapsed_cycles = DWT->CYCCNT - start_cycles;
    uint32_t us = elapsed_cycles / (SystemCoreClock / 1000000u);
    if (us > *max_us_slot)
    {
        *max_us_slot = us;
    }
}

#endif
