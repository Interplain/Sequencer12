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
#define MCP_PROBE_TEST 0
#define MCP_DEBUG_RUNTIME 0
#define DAC_PROBE_TEST 0
#define DAC_BITBANG_TEST 0
#define DEBUG_FORMAT_FRAM 0  // Set to 1 to erase entire FRAM on boot (disabled after format)
#define CALIBRATION_STAGE_TEST 0
#define UI_LOOP_MIN_MS 1

#ifndef MCP_MINIMAL_DEBUG
#define MCP_MINIMAL_DEBUG 0
#endif

static uint8_t I2C_FindFirstDevice7(I2C_HandleTypeDef *hi2c)
{
    if (hi2c == NULL)
    {
        return 0xFFu;
    }

    for (uint8_t a7 = 0x08u; a7 <= 0x77u; a7++)
    {
        if (HAL_I2C_IsDeviceReady(hi2c, (uint16_t)(a7 << 1), 2, 8) == HAL_OK)
        {
            return a7;
        }
    }

    return 0xFFu;
}

static uint8_t I2C_CountDevices(I2C_HandleTypeDef *hi2c)
{
    uint8_t count = 0u;

    if (hi2c == NULL)
    {
        return 0u;
    }

    for (uint8_t a7 = 0x08u; a7 <= 0x77u; a7++)
    {
        if (HAL_I2C_IsDeviceReady(hi2c, (uint16_t)(a7 << 1), 1, 4) == HAL_OK)
        {
            if (count < 255u)
            {
                count++;
            }
        }
    }

    return count;
}

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

#if DEBUG_FORMAT_FRAM && !MCP_MINIMAL_DEBUG
    /* Erase entire FRAM and wait (takes ~30 seconds). Display still in reset. */
    MB85RC256_Format();
    HAL_Delay(2000);
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

    /* ── Release RST and initialise display as late as possible ─────────── */
    /* A brief low-to-high transition is all the panel needs.
     * The controller was held in reset so there was nothing to display until now. */
    HAL_Delay(20);
    GPIOA->BSRR = (1U << 9);         /* RST HIGH */
    HAL_Delay(120);
    ST7789_Init();

    /* Init MCP23017 after display and other hardware are ready */
    MCP23017_Init(&hi2c1);

