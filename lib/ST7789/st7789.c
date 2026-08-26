#include "st7789.h"
#include "s12_fonts.h"
#include <stdio.h>
#include <string.h>
/* ------------------------------------------------------------------------- */
/* Pin map                                                                   */
/* PA4=CS  PA5=SCK  PA6=DC  PA7=MOSI  PA9=RST                                 */
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

#if ST7789_PIANO_TRACE
typedef struct {
    uint8_t step_piano_active;
    uint8_t in_piano_frame;
    uint8_t event_id;
    uint32_t frame_seq;
    ST7789_PianoTraceSnapshot totals;
    ST7789_PianoTraceSnapshot frame;
} ST7789_PianoTraceState;

static ST7789_PianoTraceState s_trace = {
    .step_piano_active = 0u,
    .in_piano_frame = 0u,
    .event_id = ST7789_PIANO_EVENT_NONE,
    .frame_seq = 0u
};

static uint8_t TraceActive(void)
{
    return (uint8_t)(s_trace.step_piano_active || s_trace.in_piano_frame);
}

static void TraceUpdateY(uint16_t y, uint16_t h)
{
    uint16_t y2;
    if (h == 0u) return;
    y2 = (uint16_t)(y + h - 1u);
    if (y2 > s_trace.totals.max_y2) s_trace.totals.max_y2 = y2;
    if (y2 > s_trace.frame.max_y2) s_trace.frame.max_y2 = y2;
    if (y2 >= 288u || y >= 288u)
    {
        s_trace.totals.touches_y_gte_288++;
        s_trace.frame.touches_y_gte_288++;
    }
}

static void TraceOp(const char* op,
                    uint16_t req_x,
                    uint16_t req_y,
                    uint16_t req_w,
                    uint16_t req_h,
                    uint16_t x,
                    uint16_t y,
                    uint16_t w,
                    uint16_t h,
                    uint32_t bytes,
                    uint16_t x_shifted,
                    uint16_t y_shifted,
                    uint16_t x2_shifted,
                    uint16_t y2_shifted)
{
    uint16_t y2 = (h > 0u) ? (uint16_t)(y + h - 1u) : y;
    if (!TraceActive()) return;

    s_trace.totals.op_total++;
    s_trace.frame.op_total++;
    if (!s_trace.in_piano_frame && s_trace.step_piano_active)
    {
        s_trace.totals.op_external_while_step_piano++;
        s_trace.frame.op_external_while_step_piano++;
    }

    if (strcmp(op, "SetAddressWindow") == 0)
    {
        s_trace.totals.op_set_address_window++;
        s_trace.frame.op_set_address_window++;
    }
    else if (strcmp(op, "FillRect") == 0)
    {
        s_trace.totals.op_fill_rect++;
        s_trace.frame.op_fill_rect++;
    }
    else if (strcmp(op, "DrawRGB565Buffer") == 0)
    {
        s_trace.totals.op_draw_rgb565++;
        s_trace.frame.op_draw_rgb565++;
    }
    else if (strcmp(op, "DrawString") == 0)
    {
        s_trace.totals.op_draw_string++;
        s_trace.frame.op_draw_string++;
    }

    s_trace.totals.rgb565_bytes_total += bytes;
    s_trace.frame.rgb565_bytes_total += bytes;
    s_trace.totals.spi_bytes_total += bytes;
    s_trace.frame.spi_bytes_total += bytes;
    TraceUpdateY(y, h);

    printf("LCDTRACE frame=%lu event=%u scope=%s op=%s req=(%u,%u,%u,%u) final=(%u,%u,%u,%u y2=%u) shift=(%u,%u,%u,%u) bytes=%lu footerY=%u\n",
           (unsigned long)s_trace.frame_seq,
           (unsigned)s_trace.event_id,
           s_trace.in_piano_frame ? "piano" : "external",
           op,
           (unsigned)req_x,
           (unsigned)req_y,
           (unsigned)req_w,
           (unsigned)req_h,
           (unsigned)x,
           (unsigned)y,
           (unsigned)w,
           (unsigned)h,
           (unsigned)y2,
           (unsigned)x_shifted,
           (unsigned)y_shifted,
           (unsigned)x2_shifted,
           (unsigned)y2_shifted,
           (unsigned long)bytes,
           (unsigned)((y >= 288u || y2 >= 288u) ? 1u : 0u));
}
#endif

void ST7789_DebugSetStepPianoActive(uint8_t active)
{
#if ST7789_PIANO_TRACE
    s_trace.step_piano_active = active ? 1u : 0u;
#else
    (void)active;
#endif
}

