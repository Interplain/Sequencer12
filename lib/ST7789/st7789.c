#include "st7789.h"
#include "fonts.h"
#include "fonts_extra.h"
/* ------------------------------------------------------------------------- */
/* Pin map                                                                   */
/* PB0=CS  PA5=SCK  PA6=DC  PA7=MOSI  PA9=RST                                 */
/* ------------------------------------------------------------------------- */
#if ST7789_CS_ALWAYS_ACTIVE
#if ST7789_CS_ACTIVE_LOW
#define CS_ASSERT()   HAL_GPIO_WritePin(LCD_CS_PORT, LCD_CS_PIN, GPIO_PIN_RESET)
#else
#define CS_ASSERT()   HAL_GPIO_WritePin(LCD_CS_PORT, LCD_CS_PIN, GPIO_PIN_SET)
#endif
#define CS_RELEASE()  ((void)0)
#else
#if ST7789_CS_ACTIVE_LOW
#define CS_ASSERT()   HAL_GPIO_WritePin(LCD_CS_PORT, LCD_CS_PIN, GPIO_PIN_RESET)
#define CS_RELEASE()  HAL_GPIO_WritePin(LCD_CS_PORT, LCD_CS_PIN, GPIO_PIN_SET)
#else
#define CS_ASSERT()   HAL_GPIO_WritePin(LCD_CS_PORT, LCD_CS_PIN, GPIO_PIN_SET)
#define CS_RELEASE()  HAL_GPIO_WritePin(LCD_CS_PORT, LCD_CS_PIN, GPIO_PIN_RESET)
#endif
#endif
#define DC_LOW()   (GPIOA->BSRR = (1U << (6 + 16)))
#define DC_HIGH()  (GPIOA->BSRR = (1U << 6))
#define RST_LOW()  (GPIOA->BSRR = (1U << (9 + 16)))
#define RST_HIGH() (GPIOA->BSRR = (1U << 9))

#if LCD_USE_18BIT_COLOR
static uint8_t __attribute__((aligned(4))) lineBuf[ST7789_WIDTH * 3];
#else
static uint8_t __attribute__((aligned(4))) lineBuf[ST7789_WIDTH * 2];
#endif

/* ------------------------------------------------------------------------- */
/* Low level write helpers                                                   */
/* ------------------------------------------------------------------------- */
static void WriteCommandData(uint8_t cmd, const uint8_t *buf, uint16_t len)
{
    CS_ASSERT();
    DC_LOW();
    HAL_SPI_Transmit(&hspi1, &cmd, 1, HAL_MAX_DELAY);
    if (buf != 0 && len > 0U) {
        DC_HIGH();
        HAL_SPI_Transmit(&hspi1, (uint8_t *)buf, len, HAL_MAX_DELAY);
    }
    CS_RELEASE();
}

static void WriteCommand(uint8_t cmd)
{
    WriteCommandData(cmd, 0, 0);
}

static void WriteData(const uint8_t *buf, uint16_t len)
{
    CS_ASSERT();
    DC_HIGH();
    HAL_SPI_Transmit(&hspi1, (uint8_t *)buf, len, HAL_MAX_DELAY);
    CS_RELEASE();
}

static void WriteSmallData(uint8_t data)
{
    WriteData(&data, 1);
}

static void SetAddressWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    x0 += X_SHIFT;
    x1 += X_SHIFT;
    y0 += Y_SHIFT;
    y1 += Y_SHIFT;

    {
        uint8_t d[4] = {
            (uint8_t)(x0 >> 8), (uint8_t)(x0 & 0xFF),
            (uint8_t)(x1 >> 8), (uint8_t)(x1 & 0xFF)
        };
        WriteCommandData(ST7789_CASET, d, 4);
    }

    {
        uint8_t d[4] = {
            (uint8_t)(y0 >> 8), (uint8_t)(y0 & 0xFF),
            (uint8_t)(y1 >> 8), (uint8_t)(y1 & 0xFF)
        };
        WriteCommandData(ST7789_RASET, d, 4);
    }

    WriteCommand(ST7789_RAMWR);
}

