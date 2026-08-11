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
static uint16_t s_matrix_candidate = 0;
static uint16_t s_matrix_debounced_pressed = 0;
static uint32_t s_matrix_candidate_ms = 0;
static uint8_t  s_enc_btn_prev    = 1;
static uint8_t  s_enc_btn_short_pressed = 0;
static uint8_t  s_enc_btn_long_pressed = 0;
static uint8_t  s_enc_btn_is_down = 0;
static uint8_t  s_enc_btn_long_fired = 0;
static uint32_t s_enc_btn_down_ms = 0;
static uint8_t  s_play_pressed    = 0;
static uint8_t  s_shift_play_pressed = 0;
static uint8_t  s_rec_pressed     = 0;
static uint8_t  s_shift_rec_pressed  = 0;
static uint8_t  s_shift_tap       = 0;
static uint8_t  s_shift_consumed  = 0;
static uint32_t s_mcp_ignore_events_until_ms = 0;
static uint8_t  s_mcp_online = 0;
static uint8_t  s_direct_candidate = 0;
static uint8_t  s_direct_debounced = 0;
static uint8_t  s_direct_prev_debounced = 0;
static uint32_t s_direct_candidate_ms = 0;
static uint8_t  s_mcp_fail_streak = 0;
static uint8_t  s_mcp_ok_streak = 0;
static uint8_t  s_mcp_last_read_ok = 0;
static uint16_t s_mcp_last_raw = 0xFFFF;
static uint8_t  s_mcp_last_addr7 = 0x20;
static uint8_t  s_mcp_last_scan_mask = 0x00;
static uint32_t s_last_mcp_poll_ms = 0;
static uint32_t s_last_matrix_scan_ms = 0;
static uint8_t  s_direct_armed = 0;
static uint32_t s_direct_all_released_ms = 0;
static uint32_t s_last_mcp_recovery_ms = 0;

#define MATRIX_DEBOUNCE_MS 12u
#define MATRIX_SETTLE_US 40u
#define PRIME_MAX_SAMPLES 80u
#define MCP_STARTUP_IGNORE_MS 120u
#define MCP_RECOVER_IGNORE_MS 80u
#define DIRECT_DEBOUNCE_MS 15u
#define MCP_FAIL_STREAK_OFFLINE 24u
#define MCP_OK_STREAK_ONLINE 3u
#define MCP_POLL_INTERVAL_MS 4u
#define MATRIX_SCAN_INTERVAL_MS 10u
#define DIRECT_ARM_RELEASE_MS 60u
#define ENCODER_LONG_PRESS_MS 350u
#define ENCODER_COUNTS_PER_STEP 1

static int8_t SaturatingAddInt8(int8_t base, int8_t delta)
{
    int16_t sum = (int16_t)base + (int16_t)delta;
    if (sum > 127) return 127;
    if (sum < -127) return -127;
    return (int8_t)sum;
}

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

static void BusyWaitUs(uint32_t us)
{
    /* Coarse, non-blocking-enough settle delay to avoid 1ms HAL_Delay stalls in scan loop. */
    volatile uint32_t cycles = us * 18u;
    while (cycles--)
    {
        __NOP();
    }
}

static uint8_t DirectButtonsFromRaw(uint16_t raw)
{
    uint8_t bits = 0u;

    if ((raw & BTN_PLAY_BIT) == 0u)  bits |= (1u << 0);
    if ((raw & BTN_REC_BIT) == 0u)   bits |= (1u << 1);
    if ((raw & BTN_SHIFT_BIT) == 0u) bits |= (1u << 2);

    return bits;
}

static void ClearPendingMcpEvents(void)
{
    s_play_pressed = 0;
    s_shift_play_pressed = 0;
    s_rec_pressed = 0;
    s_shift_rec_pressed = 0;
    s_shift_tap = 0;
    s_shift_consumed = 0;
    s_step_pressed_mask = 0;
    s_shift_step_pressed_mask = 0;
    s_prev_matrix_pressed = 0;
    s_matrix_candidate = 0;
    s_matrix_debounced_pressed = 0;
}

