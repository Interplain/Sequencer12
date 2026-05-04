#include "calibration.h"

#include <stdio.h>
#include "stm32f4xx_hal.h"
#include "st7789.h"
#include "fonts_extra.h"
#include "hw_init.h"
#include "platform/dac8564/dac8564.h"
#include "platform/fram/mb85rc256.h"
#include "platform/fram/fram_layout.h"

#define CAL_MAGIC          0x53314341u
#define CAL_VERSION        2u
#define CAL_BOOT_HOLD_START_MS   70u
#define CAL_BOOT_HOLD_SAMPLES    5u
#define CAL_BOOT_HOLD_STEP_MS    12u
#define CAL_CODE_STEP_FINE       1u
#define CAL_CODE_STEP_COARSE     64u
#define CAL_MIRROR_ALL_CHANNELS  0u
#define CAL_DEFAULT_NEG1_CODE    32768u
#define CAL_DEFAULT_POS2_CODE    52428u
#define CAL_DEBOUNCE_MS          80u

static inline void KeepDisplayAlive(void)
{
    GPIOA->BSRR = (1U << 9);
}


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

static uint32_t CalChecksum(const CalBlob* b)
{
    return b->magic ^ b->version ^ b->ch_a ^ b->ch_b ^ b->ch_c ^ b->ch_d ^ 0xA5A55A5Au;
}

static uint8_t CalRead(uint16_t neg1[4], uint16_t pos2[4])
{
    CalBlob b;
    if (!MB85RC256_Read(FRAM_CALIBRATION_ADDR, (uint8_t*)&b, sizeof(CalBlob))) return 0;
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
        if (pos2[i] <= neg1[i]) return 0;
    }

    return 1;
}

static uint8_t CalWrite(const uint16_t neg1[4], const uint16_t pos2[4])
{
    CalBlob b;
    for (uint8_t i = 0; i < 4; i++)
    {
        if (pos2[i] <= neg1[i]) return 0;
    }
    b.magic = CAL_MAGIC;
    b.version = CAL_VERSION;
    b.ch_a = ((uint32_t)pos2[0] << 16) | neg1[0];
    b.ch_b = ((uint32_t)pos2[1] << 16) | neg1[1];
    b.ch_c = ((uint32_t)pos2[2] << 16) | neg1[2];
    b.ch_d = ((uint32_t)pos2[3] << 16) | neg1[3];
    b.checksum = CalChecksum(&b);
    return MB85RC256_WriteAndVerify(FRAM_CALIBRATION_ADDR, (const uint8_t*)&b, sizeof(CalBlob));
}

static uint8_t EncButtonRaw(void)
{
    return (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_12) == GPIO_PIN_RESET) ? 1u : 0u;
}


static int8_t EncStep(int16_t* last)
{
    uint16_t now_u = (uint16_t)TIM2->CNT;
    uint16_t last_u = (uint16_t)(*last);
    int16_t delta = (int16_t)(now_u - last_u);

    if (delta == 0)
    {
        return 0;
    }

    *last = (int16_t)now_u;
    return (delta > 0) ? 1 : -1;
}

static void DrawCalStatic(const char* title, const char* hint)
{
    KeepDisplayAlive();
    ST7789_Fill_Color(BLACK);
    ST7789_DrawString(8, 12, "CALIBRATION", &Font16x24, WHITE, BLACK);
    ST7789_DrawString(8, 48, title, &Font12x20, CYAN, BLACK);
    ST7789_DrawString(8, 80, hint, &Font8x12, LIGHTBLUE, BLACK);

    ST7789_DrawString(8, 150, "Turn = fine adjust", &Font8x12, WHITE, BLACK);
    ST7789_DrawString(8, 164, "Hold+Turn = coarse", &Font8x12, WHITE, BLACK);
    ST7789_DrawString(8, 178, "Double-click = confirm", &Font8x12, WHITE, BLACK);
}

static void DrawCalCode(uint16_t code, uint16_t step, uint8_t force_clear)
{
    char line[40];

    if (force_clear)
    {
        ST7789_FillRect(8, 110, 220, 20, BLACK);
    }

    snprintf(line, sizeof(line), "DAC:%u  step:%u", (unsigned)code, (unsigned)step);
    ST7789_DrawString(8, 110, line, &Font12x20, YELLOW, BLACK);
}

