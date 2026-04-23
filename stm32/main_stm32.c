#include "stm32f4xx_hal.h"
#include "st7789.h"
#include "hw_init.h"
#include "mcp23017.h"
#include "platform/dac8564/dac8564.h"
#include "platform/fram/mb85rc256.h"
#include "calibration.h"
#include "sequencer_bridge.h"
#include "user_chord_bridge.h"
#include "ui/ui_sequencer.h"
#include "ui/ui_input.h"

#define DISPLAY_BRINGUP_TEST 0

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

    /* Init DAC8564 */
    DAC8564_Init(&hspi2);

    /* Encoder-held at power-up enters CV calibration wizard */
    if (Calibration_ShouldEnterOnBoot())
    {
        Calibration_RunWizard();
    }

    /* Apply saved calibration (if present) */
    Calibration_ApplySaved();

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
