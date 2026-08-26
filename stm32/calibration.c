#include "calibration.h"

#include <stdio.h>
#include "stm32f4xx_hal.h"
#include "st7789.h"
#include "s12_fonts.h"
#include "hw_init.h"
#include "platform/dac8564/dac8564.h"
#include "platform/fram/mb85rc256.h"
#include "platform/fram/fram_layout.h"
#include "sequencer_bridge.h"
#include "ui/ui_input.h"

#define CAL_MAGIC          0x53314341u
#define CAL_VERSION        4u
#define CAL_BOOT_HOLD_START_MS   70u
#define CAL_BOOT_HOLD_SAMPLES    5u
#define CAL_BOOT_HOLD_STEP_MS    12u
#define CAL_CODE_STEP_FINE       1u
#define CAL_CODE_STEP_COARSE     64u
#define CAL_ENCODER_DETENT_TICKS 4u
#define CAL_MIRROR_ALL_CHANNELS  0u
/* ── Calibration target voltages ──────────────────────────────────────────
 * These MUST match DAC_CAL_VLOW / DAC_CAL_VHIGH in dac8564.c, or every note
 * the sequencer plays comes out at the wrong voltage.
 *
 * Output stage reaches roughly +6.98V .. -4.72V, so targets must sit inside
 * that. -2V/+6V keeps the full C0..C8 span while preserving 0V at C2. */
#define CAL_V_LOW_TEXT           "-2.00V"
#define CAL_V_HIGH_TEXT          "+6.00V"

#define CAL_DEFAULT_NEG1_CODE    56000u   /* rough starting code for -2.00V */
#define CAL_DEFAULT_POS2_CODE    12000u   /* rough starting code for +6.00V */
#define CAL_DEBOUNCE_MS          80u
#define CAL_STAGE_TIMEOUT_MS   8000u
#define CAL_ENCODER_CONFIRM_MS  180u

#define CAL_DAC_LDAC_PORT        GPIOB
#define CAL_DAC_LDAC_PIN         GPIO_PIN_14
#define CAL_DAC_CS_PORT          GPIOB
#define CAL_DAC_CS_PIN           GPIO_PIN_12


typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t ch_a;
    uint32_t ch_b;
    uint32_t ch_c;
    uint32_t ch_d;
    uint32_t checksum;
} CalBlob;

static uint8_t s_fram_ready_dbg = 0u;
static uint8_t s_fram_addr_dbg = MB85RC256_ADDR_7BIT;
static uint32_t s_cal_save_count = 0u;

static const char* CalFramResultText(Mb85rc256Result result)
{
    switch (result)
    {
    case MB85RC256_RESULT_OK: return "OK";
    case MB85RC256_RESULT_NOT_INITIALIZED: return "NOINIT";
    case MB85RC256_RESULT_RANGE: return "RANGE";
    case MB85RC256_RESULT_NOT_READY: return "NORDY";
    case MB85RC256_RESULT_WRITE_FAILED: return "WRITE";
    case MB85RC256_RESULT_READ_FAILED: return "READ";
    case MB85RC256_RESULT_VERIFY_MISMATCH: return "VERIFY";
    default: return "?";
    }
}

static void CalUpdateFramDebug(uint8_t ready)
{
    s_fram_ready_dbg = ready;
    s_fram_addr_dbg = MB85RC256_GetAddress7bit();
}

static uint32_t CalChecksum(const CalBlob* b)
{
    return b->magic ^ b->version ^ b->ch_a ^ b->ch_b ^ b->ch_c ^ b->ch_d ^ 0xA5A55A5Au;
}

static uint8_t CalReadSaveCount(uint32_t* count)
{
    uint32_t value = 0u;
    if (!MB85RC256_Read(FRAM_CALIB_SAVECOUNT_ADDR, (uint8_t*)&value, sizeof(value)))
    {
        return 0u;
    }
    if (count)
    {
        *count = value;
    }
    return 1u;
}