#if MCP_MINIMAL_DEBUG
    /* ── GPIO drive test: deassert I2C1, force PB8/PB9 high, check if lines respond ── */
    HAL_I2C_DeInit(&hi2c1);
    {
        GPIO_InitTypeDef _g = {0};
        _g.Pin   = GPIO_PIN_8 | GPIO_PIN_9;
        _g.Mode  = GPIO_MODE_OUTPUT_PP;  /* push-pull: overrides any OD */
        _g.Pull  = GPIO_NOPULL;
        _g.Speed = GPIO_SPEED_FREQ_LOW;
        HAL_GPIO_Init(GPIOB, &_g);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8 | GPIO_PIN_9, GPIO_PIN_SET);
        HAL_Delay(5);
    }
    uint8_t pb8_drive_hi = (uint8_t)HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_8);
    uint8_t pb9_drive_hi = (uint8_t)HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_9);
    /* Reinit I2C1 for further scans */
    {
        __HAL_RCC_I2C1_FORCE_RESET();
        HAL_Delay(2);
        __HAL_RCC_I2C1_RELEASE_RESET();
        GPIO_InitTypeDef _g = {0};
        _g.Pin       = GPIO_PIN_8 | GPIO_PIN_9;
        _g.Mode      = GPIO_MODE_AF_OD;
        _g.Pull      = GPIO_PULLUP;
        _g.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
        _g.Alternate = GPIO_AF4_I2C1;
        HAL_GPIO_Init(GPIOB, &_g);
        HAL_I2C_Init(&hi2c1);
    }

    MCP23017_SetDirections(&hi2c1, 0xFF, 0xFF);
    MCP23017_SetPullups(&hi2c1, 0xFF, 0xFF);

    ST7789_DisplayOn();
    ST7789_Fill_Color(BLACK);

    while (1)
    {
        char line[36];
        uint8_t i2c1_first = I2C_FindFirstDevice7(&hi2c1);
        uint8_t i2c3_first = I2C_FindFirstDevice7(&hi2c3);
        uint8_t i2c1_count = I2C_CountDevices(&hi2c1);
        uint8_t i2c3_count = I2C_CountDevices(&hi2c3);
        uint8_t pb8_scl = (uint8_t)HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_8);
        uint8_t pb9_sda = (uint8_t)HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_9);
        uint8_t pa8_scl = (uint8_t)HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_8);
        uint8_t pc9_sda = (uint8_t)HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_9);
        uint8_t iodira = MCP23017_ReadReg(&hi2c1, MCP_IODIRA);
        uint8_t iodirb = MCP23017_ReadReg(&hi2c1, MCP_IODIRB);
        uint8_t gppua = MCP23017_ReadReg(&hi2c1, MCP_GPPUA);
        uint8_t gppub = MCP23017_ReadReg(&hi2c1, MCP_GPPUB);
        uint8_t gpioa = MCP23017_ReadReg(&hi2c1, MCP_GPIOA);
        uint8_t gpiob = MCP23017_ReadReg(&hi2c1, MCP_GPIOB);

        ST7789_FillRect(0, 0, 240, 240, BLACK);
        ST7789_DrawStringScaled(8, 8, "MCP BUS DEBUG", &Font16x24, 1, CYAN, BLACK);

        /* Drive test — most important result */
        snprintf(line, sizeof(line), "PB8 DRV:%u PB9 DRV:%u", pb8_drive_hi, pb9_drive_hi);
        uint16_t drv_color = (pb8_drive_hi && pb9_drive_hi) ? GREEN : RED;
        ST7789_DrawStringScaled(8, 38, line, &Font16x24, 1, drv_color, BLACK);
        ST7789_DrawStringScaled(8, 66,
            (pb8_drive_hi && pb9_drive_hi) ? "BUS OK, CHECK MCP" : "HARD SHORT TO GND",
            &Font16x24, 1, drv_color, BLACK);

        snprintf(line, sizeof(line), "I2C1 F:%02X C:%02u", i2c1_first, i2c1_count);
        ST7789_DrawStringScaled(8, 100, line, &Font16x24, 1, i2c1_count ? GREEN : RED, BLACK);

        snprintf(line, sizeof(line), "I2C3 F:%02X C:%02u", i2c3_first, i2c3_count);
        ST7789_DrawStringScaled(8, 128, line, &Font16x24, 1, i2c3_count ? GREEN : YELLOW, BLACK);

        snprintf(line, sizeof(line), "PB8:%u PB9:%u (live)", pb8_scl, pb9_sda);
        ST7789_DrawStringScaled(8, 156, line, &Font16x24, 1, YELLOW, BLACK);

        snprintf(line, sizeof(line), "PA8:%u PC9:%u (I2C3)", pa8_scl, pc9_sda);
        ST7789_DrawStringScaled(8, 184, line, &Font16x24, 1, YELLOW, BLACK);

        snprintf(line, sizeof(line), "ADDR:20 OK:%u REG:%02X", (i2c1_first == 0x20) ? 1u : 0u, iodira);
        ST7789_DrawStringScaled(8, 212, line, &Font16x24, 1, (i2c1_first == 0x20) ? GREEN : RED, BLACK);

        (void)gppua;
        (void)gppub;
        (void)gpioa;
        (void)gpiob;

        HAL_Delay(150);
    }
#endif

    UI_Input_Init();

