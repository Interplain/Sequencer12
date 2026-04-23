#include "stm32f4xx_hal.h"
#include "ui/ui_sequencer.h"
#include "stm32/hw/hw_init.h"

void SysTick_Handler(void)
{
    HAL_IncTick();
    UI_Sequencer_Tick1ms();
}

void DMA2_Stream3_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_spi1_tx);
}
