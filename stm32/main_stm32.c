#include "stm32f4xx_hal.h"
#include "st7789.h"
#include "hw_init.h"
#include "mcp23017.h"
#include "ui/ui_input.h"
#include "fonts_extra.h"
#include <stdio.h>
#include <string.h>

#define SCREEN_TITLE_Y 8
#define SCREEN_LINE1_Y  42
#define SCREEN_LINE2_Y  72
#define SCREEN_LINE3_Y  102
#define SCREEN_LINE4_Y  132
#define SCREEN_LINE5_Y  162
#define SCREEN_LINE6_Y  192

static void DrawLabel(uint16_t x, uint16_t y, const char* label)
{
    ST7789_DrawString(x, y, label, &Font12x20, WHITE, RGB565(18, 24, 36));
}

static void DrawValue(uint16_t x, uint16_t y, const char* value, uint16_t fg)
{
    ST7789_FillRect(x, y, 220, 22, RGB565(18, 24, 36));
    ST7789_DrawString(x, y, value, &Font12x20, fg, RGB565(18, 24, 36));
}

static void DrawBootScreen(void)
{
    ST7789_Fill_Color(RGB565(18, 24, 36));
    ST7789_DrawString(10, SCREEN_TITLE_Y, "INPUT DIAGNOSTIC", &Font16x24, CYAN, RGB565(18, 24, 36));
    ST7789_DrawString(10, 28, "Buttons and encoder live", &Font10x16, LIGHTBLUE, RGB565(18, 24, 36));

    DrawLabel(10, SCREEN_LINE1_Y, "PLAY:");
    DrawLabel(10, SCREEN_LINE2_Y, "REC:");
    DrawLabel(10, SCREEN_LINE3_Y, "SHIFT:");
    DrawLabel(10, SCREEN_LINE4_Y, "ENC DELTA:");
    DrawLabel(10, SCREEN_LINE5_Y, "ENC PRESS:");
    DrawLabel(10, SCREEN_LINE6_Y, "STEP/SHIFT:");
}

int main(void)
{
    HW_Init();
    MCP23017_Init(&hi2c1);
    UI_Input_Init();

    GPIOA->BSRR = (1U << (9 + 16));
    HAL_Delay(20);
    GPIOA->BSRR = (1U << 9);
    HAL_Delay(120);

    ST7789_Init();
    ST7789_DisplayOn();
    DrawBootScreen();

    uint8_t prev_play = 0xFFu;
    uint8_t prev_rec = 0xFFu;
    uint8_t prev_shift = 0xFFu;
    int8_t prev_delta = 0x7Fu;
    uint8_t prev_enc_press = 0xFFu;
    uint8_t prev_step = 0xFFu;
    uint8_t prev_shift_step = 0xFFu;

    while (1)
    {
        GPIOA->BSRR = (1U << 9);
        UI_Input_Poll();

        uint8_t play = UI_Input_IsPlayPressed();
        uint8_t rec = UI_Input_IsRecPressed();
        uint8_t shift = UI_Input_IsShiftHeld();
        int8_t delta = UI_Input_GetEncoderDelta();
        uint8_t enc_press = UI_Input_IsEncoderPressed();
        uint8_t step = UI_Input_GetStepPressed();
        uint8_t shift_step = UI_Input_GetShiftStepPressed();

        char line[32];

        if (play != prev_play)
        {
            snprintf(line, sizeof(line), "%s", play ? "PRESSED" : "idle");
            DrawValue(130, SCREEN_LINE1_Y, line, play ? GREEN : WHITE);
            prev_play = play;
        }

        if (rec != prev_rec)
        {
            snprintf(line, sizeof(line), "%s", rec ? "PRESSED" : "idle");
            DrawValue(130, SCREEN_LINE2_Y, line, rec ? RED : WHITE);
            prev_rec = rec;
        }

        if (shift != prev_shift)
        {
            snprintf(line, sizeof(line), "%s", shift ? "HELD" : "idle");
            DrawValue(130, SCREEN_LINE3_Y, line, shift ? YELLOW : WHITE);
            prev_shift = shift;
        }

        if (delta != prev_delta)
        {
            snprintf(line, sizeof(line), "%+d", delta);
            DrawValue(130, SCREEN_LINE4_Y, line, CYAN);
            prev_delta = delta;
        }

        if (enc_press != prev_enc_press)
        {
            snprintf(line, sizeof(line), "%s", enc_press ? "PRESSED" : "idle");
            DrawValue(130, SCREEN_LINE5_Y, line, enc_press ? LIGHTGREEN : WHITE);
            prev_enc_press = enc_press;
        }

        if (step != prev_step || shift_step != prev_shift_step)
        {
            if (step != 0u)
            {
                snprintf(line, sizeof(line), "step %u", step);
                DrawValue(130, SCREEN_LINE6_Y, line, GREEN);
            }
            else if (shift_step != 0u)
            {
                snprintf(line, sizeof(line), "shift %u", shift_step);
                DrawValue(130, SCREEN_LINE6_Y, line, ORANGE);
            }
            else
            {
                DrawValue(130, SCREEN_LINE6_Y, "idle", WHITE);
            }

            prev_step = step;
            prev_shift_step = shift_step;
        }

        HAL_Delay(10);
    }
}