static uint8_t CalWriteSaveCount(uint32_t count)
{
    return MB85RC256_WriteAndVerify(FRAM_CALIB_SAVECOUNT_ADDR, (const uint8_t*)&count, sizeof(count));
}

static uint8_t CalRead(uint16_t neg1[4], uint16_t pos2[4])
{
    CalBlob b;
    if (!MB85RC256_Read(FRAM_CALIBRATION_ADDR, (uint8_t*)&b, sizeof(CalBlob)))
    {
        CalUpdateFramDebug(0u);
        return 0;
    }
    CalUpdateFramDebug(1u);
    if (b.magic != CAL_MAGIC) return 0;
    if (b.version != CAL_VERSION) return 0;
    if (b.checksum != CalChecksum(&b)) return 0;

    neg1[0] = (uint16_t)(b.ch_a & 0xFFFFu);
    pos2[0] = (uint16_t)((b.ch_a >> 16) & 0xFFFFu);
    neg1[1] = (uint16_t)(b.ch_b & 0xFFFFu);
    pos2[1] = (uint16_t)((b.ch_b >> 16) & 0xFFFFu);
    neg1[2] = (uint16_t)(b.ch_c & 0xFFFFu);
    pos2[2] = (uint16_t)((b.ch_c >> 16) & 0xFFFFu);
    neg1[3] = (uint16_t)(b.ch_d & 0xFFFFu);
    pos2[3] = (uint16_t)((b.ch_d >> 16) & 0xFFFFu);

    for (uint8_t i = 0; i < 4; i++)
    {
        if (pos2[i] == neg1[i]) return 0;
    }

    return 1;
}

static uint8_t CalWrite(const uint16_t neg1[4], const uint16_t pos2[4])
{
    CalBlob b;
    uint8_t ok;
    for (uint8_t i = 0; i < 4; i++)
    {
        if (pos2[i] == neg1[i]) return 0;
    }
    b.magic = CAL_MAGIC;
    b.version = CAL_VERSION;
    b.ch_a = ((uint32_t)pos2[0] << 16) | neg1[0];
    b.ch_b = ((uint32_t)pos2[1] << 16) | neg1[1];
    b.ch_c = ((uint32_t)pos2[2] << 16) | neg1[2];
    b.ch_d = ((uint32_t)pos2[3] << 16) | neg1[3];
    b.checksum = CalChecksum(&b);
    ok = MB85RC256_WriteAndVerify(FRAM_CALIBRATION_ADDR, (const uint8_t*)&b, sizeof(CalBlob));
    CalUpdateFramDebug((uint8_t)(ok ? 1u : 0u));
    return ok;
}

static uint8_t EncButtonRaw(void)
{
    return (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_12) == GPIO_PIN_RESET) ? 1u : 0u;
}

/* Debounced button state: returns a stable 0/1, ignoring bounce.
 * Call frequently; it integrates samples over CAL_DEBOUNCE_MS. */
static uint8_t EncButtonStable(void)
{
    static uint8_t stable = 0u;
    static uint8_t candidate = 0u;
    static uint32_t since = 0u;

    uint8_t raw = EncButtonRaw();
    if (raw != candidate)
    {
        candidate = raw;
        since = HAL_GetTick();
    }
    else if (candidate != stable &&
             (HAL_GetTick() - since) >= CAL_DEBOUNCE_MS)
    {
        stable = candidate;   /* held long enough to be real */
    }
    return stable;
}

/* Returns 1 exactly once on a debounced press edge (release->press). */
static uint8_t EncButtonPressed(void)
{
    static uint8_t prev = 0u;
    uint8_t now = EncButtonStable();
    uint8_t edge = (now && !prev) ? 1u : 0u;
    prev = now;
    return edge;
}