#if DAC_BITBANG_TEST
    {
        GPIO_InitTypeDef g = {0};

        g.Pin   = GPIO_PIN_13 | GPIO_PIN_15;
        g.Mode  = GPIO_MODE_OUTPUT_PP;
        g.Pull  = GPIO_NOPULL;
        g.Speed = GPIO_SPEED_FREQ_LOW;
        HAL_GPIO_Init(GPIOB, &g);

        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_RESET); /* LDAC low */
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_SET);   /* SYNC idle */
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_15, GPIO_PIN_RESET); /* CLK idle */

        ST7789_DisplayOn();
        ST7789_Fill_Color(BLACK);
        ST7789_DrawStringScaled(10, 10, "CROSSED WIRING TEST", &Font16x24, 1, CYAN, BLACK);

        while (1)
        {
            /* Phase A */
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_13, GPIO_PIN_SET);   /* PB13 data high */
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_15, GPIO_PIN_RESET); /* PB15 clock low */
            ST7789_FillRect(10, 60, 220, 30, BLACK);
            ST7789_DrawStringScaled(10, 60, "PB13=1  PB15=0", &Font16x24, 1, YELLOW, BLACK);
            HAL_Delay(15000);

            /* Phase B */
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_13, GPIO_PIN_RESET);
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_15, GPIO_PIN_SET);
            ST7789_FillRect(10, 60, 220, 30, BLACK);
            ST7789_DrawStringScaled(10, 60, "PB13=0  PB15=1", &Font16x24, 1, CYAN, BLACK);
            HAL_Delay(15000);
        }
    }
#endif

#if DAC_PROBE_TEST
    {
        static const uint16_t k_test_codes[] = {0u, 16384u, 32768u, 49152u, 65535u};
        char line[40];

        ST7789_DisplayOn();

        while (1)
        {
            for (uint32_t i = 0; i < (uint32_t)(sizeof(k_test_codes) / sizeof(k_test_codes[0])); ++i)
            {
                const uint16_t code = k_test_codes[i];

                DAC8564_SetAllRaw(code, code, code, code);

                ST7789_Fill_Color(BLACK);
                ST7789_DrawStringScaled(10, 20, "DAC PROBE", &Font16x24, 1, CYAN, BLACK);

                snprintf(line, sizeof(line), "CODE: %5u", (unsigned)code);
                ST7789_DrawStringScaled(10, 62, line, &Font16x24, 1, YELLOW, BLACK);

                snprintf(line, sizeof(line), "SPI: %ld", (long)DAC8564_GetLastSpiStatus());
                ST7789_DrawStringScaled(10, 94, line, &Font16x24, 1, WHITE, BLACK);

                snprintf(line, sizeof(line), "W: %lu", (unsigned long)DAC8564_GetWriteCount());
                ST7789_DrawStringScaled(10, 126, line, &Font16x24, 1, GREEN, BLACK);

                snprintf(line, sizeof(line), "TX0: %02X", (unsigned)DAC8564_GetLastTx0());
                ST7789_DrawStringScaled(10, 158, line, &Font16x24, 1, LIGHTBLUE, BLACK);

                HAL_Delay(1200);
            }
        }
    }
#endif

#if MCP_PROBE_TEST
    {
        uint8_t found = MCP23017_Probe(&hi2c1);
        char msg[24];

        ST7789_DisplayOn();
        ST7789_Fill_Color(BLACK);

        if (found == 0xFFu)
        {
            ST7789_DrawStringScaled(10, 60, "MCP: NO ACK", &Font16x24, 1, RED, BLACK);
            ST7789_DrawStringScaled(10, 100, "CHECK BUS", &Font16x24, 1, WHITE, BLACK);
        }
        else
        {
            snprintf(msg, sizeof(msg), "MCP AT 0x%02X", found);
            ST7789_DrawStringScaled(10, 60, msg, &Font16x24, 1, GREEN, BLACK);
        }
        while (1) {}
    }
#endif

