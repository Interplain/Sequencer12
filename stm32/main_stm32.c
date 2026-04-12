#include "stm32f4xx_hal.h"
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

/* ── Forward declarations ────────────────────────────────────────────────── */
static void DrawRectBorder(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
static void DrawStepBox(uint8_t step, uint8_t selected);
static void DrawHeader(void);
static void PollButtons(void);

/* ── Button state memory ───────────────────────────────────────────── */
static uint16_t g_prev_raw = 0xFFFF;
static uint8_t enc_prev = 1;
static uint16_t PrimeButtons(void)

{
    uint16_t last = 0xFFFF;
    uint16_t now  = 0xFFFF;
    uint8_t stable_count = 0;

    while (stable_count < 5)
    {
        now = MCP23017_ReadGPIO(&hi2c1);

        if (now == last)
        {
            stable_count++;
        }
        else
        {
            stable_count = 0;
            last = now;
        }

        HAL_Delay(2);
    }

    return now;
}


/* ── Button polling PLAY, RECORD, SHIFT────────────────────────────────── */
static void PollButtons(void)
{
    uint16_t raw     = MCP23017_ReadGPIO(&hi2c1);
    uint16_t changed = g_prev_raw ^ raw;
    uint16_t rising  = changed & raw;
    g_prev_raw       = raw;

    if (rising & BTN_PLAY_BIT)
        ST7789_FillRect(0, 0, 240, 4, GREEN);

    if (rising & BTN_REC_BIT)
        ST7789_FillRect(0, 0, 240, 4, RED);

    if (rising & BTN_SHIFT_BIT)
        ST7789_FillRect(0, 0, 240, 4, YELLOW);

    if ((raw & (BTN_PLAY_BIT | BTN_REC_BIT | BTN_SHIFT_BIT)) == 0u)
        ST7789_FillRect(0, 0, 240, 4, BLACK);
}
//*Encoder Button */
static void PollEncoderButton(void)
{
    uint8_t now = HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_12);

    if ((enc_prev == 1u) && (now == 0u))
    {
        /* pressed */
        ST7789_FillRect(0, 0, 240, 4, BLUE);
    }

    if ((enc_prev == 0u) && (now == 1u))
    {
        /* released */
        uint16_t raw = MCP23017_ReadGPIO(&hi2c1);
        if ((raw & (BTN_PLAY_BIT | BTN_REC_BIT | BTN_SHIFT_BIT)) == 0u)
        {
            ST7789_FillRect(0, 0, 240, 4, BLACK);
        }
    }
    enc_prev = now;
}


   int main(void)
{
    HW_Init();

    /* Display reset */
    GPIOA->BSRR = (1U << (8 + 16));
    HAL_Delay(20);
    GPIOA->BSRR = (1U << 8);
    HAL_Delay(50);

    MCP23017_Init(&hi2c1);

    /* Init display */
    ST7789_Init();
    ST7789_Fill_Color(BLACK);
    DrawHeader();
    for (uint8_t i = 1; i <= 12; i++)
        DrawStepBox(i, (i == 1) ? 1 : 0);

    /* Backlight ON */
    GPIOB->BSRR = (1U << 0);

    ST7789_FillRect(0, 236, 240, 4, BLACK);

    /* Encoder state */
    int16_t last_enc       = (int16_t)TIM2->CNT;
    int8_t  selected_block = 1;

    /* Prime button history from stable MCP state */
    g_prev_raw = PrimeButtons();
    /*Encoder Button*/
    enc_prev = HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_12); 

    while (1)
    {
        PollButtons();
        PollEncoderButton();
        
        int16_t enc   = (int16_t)TIM2->CNT;
        int16_t delta = enc - last_enc;

        if (delta >= 2)
        {
            last_enc = enc;
            int8_t prev = selected_block;
            if (++selected_block > 12) selected_block = 1;
            DrawStepBox((uint8_t)prev, 0);
            DrawStepBox((uint8_t)selected_block, 1);
        }
        else if (delta <= -2)
        {
            last_enc = enc;
            int8_t prev = selected_block;
            if (--selected_block < 1) selected_block = 12;
            DrawStepBox((uint8_t)prev, 0);
            DrawStepBox((uint8_t)selected_block, 1);
        }

        HAL_Delay(5);
    }
}


/* ── Drawing ─────────────────────────────────────────────────────────────── */
static void DrawRectBorder(uint16_t x, uint16_t y,
                           uint16_t w, uint16_t h, uint16_t color)
{
    ST7789_FillRect(x, y, w, BORDER_T, color);
    ST7789_FillRect(x, y + h - BORDER_T, w, BORDER_T, color);
    ST7789_FillRect(x, y, BORDER_T, h, color);
    ST7789_FillRect(x + w - BORDER_T, y, BORDER_T, h, color);
}

static void DrawStepBox(uint8_t step, uint8_t selected)
{
    uint8_t  index = step - 1;
    uint8_t  col   = index % 4;
    uint8_t  row   = index / 4;

    uint16_t x = GRID_X + col * (BOX_W + GAP_X);
    uint16_t y = GRID_Y + row * (BOX_H + GAP_Y);

    uint16_t borderColor = selected ? GREEN : WHITE;
    uint16_t textColor   = selected ? GREEN : WHITE;

    char s[4];
    snprintf(s, sizeof(s), "%u", step);

    ST7789_FillRect(x, y, BOX_W, BOX_H, BLACK);
    DrawRectBorder(x, y, BOX_W, BOX_H, borderColor);

    uint8_t len = 0;
    while (s[len]) len++;

    uint16_t textW = (uint16_t)(len * Font8x12.width * 2);
    uint16_t textH = (uint16_t)(Font8x12.height * 2);
    uint16_t tx    = x + ((BOX_W - textW) / 2);
    uint16_t ty    = y + ((BOX_H - textH) / 2);

    ST7789_DrawStringScaled(tx, ty, s, &Font8x12, 2, textColor, BLACK);
}

static void DrawHeader(void)
{
    ST7789_FillRect(0, 0, 240, HEADER_H, BLACK);
    ST7789_DrawStringScaled(6, 2, "120", &Font8x12, 2, WHITE, BLACK);
    ST7789_DrawString(60, 2, "BPM", &Font16x24, WHITE, BLACK);

    char play_big[2] = {91, 0};
    char stop_big[2] = {92, 0};
    char rec_big[2]  = {93, 0};

    ST7789_DrawString(153, 0, play_big, &Font16x24, WHITE, BLACK);
    ST7789_DrawString(176, 0, stop_big, &Font16x24, WHITE, BLACK);
    ST7789_DrawString(199, 0, rec_big,  &Font16x24, WHITE, BLACK);
}