/* Returns 1 once when a long-press (>= 600ms held) is detected. */
static uint8_t EncButtonLongPress(void)
{
    static uint8_t was_down = 0u;
    static uint32_t down_at = 0u;
    static uint8_t fired = 0u;
    uint8_t now = EncButtonStable();

    if (now && !was_down) { down_at = HAL_GetTick(); fired = 0u; }
    if (!now)             { was_down = 0u; fired = 0u; return 0u; }
    was_down = 1u;
    if (!fired && (HAL_GetTick() - down_at) >= 600u) { fired = 1u; return 1u; }
    return 0u;
}

static void EncStepReset(int16_t now)
{
    (void)now;
}

static int8_t EncStep(int16_t* last)
{
    int16_t now = (int16_t)TIM2->CNT;
    int16_t delta = (int16_t)(now - *last);
    const int16_t detent = (int16_t)CAL_ENCODER_DETENT_TICKS;

    if (delta >= detent)
    {
        *last = (int16_t)(*last + detent);
        return 1;
    }
    if (delta <= -detent)
    {
        *last = (int16_t)(*last - detent);
        return -1;
    }

    return 0;
}

/* ── Screen layout (240x240) ──────────────────────────────────────────────
 *   y   0..30   header: "CALIBRATION"  +  step counter (right)
 *   y  40..64   channel + target voltage
 *   y  90..118  DAC code (large)
 *   y 130..146  step size (fine/coarse)
 *   y 190..232  controls help
 * Nothing overlaps; each region is cleared before it is written.        */
#define CAL_Y_HEADER    6
#define CAL_Y_TARGET    44
#define CAL_Y_CODE      110
#define CAL_Y_STEP      132
#define CAL_Y_HELP      190

static void DrawCalHeader(uint8_t step, uint8_t total)
{
    char line[16];

    ST7789_FillRect(0, 0, ST7789_WIDTH, 32, BLACK);
    ST7789_DrawString(8, CAL_Y_HEADER, "CALIBRATION", &Font12x20, WHITE, BLACK);

    if (total > 0u)
    {
        snprintf(line, sizeof(line), "%u/%u", (unsigned)step, (unsigned)total);
        ST7789_DrawString(180, CAL_Y_HEADER + 4, line, &Font8x12, GRAY, BLACK);
    }
}

static void DrawCalStatic(const char* title, const char* hint)
{
    (void)hint;   /* hint no longer shown separately — title carries it */

    ST7789_FillRect(0, 36, ST7789_WIDTH, 40, BLACK);
    ST7789_DrawString(8, CAL_Y_TARGET, title, &Font16x24, CYAN, BLACK);

    ST7789_FillRect(0, CAL_Y_HELP, ST7789_WIDTH,
                    (uint16_t)(ST7789_HEIGHT - CAL_Y_HELP), BLACK);
    ST7789_DrawString(8, CAL_Y_HELP,      "Turn  = adjust",       &Font8x12, GRAY, BLACK);
    ST7789_DrawString(8, CAL_Y_HELP + 16, "Click = fine/coarse",  &Font8x12, GRAY, BLACK);
    ST7789_DrawString(8, CAL_Y_HELP + 32, "Hold  = set & next",   &Font8x12, GRAY, BLACK);
}

static void DrawCalCode(uint16_t code, uint16_t step, uint8_t force_clear)
{
    char line[24];

    if (force_clear)
    {
        ST7789_FillRect(0, CAL_Y_CODE, ST7789_WIDTH, 30, BLACK);
        ST7789_FillRect(0, CAL_Y_STEP, ST7789_WIDTH, 18, BLACK);
    }

    snprintf(line, sizeof(line), "%5u", (unsigned)code);
    ST7789_DrawString(8, CAL_Y_CODE, line, &Font16x24, YELLOW, BLACK);

    ST7789_DrawString(8, CAL_Y_STEP,
                      (step == CAL_CODE_STEP_FINE) ? "step: fine" : "step: COARSE",
                      &Font8x12,
                      (step == CAL_CODE_STEP_FINE) ? GRAY : GREEN,
                      BLACK);
}

