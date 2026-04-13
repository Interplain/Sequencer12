#include "st7789.h"
#include "fonts.h"
#include "fonts_extra.h"
/* ------------------------------------------------------------------------- */
/* Pin map                                                                   */
/* PA4=CS  PA5=SCK  PA6=DC  PA7=MOSI  PA8=RST  PB0=BLK                       */
/* ------------------------------------------------------------------------- */
#define CS_LOW()   (GPIOA->BSRR = (1U << (4 + 16)))
#define CS_HIGH()  (GPIOA->BSRR = (1U << 4))
#define DC_LOW()   (GPIOA->BSRR = (1U << (6 + 16)))
#define DC_HIGH()  (GPIOA->BSRR = (1U << 6))
#define RST_LOW()  (GPIOA->BSRR = (1U << (8 + 16)))
#define RST_HIGH() (GPIOA->BSRR = (1U << 8))

/* One scanline buffer, 4-byte aligned for DMA */
static uint8_t __attribute__((aligned(4))) lineBuf[ST7789_WIDTH * 2];

/* ------------------------------------------------------------------------- */
/* Low level write helpers                                                   */
/* ------------------------------------------------------------------------- */
static void WriteCommand(uint8_t cmd)
{
    CS_LOW();
    DC_LOW();
    HAL_SPI_Transmit(&hspi1, &cmd, 1, HAL_MAX_DELAY);
    CS_HIGH();
}

static void WriteData(const uint8_t *buf, uint16_t len)
{
    CS_LOW();
    DC_HIGH();
    HAL_SPI_Transmit(&hspi1, (uint8_t *)buf, len, HAL_MAX_DELAY);
    CS_HIGH();
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

    WriteCommand(ST7789_CASET);
    {
        uint8_t d[4] = {
            (uint8_t)(x0 >> 8), (uint8_t)(x0 & 0xFF),
            (uint8_t)(x1 >> 8), (uint8_t)(x1 & 0xFF)
        };
        WriteData(d, 4);
    }

    WriteCommand(ST7789_RASET);
    {
        uint8_t d[4] = {
            (uint8_t)(y0 >> 8), (uint8_t)(y0 & 0xFF),
            (uint8_t)(y1 >> 8), (uint8_t)(y1 & 0xFF)
        };
        WriteData(d, 4);
    }

    WriteCommand(ST7789_RAMWR);
}

/* ------------------------------------------------------------------------- */
/* DMA send: SPI1 TX -> DMA2 Stream3 Channel3                                */
/* ------------------------------------------------------------------------- */
static void dma_send(uint8_t *buf, uint16_t len)
{
    /* Disable stream before reconfiguring */
    DMA2_Stream3->CR &= ~DMA_SxCR_EN;
    while (DMA2_Stream3->CR & DMA_SxCR_EN) {
    }

    /* Clear all stream3 low interrupt flags before enable */
    DMA2->LIFCR = (0x3FU << 22U);

    DMA2_Stream3->PAR  = (uint32_t)&SPI1->DR;
    DMA2_Stream3->M0AR = (uint32_t)buf;
    DMA2_Stream3->NDTR = len;
    DMA2_Stream3->FCR  = 0;

    DMA2_Stream3->CR =
          (3U << 25U)     /* CHSEL = Channel 3 */
        | (0U << 16U)     /* MSIZE = 8-bit */
        | (0U << 11U)     /* PSIZE = 8-bit */
        | DMA_SxCR_MINC
        | DMA_SxCR_DIR_0; /* mem -> periph */

    SPI1->CR2 |= SPI_CR2_TXDMAEN;
    DMA2_Stream3->CR |= DMA_SxCR_EN;

    while (!(DMA2->LISR & DMA_LISR_TCIF3)) {
    }

    DMA2->LIFCR = (0x3FU << 22U);

    while (SPI1->SR & SPI_SR_BSY) {
    }

    SPI1->CR2 &= ~SPI_CR2_TXDMAEN;

    /* clear possible overrun state */
    (void)SPI1->DR;
    (void)SPI1->SR;
}