#if DISPLAY_BRINGUP_TEST
    ST7789_DisplayOn();
    ST7789_Fill_Color(BLACK);
    ST7789_DrawStringScaled(12, 40, "BPM", &Font16x24, 2, WHITE, BLACK);
    ST7789_DrawStringScaled(12, 110, "123", &Font16x24, 2, GREEN, BLACK);
    ST7789_DrawStringScaled(12, 180, "RGB", &Font16x24, 2, RED, BLACK);
    while (1) {}
#endif

#if !MCP_MINIMAL_DEBUG
    if (enter_cal)
    {
        ST7789_DisplayOn();
        ST7789_Fill_Color(BLACK);
        ST7789_DrawStringScaled(10, 48, "DAC CAL", &Font16x24, 1, CYAN, BLACK);
        ST7789_DrawStringScaled(10, 86, "STARTING", &Font16x24, 1, WHITE, BLACK);
        ST7789_DrawStringScaled(10, 116, "CALIBRATION", &Font16x24, 1, WHITE, BLACK);

    #if CALIBRATION_STAGE_TEST
        Calibration_RunBringupStages();
    #else
        Calibration_RunWizard();
    #endif
    }
    else
    {
        /* Already applied above for earliest possible CV clamping. */
    }

    /* Re-apply zero codes (wizard may have updated calibration). */
    Bridge_ApplyZeroOutputCodes();

    /* UI_Sequencer_Init performs the first full UI draw. */
    UI_Sequencer_Init();

#if MCP_DEBUG_RUNTIME
    {
        char line[32];
        ST7789_Fill_Color(BLACK);

        while (1)
        {
            UI_Input_Poll();

            uint8_t online, read_ok, gpio_a, gpio_b, addr7, scan_mask;
            UI_Input_GetMcpDebug(&online, &read_ok, &gpio_a, &gpio_b, &addr7, &scan_mask);
            uint8_t ok_streak = UI_Input_GetMcpOkStreak();
            uint8_t fail_streak = UI_Input_GetMcpFailStreak();

            uint8_t iodira = MCP23017_ReadReg(&hi2c1, 0x00);

            ST7789_FillRect(0, 0, 240, 240, BLACK);
            ST7789_DrawStringScaled(10, 10, "MCP DEBUG", &Font16x24, 1, CYAN, BLACK);

            snprintf(line, sizeof(line), "ADDR: 0x%02X", addr7);
            ST7789_DrawStringScaled(10, 40, line, &Font16x24, 1, WHITE, BLACK);

            snprintf(line, sizeof(line), "ONLINE: %d", online);
            ST7789_DrawStringScaled(10, 70, line, &Font16x24, 1, online ? GREEN : RED, BLACK);

            snprintf(line, sizeof(line), "OK:%d FAIL:%d", ok_streak, fail_streak);
            ST7789_DrawStringScaled(10, 100, line, &Font16x24, 1, YELLOW, BLACK);

            snprintf(line, sizeof(line), "GPIO_A: 0x%02X", gpio_a);
            ST7789_DrawStringScaled(10, 130, line, &Font16x24, 1, YELLOW, BLACK);

            snprintf(line, sizeof(line), "GPIO_B: 0x%02X", gpio_b);
            ST7789_DrawStringScaled(10, 160, line, &Font16x24, 1, YELLOW, BLACK);

            snprintf(line, sizeof(line), "IODIRA: 0x%02X", iodira);
            ST7789_DrawStringScaled(10, 190, line, &Font16x24, 1, LIGHTBLUE, BLACK);

            HAL_Delay(100);
        }
    }
#endif  /* MCP_DEBUG_RUNTIME */

    while (1)
    {
        /* Keep display reset line asserted high to avoid transient blanking. */
        GPIOA->BSRR = (1U << 9);

        uint32_t t0 = HAL_GetTick();
        UI_Input_Poll();
        UI_Sequencer_Update();
        uint32_t elapsed = HAL_GetTick() - t0;
        if (elapsed < UI_LOOP_MIN_MS)
            HAL_Delay(UI_LOOP_MIN_MS - elapsed);
    }
#endif  /* !MCP_MINIMAL_DEBUG */
}