static void CalWriteOutput(Dac8564Channel channel, uint16_t code)
{
    uint8_t ch = (uint8_t)channel;

    if (ch > 3u)
    {
        ch = 0u;
    }

#if CAL_MIRROR_ALL_CHANNELS
    DAC8564_SetAllRaw(code, code, code, code);
#else
    DAC8564_SetChannelRaw((Dac8564Channel)ch, code);
#endif
}

static uint8_t StageButtonRaw(void)
{
    return (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_12) == GPIO_PIN_RESET) ? 1u : 0u;
}

static void StageWaitForAdvance(void)
{
    uint8_t was_down = 0u;

    while (1)
    {
        uint8_t down = StageButtonRaw();
        if (down)
        {
            was_down = 1u;
        }
        else if (was_down)
        {
            return;
        }

        HAL_Delay(4);
    }
}

static void StageDrawFrame(const char* title, const char* line1, const char* line2)
{
    ST7789_Fill_Color(BLACK);
    ST7789_DrawString(8, 12, title, &Font12x20, WHITE, BLACK);
    ST7789_DrawString(8, 52, line1, &Font8x12, CYAN, BLACK);
    ST7789_DrawString(8, 68, line2, &Font8x12, LIGHTBLUE, BLACK);
    ST7789_DrawString(8, 170, "Press encoder to advance", &Font8x12, WHITE, BLACK);
}


static void StageEncoderOnly(void)
{
    int16_t last_enc = (int16_t)TIM2->CNT;
    uint8_t button_prev = 0u;
    uint32_t last_beat = HAL_GetTick();
    uint8_t beat_on = 0u;

    StageDrawFrame("S2 ENCODER ONLY", "Rotate encoder slowly.", "No screen updates on motion.");
    ST7789_DrawString(8, 92, "TIM2 raw sample active", &Font8x12, YELLOW, BLACK);
    ST7789_DrawString(8, 108, "HB:", &Font8x12, GREEN, BLACK);

    while (1)
    {
        int16_t now_enc = (int16_t)TIM2->CNT;
        int16_t delta = (int16_t)(now_enc - last_enc);

        if (delta >= (int16_t)CAL_ENCODER_DETENT_TICKS ||
            delta <= -(int16_t)CAL_ENCODER_DETENT_TICKS)
        {
            last_enc = now_enc;
        }

        if ((HAL_GetTick() - last_beat) >= 1000u)
        {
            beat_on = (uint8_t)!beat_on;
            ST7789_DrawString(32, 108, beat_on ? "*" : ".", &Font8x12, GREEN, BLACK);
            last_beat = HAL_GetTick();
        }

        {
            uint8_t button_now = StageButtonRaw();
            if (button_now && !button_prev)
            {
                while (StageButtonRaw())
                {
                    HAL_Delay(4);
                }
                return;
            }
            button_prev = button_now;
        }

        if (StageButtonRaw())
        {
            return;
        }

        HAL_Delay(4);
    }
}

static void StageScreenOnly(void)
{
    StageDrawFrame("S1 SCREEN ONLY", "No encoder, DAC, or FRAM calls", "If this hangs, it is the display path.");
    StageWaitForAdvance();
}

