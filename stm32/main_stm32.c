#include "stm32f4xx_hal.h"
#include "st7789.h"
#include "hw_init.h"
#include "mcp23017.h"
#include "platform/dac8564/dac8564.h"
#include "platform/fram/mb85rc256.h"
#include "calibration.h"
#include "sequencer_bridge.h"
#include "user_chord_bridge.h"
#include "ui/ui_display.h"
#include "ui/ui_sequencer.h"
#include "ui/ui_input.h"
#include <stdio.h>

#define DISPLAY_BRINGUP_TEST 0
#define DEBUG_FORMAT_FRAM 0  // Set to 1 to erase entire FRAM on boot (disabled after format)

int main(void)
{
    HW_Init();

    /* Init MCP23017 (GPIO expander) before display */
    MCP23017_Init(&hi2c1);

    /* Display reset */
    GPIOA->BSRR = (1U << (9 + 16));
    HAL_Delay(20);
    GPIOA->BSRR = (1U << 9);
    HAL_Delay(120);

    /* Init display */
    ST7789_Init();

    /* Keep reset asserted during early panel settle to prevent transient white screen. */
    for (uint8_t i = 0; i < 12; i++)
    {
        GPIOA->BSRR = (1U << 9);
        HAL_Delay(5);
    }

    ST7789_DisplayOn();
    ST7789_Fill_Color(BLACK);

#if DISPLAY_BRINGUP_TEST
    ST7789_Fill_Color(BLACK);
    HAL_Delay(40);
    ST7789_Fill_Color(BLACK);
    ST7789_DrawStringScaled(12, 40, "BPM", &Font16x24, 2, WHITE, BLACK);
    ST7789_DrawStringScaled(12, 110, "123", &Font16x24, 2, GREEN, BLACK);
    ST7789_DrawStringScaled(12, 180, "RGB", &Font16x24, 2, RED, BLACK);
    while (1)
    {
    }
#endif

    /* Init FRAM on dedicated I2C3 bus (PA8=SCL / PC9=SDA) */
    MB85RC256_Init(&hi2c3);

#if DEBUG_FORMAT_FRAM
    /* Erase entire FRAM and wait (takes ~30 seconds) */
    ST7789_Fill_Color(BLACK);
    ST7789_DrawStringScaled(12, 100, "Formatting", &Font16x24, 2, YELLOW, BLACK);
    ST7789_DrawStringScaled(12, 150, "FRAM...", &Font16x24, 2, YELLOW, BLACK);
    MB85RC256_Format();
    ST7789_Fill_Color(BLACK);
    ST7789_DrawStringScaled(12, 100, "Done!", &Font16x24, 2, GREEN, BLACK);
    HAL_Delay(2000);
#endif

    /* Init DAC8564 */
    DAC8564_Init(&hspi2);

    if (Calibration_ShouldEnterOnBoot())
    {
        GPIOA->BSRR = (1U << 9);

        ST7789_Fill_Color(BLACK);
        ST7789_DrawStringScaled(10, 48, "DAC CAL", &Font16x24, 1, CYAN, BLACK);
        ST7789_DrawStringScaled(10, 86, "RELEASE ENCODER", &Font16x24, 1, WHITE, BLACK);
        ST7789_DrawStringScaled(10, 116, "TO START", &Font16x24, 1, WHITE, BLACK);

        while (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_12) == GPIO_PIN_RESET)
        {
            GPIOA->BSRR = (1U << 9);
            HAL_Delay(5);
        }

        Calibration_RunWizard();
    }
    else
    {
        /* Apply saved calibration (if present) */
        Calibration_ApplySaved();
    }

    /* Init engine */
    Bridge_Init();
    Bridge_UserChord_Init();

    /* Init UI */
    UI_Input_Init();
    UI_Sequencer_Init();

    while (1)
    {
        /* Keep OLED reset line asserted high to avoid transient blanking. */
        GPIOA->BSRR = (1U << 9);

        uint32_t t0 = HAL_GetTick();
        UI_Input_Poll();
        UI_Sequencer_Update();
        uint32_t elapsed = HAL_GetTick() - t0;
        if (elapsed < 10)
            HAL_Delay(10 - elapsed);
    }
}