void ST7789_DebugSetPianoEvent(uint8_t event_id)
{
#if ST7789_PIANO_TRACE
    s_trace.event_id = event_id;
#else
    (void)event_id;
#endif
}

void ST7789_DebugBeginPianoRenderFrame(const char* tag)
{
#if ST7789_PIANO_TRACE
    (void)tag;
    s_trace.in_piano_frame = 1u;
    s_trace.frame_seq++;
    memset(&s_trace.frame, 0, sizeof(s_trace.frame));
    s_trace.frame.frame_seq = s_trace.frame_seq;
    s_trace.frame.last_event = s_trace.event_id;
    s_trace.frame.step_piano_active = s_trace.step_piano_active;
    s_trace.totals.frame_seq = s_trace.frame_seq;
    s_trace.totals.last_event = s_trace.event_id;
    s_trace.totals.step_piano_active = s_trace.step_piano_active;
    printf("LCDTRACE frame=%lu begin event=%u\n",
           (unsigned long)s_trace.frame_seq,
           (unsigned)s_trace.event_id);
#else
    (void)tag;
#endif
}

void ST7789_DebugEndPianoRenderFrame(void)
{
#if ST7789_PIANO_TRACE
    if (!s_trace.in_piano_frame) return;
    printf("LCDTRACE frame=%lu end ops=%lu setWin=%lu fillRect=%lu drawRGB=%lu drawString=%lu external=%lu bytes=%lu y>=288=%lu maxY2=%u\n",
           (unsigned long)s_trace.frame.frame_seq,
           (unsigned long)s_trace.frame.op_total,
           (unsigned long)s_trace.frame.op_set_address_window,
           (unsigned long)s_trace.frame.op_fill_rect,
           (unsigned long)s_trace.frame.op_draw_rgb565,
           (unsigned long)s_trace.frame.op_draw_string,
           (unsigned long)s_trace.frame.op_external_while_step_piano,
           (unsigned long)s_trace.frame.spi_bytes_total,
           (unsigned long)s_trace.frame.touches_y_gte_288,
           (unsigned)s_trace.frame.max_y2);
    s_trace.in_piano_frame = 0u;
#endif
}

void ST7789_DebugResetPianoTrace(void)
{
#if ST7789_PIANO_TRACE
    memset(&s_trace.totals, 0, sizeof(s_trace.totals));
    memset(&s_trace.frame, 0, sizeof(s_trace.frame));
    s_trace.frame_seq = 0u;
    s_trace.event_id = ST7789_PIANO_EVENT_NONE;
    s_trace.step_piano_active = 0u;
    s_trace.in_piano_frame = 0u;
#endif
}

void ST7789_DebugGetPianoTraceSnapshot(ST7789_PianoTraceSnapshot* out)
{
    if (!out) return;
#if ST7789_PIANO_TRACE
    *out = s_trace.totals;
#else
    memset(out, 0, sizeof(*out));
#endif
}

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
    const uint16_t raw_x0 = x0;
    const uint16_t raw_y0 = y0;
    const uint16_t raw_x1 = x1;
    const uint16_t raw_y1 = y1;
    x0 += X_SHIFT;
    x1 += X_SHIFT;
    y0 += Y_SHIFT;
    y1 += Y_SHIFT;

#if ST7789_PIANO_TRACE
    {
        const uint16_t w = (raw_x1 >= raw_x0) ? (uint16_t)(raw_x1 - raw_x0 + 1u) : 0u;
        const uint16_t h = (raw_y1 >= raw_y0) ? (uint16_t)(raw_y1 - raw_y0 + 1u) : 0u;
        TraceOp("SetAddressWindow",
                raw_x0, raw_y0, w, h,
                raw_x0, raw_y0, w, h,
                0u,
                x0, y0, x1, y1);
    }
#endif

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

void ST7789_DisplayOff(void)
{
    WriteCommand(ST7789_DISPOFF);
    HAL_Delay(20);
}

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
    const uint16_t req_x = x;
    const uint16_t req_y = y;
    const uint16_t req_w = w;
    const uint16_t req_h = h;
    if (x >= ST7789_WIDTH || y >= ST7789_HEIGHT)
    {
#if ST7789_PIANO_TRACE
        TraceOp("FillRectReject", req_x, req_y, req_w, req_h,
                req_x, req_y, req_w, req_h, 0u,
                (uint16_t)(req_x + X_SHIFT),
                (uint16_t)(req_y + Y_SHIFT),
                (uint16_t)(req_x + X_SHIFT),
                (uint16_t)(req_y + Y_SHIFT));
#endif
        return;
    }
    if (w == 0 || h == 0) return;
    if ((x + w) > ST7789_WIDTH)  w = ST7789_WIDTH  - x;
    if ((y + h) > ST7789_HEIGHT) h = ST7789_HEIGHT - y;