static void StageDacOnly(void)
{
    int16_t last_enc = (int16_t)TIM2->CNT;
    uint16_t code = CAL_DEFAULT_NEG1_CODE;
    uint32_t last_draw = 0u;

    StageDrawFrame("S3 ADD DAC", "Rotate to update DAC A only.", "No FRAM calls yet.");
    DAC8564_SetChannelRaw(DAC8564_CH_A, code);

    while (1)
    {
        int16_t now_enc = (int16_t)TIM2->CNT;
        int16_t delta = (int16_t)(now_enc - last_enc);

        if (delta >= (int16_t)CAL_ENCODER_DETENT_TICKS)
        {
            last_enc = now_enc;
            if (code < 65535u) code++;
            DAC8564_SetChannelRaw(DAC8564_CH_A, code);
        }
        else if (delta <= -(int16_t)CAL_ENCODER_DETENT_TICKS)
        {
            last_enc = now_enc;
            if (code > 0u) code--;
            DAC8564_SetChannelRaw(DAC8564_CH_A, code);
        }

        if ((HAL_GetTick() - last_draw) >= 100u)
        {
            char line[48];
            snprintf(line, sizeof(line), "DAC:%5u CNT:%5u   ", (unsigned)code, (unsigned)now_enc);
            ST7789_FillRect(8, 92, 224, 14, BLACK);
            ST7789_DrawString(8, 92, line, &Font8x12, YELLOW, BLACK);
            last_draw = HAL_GetTick();
        }

        if (StageButtonRaw())
        {
            StageWaitForAdvance();
            return;
        }

        HAL_Delay(4);
    }
}

static void StageFramReadOnly(void)
{
    uint8_t byte = 0u;
    uint8_t ok = MB85RC256_Read(FRAM_CALIBRATION_ADDR, &byte, 1u);
    char line[48];

    snprintf(line, sizeof(line), "READ:%s VAL:%02X   ", ok ? "OK" : "FAIL", (unsigned)byte);
    StageDrawFrame("S4 ADD FRAM READ", "Direct FRAM read test only.", line);
    StageWaitForAdvance();
}

static void StageFramWriteOnly(void)
{
    uint8_t backup[8];
    uint8_t pattern[8] = {0xA5u, 0x5Au, 0x11u, 0xEEu, 0x33u, 0xCCu, 0x77u, 0x88u};
    uint8_t ok = 0u;

    if (MB85RC256_Read(FRAM_FUTURE_ADDR, backup, sizeof(backup)))
    {
        ok = MB85RC256_WriteAndVerify(FRAM_FUTURE_ADDR, pattern, sizeof(pattern));
        if (ok)
        {
            (void)MB85RC256_WriteAndVerify(FRAM_FUTURE_ADDR, backup, sizeof(backup));
        }
    }

    StageDrawFrame("S5 ADD FRAM WRITE", ok ? "Write/verify passed." : "Write/verify failed.", "Reserved FRAM area only.");
    StageWaitForAdvance();
}

void Calibration_RunBringupStages(void)
{
    Bridge_SetTickEnabled(0u);

    StageScreenOnly();
    StageEncoderOnly();
    StageDacOnly();
    StageFramReadOnly();
    StageFramWriteOnly();

    Bridge_SetTickEnabled(1u);
}



static uint16_t AdjustCodeStep(const char* title,
                               const char* hint,
                               uint16_t start_code,
                               Dac8564Channel channel,
                               uint8_t wiz_step,
                               uint8_t wiz_total)
{
    uint16_t code = start_code;
    int16_t last_enc = (int16_t)TIM2->CNT;
    uint16_t shown_code = (uint16_t)(code ^ 0xFFFFu);
    uint16_t current_step = CAL_CODE_STEP_COARSE;
    uint8_t swallow_release = 0u;

    /* Prime the debounced button so a press used to *enter* this screen
     * isn't seen as an immediate action. */
    (void)EncButtonStable();
    (void)EncButtonPressed();
    (void)EncButtonLongPress();
    EncStepReset(last_enc);

    DrawCalHeader(wiz_step, wiz_total);
    DrawCalStatic(title, hint);
    DrawCalCode(code, current_step, 1u);
    shown_code = code;
    CalWriteOutput(channel, code);

    while (1)
    {
        int8_t step = EncStep(&last_enc);
        if (step != 0)
        {
            int32_t v = (int32_t)code + (int32_t)step * (int32_t)current_step;
            if (v < 0) v = 0;
            if (v > 65535) v = 65535;
            code = (uint16_t)v;
            CalWriteOutput(channel, code);
        }

        /* Redraw the code value only when it changes (cheap). */
        if (code != shown_code)
        {
            DrawCalCode(code, current_step, 1u);
            shown_code = code;
        }

        /* Long-press confirms this point and advances to next step/channel. */
        if (EncButtonLongPress())
        {
            return code;
        }
        /* Short click toggles fine/coarse step size. */
        else if (!swallow_release && EncButtonPressed())
        {
            current_step = (current_step == CAL_CODE_STEP_FINE)
                           ? CAL_CODE_STEP_COARSE : CAL_CODE_STEP_FINE;
            DrawCalCode(code, current_step, 1u);
            shown_code = code;
            swallow_release = 1u;
        }

        if (swallow_release && !EncButtonStable())
        {
            swallow_release = 0u;
            (void)EncButtonPressed();
        }

        HAL_Delay(4);
    }
}