/* ------------------------------------------------------------------------- */
/* DMA send: SPI1 TX -> DMA2 Stream3 Channel3                                */
/* ------------------------------------------------------------------------- */
static void dma_send(uint8_t *buf, uint16_t len)
{
#if ST7789_USE_DMA
    /* DMA only worthwhile for larger transfers; use blocking SPI for small ones */
    if (len >= 64U) {
        while (HAL_SPI_GetState(&hspi1) != HAL_SPI_STATE_READY) {
        }
        if (HAL_SPI_Transmit_DMA(&hspi1, buf, len) == HAL_OK) {
            while (HAL_SPI_GetState(&hspi1) != HAL_SPI_STATE_READY) {
            }
            /* Wait for shift register to fully drain before caller releases CS */
            while (__HAL_SPI_GET_FLAG(&hspi1, SPI_FLAG_BSY)) {
            }
            return;
        }
    }
#endif
    HAL_SPI_Transmit(&hspi1, buf, len, HAL_MAX_DELAY);
}

/* ------------------------------------------------------------------------- */
/* Init                                                                      */
/* ------------------------------------------------------------------------- */
void ST7789_Init(void)
{
#if ST7789_CS_ALWAYS_ACTIVE
    /* Keep panel selected when CS is wired and configured as always-active. */
    CS_ASSERT();
#endif

    /* Hardware reset */
    RST_HIGH();
    HAL_Delay(10);
    RST_LOW();
    HAL_Delay(10);
    RST_HIGH();
    HAL_Delay(120);

    /* Software reset then sleep out */
    WriteCommand(ST7789_SWRESET);
    HAL_Delay(150);

    WriteCommand(ST7789_SLPOUT);
    HAL_Delay(120);

    /* Pixel format: 16-bit RGB565 over SPI */
#if LCD_USE_18BIT_COLOR
    {
        uint8_t d[] = {ST7789_COLOR_MODE_18BIT};
        WriteCommandData(ST7789_COLMOD, d, sizeof(d));
    }
#else
    {
        uint8_t d[] = {ST7789_COLOR_MODE_16BIT};
        WriteCommandData(ST7789_COLMOD, d, sizeof(d));
    }
#endif

    /* Porch setting */
    WriteCommand(0xB2);
    {
        uint8_t d[] = {0x0C, 0x0C, 0x00, 0x33, 0x33};
        WriteData(d, sizeof(d));
    }

    /* Gate control */
    {
        uint8_t d[] = {0x35};
        WriteCommandData(0xB7, d, sizeof(d));
    }

    /* VCOM setting */
    {
        uint8_t d[] = {0x19};
        WriteCommandData(0xBB, d, sizeof(d));
    }

    /* LCM control */
    {
        uint8_t d[] = {0x2C};
        WriteCommandData(0xC0, d, sizeof(d));
    }

    /* VDV and VRH command enable */
    {
        uint8_t d[] = {0x01};
        WriteCommandData(0xC2, d, sizeof(d));
    }

    /* VRH set */
    {
        uint8_t d[] = {0x12};
        WriteCommandData(0xC3, d, sizeof(d));
    }

    /* VDV set */
    {
        uint8_t d[] = {0x20};
        WriteCommandData(0xC4, d, sizeof(d));
    }

    /* Frame rate: 60 Hz */
    {
        uint8_t d[] = {0x0F};
        WriteCommandData(0xC6, d, sizeof(d));
    }

    /* Power control */
    WriteCommand(0xD0);
    {
        uint8_t d[] = {0xA4, 0xA1};
        WriteData(d, sizeof(d));
    }

    /* Positive voltage gamma */
    WriteCommand(0xE0);
    {
        uint8_t d[] = {
            0xD0, 0x04, 0x0D, 0x11, 0x13, 0x2B, 0x3F,
            0x54, 0x4C, 0x18, 0x0D, 0x0B, 0x1F, 0x23
        };
        WriteData(d, sizeof(d));
    }

    /* Negative voltage gamma */
    WriteCommand(0xE1);
    {
        uint8_t d[] = {
            0xD0, 0x04, 0x0C, 0x11, 0x13, 0x2C, 0x3F,
            0x44, 0x51, 0x2F, 0x1F, 0x1F, 0x20, 0x23
        };
        WriteData(d, sizeof(d));
    }

    WriteCommand(ST7789_NORON);
    HAL_Delay(10);

    WriteCommand(ST7789_INVERT_DEFAULT ? ST7789_INVON : ST7789_INVOFF);

    ST7789_SetRotation(ST7789_ROTATION);

        /* DISPON is deferred — caller fills GRAM first, then calls ST7789_DisplayOn() */
    }
/* Rotation / inversion                                                      */
/* ------------------------------------------------------------------------- */

void ST7789_DisplayOn(void)
{
    WriteCommand(ST7789_DISPON);
    HAL_Delay(20);
}

