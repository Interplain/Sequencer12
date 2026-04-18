#include "ui_input.h"
#include "ui_display.h"
#include "mcp23017.h"
#include "hw_init.h"
#include "stm32f4xx_hal.h"

/* ── State ───────────────────────────────────────────────────────────────── */
static int16_t  s_last_enc        = 0;
static int8_t   s_encoder_delta   = 0;
static uint8_t  s_shift_held      = 0;
static uint8_t  s_enc_btn_pressed = 0;
static uint16_t s_step_pressed_mask = 0;
static uint16_t s_shift_step_pressed_mask = 0;
static uint16_t s_prev_raw        = 0xFFFF;
static uint16_t s_prev_matrix_pressed = 0;
static uint8_t  s_enc_btn_prev    = 1;
static uint8_t  s_play_pressed    = 0;
static uint8_t  s_shift_play_pressed = 0;
static uint8_t  s_rec_pressed     = 0;
static uint8_t  s_shift_rec_pressed  = 0;
static uint8_t  s_shift_tap       = 0;
static uint8_t  s_shift_consumed  = 0;
static uint32_t s_last_mcp_check_ms = 0;

static const uint8_t s_col_bits[4] = {
    MCP_MATRIX_COL1_BIT,
    MCP_MATRIX_COL2_BIT,
    MCP_MATRIX_COL3_BIT,
    MCP_MATRIX_COL4_BIT
};

static const uint8_t s_row_bits[3] = {
    MCP_MATRIX_ROW1_BIT,
    MCP_MATRIX_ROW2_BIT,
    MCP_MATRIX_ROW3_BIT
};

/* ── Matrix scan (3x4, active low) ─────────────────────────────────────── */
static uint16_t ScanStepMatrix(void)
{
    uint16_t pressed = 0;

    for (uint8_t col = 0; col < 4; col++)
    {
        uint8_t out_a = (uint8_t)(MCP_MATRIX_COL_MASK);
        out_a &= (uint8_t)(~s_col_bits[col]); /* drive selected column low */
        MCP23017_WriteGPIOA(&hi2c1, out_a);

        /* Small settle delay for MCP write/read propagation */
        HAL_Delay(1);

        uint8_t gpio_a = MCP23017_ReadGPIOA(&hi2c1);

        for (uint8_t row = 0; row < 3; row++)
        {
            if ((gpio_a & s_row_bits[row]) == 0u)
            {
                uint8_t step_index = (uint8_t)(row * 4 + col); /* 0..11 */
                pressed |= (uint16_t)(1u << step_index);
            }
        }
    }

    /* Return all columns high when idle */
    MCP23017_WriteGPIOA(&hi2c1, (uint8_t)MCP_MATRIX_COL_MASK);

    return pressed;
}

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
    s_step_pressed_mask = 0;
    s_shift_step_pressed_mask = 0;
     s_prev_matrix_pressed = 0;
    s_enc_btn_prev    = HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_12);
    s_play_pressed    = 0;
    s_shift_play_pressed = 0;
    s_rec_pressed     = 0;
    s_shift_rec_pressed = 0;
    s_shift_tap       = 0;
    s_shift_consumed  = 0;
    s_last_mcp_check_ms = HAL_GetTick();

     /* Configure MCP for matrix scan:
         A: rows+shift inputs, cols outputs
         B: all inputs (play/rec on B0/B1) */
     MCP23017_SetDirections(&hi2c1, 0x87, 0xFF);
     MCP23017_SetPullups(&hi2c1, 0x87, 0xFF);
     MCP23017_WriteGPIOA(&hi2c1, (uint8_t)MCP_MATRIX_COL_MASK);

    /* Prime button state from stable hardware read */
    s_prev_raw = PrimeButtons();
}