uint8_t Calibration_ShouldEnterOnBoot(void)
{
    if (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_12) != GPIO_PIN_RESET) return 0u;

    HAL_Delay(CAL_BOOT_HOLD_START_MS);
    for (uint8_t i = 0; i < CAL_BOOT_HOLD_SAMPLES; i++)
    {
        if (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_12) != GPIO_PIN_RESET)
        {
            return 0u;
        }
        HAL_Delay(CAL_BOOT_HOLD_STEP_MS);
    }

    return 1u;
}

uint8_t Calibration_EnterIfHeldOnBoot(void)
{
    if (!Calibration_ShouldEnterOnBoot())
    {
        return 0u;
    }

    ST7789_FillRect(0, 0, ST7789_WIDTH, 160, BLACK);
    ST7789_DrawStringScaled(12, 56, "CALIBRATION", &Font16x24, 1, WHITE, BLACK);
    ST7789_DrawStringScaled(12, 98, "ENTRY TEST", &Font16x24, 1, CYAN, BLACK);
    ST7789_DrawStringScaled(12, 140, "HOLD OK", &Font16x24, 1, GREEN, BLACK);
    return 1u;
}

void Calibration_ApplySaved(void)
{
    uint16_t neg1[4] = {CAL_DEFAULT_NEG1_CODE, CAL_DEFAULT_NEG1_CODE, CAL_DEFAULT_NEG1_CODE, CAL_DEFAULT_NEG1_CODE};
    uint16_t pos2[4] = {CAL_DEFAULT_POS2_CODE, CAL_DEFAULT_POS2_CODE, CAL_DEFAULT_POS2_CODE, CAL_DEFAULT_POS2_CODE};

    (void)CalRead(neg1, pos2);
    if (!CalReadSaveCount(&s_cal_save_count))
    {
        s_cal_save_count = 0u;
    }

    DAC8564_SetPitchCalibrationForChannel(DAC8564_CH_A, neg1[0], pos2[0]);
    DAC8564_SetPitchCalibrationForChannel(DAC8564_CH_B, neg1[1], pos2[1]);
    DAC8564_SetPitchCalibrationForChannel(DAC8564_CH_C, neg1[2], pos2[2]);
    DAC8564_SetPitchCalibrationForChannel(DAC8564_CH_D, neg1[3], pos2[3]);
}