static void DrawCalEncoderState(uint16_t cnt, int8_t step, uint8_t force_clear)
{
    char line[32];

    if (force_clear)
    {
        ST7789_FillRect(8, 132, 220, 14, BLACK);
    }

    snprintf(line, sizeof(line), "ENC:%5u d:%2d", (unsigned)cnt, (int)step);
    ST7789_DrawString(8, 132, line, &Font8x12, LIGHTBLUE, BLACK);
}

static void DrawCalDacDebug(uint8_t force_clear)
{
    char line[40];
    HAL_StatusTypeDef st = DAC8564_GetLastSpiStatus();
    uint32_t writes = DAC8564_GetWriteCount();

    if (force_clear)
    {
        ST7789_FillRect(8, 146, 220, 14, BLACK);
    }

    snprintf(line, sizeof(line), "SPI:%ld W:%lu TX0:%02X", (long)st, (unsigned long)writes, (unsigned)DAC8564_GetLastTx0());
    ST7789_DrawString(8, 146, line, &Font8x12, GRAY, BLACK);
}

static void CalWriteOutput(Dac8564Channel channel, uint16_t code)
{
#if CAL_MIRROR_ALL_CHANNELS
    (void)channel;
    DAC8564_SetAllRaw(code, code, code, code);
#else
    DAC8564_SetChannelRaw(channel, code);
#endif
}

