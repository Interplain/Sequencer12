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
#define CAL_CODE_STEP            16u
#define CAL_MIRROR_ALL_CHANNELS  0u
#define CAL_DEFAULT_NEG1_CODE    32768u
#define CAL_DEFAULT_POS2_CODE    52428u

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

// Read calibration blob from FRAM and unpack
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

// Write calibration blob to FRAM with verification
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

static uint8_t EncPressedEdge(uint8_t* prev)
{
    uint8_t now = (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_12) == GPIO_PIN_RESET) ? 1u : 0u;
    uint8_t edge = (uint8_t)((*prev == 0u && now == 1u) ? 1u : 0u);
    *prev = now;
    return edge;
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

    /* Track every observed movement so reverse ticks are not lost by thresholding. */
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

    ST7789_DrawString(8, 150, "Turn encoder", &Font8x12, WHITE, BLACK);
    ST7789_DrawString(8, 164, "Press to confirm", &Font8x12, WHITE, BLACK);
}

static void DrawCalCode(uint16_t code, uint8_t force_clear)
{
    char line[32];

    if (force_clear)
    {
        ST7789_FillRect(8, 110, 220, 20, BLACK);
    }

    snprintf(line, sizeof(line), "DAC code: %u", (unsigned)code);
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
    uint8_t prev_btn = (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_12) == GPIO_PIN_RESET) ? 1u : 0u;
    uint16_t shown_code = (uint16_t)(code ^ 0xFFFFu);

    DrawCalStatic(title, hint);
    DrawCalCode(code, 1u);
    DrawCalEncoderState((uint16_t)TIM2->CNT, 0, 1u);
    DrawCalDacDebug(1u);
    shown_code = code;
    CalWriteOutput(channel, code);
    DrawCalDacDebug(1u);

    while (1)
    {
        KeepDisplayAlive();

        int8_t step = EncStep(&last_enc);
        if (step != 0)
        {
            int32_t v = (int32_t)code + (int32_t)step * (int32_t)CAL_CODE_STEP;
            if (v < 0) v = 0;
            if (v > 65535) v = 65535;
            code = (uint16_t)v;
            CalWriteOutput(channel, code);
            DrawCalEncoderState((uint16_t)TIM2->CNT, step, 1u);
            DrawCalDacDebug(1u);
        }

        if (code != shown_code)
        {
            DrawCalCode(code, 1u);
            shown_code = code;
        }

        if (EncPressedEdge(&prev_btn))
        {
            while (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_12) == GPIO_PIN_RESET)
            {
                KeepDisplayAlive();
                HAL_Delay(5);
            }
            return code;
        }

        HAL_Delay(5);
    }
}

uint8_t Calibration_ShouldEnterOnBoot(void)
{
    /* Encoder held at boot enters calibration mode.
     * Require a stable hold window to avoid false triggers during power ramp. */
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

    /* Diagnostic mode: show only a static screen so boot-entry rendering
     * can be validated in isolation before enabling wizard flow. */
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

    /* Ensure encoder counter is alive in calibration path even if prior mode touched TIM2. */
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
        uint8_t prev_btn = 0;
        while (!EncPressedEdge(&prev_btn))
        {
            KeepDisplayAlive();
            HAL_Delay(5);
        }
        while (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_12) == GPIO_PIN_RESET)
        {
            KeepDisplayAlive();
            HAL_Delay(5);
        }
    }
}
