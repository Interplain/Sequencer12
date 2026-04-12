#include "stm32f4xx_hal.h"
#include "st7789.h"
#include "hw_init.h"
#include "mcp23017.h"
#include "sequencer_bridge.h"
#include "ui/ui_sequencer.h"
#include "ui/ui_input.h"

int main(void)
{
    HW_Init();

    /* Display reset */
    GPIOA->BSRR = (1U << (8 + 16));
    HAL_Delay(20);
    GPIOA->BSRR = (1U << 8);
    HAL_Delay(120);

    /* Init display */
    ST7789_Init();

    /* Init MCP23017 */
    MCP23017_Init(&hi2c1);

    /* Init engine */
    Bridge_Init();

    /* Init UI */
    UI_Input_Init();
    UI_Sequencer_Init();

    /* Backlight ON */
    GPIOB->BSRR = (1U << 0);

    while (1)
    {
        UI_Input_Poll();
        UI_Sequencer_Update();
        HAL_Delay(10);
    }
}