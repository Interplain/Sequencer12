#ifndef __ST7789_H
#define __ST7789_H

#include "stm32f4xx_hal.h"
#include "fonts.h"

#ifdef __cplusplus
extern "C" {
#endif

extern SPI_HandleTypeDef hspi1;

#define ST7789_WIDTH   240
#define ST7789_HEIGHT  240

/*
 * Rotation:
 *   0 = default
 *   1 = rotate 90°
 *   2 = rotate 180°
 *   3 = rotate 270°
 *
 * Set to 1 so your transport row ends up on the physical right side
 * when the display is mounted the other way on the PCB.
 *
 * If it lands on the left instead of the right, change this to 3.
 */
#ifndef ST7789_ROTATION
#define ST7789_ROTATION 2
#endif

/* 240x240 panel: no crop shift needed */
#define X_SHIFT 0
#define Y_SHIFT 0

/* RGB565 colours */
#define WHITE       0xFFFFU
#define BLACK       0x0000U
#define BLUE        0x001FU
#define RED         0xF800U
#define MAGENTA     0xF81FU
#define GREEN       0x07E0U
#define CYAN        0x07FFU
#define YELLOW      0xFFE0U
#define GRAY        0x8430U
#define DARKBLUE    0x01CFU
#define LIGHTBLUE   0x7D7CU
#define LIGHTGREEN  0x87F0U
#define LGRAY       0xC618U
#define ORANGE      0xFD20U
#define BROWN       0xBC40U

/* ST7789 commands */
#define ST7789_NOP       0x00
#define ST7789_SWRESET   0x01
#define ST7789_SLPIN     0x10
#define ST7789_SLPOUT    0x11
#define ST7789_NORON     0x13
#define ST7789_INVOFF    0x21
#define ST7789_INVON     0x20
#define ST7789_DISPOFF   0x28
#define ST7789_DISPON    0x29
#define ST7789_CASET     0x2A
#define ST7789_RASET     0x2B
#define ST7789_RAMWR     0x2C
#define ST7789_MADCTL    0x36
#define ST7789_COLMOD    0x3A

/* MADCTL bits */
#define ST7789_MADCTL_MY   0x80
#define ST7789_MADCTL_MX   0x40
#define ST7789_MADCTL_MV   0x20
#define ST7789_MADCTL_RGB  0x00
#define ST7789_MADCTL_BGR  0x08

#define ST7789_COLOR_MODE_16BIT  0x55

void ST7789_Init(void);
void ST7789_SetRotation(uint8_t rotation);
void ST7789_InvertColors(uint8_t invert);

void ST7789_Fill_Color(uint16_t color);
void ST7789_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void ST7789_DrawPixel(uint16_t x, uint16_t y, uint16_t color);

void ST7789_DrawChar(uint16_t x, uint16_t y, char c,
                     const Font_t *font, uint16_t fg, uint16_t bg);
void ST7789_DrawString(uint16_t x, uint16_t y, const char *str,
                       const Font_t *font, uint16_t fg, uint16_t bg);

/* New: integer scaling using your existing fonts */
void ST7789_DrawCharScaled(uint16_t x, uint16_t y, char c,
                           const Font_t *font, uint8_t scale,
                           uint16_t fg, uint16_t bg);
void ST7789_DrawStringScaled(uint16_t x, uint16_t y, const char *str,
                             const Font_t *font, uint8_t scale,
                             uint16_t fg, uint16_t bg);

#ifdef __cplusplus
}
#endif

#endif