#if ST7789_PIANO_TRACE
    TraceOp("FillRect",
            req_x, req_y, req_w, req_h,
            x, y, w, h,
            (uint32_t)w * (uint32_t)h * 2u,
            (uint16_t)(x + X_SHIFT),
            (uint16_t)(y + Y_SHIFT),
            (uint16_t)(x + w - 1u + X_SHIFT),
            (uint16_t)(y + h - 1u + Y_SHIFT));
#endif

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

void ST7789_FillRectBordered(uint16_t x, uint16_t y,
                             uint16_t w, uint16_t h,
                             uint16_t fill_color,
                             uint16_t border_color,
                             uint8_t border_t)
{
    if (x >= ST7789_WIDTH || y >= ST7789_HEIGHT) return;
    if (w == 0 || h == 0) return;
    if ((x + w) > ST7789_WIDTH)  w = ST7789_WIDTH - x;
    if ((y + h) > ST7789_HEIGHT) h = ST7789_HEIGHT - y;

    if (border_t == 0U || fill_color == border_color)
    {
        ST7789_FillRect(x, y, w, h, fill_color);
        return;
    }

    uint16_t max_border_t = (uint16_t)(w / 2U);
    if ((uint16_t)(h / 2U) < max_border_t)
    {
        max_border_t = (uint16_t)(h / 2U);
    }
    if (border_t > max_border_t)
    {
        border_t = (uint8_t)max_border_t;
    }
    if (border_t == 0U)
    {
        ST7789_FillRect(x, y, w, h, fill_color);
        return;
    }

    {
        uint8_t fill_hi = (uint8_t)(fill_color >> 8);
        uint8_t fill_lo = (uint8_t)(fill_color & 0xFF);
        uint8_t border_hi = (uint8_t)(border_color >> 8);
        uint8_t border_lo = (uint8_t)(border_color & 0xFF);
        uint16_t row_bytes = (uint16_t)(w * 2U);
        uint16_t inner_start = border_t;
        uint16_t inner_end = (uint16_t)(w - border_t);

        SetAddressWindow(x, y, (uint16_t)(x + w - 1U), (uint16_t)(y + h - 1U));
        CS_ASSERT();
        DC_HIGH();

        for (uint16_t i = 0; i < w; i++)
        {
            lineBuf[i * 2U] = border_hi;
            lineBuf[i * 2U + 1U] = border_lo;
        }

        for (uint8_t row = 0; row < border_t; row++)
        {
            (void)row;
            dma_send(lineBuf, row_bytes);
        }

        for (uint16_t i = 0; i < w; i++)
        {
            uint8_t use_border = (i < inner_start || i >= inner_end) ? 1U : 0U;
            lineBuf[i * 2U] = use_border ? border_hi : fill_hi;
            lineBuf[i * 2U + 1U] = use_border ? border_lo : fill_lo;
        }

        for (uint16_t row = border_t; row < (uint16_t)(h - border_t); row++)
        {
            (void)row;
            dma_send(lineBuf, row_bytes);
        }

        for (uint16_t i = 0; i < w; i++)
        {
            lineBuf[i * 2U] = border_hi;
            lineBuf[i * 2U + 1U] = border_lo;
        }

        for (uint8_t row = 0; row < border_t; row++)
        {
            (void)row;
            dma_send(lineBuf, row_bytes);
        }

        CS_RELEASE();
    }
}

