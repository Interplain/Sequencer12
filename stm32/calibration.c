#include "calibration.h"

#include <stdio.h>
#include "stm32f4xx_hal.h"
#include "st7789.h"
#include "fonts_extra.h"
#include "hw_init.h"
#include "platform/dac8564/dac8564.h"

#define CAL_MAGIC          0x53314341u
#define CAL_VERSION        2u
#define CAL_FLASH_ADDR     0x080E0000u
#define CAL_FLASH_SECTOR   FLASH_SECTOR_11

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
    const CalBlob* b = (const CalBlob*)CAL_FLASH_ADDR;
    if (b->magic != CAL_MAGIC) return 0;
    if (b->version != CAL_VERSION) return 0;
    if (b->checksum != CalChecksum(b)) return 0;

    neg1[0] = (uint16_t)(b->ch_a & 0xFFFFu);
    pos2[0] = (uint16_t)((b->ch_a >> 16) & 0xFFFFu);
    neg1[1] = (uint16_t)(b->ch_b & 0xFFFFu);
    pos2[1] = (uint16_t)((b->ch_b >> 16) & 0xFFFFu);
    neg1[2] = (uint16_t)(b->ch_c & 0xFFFFu);
    pos2[2] = (uint16_t)((b->ch_c >> 16) & 0xFFFFu);
    neg1[3] = (uint16_t)(b->ch_d & 0xFFFFu);
    pos2[3] = (uint16_t)((b->ch_d >> 16) & 0xFFFFu);

    for (uint8_t i = 0; i < 4; i++)
    {
        if (pos2[i] <= neg1[i]) return 0;
    }

    return 1;
}

static uint8_t CalWrite(const uint16_t neg1[4], const uint16_t pos2[4])
{
    FLASH_EraseInitTypeDef e;
    uint32_t err = 0;
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

    HAL_FLASH_Unlock();

    e.TypeErase = FLASH_TYPEERASE_SECTORS;
    e.Sector = CAL_FLASH_SECTOR;
    e.NbSectors = 1;
    e.VoltageRange = FLASH_VOLTAGE_RANGE_3;

    if (HAL_FLASHEx_Erase(&e, &err) != HAL_OK)
    {
        HAL_FLASH_Lock();
        return 0;
    }

    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, CAL_FLASH_ADDR + 0u, b.magic) != HAL_OK ||
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, CAL_FLASH_ADDR + 4u, b.version) != HAL_OK ||
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, CAL_FLASH_ADDR + 8u, b.ch_a) != HAL_OK ||
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, CAL_FLASH_ADDR + 12u, b.ch_b) != HAL_OK ||
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, CAL_FLASH_ADDR + 16u, b.ch_c) != HAL_OK ||
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, CAL_FLASH_ADDR + 20u, b.ch_d) != HAL_OK ||
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, CAL_FLASH_ADDR + 24u, b.checksum) != HAL_OK)
    {
        HAL_FLASH_Lock();
        return 0;
    }

    HAL_FLASH_Lock();
    return 1;
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
    int16_t now = (int16_t)TIM2->CNT;
    int16_t delta = (int16_t)(now - *last);

    if (delta >= 4)
    {
        *last = now;
        return 1;
    }
    if (delta <= -4)
    {
        *last = now;
        return -1;
    }
    return 0;
}

static void DrawCalScreen(const char* title, const char* hint, uint16_t code)
{
    char line[32];

    ST7789_Fill_Color(BLACK);
    ST7789_DrawString(8, 12, "Calibration", &Font16x24, WHITE, BLACK);
    ST7789_DrawString(8, 48, title, &Font12x20, CYAN, BLACK);
    ST7789_DrawString(8, 80, hint, &Font8x12, LIGHTBLUE, BLACK);

    snprintf(line, sizeof(line), "DAC code: %u", (unsigned)code);
    ST7789_DrawString(8, 110, line, &Font12x20, YELLOW, BLACK);

    ST7789_DrawString(8, 150, "Turn encoder", &Font8x12, WHITE, BLACK);
    ST7789_DrawString(8, 164, "Press to confirm", &Font8x12, WHITE, BLACK);
}

static uint16_t AdjustCodeStep(const char* title,
                               const char* hint,
                               uint16_t start_code,
                               Dac8564Channel channel)
{
    uint16_t code = start_code;
    int16_t last_enc = (int16_t)TIM2->CNT;
    uint8_t prev_btn = 0;
    uint8_t need_redraw = 1;

    while (1)
    {
        int8_t step = EncStep(&last_enc);
        if (step != 0)
        {
            int32_t v = (int32_t)code + (int32_t)step * 32;
            if (v < 0) v = 0;
            if (v > 65535) v = 65535;
            code = (uint16_t)v;
            DAC8564_SetChannelRaw(channel, code);
            need_redraw = 1;
        }

        if (need_redraw)
        {
            DrawCalScreen(title, hint, code);
            need_redraw = 0;
        }

        if (EncPressedEdge(&prev_btn))
        {
            while (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_12) == GPIO_PIN_RESET)
            {
                HAL_Delay(5);
            }
            return code;
        }

        HAL_Delay(5);
    }
}

uint8_t Calibration_ShouldEnterOnBoot(void)
{
    /* Encoder held at boot enters calibration mode */
    if (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_12) != GPIO_PIN_RESET) return 0;

    HAL_Delay(250);
    return (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_12) == GPIO_PIN_RESET) ? 1u : 0u;
}

void Calibration_ApplySaved(void)
{
    uint16_t neg1[4] = {0u, 0u, 0u, 0u};
    uint16_t pos2[4] = {26214u, 26214u, 26214u, 26214u};

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
    uint16_t neg1[4] = {0u, 0u, 0u, 0u};
    uint16_t pos2[4] = {26214u, 26214u, 26214u, 26214u};
    static const char* channel_names[4] = {"A", "B", "C", "D"};
    static const Dac8564Channel channels[4] = {DAC8564_CH_A, DAC8564_CH_B, DAC8564_CH_C, DAC8564_CH_D};
    uint8_t saved;
    char title[24];
    char hint[44];

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
    ST7789_DrawString(8, 12, "Calibration", &Font16x24, WHITE, BLACK);
    ST7789_DrawString(8, 58, saved ? "Saved to flash" : "Save failed", &Font12x20, saved ? GREEN : RED, BLACK);
    ST7789_DrawString(8, 88, "Press encoder to continue", &Font8x12, WHITE, BLACK);

    {
        uint8_t prev_btn = 0;
        while (!EncPressedEdge(&prev_btn))
        {
            HAL_Delay(5);
        }
        while (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_12) == GPIO_PIN_RESET)
        {
            HAL_Delay(5);
        }
    }
}