static uint16_t AdjustCodeStep(const char* title,
                               const char* hint,
                               uint16_t start_code,
                               Dac8564Channel channel)
{
    uint16_t code = start_code;
    int16_t last_enc = (int16_t)TIM2->CNT;
    uint16_t shown_code = (uint16_t)(code ^ 0xFFFFu);
    uint16_t current_step = CAL_CODE_STEP_FINE;
    uint8_t prev_btn = 0;
    uint8_t click_count = 0;
    uint32_t first_click_time = 0;
    uint8_t encoder_moved_during_press = 0;

    DrawCalStatic(title, hint);
    DrawCalCode(code, current_step, 1u);
    DrawCalEncoderState((uint16_t)TIM2->CNT, 0, 1u);
    DrawCalDacDebug(1u);
    shown_code = code;
    CalWriteOutput(channel, code);
    DrawCalDacDebug(1u);

    while (1)
    {
        KeepDisplayAlive();

        uint8_t btn_now = EncButtonRaw();

        /* Choose step size: hold button + turn = coarse, turn alone = fine */
        current_step = btn_now ? CAL_CODE_STEP_COARSE : CAL_CODE_STEP_FINE;

        int8_t step = EncStep(&last_enc);
        if (step != 0)
        {
            int32_t v = (int32_t)code + (int32_t)step * (int32_t)current_step;
            if (v < 0) v = 0;
            if (v > 65535) v = 65535;
            code = (uint16_t)v;
            CalWriteOutput(channel, code);
            DrawCalEncoderState((uint16_t)TIM2->CNT, step, 1u);
            DrawCalDacDebug(1u);
            encoder_moved_during_press = 1;
        }

        if (code != shown_code)
        {
            DrawCalCode(code, current_step, 1u);
            shown_code = code;
        }

        /* Track button press for encoder movement detection */
        if (btn_now && !prev_btn)
        {
            /* Button just pressed — reset movement flag */
            encoder_moved_during_press = 0;
        }

        /* Detect rising edge (button released) for double-click */
        if (!btn_now && prev_btn)
        {
            /* Button just released — only count if encoder didn't move */
            if (!encoder_moved_during_press)
            {
                if (click_count == 0)
                {
                    click_count = 1;
                    first_click_time = HAL_GetTick();
                }
                else if (click_count == 1)
                {
                    /* Second click within window — confirm */
                    if ((HAL_GetTick() - first_click_time) < 500u)
                    {
                        HAL_Delay(CAL_DEBOUNCE_MS);
                        prev_btn = btn_now;
                        return code;
                    }
                    else
                    {
                        /* Too slow — treat as first click of new pair */
                        click_count = 1;
                        first_click_time = HAL_GetTick();
                    }
                }
            }
        }

        /* Reset click count if window expired */
        if (click_count == 1 && (HAL_GetTick() - first_click_time) >= 500u)
        {
            click_count = 0;
        }

        prev_btn = btn_now;
        HAL_Delay(5);
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

    ST7789_Fill_Color(BLACK);
    ST7789_DrawStringScaled(12, 56, "CALIBRATION", &Font16x24, 1, WHITE, BLACK);
    ST7789_DrawStringScaled(12, 98, "ENTRY TEST", &Font16x24, 1, CYAN, BLACK);
    ST7789_DrawStringScaled(12, 140, "HOLD OK", &Font16x24, 1, GREEN, BLACK);
    return 1u;
}

void Calibration_ApplySaved(void)
{
    uint16_t neg1[4] = {CAL_DEFAULT_NEG1_CODE, CAL_DEFAULT_NEG1_CODE, CAL_DEFAULT_NEG1_CODE, CAL_DEFAULT_NEG1_CODE};
    uint16_t pos2[4] = {CAL_DEFAULT_POS2_CODE, CAL_DEFAULT_POS2_CODE, CAL_DEFAULT_POS2_CODE, CAL_DEFAULT_POS2_CODE};

    if (CalRead(neg1, pos2))
    {
        DAC8564_SetPitchCalibrationForChannel(DAC8564_CH_A, neg1[0], pos2[0]);
        DAC8564_SetPitchCalibrationForChannel(DAC8564_CH_B, neg1[1], pos2[1]);
        DAC8564_SetPitchCalibrationForChannel(DAC8564_CH_C, neg1[2], pos2[2]);
        DAC8564_SetPitchCalibrationForChannel(DAC8564_CH_D, neg1[3], pos2[3]);
    }
}

void Calibration_RunWizard(void)
{
    uint16_t neg1[4] = {CAL_DEFAULT_NEG1_CODE, CAL_DEFAULT_NEG1_CODE, CAL_DEFAULT_NEG1_CODE, CAL_DEFAULT_NEG1_CODE};
    uint16_t pos2[4] = {CAL_DEFAULT_POS2_CODE, CAL_DEFAULT_POS2_CODE, CAL_DEFAULT_POS2_CODE, CAL_DEFAULT_POS2_CODE};
    static const char* channel_names[4] = {"A", "B", "C", "D"};
    static const Dac8564Channel channels[4] = {DAC8564_CH_A, DAC8564_CH_B, DAC8564_CH_C, DAC8564_CH_D};
    uint8_t saved;
    char title[24];
    char hint[44];

    HAL_TIM_Encoder_Stop(&htim2, TIM_CHANNEL_ALL);
    __HAL_TIM_SET_COUNTER(&htim2, 0);
    HAL_TIM_Encoder_Start(&htim2, TIM_CHANNEL_ALL);

    CalRead(neg1, pos2);

    for (uint8_t ch = 0; ch < 4; ch++)
    {
        snprintf(title, sizeof(title), "CH %s: set -1.00V", channel_names[ch]);
        snprintf(hint, sizeof(hint), "Adjust output %s to -1.00V", channel_names[ch]);
        neg1[ch] = AdjustCodeStep(title, hint, neg1[ch], channels[ch]);

        snprintf(title, sizeof(title), "CH %s: set +2.00V", channel_names[ch]);
        snprintf(hint, sizeof(hint), "Adjust output %s to +2.00V", channel_names[ch]);
        pos2[ch] = AdjustCodeStep(title, hint, pos2[ch], channels[ch]);

        if (pos2[ch] <= neg1[ch])
        {
            pos2[ch] = (uint16_t)(neg1[ch] + 1u);
        }

        DAC8564_SetPitchCalibrationForChannel(channels[ch], neg1[ch], pos2[ch]);
    }

    saved = CalWrite(neg1, pos2);

    ST7789_Fill_Color(BLACK);
    ST7789_DrawString(8, 12, "CALIBRATION", &Font16x24, WHITE, BLACK);
    ST7789_DrawString(8, 58, saved ? "Saved to FRAM" : "Save failed", &Font12x20, saved ? GREEN : RED, BLACK);
    ST7789_DrawString(8, 88, "Press encoder to continue", &Font8x12, WHITE, BLACK);

    {
        uint8_t wait_btn = 0;
        uint32_t wait_start = 0;

        while (1)
        {
            KeepDisplayAlive();
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
}