void ST7789_DrawRGB565Buffer(uint16_t x, uint16_t y,
                             uint16_t w, uint16_t h,
                             const uint8_t* buffer,
                             uint32_t buffer_size)
{
    const uint16_t req_x = x;
    const uint16_t req_y = y;
    const uint16_t req_w = w;
    const uint16_t req_h = h;
    const uint32_t req_buffer_size = buffer_size;
    const uint32_t min_bytes = (uint32_t)w * (uint32_t)h * 2u;

        if (buffer == 0) return;
        if (x >= ST7789_WIDTH || y >= ST7789_HEIGHT)
        {
    #if ST7789_PIANO_TRACE
        TraceOp("DrawRGB565BufferReject", req_x, req_y, req_w, req_h,
            req_x, req_y, req_w, req_h, req_buffer_size,
            (uint16_t)(req_x + X_SHIFT),
            (uint16_t)(req_y + Y_SHIFT),
            (uint16_t)(req_x + X_SHIFT),
            (uint16_t)(req_y + Y_SHIFT));
    #endif
        return;
        }
        if (w == 0 || h == 0) return;
        if ((x + w) > ST7789_WIDTH || (y + h) > ST7789_HEIGHT)
        {
    #if ST7789_PIANO_TRACE
        TraceOp("DrawRGB565BufferReject", req_x, req_y, req_w, req_h,
            x, y, w, h, req_buffer_size,
            (uint16_t)(x + X_SHIFT),
            (uint16_t)(y + Y_SHIFT),
            (uint16_t)(x + w - 1u + X_SHIFT),
            (uint16_t)(y + h - 1u + Y_SHIFT));
    #endif
        return;
        }
        if (buffer_size < min_bytes)
        {
    #if ST7789_PIANO_TRACE
        TraceOp("DrawRGB565BufferReject", req_x, req_y, req_w, req_h,
            x, y, w, h, req_buffer_size,
            (uint16_t)(x + X_SHIFT),
            (uint16_t)(y + Y_SHIFT),
            (uint16_t)(x + w - 1u + X_SHIFT),
            (uint16_t)(y + h - 1u + Y_SHIFT));
    #endif
        return;
        }

#if ST7789_PIANO_TRACE
    TraceOp("DrawRGB565Buffer",
            req_x, req_y, req_w, req_h,
            x, y, w, h,
            req_buffer_size,
            (uint16_t)(x + X_SHIFT),
            (uint16_t)(y + Y_SHIFT),
            (uint16_t)(x + w - 1u + X_SHIFT),
            (uint16_t)(y + h - 1u + Y_SHIFT));
    if (req_buffer_size != min_bytes)
    {
        printf("LCDTRACE frame=%lu warn=buffer_size_mismatch reqBytes=%lu expectedBytes=%lu\n",
               (unsigned long)s_trace.frame_seq,
               (unsigned long)req_buffer_size,
               (unsigned long)min_bytes);
    }
#endif

    SetAddressWindow(x, y, (uint16_t)(x + w - 1u), (uint16_t)(y + h - 1u));
    CS_ASSERT();
    DC_HIGH();

    buffer_size = min_bytes;

    while (buffer_size > 0u)
    {
        uint16_t chunk = (buffer_size > 65535u) ? 65535u : (uint16_t)buffer_size;
        dma_send((uint8_t*)buffer, chunk);
        buffer += chunk;
        buffer_size -= chunk;
    }

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
    uint16_t start_x = x;
    uint16_t start_y = y;
    uint16_t max_right = x;
    uint16_t max_bottom = y;
    uint32_t glyph_count = 0u;
    if (font == 0 || str == 0 || scale == 0) return;

    const uint16_t stepX = (uint16_t)(font->width * scale);
    const uint16_t stepY = (uint16_t)(font->height * scale);

#if ST7789_PIANO_TRACE
    {
        const char* p = str;
        uint16_t tx = x;
        uint16_t ty = y;
        while (*p)
        {
            if ((tx + stepX) > ST7789_WIDTH)
            {
                tx = 0u;
                ty = (uint16_t)(ty + stepY);
            }
            if ((ty + stepY) > ST7789_HEIGHT)
            {
                break;
            }
            glyph_count++;
            if ((uint16_t)(tx + stepX) > max_right) max_right = (uint16_t)(tx + stepX);
            if ((uint16_t)(ty + stepY) > max_bottom) max_bottom = (uint16_t)(ty + stepY);
            tx = (uint16_t)(tx + stepX);
            ++p;
        }

        if (glyph_count > 0u)
        {
            TraceOp("DrawString",
                    start_x, start_y,
                    (uint16_t)(max_right - start_x),
                    (uint16_t)(max_bottom - start_y),
                    start_x, start_y,
                    (uint16_t)(max_right - start_x),
                    (uint16_t)(max_bottom - start_y),
                    (uint32_t)(max_right - start_x) * (uint32_t)(max_bottom - start_y) * 2u,
                    (uint16_t)(start_x + X_SHIFT),
                    (uint16_t)(start_y + Y_SHIFT),
                    (uint16_t)(max_right - 1u + X_SHIFT),
                    (uint16_t)(max_bottom - 1u + Y_SHIFT));
        }
    }
#endif

    (void)fg;
    (void)bg;

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