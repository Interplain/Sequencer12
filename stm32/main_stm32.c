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

int main(void)
{
    HW_Init();

    /* Init I2C peripherals first (MCP23017 + FRAM) before display to avoid conflicts */
    MCP23017_Init(&hi2c1);
    MB85RC256_Init(&hi2c1);

    /* Display reset */
    GPIOA->BSRR = (1U << (8 + 16));
    HAL_Delay(20);
    GPIOA->BSRR = (1U << 8);
    HAL_Delay(120);

    /* Init display */
    ST7789_Init();

    /* Backlight ON early so startup diagnostics are visible */
    GPIOB->BSRR = (1U << 0);

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
        UI_Input_Poll();
        UI_Sequencer_Update();
        HAL_Delay(10);
    }
}
