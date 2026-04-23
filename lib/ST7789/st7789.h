#ifndef __ST7789_H
#define __ST7789_H

#include "stm32f4xx_hal.h"
#include "fonts.h"

#ifdef __cplusplus
extern "C" {
#endif

extern SPI_HandleTypeDef hspi1;

#define LCD_CTRL_ST7789  1
#define LCD_CTRL_ILI9341 2
#define LCD_CTRL_ILI9488 3

#ifndef LCD_CONTROLLER
#define LCD_CONTROLLER LCD_CTRL_ST7789  /* GMT020-02 ST7789VW */
#endif

/* GMT020-01 ST7789VW 2.0" normally-black landscape module.
 * Native geometry is 240x320.
 * SPI stable at 10.5 MHz (APB2/16) — higher speeds corrupt pixels over wires.
 * Rotation 1 = logical 320x240 landscape. */
#define ST7789_NATIVE_WIDTH   240
#define ST7789_NATIVE_HEIGHT  320
#define ST7789_ROTATION_DEFAULT 0
#define ST7789_X_SHIFT_DEFAULT 0
#define ST7789_Y_SHIFT_DEFAULT 0

#define RGB565(r,g,b)  ((uint16_t)((((r) & 0x1FU) << 11) | (((g) & 0x3FU) << 5) | ((b) & 0x1FU)))

/*
 * Rotation:
 *   0 = default
 *   1 = rotate 90 degrees
 *   2 = rotate 180 degrees
 *   3 = rotate 270 degrees
 */
#ifndef ST7789_ROTATION
#define ST7789_ROTATION ST7789_ROTATION_DEFAULT
#endif

/* Logical framebuffer size after rotation */
#if (((ST7789_ROTATION) & 1) == 0)
#define ST7789_WIDTH   ST7789_NATIVE_WIDTH
#define ST7789_HEIGHT  ST7789_NATIVE_HEIGHT
#else
#define ST7789_WIDTH   ST7789_NATIVE_HEIGHT
#define ST7789_HEIGHT  ST7789_NATIVE_WIDTH
#endif

/* GRAM crop shift for panel variants/tab versions */
#ifndef X_SHIFT
#define X_SHIFT ST7789_X_SHIFT_DEFAULT
#endif

#ifndef Y_SHIFT
#define Y_SHIFT ST7789_Y_SHIFT_DEFAULT
#endif

/* Display SPI control pin mapping (MCU pin 26 = PB0 on STM32F405RG LQFP64). */
#ifndef LCD_CS_PORT
#define LCD_CS_PORT GPIOB
#endif
#ifndef LCD_CS_PIN
#define LCD_CS_PIN  GPIO_PIN_0
#endif

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
#define ST7789_INVOFF    0x20
#define ST7789_INVON     0x21
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

/* Set to 1 if panel expects BGR byte order in MADCTL, 0 for RGB. */
#ifndef ST7789_COLOR_ORDER_BGR
#define ST7789_COLOR_ORDER_BGR 0
#endif

#define ST7789_COLOR_MODE_16BIT  0x55
#define ST7789_COLOR_MODE_18BIT  0x66

/* ST7789 uses 16-bit color over SPI. */
#ifndef LCD_USE_18BIT_COLOR
#define LCD_USE_18BIT_COLOR 0
#endif

/* Set to 0 for debugging unstable/noisy panels by bypassing DMA writes. */
#ifndef ST7789_USE_DMA
#define ST7789_USE_DMA 0
#endif

/* Set to 1 when panel CS is tied active and should not be toggled per transfer. */
#ifndef ST7789_CS_ALWAYS_ACTIVE
#define ST7789_CS_ALWAYS_ACTIVE 0
#endif

/* Set to 1 for active-low CS (standard), 0 for active-high CS wiring. */
#ifndef ST7789_CS_ACTIVE_LOW
#define ST7789_CS_ACTIVE_LOW 1
#endif

/* 0 = normal colours, 1 = inverted colours. */
#ifndef ST7789_INVERT_DEFAULT
#define ST7789_INVERT_DEFAULT 0
#endif

/* Use a minimal init sequence for the active 240x320 panel. */
#ifndef ST7789_MINIMAL_INIT
#define ST7789_MINIMAL_INIT 1
#endif

void ST7789_Init(void);
void ST7789_DisplayOn(void);
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