void ST7789_SetRotation(uint8_t rotation)
{
    const uint8_t colorOrder = ST7789_COLOR_ORDER_BGR ? ST7789_MADCTL_BGR : ST7789_MADCTL_RGB;

    WriteCommand(ST7789_MADCTL);

    switch (rotation & 3U) {
        case 0:
            WriteSmallData(ST7789_MADCTL_MX | ST7789_MADCTL_MY | colorOrder);
            break;
        case 1:
            WriteSmallData(ST7789_MADCTL_MX | ST7789_MADCTL_MV | colorOrder);
            break;
        case 2:
            WriteSmallData(colorOrder);
            break;
        case 3:
            WriteSmallData(ST7789_MADCTL_MY | ST7789_MADCTL_MV | colorOrder);
            break;
        default:
            WriteSmallData(colorOrder);
            break;
    }
}

void ST7789_InvertColors(uint8_t invert)
{
    WriteCommand(invert ? ST7789_INVON : ST7789_INVOFF);
}

/* ------------------------------------------------------------------------- */
/* Primitives                                                                */
/* ------------------------------------------------------------------------- */
void ST7789_Fill_Color(uint16_t color)
{
#if LCD_USE_18BIT_COLOR
    uint8_t r = (uint8_t)(((color >> 11) & 0x1F) << 3);
    uint8_t g = (uint8_t)(((color >>  5) & 0x3F) << 2);
    uint8_t b = (uint8_t)(((color      ) & 0x1F) << 3);
    for (uint16_t i = 0; i < ST7789_WIDTH; i++) {
        lineBuf[i * 3U]     = r;
        lineBuf[i * 3U + 1] = g;
        lineBuf[i * 3U + 2] = b;
    }
#else
    uint8_t hi = (uint8_t)(color >> 8);
    uint8_t lo = (uint8_t)(color & 0xFF);
    for (uint16_t i = 0; i < ST7789_WIDTH; i++) {
        lineBuf[i * 2U]     = hi;
        lineBuf[i * 2U + 1] = lo;
    }
#endif
    SetAddressWindow(0, 0, ST7789_WIDTH - 1, ST7789_HEIGHT - 1);
    CS_ASSERT(); DC_HIGH();  /* HIGH = data */
    for (uint16_t line = 0; line < ST7789_HEIGHT; line++)
        dma_send(lineBuf, sizeof(lineBuf));
    CS_RELEASE();
}

void ST7789_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    if (x >= ST7789_WIDTH || y >= ST7789_HEIGHT) return;
    if (w == 0 || h == 0) return;
    if ((x + w) > ST7789_WIDTH)  w = ST7789_WIDTH  - x;
    if ((y + h) > ST7789_HEIGHT) h = ST7789_HEIGHT - y;

    uint8_t hi = (uint8_t)(color >> 8);
    uint8_t lo = (uint8_t)(color & 0xFF);
    for (uint16_t i = 0; i < w; i++) {
        lineBuf[i * 2U]     = hi;
        lineBuf[i * 2U + 1] = lo;
    }
    SetAddressWindow(x, y, x + w - 1, y + h - 1);
    CS_ASSERT(); DC_HIGH();  /* HIGH = data */
    for (uint16_t row = 0; row < h; row++)
        dma_send(lineBuf, (uint16_t)(w * 2U));
    CS_RELEASE();
}

void ST7789_DrawPixel(uint16_t x, uint16_t y, uint16_t color)
{
    if (x >= ST7789_WIDTH || y >= ST7789_HEIGHT) return;
    SetAddressWindow(x, y, x, y);
    CS_ASSERT();
    DC_HIGH();
#if LCD_USE_18BIT_COLOR
    uint8_t d[3] = {
        (uint8_t)(((color >> 11) & 0x1F) << 3),
        (uint8_t)(((color >>  5) & 0x3F) << 2),
        (uint8_t)(((color      ) & 0x1F) << 3)
    };
    HAL_SPI_Transmit(&hspi1, d, 3, HAL_MAX_DELAY);
#else
    uint8_t d[2] = { (uint8_t)(color >> 8), (uint8_t)(color & 0xFF) };
    HAL_SPI_Transmit(&hspi1, d, 2, HAL_MAX_DELAY);
#endif
    CS_RELEASE();
}

/* ------------------------------------------------------------------------- */
/* Text rendering                                                            */
/* ------------------------------------------------------------------------- */
void ST7789_DrawChar(uint16_t x, uint16_t y, char c,
                     const Font_t *font, uint16_t fg, uint16_t bg)
{
    ST7789_DrawCharScaled(x, y, c, font, 1, fg, bg);
}