/* ------------------------------------------------------------------------- */
/* Init                                                                      */
/* ------------------------------------------------------------------------- */
void ST7789_Init(void)
{
   
    RST_HIGH();
    HAL_Delay(20);
    RST_LOW();
    HAL_Delay(20);
    RST_HIGH();
    HAL_Delay(120);

    WriteCommand(ST7789_COLMOD);
    WriteSmallData(ST7789_COLOR_MODE_16BIT);

    WriteCommand(0xB2);
    {
        uint8_t d[] = {0x0C, 0x0C, 0x00, 0x33, 0x33};
        WriteData(d, sizeof(d));
    }

    WriteCommand(0x36);
    WriteSmallData(0x00);

    WriteCommand(0xB7);
    WriteSmallData(0x35);

    WriteCommand(0xBB);
    WriteSmallData(0x19);

    WriteCommand(0xC0);
    WriteSmallData(0x2C);

    WriteCommand(0xC2);
    WriteSmallData(0x01);

    WriteCommand(0xC3);
    WriteSmallData(0x12);

    WriteCommand(0xC4);
    WriteSmallData(0x20);

    WriteCommand(0xC6);
    WriteSmallData(0x0F);

    WriteCommand(0xD0);
    WriteSmallData(0xA4);
    WriteSmallData(0xA1);


    WriteCommand(0xE0);
    {
        uint8_t d[] = {
            0xD0, 0x04, 0x0D, 0x11, 0x13, 0x2B, 0x3F,
            0x54, 0x4C, 0x18, 0x0D, 0x0B, 0x1F, 0x23
        };
        WriteData(d, sizeof(d));
    }

    WriteCommand(0xE1);
    {
        uint8_t d[] = {
            0xD0, 0x04, 0x0C, 0x11, 0x13, 0x2C, 0x3F,
            0x44, 0x51, 0x2F, 0x1F, 0x1F, 0x20, 0x23
        };
        WriteData(d, sizeof(d));
    } 


    WriteCommand(ST7789_SLPOUT);
    HAL_Delay(120);          // panel needs this full 120ms to stabilise

    WriteCommand(ST7789_NORON);   
    HAL_Delay(10);

    ST7789_SetRotation(ST7789_ROTATION);  // MADCTL before display on

    WriteCommand(ST7789_INVOFF);  // inversion state set before display on
   
    WriteCommand(ST7789_DISPON);  // NOW turn the display on, everything already configured
    HAL_Delay(20);  
}

/* ------------------------------------------------------------------------- */
/* Rotation / inversion                                                      */
/* ------------------------------------------------------------------------- */
/* Pick which MADCTL variant rotation=1 should use */
#define ROT1_MODE  2
/* 0 = MV
   1 = MX | MV
   2 = MY | MV
   3 = MX | MY | MV */

void ST7789_SetRotation(uint8_t rotation)
{
    WriteCommand(ST7789_MADCTL);

    switch (rotation & 3U) {
        case 0:
            WriteSmallData(ST7789_MADCTL_MX | ST7789_MADCTL_MY | ST7789_MADCTL_RGB);
            break;
        case 1:
            WriteSmallData(ST7789_MADCTL_MX | ST7789_MADCTL_MV | ST7789_MADCTL_RGB);
            break;
        case 2:
            WriteSmallData(ST7789_MADCTL_RGB);
            break;
        case 3:
            WriteSmallData(ST7789_MADCTL_MY | ST7789_MADCTL_MV | ST7789_MADCTL_RGB);
            break;
        default:
            WriteSmallData(ST7789_MADCTL_RGB);
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
    uint8_t hi = (uint8_t)(color >> 8);
    uint8_t lo = (uint8_t)(color & 0xFF);

    for (uint16_t i = 0; i < ST7789_WIDTH; i++) {
        lineBuf[(i * 2U)]     = hi;
        lineBuf[(i * 2U) + 1] = lo;
    }

    SetAddressWindow(0, 0, ST7789_WIDTH - 1, ST7789_HEIGHT - 1);

    CS_LOW();
    DC_HIGH();

    for (uint16_t line = 0; line < ST7789_HEIGHT; line++) {
        dma_send(lineBuf, sizeof(lineBuf));
    }

    CS_HIGH();
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
        lineBuf[(i * 2U)]     = hi;
        lineBuf[(i * 2U) + 1] = lo;
    }

    SetAddressWindow(x, y, x + w - 1, y + h - 1);

    CS_LOW();
    DC_HIGH();

    for (uint16_t row = 0; row < h; row++) {
        dma_send(lineBuf, (uint16_t)(w * 2U));
    }

    CS_HIGH();
}

void ST7789_DrawPixel(uint16_t x, uint16_t y, uint16_t color)
{
    if (x >= ST7789_WIDTH || y >= ST7789_HEIGHT) return;

    SetAddressWindow(x, y, x, y);

    uint8_t d[2] = {
        (uint8_t)(color >> 8),
        (uint8_t)(color & 0xFF)
    };

    CS_LOW();
    DC_HIGH();
    HAL_SPI_Transmit(&hspi1, d, 2, HAL_MAX_DELAY);
    CS_HIGH();
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

    SetAddressWindow(x, y, x + dstW - 1, y + dstH - 1);

    CS_LOW();
    DC_HIGH();

    for (uint16_t srcRow = 0; srcRow < srcH; srcRow++) {
        for (uint8_t sy = 0; sy < scale; sy++) {

            uint16_t out = 0;

            for (uint16_t srcCol = 0; srcCol < srcW; srcCol++) {
                const uint8_t byteIndex = (uint8_t)(srcCol / 8U);
                const uint8_t bitMask   = (uint8_t)(0x80U >> (srcCol % 8U));
                const uint8_t pixelOn   = (charData[(srcRow * bytesPerRow) + byteIndex] & bitMask) ? 1U : 0U;

                for (uint8_t sx = 0; sx < scale; sx++) {
                    lineBuf[out++] = pixelOn ? fg_hi : bg_hi;
                    lineBuf[out++] = pixelOn ? fg_lo : bg_lo;
                }
            }

            dma_send(lineBuf, (uint16_t)(dstW * 2U));
        }
    }

    CS_HIGH();
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