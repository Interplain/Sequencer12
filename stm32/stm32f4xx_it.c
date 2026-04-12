#include "stm32f4xx_hal.h"
#include "ui/ui_sequencer.h"

void SysTick_Handler(void)
{
    HAL_IncTick();
    UI_Sequencer_Tick1ms();
}
