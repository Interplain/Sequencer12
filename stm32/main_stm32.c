#include "stm32f4xx_hal.h"
#include "ui/ui_sequencer.h"
#include "ui/ui_input.h"
#include "st7789.h"
#include "hw_init.h"
#include "mcp23017.h"
#include <stdio.h>

/* ── UI layout ──────────────────────────────────────────────────────────── */
#define SCREEN_W    240
#define SCREEN_H    240
#define HEADER_H     24
#define GRID_X        8
#define GRID_Y       56
#define BOX_W        50
#define BOX_H        56
#define GAP_X         6
#define GAP_Y         8
#define BORDER_T      2

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

    /* Init UI — display must be ready before this */
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

void HAL_SYSTICK_Callback(void)
{
    UI_Sequencer_Tick1ms();
}