/* ── Matrix scan (3x4, active low) ─────────────────────────────────────── */
static uint16_t ScanStepMatrix(void)
{
    uint16_t pressed = 0;

    for (uint8_t col = 0; col < 4; col++)
    {
        uint8_t out_a = (uint8_t)(MCP_MATRIX_COL_MASK);
        out_a &= (uint8_t)(~s_col_bits[col]); /* drive selected column low */
        MCP23017_WriteGPIOA(&hi2c1, out_a);

        /* Small settle delay for MCP write/read propagation. */
        BusyWaitUs(MATRIX_SETTLE_US);

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
    uint32_t samples      = 0;

    while (stable_count < 3u && samples < PRIME_MAX_SAMPLES)
    {
        now = MCP23017_ReadGPIO(&hi2c1);
        samples++;

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
    s_matrix_candidate = 0;
    s_matrix_debounced_pressed = 0;
    s_matrix_candidate_ms = HAL_GetTick();
    s_enc_btn_prev    = HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_12);
    s_enc_btn_short_pressed = 0;
    s_enc_btn_long_pressed = 0;
    s_enc_btn_is_down = 0;
    s_enc_btn_long_fired = 0;
    s_enc_btn_down_ms = HAL_GetTick();
    s_play_pressed    = 0;
    s_shift_play_pressed = 0;
    s_rec_pressed     = 0;
    s_shift_rec_pressed = 0;
    s_shift_tap       = 0;
    s_shift_consumed  = 0;
    s_mcp_ignore_events_until_ms = HAL_GetTick() + MCP_STARTUP_IGNORE_MS;
    s_mcp_online = 0;
    s_direct_candidate = 0;
    s_direct_debounced = 0;
    s_direct_prev_debounced = 0;
    s_direct_candidate_ms = HAL_GetTick();
    s_mcp_fail_streak = 0;
    s_mcp_ok_streak = 0;
    s_mcp_last_read_ok = 0;
    s_mcp_last_raw = 0xFFFF;
    s_last_mcp_poll_ms = HAL_GetTick();
    s_last_matrix_scan_ms = HAL_GetTick();
    s_direct_armed = 0;
    s_direct_all_released_ms = HAL_GetTick();
    s_last_mcp_recovery_ms = HAL_GetTick();
    s_last_mcp_recovery_ms = HAL_GetTick();

     /* Configure MCP for matrix scan:
         A: rows+shift inputs, cols outputs
         B: all inputs (play/rec on B0/B1) */
     MCP23017_SetDirections(&hi2c1, 0x87, 0xFF);
     MCP23017_SetPullups(&hi2c1, 0x87, 0xFF);
     MCP23017_WriteGPIOA(&hi2c1, (uint8_t)MCP_MATRIX_COL_MASK);

    /* Prime button state from stable hardware read */
    s_prev_raw = 0xFFFF;
    for (int i = 0; i < 5; i++)
    {
        s_prev_raw = MCP23017_ReadGPIO(&hi2c1);
        HAL_Delay(2);
    }
    s_mcp_last_raw = s_prev_raw;
    s_mcp_last_read_ok = 1;
    s_direct_candidate = DirectButtonsFromRaw(s_prev_raw);
    s_direct_debounced = s_direct_candidate;
    s_direct_prev_debounced = s_direct_candidate;
}

/* ── Poll ────────────────────────────────────────────────────────────────── */
void UI_Input_Poll(void)
{
    /* ── Encoder first: keep local controls responsive even if MCP bus glitches ── */
    int16_t enc   = (int16_t)TIM2->CNT;
    int16_t delta = enc - s_last_enc;

    if (delta >= ENCODER_COUNTS_PER_STEP || delta <= -ENCODER_COUNTS_PER_STEP)
    {
        int16_t steps = (int16_t)(delta / ENCODER_COUNTS_PER_STEP);

        if (steps > 127) steps = 127;
        if (steps < -127) steps = -127;

        s_last_enc = (int16_t)(s_last_enc + (steps * ENCODER_COUNTS_PER_STEP));
        s_encoder_delta = SaturatingAddInt8(s_encoder_delta, (int8_t)steps);
        if (s_shift_held) s_shift_consumed = 1;
    }

    /* ── Encoder button — PC12 ────────────────────────────────────────── */
    {
        uint8_t enc_btn = HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_12);
        uint32_t now_btn_ms = HAL_GetTick();

        if (s_enc_btn_prev == 1u && enc_btn == 0u)
        {
            s_enc_btn_pressed = 1;
            s_enc_btn_is_down = 1u;
            s_enc_btn_long_fired = 0u;
            s_enc_btn_down_ms = now_btn_ms;
            if (s_shift_held) s_shift_consumed = 1;
        }
        else if (s_enc_btn_prev == 0u && enc_btn == 0u)
        {
            if (s_enc_btn_is_down && !s_enc_btn_long_fired)
            {
                if ((uint32_t)(now_btn_ms - s_enc_btn_down_ms) >= ENCODER_LONG_PRESS_MS)
                {
                    s_enc_btn_long_pressed = 1u;
                    s_enc_btn_long_fired = 1u;
                }
            }
        }
        else if (s_enc_btn_prev == 0u && enc_btn == 1u)
        {
            if (s_enc_btn_is_down && !s_enc_btn_long_fired)
            {
                s_enc_btn_short_pressed = 1u;
            }
            s_enc_btn_is_down = 0u;
            s_enc_btn_long_fired = 0u;
        }

        s_enc_btn_prev = enc_btn;
    }

    /* ── MCP23017 ─────────────────────────────────────────────────────── */
    {
        uint32_t now_ms = HAL_GetTick();
        if ((uint32_t)(now_ms - s_last_mcp_poll_ms) < MCP_POLL_INTERVAL_MS)
        {
            return;
        }
        s_last_mcp_poll_ms = now_ms;
    }

    uint16_t raw = MCP23017_ReadGPIO(&hi2c1);
    uint32_t now_ms = HAL_GetTick();

    s_mcp_last_read_ok = 1;
    s_mcp_last_raw = raw;
    s_mcp_ok_streak = 1;
    s_mcp_fail_streak = 0;
    s_mcp_online = 1;

    uint8_t direct_raw = DirectButtonsFromRaw(raw);
    if (direct_raw != s_direct_candidate)
    {
        s_direct_candidate = direct_raw;
        s_direct_candidate_ms = now_ms;
    }
    else if ((uint32_t)(now_ms - s_direct_candidate_ms) >= DIRECT_DEBOUNCE_MS)
    {
        s_direct_debounced = s_direct_candidate;
    }

    uint8_t direct_falling = (uint8_t)(s_direct_debounced & (uint8_t)(~s_direct_prev_debounced));
    uint8_t shift_was_held = (s_direct_prev_debounced & (1u << 2)) ? 1u : 0u;
    uint8_t shift_now_held = (s_direct_debounced & (1u << 2)) ? 1u : 0u;
    uint8_t suppress_mcp_events = ((int32_t)(now_ms - s_mcp_ignore_events_until_ms) < 0) ? 1u : 0u;

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
    s_direct_prev_debounced = s_direct_debounced;

    if (!s_direct_armed)
    {
        if (s_direct_debounced == 0u)
        {
            if ((uint32_t)(now_ms - s_direct_all_released_ms) >= DIRECT_ARM_RELEASE_MS)
            {
                s_direct_armed = 1u;
            }
        }
        else
        {
            s_direct_all_released_ms = now_ms;
        }
    }

    /* Shift — level, active LOW */
    s_shift_held = shift_now_held;
    UI_Display_SetShiftIndicator(s_shift_held);

    /* Button 1 — Play/Stop or Reset */
    if (s_direct_armed && !suppress_mcp_events && (direct_falling & (1u << 0)))
    {
        if (shift_now_held)
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
    if (s_direct_armed && !suppress_mcp_events && (direct_falling & (1u << 1)))
    {
        if (shift_now_held)
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
        if ((uint32_t)(now_ms - s_last_matrix_scan_ms) < MATRIX_SCAN_INTERVAL_MS)
        {
            return;
        }
        s_last_matrix_scan_ms = now_ms;

        uint32_t matrix_now = HAL_GetTick();
        uint16_t matrix_pressed = ScanStepMatrix();

        if (matrix_pressed != s_matrix_candidate)
        {
            s_matrix_candidate = matrix_pressed;
            s_matrix_candidate_ms = matrix_now;
        }
        else if ((uint32_t)(matrix_now - s_matrix_candidate_ms) >= MATRIX_DEBOUNCE_MS)
        {
            s_prev_matrix_pressed = s_matrix_debounced_pressed;
            s_matrix_debounced_pressed = s_matrix_candidate;

            uint16_t step_falling = (uint16_t)(s_matrix_debounced_pressed & (uint16_t)(~s_prev_matrix_pressed));

            if (s_direct_armed && !suppress_mcp_events && step_falling != 0u)
            {
                /*
                 * Hardware transients can briefly report multiple steps in one scan.
                 * Queue only one press (highest index), which keeps rightmost-column
                 * keys from walking through lower-numbered steps.
                 */
                if ((step_falling & (uint16_t)(step_falling - 1u)) != 0u)
                {
                    uint16_t single = 0;
                    for (int8_t i = 11; i >= 0; i--)
                    {
                        uint16_t bit = (uint16_t)(1u << (uint8_t)i);
                        if (step_falling & bit)
                        {
                            single = bit;
                            break;
                        }
                    }
                    step_falling = single;
                }

                for (uint8_t i = 0; i < 12; i++)
                {
                    if (step_falling & (uint16_t)(1u << i))
                    {
                        if (shift_now_held)
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
    }

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

uint8_t UI_Input_IsEncoderShortPressed(void)
{
    uint8_t p = s_enc_btn_short_pressed;
    s_enc_btn_short_pressed = 0;
    return p;
}

uint8_t UI_Input_IsEncoderLongPressed(void)
{
    uint8_t p = s_enc_btn_long_pressed;
    s_enc_btn_long_pressed = 0;
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

void UI_Input_GetMcpDebug(uint8_t* online, uint8_t* read_ok, uint8_t* gpio_a, uint8_t* gpio_b, uint8_t* addr7, uint8_t* scan_mask)
{
    if (online)
    {
        *online = s_mcp_online;
    }

    if (read_ok)
    {
        *read_ok = s_mcp_last_read_ok;
    }

    if (gpio_a)
    {
        *gpio_a = (uint8_t)(s_mcp_last_raw >> 8);
    }

    if (gpio_b)
    {
        *gpio_b = (uint8_t)(s_mcp_last_raw & 0xFFu);
    }

    if (addr7)
    {
        *addr7 = 0x20;  /* Fixed address */
    }

    if (scan_mask)
    {
        *scan_mask = 0x01;  /* Always found */
    }
}

uint8_t UI_Input_GetMcpOkStreak(void)
{
    return s_mcp_ok_streak;
}

uint8_t UI_Input_GetMcpFailStreak(void)
{
    return s_mcp_fail_streak;
}