/* ── Poll ────────────────────────────────────────────────────────────────── */
void UI_Input_Poll(void)
{
    /* MCP23017 self-heal: if config is lost at startup/bus glitch, restore it. */
    {
        uint32_t now = HAL_GetTick();
        if ((uint32_t)(now - s_last_mcp_check_ms) >= 200u)
        {
            s_last_mcp_check_ms = now;
            uint8_t iodira = MCP23017_ReadReg(&hi2c1, MCP_IODIRA);
            uint8_t iodirb = MCP23017_ReadReg(&hi2c1, MCP_IODIRB);

            if (iodira != 0x87u || iodirb != 0xFFu)
            {
                MCP23017_SetDirections(&hi2c1, 0x87, 0xFF);
                MCP23017_SetPullups(&hi2c1, 0x87, 0xFF);
                MCP23017_WriteGPIOA(&hi2c1, (uint8_t)MCP_MATRIX_COL_MASK);
                s_prev_raw = PrimeButtons();
                s_prev_matrix_pressed = 0;
            }
        }
    }

    /* ── MCP23017 ─────────────────────────────────────────────────────── */
    uint16_t raw            = MCP23017_ReadGPIO(&hi2c1);
    uint16_t changed        = s_prev_raw ^ raw;
    uint16_t falling        = changed & s_prev_raw;   /* 1→0 = press */
    uint8_t  shift_was_held = ((s_prev_raw & BTN_SHIFT_BIT) == 0u) ? 1u : 0u;
    uint8_t  shift_now_held = ((raw & BTN_SHIFT_BIT) == 0u) ? 1u : 0u;

    /* Shift tap detection: release with no modified input use. */
    if (!shift_was_held && shift_now_held)
    {
        s_shift_consumed = 0;
    }
    if (shift_was_held && !shift_now_held)
    {
        if (!s_shift_consumed)
        {
            s_shift_tap = 1;
        }
    }

    s_prev_raw = raw;

    /* Shift — level, active LOW */
    s_shift_held = shift_now_held;
    UI_Display_SetShiftIndicator(s_shift_held);

    /* Button 1 — Play/Stop or Reset */
    if (falling & BTN_PLAY_BIT)
    {
        if (shift_was_held)
        {
            s_shift_play_pressed = 1;
            s_shift_consumed = 1;
        }
        else
        {
            s_play_pressed = 1;
        }
    }

    /* Button 2 — Rec arm or Rec clear */
    if (falling & BTN_REC_BIT)
    {
        if (shift_was_held)
        {
            s_shift_rec_pressed = 1;
            s_shift_consumed = 1;
        }
        else
        {
            s_rec_pressed = 1;
        }
    }

    /* Step matrix scan (active low) */
    {
        uint16_t matrix_pressed = ScanStepMatrix();
        uint16_t step_falling = (uint16_t)(matrix_pressed & (uint16_t)(~s_prev_matrix_pressed));
        s_prev_matrix_pressed = matrix_pressed;

        if (step_falling != 0u)
        {
            for (uint8_t i = 0; i < 12; i++)
            {
                if (step_falling & (uint16_t)(1u << i))
                {
                    if (shift_was_held)
                    {
                        s_shift_step_pressed_mask |= (uint16_t)(1u << i);
                        s_shift_consumed = 1;
                    }
                    else
                    {
                        s_step_pressed_mask |= (uint16_t)(1u << i);
                    }
                }
            }
        }
    }

    
    /* ── Encoder ──────────────────────────────────────────────────────── */
    int16_t enc   = (int16_t)TIM2->CNT;
    int16_t delta = enc - s_last_enc;

    if (delta >= 4)
    {
        s_last_enc      = enc;
        s_encoder_delta = 1;
        if (s_shift_held) s_shift_consumed = 1;
    }
    else if (delta <= -4)
    {
        s_last_enc      = enc;
        s_encoder_delta = -1;
        if (s_shift_held) s_shift_consumed = 1;
    }

    /* ── Encoder button — PC15 ────────────────────────────────────────── */
    uint8_t enc_btn = HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_12);

    if (s_enc_btn_prev == 1u && enc_btn == 0u)
    {
        s_enc_btn_pressed = 1;
        if (s_shift_held) s_shift_consumed = 1;
    }

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

uint8_t UI_Input_IsPlayPressed(void)
{
    uint8_t p      = s_play_pressed;
    s_play_pressed = 0;
    return p;
}

uint8_t UI_Input_IsShiftPlayPressed(void)
{
    uint8_t p           = s_shift_play_pressed;
    s_shift_play_pressed = 0;
    return p;
}

uint8_t UI_Input_IsRecPressed(void)
{
    uint8_t p     = s_rec_pressed;
    s_rec_pressed = 0;
    return p;
}

uint8_t UI_Input_IsShiftRecPressed(void)
{
    uint8_t p          = s_shift_rec_pressed;
    s_shift_rec_pressed = 0;
    return p;
}

uint8_t UI_Input_GetShiftTap(void)
{
    uint8_t tap = s_shift_tap;
    s_shift_tap = 0;
    return tap;
}

uint8_t UI_Input_GetStepPressed(void)
{
    for (uint8_t i = 0; i < 12; i++)
    {
        uint16_t bit = (uint16_t)(1u << i);
        if (s_step_pressed_mask & bit)
        {
            s_step_pressed_mask &= (uint16_t)(~bit);
            return (uint8_t)(i + 1);
        }
    }
    return 0;
}

uint8_t UI_Input_GetShiftStepPressed(void)
{
    for (uint8_t i = 0; i < 12; i++)
    {
        uint16_t bit = (uint16_t)(1u << i);
        if (s_shift_step_pressed_mask & bit)
        {
            s_shift_step_pressed_mask &= (uint16_t)(~bit);
            return (uint8_t)(i + 1);
        }
    }
    return 0;
}