void ST7789_DrawString(uint16_t x, uint16_t y, const char *str,
                       const Font_t *font, uint16_t fg, uint16_t bg)
{
    ST7789_DrawStringScaled(x, y, str, font, 1, fg, bg);
}

void ST7789_DrawCharScaled(uint16_t x, uint16_t y, char c,
                           const Font_t *font, uint8_t scale,
                           uint16_t fg, uint16_t bg)
{
    if (font == 0 || scale == 0) return;

    if ((uint8_t)c < font->first || (uint8_t)c > font->last) {
        c = ' ';
    }

    const uint16_t srcW = font->width;
    const uint16_t srcH = font->height;
    const uint16_t dstW = (uint16_t)(srcW * scale);
    const uint16_t dstH = (uint16_t)(srcH * scale);

    if (x >= ST7789_WIDTH || y >= ST7789_HEIGHT) return;
    if ((x + dstW) > ST7789_WIDTH)  return;
    if ((y + dstH) > ST7789_HEIGHT) return;

    const uint8_t bytesPerRow = (uint8_t)((srcW + 7U) / 8U);
    const uint8_t *charData   = font->data + (((uint8_t)c - font->first) * srcH * bytesPerRow);

    const uint8_t fg_hi = (uint8_t)(fg >> 8);
    const uint8_t fg_lo = (uint8_t)(fg & 0xFF);
    const uint8_t bg_hi = (uint8_t)(bg >> 8);
    const uint8_t bg_lo = (uint8_t)(bg & 0xFF);
#if LCD_USE_18BIT_COLOR
    const uint8_t fg_r = (uint8_t)(((fg >> 11) & 0x1F) << 3);
    const uint8_t fg_g = (uint8_t)(((fg >>  5) & 0x3F) << 2);
    const uint8_t fg_b = (uint8_t)(((fg      ) & 0x1F) << 3);
    const uint8_t bg_r = (uint8_t)(((bg >> 11) & 0x1F) << 3);
    const uint8_t bg_g = (uint8_t)(((bg >>  5) & 0x3F) << 2);
    const uint8_t bg_b = (uint8_t)(((bg      ) & 0x1F) << 3);
#endif

    SetAddressWindow(x, y, x + dstW - 1, y + dstH - 1);

    CS_ASSERT();
    DC_HIGH();

    for (uint16_t srcRow = 0; srcRow < srcH; srcRow++) {
        for (uint8_t sy = 0; sy < scale; sy++) {

            uint16_t out = 0;

            for (uint16_t srcCol = 0; srcCol < srcW; srcCol++) {
                const uint8_t byteIndex = (uint8_t)(srcCol / 8U);
                const uint8_t bitMask   = (uint8_t)(0x80U >> (srcCol % 8U));
                const uint8_t pixelOn   = (charData[(srcRow * bytesPerRow) + byteIndex] & bitMask) ? 1U : 0U;

                for (uint8_t sx = 0; sx < scale; sx++) {
#if LCD_USE_18BIT_COLOR
                    lineBuf[out++] = pixelOn ? fg_r : bg_r;
                    lineBuf[out++] = pixelOn ? fg_g : bg_g;
                    lineBuf[out++] = pixelOn ? fg_b : bg_b;
#else
                    lineBuf[out++] = pixelOn ? fg_hi : bg_hi;
                    lineBuf[out++] = pixelOn ? fg_lo : bg_lo;
#endif
                }
            }

#if LCD_USE_18BIT_COLOR
            dma_send(lineBuf, (uint16_t)(dstW * 3U));
#else
            dma_send(lineBuf, (uint16_t)(dstW * 2U));
#endif
        }
    }

    CS_RELEASE();
}

void ST7789_DrawStringScaled(uint16_t x, uint16_t y, const char *str,
                             const Font_t *font, uint8_t scale,
                             uint16_t fg, uint16_t bg)
{
    if (font == 0 || str == 0 || scale == 0) return;

    const uint16_t stepX = (uint16_t)(font->width * scale);
    const uint16_t stepY = (uint16_t)(font->height * scale);

    while (*str) {
        if ((x + stepX) > ST7789_WIDTH) {
            x  = 0;
            y += stepY;
        }

        if ((y + stepY) > ST7789_HEIGHT) {
            break;
        }

        ST7789_DrawCharScaled(x, y, *str, font, scale, fg, bg);
        x = (uint16_t)(x + stepX);
        str++;
    }
}