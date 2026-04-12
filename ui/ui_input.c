#include "ui_input.h"
#include "ui_transport.h"
#include "ui_display.h"
#include "mcp23017.h"
#include "hw_init.h"
#include "stm32f4xx_hal.h"

/* ── State ───────────────────────────────────────────────────────────────── */
static int16_t  s_last_enc        = 0;
static int8_t   s_encoder_delta   = 0;
static uint8_t  s_shift_held      = 0;
static uint8_t  s_enc_btn_pressed = 0;
static uint16_t s_prev_raw        = 0xFFFF;
static uint8_t  s_enc_btn_prev    = 1;

/* ── Prime — read until stable ───────────────────────────────────────────── */
static uint16_t PrimeButtons(void)
{
    uint16_t last         = 0xFFFF;
    uint16_t now          = 0xFFFF;
    uint8_t  stable_count = 0;

    while (stable_count < 5)
    {
        now = MCP23017_ReadGPIO(&hi2c1);

        if (now == last)
            stable_count++;
        else
        {
            stable_count = 0;
            last         = now;
        }

        HAL_Delay(2);
    }

    return now;
}

/* ── Init ────────────────────────────────────────────────────────────────── */
void UI_Input_Init(void)
{
    s_last_enc        = (int16_t)TIM2->CNT;
    s_encoder_delta   = 0;
    s_shift_held      = 0;
    s_enc_btn_pressed = 0;
    s_enc_btn_prev    = HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_15);

    /* Prime button state from stable hardware read */
    s_prev_raw = PrimeButtons();
}

/* ── Poll ────────────────────────────────────────────────────────────────── */
void UI_Input_Poll(void)
{
    /* ── MCP23017 ─────────────────────────────────────────────────────── */
    uint16_t raw     = MCP23017_ReadGPIO(&hi2c1);
    uint16_t changed = s_prev_raw ^ raw;
    uint16_t rising  = changed & raw;    /* 0→1 = press */
    s_prev_raw       = raw;

    /* Shift — level */
    s_shift_held = (raw & BTN_SHIFT_BIT) ? 1 : 0;
    UI_Display_SetShiftIndicator(s_shift_held);

    /* Button 1 — Play/Stop or Reset */
    if (rising & BTN_PLAY_BIT)
    {
        if (s_shift_held)
            UI_Transport_Reset();
        else
            UI_Transport_PlayStop();
    }

    /* Button 2 — Rec arm or Rec clear */
    if (rising & BTN_REC_BIT)
    {
        if (s_shift_held)
            UI_Transport_RecClear();
        else
            UI_Transport_RecArm();
    }

    
    /* ── Encoder ──────────────────────────────────────────────────────── */
    int16_t enc   = (int16_t)TIM2->CNT;
    int16_t delta = enc - s_last_enc;

    if (delta >= 4)
    {
    s_last_enc      = enc;
    s_encoder_delta = 1;
    }
    else if (delta <= -4)
    {
    s_last_enc      = enc;
    s_encoder_delta = -1;
    }

    /* ── Encoder button — PC15 ────────────────────────────────────────── */
    uint8_t enc_btn = HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_15);

    if (s_enc_btn_prev == 1u && enc_btn == 0u)
        s_enc_btn_pressed = 1;

    s_enc_btn_prev = enc_btn;
}

/* ── Getters ─────────────────────────────────────────────────────────────── */
int8_t UI_Input_GetEncoderDelta(void)
{
    int8_t d        = s_encoder_delta;
    s_encoder_delta = 0;
    return d;
}

uint8_t UI_Input_IsShiftHeld(void)
{
    return s_shift_held;
}

uint8_t UI_Input_IsEncoderPressed(void)
{
    uint8_t p         = s_enc_btn_pressed;
    s_enc_btn_pressed = 0;
    return p;
}