void Calibration_RunWizard(void)
{
    uint16_t neg1[4] = {CAL_DEFAULT_NEG1_CODE, CAL_DEFAULT_NEG1_CODE, CAL_DEFAULT_NEG1_CODE, CAL_DEFAULT_NEG1_CODE};
    uint16_t pos2[4] = {CAL_DEFAULT_POS2_CODE, CAL_DEFAULT_POS2_CODE, CAL_DEFAULT_POS2_CODE, CAL_DEFAULT_POS2_CODE};
    static const char* channel_names[4] = {"A", "B", "C", "D"};
    static const Dac8564Channel channels[4] = {DAC8564_CH_A, DAC8564_CH_B, DAC8564_CH_C, DAC8564_CH_D};
    uint8_t saved;
    uint8_t count_saved = 0u;
    uint8_t step = 1u;
    char title[24];
    char hint[44];

    Bridge_SetTickEnabled(0u);

    DrawCalHeader(0u, 0u);
    CalRead(neg1, pos2);
    if (!CalReadSaveCount(&s_cal_save_count))
    {
        s_cal_save_count = 0u;
    }

    for (uint8_t ch = 0; ch < 4; ch++)
    {
        snprintf(title, sizeof(title), "CH %s  " CAL_V_LOW_TEXT, channel_names[ch]);
        snprintf(hint, sizeof(hint), " ");
        neg1[ch] = AdjustCodeStep(title, hint, neg1[ch], channels[ch], step++, 8u);

        snprintf(title, sizeof(title), "CH %s  " CAL_V_HIGH_TEXT, channel_names[ch]);
        snprintf(hint, sizeof(hint), " ");
        pos2[ch] = AdjustCodeStep(title, hint, pos2[ch], channels[ch], step++, 8u);

        if (pos2[ch] == neg1[ch])
        {
            pos2[ch] = (uint16_t)(neg1[ch] + 1u);
        }

        DAC8564_SetPitchCalibrationForChannel(channels[ch], neg1[ch], pos2[ch]);
        
        /* Reset this channel back to calibrated 0V before the next channel. */
        DAC8564_SetChannelRaw(channels[ch], DAC8564_PitchVoltsToCodeForChannel(channels[ch], 0.0f));
    }

    saved = CalWrite(neg1, pos2);
    if (saved)
    {
        uint32_t next_count = s_cal_save_count + 1u;
        if (CalWriteSaveCount(next_count))
        {
            s_cal_save_count = next_count;
            count_saved = 1u;
        }
    }

    /* Clear the whole screen — the adjust screen's help text sits low down. */
    ST7789_Fill_Color(BLACK);
    ST7789_DrawString(8, 12, "CALIBRATION", &Font16x24, WHITE, BLACK);
    ST7789_DrawString(8, 58, saved ? "Saved to FRAM" : "Save failed", &Font12x20, saved ? GREEN : RED, BLACK);
    if (saved)
    {
        char count_line[56];
        if (count_saved)
        {
            snprintf(count_line, sizeof(count_line), "Save count: %lu", (unsigned long)s_cal_save_count);
            ST7789_DrawString(8, 88, count_line, &Font8x12, GREEN, BLACK);
        }
        else
        {
            snprintf(count_line, sizeof(count_line), "Save count write failed");
            ST7789_DrawString(8, 88, count_line, &Font8x12, YELLOW, BLACK);
        }
    }
    else
    {
        char line[72];
        snprintf(line,
                 sizeof(line),
                 "FRAM:%s HAL:%ld @%02X",
                 CalFramResultText(MB85RC256_GetLastResult()),
                 (long)MB85RC256_GetLastHalStatus(),
                 (unsigned)MB85RC256_GetAddress7bit());
        ST7789_DrawString(8, 88, line, &Font8x12, YELLOW, BLACK);
        ST7789_DrawString(8, 104, "stage: READ / WRITE / VERIFY", &Font8x12, GRAY, BLACK);
    }
    ST7789_DrawString(8, 124, "Press encoder to continue", &Font8x12, WHITE, BLACK);

    {
        uint8_t wait_btn = 0;
        uint32_t wait_start = 0;

        while (1)
        {
            uint8_t pressed = EncButtonRaw();

            if (pressed && !wait_btn)
            {
                wait_start = HAL_GetTick();
                wait_btn = 1;
            }
            else if (!pressed && wait_btn)
            {
                if ((HAL_GetTick() - wait_start) >= CAL_DEBOUNCE_MS)
                {
                    HAL_Delay(CAL_DEBOUNCE_MS);
                    break;
                }
                wait_btn = 0;
            }

            HAL_Delay(5);
        }
    }

    Bridge_SetTickEnabled(1u);
}
