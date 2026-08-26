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

#define UI_LOOP_MIN_MS 1

#ifndef MCP_MINIMAL_DEBUG
#define MCP_MINIMAL_DEBUG 0
#endif

int main(void)
{
    HW_Init();

    /* Assert display RST low immediately and hold it throughout all early init.
     * The panel controller stays in hardware reset — its backlight may still
     * power up from VCC, but no image is visible because GRAM is not driven.
     * RST is released only just before ST7789_Init() below. */
    GPIOA->BSRR = (1U << (9 + 16));  /* RST LOW */

#if !MCP_MINIMAL_DEBUG
    /* Latch calibration-entry hold as early as possible in boot. */
    uint8_t enter_cal = Calibration_ShouldEnterOnBoot();
#endif

    /* Init FRAM on dedicated I2C3 bus (PA8=SCL / PC9=SDA) */
#if !MCP_MINIMAL_DEBUG
    MB85RC256_Init(&hi2c3);
#endif

    /* Init DAC8564 */
#if !MCP_MINIMAL_DEBUG
    DAC8564_Init(&hspi2);

    /* Apply saved calibration and clamp CVs to true 0V immediately. */
    Calibration_ApplySaved();
    Bridge_Init();
    Bridge_UserChord_Init();
    Bridge_ApplyZeroOutputCodes();
#endif

    /* Release RST and initialise display as late as possible */
    /* A brief low-to-high transition is all the panel needs.
     * The controller was held in reset so there was nothing to display until now. */
    HAL_Delay(20);
    GPIOA->BSRR = (1U << 9);         /* RST HIGH */
    HAL_Delay(120);
    ST7789_Init();

    /* Init MCP23017 after display and other hardware are ready */
    MCP23017_Init(&hi2c1);

    UI_Input_Init();

#if !MCP_MINIMAL_DEBUG
    if (enter_cal)
    {
        ST7789_DisplayOn();
        ST7789_Fill_Color(BLACK);
        ST7789_DrawStringScaled(10, 48, "DAC CAL", &Font16x24, 1, CYAN, BLACK);
        ST7789_DrawStringScaled(10, 86, "STARTING", &Font16x24, 1, WHITE, BLACK);
        ST7789_DrawStringScaled(10, 116, "CALIBRATION", &Font16x24, 1, WHITE, BLACK);

        Calibration_RunWizard();
    }

    /* Re-apply zero codes (wizard may have updated calibration). */
    Bridge_ApplyZeroOutputCodes();

    /* UI_Sequencer_Init performs the first full UI draw. */
    UI_Sequencer_Init();
#endif

    return 0;
}