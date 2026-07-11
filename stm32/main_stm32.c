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
#define UI_LOOP_MIN_MS 1

int main(void)
{
    HW_Init();

    /* Init MCP23017 (GPIO expander) before display */
    MCP23017_Init(&hi2c1);

    /* Assert display RST low immediately and hold it throughout all early init.
     * The panel controller stays in hardware reset — its backlight may still
     * power up from VCC, but no image is visible because GRAM is not driven.
     * RST is released only just before ST7789_Init() below. */
    GPIOA->BSRR = (1U << (9 + 16));  /* RST LOW */

    /* Init FRAM on dedicated I2C3 bus (PA8=SCL / PC9=SDA) */
    MB85RC256_Init(&hi2c3);

#if DEBUG_FORMAT_FRAM
    /* Erase entire FRAM and wait (takes ~30 seconds). Display still in reset. */
    MB85RC256_Format();
    HAL_Delay(2000);
#endif

    /* Init DAC8564 */
    DAC8564_Init(&hspi2);

    /* Init engine before display so the first UI frame has real data. */
    Bridge_Init();
    Bridge_UserChord_Init();
    UI_Input_Init();

    /* Check calibration mode while display is still in reset. */
    uint8_t enter_cal = Calibration_ShouldEnterOnBoot();

    /* ── Release RST and initialise display as late as possible ─────────── */
    /* A brief low-to-high transition is all the panel needs.
     * The controller was held in reset so there was nothing to display until now. */
    HAL_Delay(20);
    GPIOA->BSRR = (1U << 9);         /* RST HIGH */
    HAL_Delay(120);

    /* Initialise display and write the first real frame before normal UI use. */
    ST7789_Init();

#if DISPLAY_BRINGUP_TEST
    ST7789_DisplayOn();
    ST7789_Fill_Color(BLACK);
    ST7789_DrawStringScaled(12, 40, "BPM", &Font16x24, 2, WHITE, BLACK);
    ST7789_DrawStringScaled(12, 110, "123", &Font16x24, 2, GREEN, BLACK);
    ST7789_DrawStringScaled(12, 180, "RGB", &Font16x24, 2, RED, BLACK);
    while (1) {}
#endif

    if (enter_cal)
    {
        ST7789_DisplayOn();
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
        Calibration_ApplySaved();
    }

    /* UI_Sequencer_Init performs the first full UI draw. */
    UI_Sequencer_Init();

    while (1)
    {
        /* Keep OLED reset line asserted high to avoid transient blanking. */
        GPIOA->BSRR = (1U << 9);

        uint32_t t0 = HAL_GetTick();
        UI_Input_Poll();
        UI_Sequencer_Update();
        uint32_t elapsed = HAL_GetTick() - t0;
        if (elapsed < UI_LOOP_MIN_MS)
            HAL_Delay(UI_LOOP_MIN_MS - elapsed);
    }
}
