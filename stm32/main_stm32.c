#include "stm32f4xx_hal.h"
#include "st7789.h"
#include "fonts_extra.h"
#include "hw_init.h"
#include "mcp23017.h"
#include <stdio.h>
#include <string.h>

#define FRAM_ADDR     (0x50u << 1)

#define MATRIX_ROW_MASK 0x07u /* GPA0..GPA2 */
#define MATRIX_COL_MASK 0x78u /* GPA3..GPA6 */
#define MATRIX_DEBOUNCE_MS 4u

#define UI_GRID_ROWS 3u
#define UI_GRID_COLS 5u
#define UI_GAP 2u
#define UI_TOP_H (ST7789_HEIGHT / 2u)
#define UI_GRID_X UI_GAP
#define UI_GRID_Y UI_GAP
#define UI_BTN_W ((ST7789_WIDTH - ((UI_GRID_COLS + 1u) * UI_GAP)) / UI_GRID_COLS)
#define UI_BTN_H ((UI_TOP_H - ((UI_GRID_ROWS + 1u) * UI_GAP)) / UI_GRID_ROWS)
#define UI_STATUS_Y1 (UI_TOP_H + 28u)
#define UI_STATUS_Y2 (UI_TOP_H + 48u)

static uint16_t s_matrix_candidate = 0u;
static uint16_t s_matrix_stable = 0u;
static uint32_t s_matrix_candidate_ms = 0u;
static uint8_t s_ui_initialized = 0u;
static uint8_t s_prev_play = 0xFFu;
static uint8_t s_prev_rec = 0xFFu;
static uint8_t s_prev_shift = 0xFFu;
static uint16_t s_prev_matrix_display_mask = 0xFFFFu;

static void draw_step_cell_frame(uint8_t idx)
{
    uint8_t row = (uint8_t)(idx / 4u);
    uint8_t col = (uint8_t)((idx % 4u) + 1u);
    uint16_t cell_x = (uint16_t)(UI_GRID_X + col * (UI_BTN_W + UI_GAP));
    uint16_t cell_y = (uint16_t)(UI_GRID_Y + row * (UI_BTN_H + UI_GAP));

    ST7789_FillRectBordered(cell_x, cell_y, UI_BTN_W, UI_BTN_H, BLACK, WHITE, 2);
}

static void draw_transport_tile(uint8_t row, const char *label, uint8_t active, uint16_t active_color)
{
    uint16_t x = UI_GRID_X;
    uint16_t y = (uint16_t)(UI_GRID_Y + row * (UI_BTN_H + UI_GAP));
    uint16_t bg = active ? active_color : DARKBLUE;
    uint16_t text_w = (uint16_t)(Font10x16.width * strlen(label));
    uint16_t text_h = Font10x16.height;
    uint16_t text_x = (uint16_t)(x + (UI_BTN_W - text_w) / 2u);
    uint16_t text_y = (uint16_t)(y + (UI_BTN_H - text_h) / 2u);

    ST7789_FillRectBordered(x, y, UI_BTN_W, UI_BTN_H, bg, WHITE, 1);
    ST7789_DrawString(text_x, text_y, label, &Font10x16, WHITE, bg);
}

static void draw_step_cell(uint8_t idx, uint8_t active)
{
    char line[8];
    uint8_t row = (uint8_t)(idx / 4u);
    uint8_t col = (uint8_t)((idx % 4u) + 1u);
    uint16_t cell_x = (uint16_t)(UI_GRID_X + col * (UI_BTN_W + UI_GAP));
    uint16_t cell_y = (uint16_t)(UI_GRID_Y + row * (UI_BTN_H + UI_GAP));
    uint16_t fill = active ? ORANGE : BLACK;
    uint16_t text = active ? BLACK : WHITE;
    uint16_t text_w;
    uint16_t text_h;
    uint16_t text_x;
    uint16_t text_y;

    /* Keep frame static and update only inner area to reduce visible tearing. */
    ST7789_FillRect((uint16_t)(cell_x + 2u),
                    (uint16_t)(cell_y + 2u),
                    (uint16_t)(UI_BTN_W - 4u),
                    (uint16_t)(UI_BTN_H - 4u),
                    fill);
    snprintf(line, sizeof(line), "%u", (unsigned int)(idx + 1u));

    text_w = (uint16_t)(Font12x20.width * strlen(line));
    text_h = Font12x20.height;
    text_x = (uint16_t)(cell_x + (UI_BTN_W - text_w) / 2u);
    text_y = (uint16_t)(cell_y + (UI_BTN_H - text_h) / 2u);

    ST7789_DrawString(text_x, text_y, line, &Font12x20, text, fill);
}

static uint16_t scan_step_matrix(uint8_t col_samples[4])
{
    static const uint8_t col_bits[4] = { 0x08u, 0x10u, 0x20u, 0x40u };
    static const uint8_t row_bits[3] = { 0x01u, 0x02u, 0x04u };
    uint16_t pressed = 0u;

    for (uint8_t col = 0; col < 4; col++)
    {
        uint8_t out_a = MATRIX_COL_MASK;
        out_a = (uint8_t)(out_a & (uint8_t)~col_bits[col]);
        MCP23017_WriteGPIOA(&hi2c1, out_a);
        HAL_Delay(1);

        uint8_t gpio_a = MCP23017_ReadGPIOA(&hi2c1);
        col_samples[col] = gpio_a;

        for (uint8_t row = 0; row < 3; row++)
        {
            if ((gpio_a & row_bits[row]) == 0u)
            {
                uint8_t step = (uint8_t)(row * 4u + col);
                pressed |= (uint16_t)(1u << step);
            }
        }
    }

    MCP23017_WriteGPIOA(&hi2c1, MATRIX_COL_MASK);
    return pressed;
}

void UI_Sequencer_Tick1ms(void)
{
    /* Standalone diagnostics build: keep SysTick linkage without sequencer code. */
}

static void draw_start_screen(uint32_t tick_ms)
{
    char line[56];
    uint8_t mcp_ok = (HAL_I2C_IsDeviceReady(&hi2c1, MCP23017_ADDR, 1, 20) == HAL_OK) ? 1u : 0u;
    uint8_t fram_ok = (HAL_I2C_IsDeviceReady(&hi2c3, FRAM_ADDR, 1, 20) == HAL_OK) ? 1u : 0u;
    uint8_t enc_sw = (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_12) == GPIO_PIN_RESET) ? 1u : 0u;
    uint8_t gpioa = 0xFFu;
    uint8_t gpiob = 0xFFu;
    uint16_t raw = 0xFFFFu;
    uint8_t play = 0u;
    uint8_t rec = 0u;
    uint8_t shift = 0u;
    uint16_t matrix_mask = 0u;
    uint16_t matrix_display_mask = 0u;
    uint8_t col_samples[4] = { 0xFFu, 0xFFu, 0xFFu, 0xFFu };

    if (!s_ui_initialized)
    {
        ST7789_Fill_Color(BLACK);

        draw_transport_tile(0u, "PLAY", 0u, GREEN);
        draw_transport_tile(1u, "REC", 0u, RED);
        draw_transport_tile(2u, "SHFT", 0u, ORANGE);

        for (uint8_t i = 0; i < 12; i++)
        {
            draw_step_cell_frame(i);
            draw_step_cell(i, 0u);
        }

        s_prev_play = 0u;
        s_prev_rec = 0u;
        s_prev_shift = 0u;
        s_prev_matrix_display_mask = 0u;
        s_ui_initialized = 1u;
    }

    if (mcp_ok)
    {
        (void)HAL_I2C_Mem_Read(&hi2c1, MCP23017_ADDR, 0x12, I2C_MEMADD_SIZE_8BIT, &gpioa, 1, 20);
        (void)HAL_I2C_Mem_Read(&hi2c1, MCP23017_ADDR, 0x13, I2C_MEMADD_SIZE_8BIT, &gpiob, 1, 20);
        raw = (uint16_t)gpiob | ((uint16_t)gpioa << 8);
        play = (raw & BTN_PLAY) ? 0u : 1u;
        rec = (raw & BTN_REC) ? 0u : 1u;
        shift = (raw & BTN_SHIFT) ? 0u : 1u;
        matrix_mask = scan_step_matrix(col_samples);

        if (matrix_mask != s_matrix_candidate)
        {
            s_matrix_candidate = matrix_mask;
            s_matrix_candidate_ms = tick_ms;
        }
        else if ((uint32_t)(tick_ms - s_matrix_candidate_ms) >= MATRIX_DEBOUNCE_MS)
        {
            s_matrix_stable = s_matrix_candidate;
        }

        matrix_display_mask = s_matrix_stable;
        if ((matrix_display_mask & (uint16_t)(matrix_display_mask - 1u)) != 0u)
        {
            uint16_t single = 0u;
            for (int8_t i = 11; i >= 0; i--)
            {
                uint16_t bit = (uint16_t)(1u << (uint8_t)i);
                if ((matrix_display_mask & bit) != 0u)
                {
                    single = bit;
                    break;
                }
            }
            matrix_display_mask = single;
        }
    }

    if (play != s_prev_play)
    {
        draw_transport_tile(0u, "PLAY", play, GREEN);
        s_prev_play = play;
    }
    if (rec != s_prev_rec)
    {
        draw_transport_tile(1u, "REC", rec, RED);
        s_prev_rec = rec;
    }
    if (shift != s_prev_shift)
    {
        draw_transport_tile(2u, "SHFT", shift, ORANGE);
        s_prev_shift = shift;
    }

    uint16_t changed = (uint16_t)(matrix_display_mask ^ s_prev_matrix_display_mask);
    if (changed != 0u)
    {
        for (uint8_t i = 0; i < 12; i++)
        {
            uint16_t bit = (uint16_t)(1u << i);
            if ((changed & bit) != 0u)
            {
                uint8_t active = (matrix_display_mask & bit) ? 1u : 0u;
                draw_step_cell(i, active);
            }
        }
        s_prev_matrix_display_mask = matrix_display_mask;
    }

    snprintf(line, sizeof(line), "MCP:%s FRAM:%s ENC:%s",
             mcp_ok ? "OK" : "FAIL",
             fram_ok ? "OK" : "FAIL",
             enc_sw ? "DOWN" : "UP");
    ST7789_FillRect(8, UI_STATUS_Y1, 232, 16, BLACK);
    ST7789_DrawStringScaled(8, UI_STATUS_Y1, line, &Font8x12, 1, mcp_ok ? GREEN : RED, BLACK);

    snprintf(line, sizeof(line), "A:%02X B:%02X MASK:%03X",
             gpioa, gpiob, (unsigned int)matrix_display_mask);
    ST7789_FillRect(8, UI_STATUS_Y2, 232, 16, BLACK);
    ST7789_DrawStringScaled(8, UI_STATUS_Y2, line, &Font8x12, 1, WHITE, BLACK);
}

int main(void)
{
    HW_Init();

    /* Configure MCP for matrix scan: GPA rows+shift inputs, cols outputs, GPB inputs. */
    {
        uint8_t iocon = 0x00u;
        uint8_t ipola = 0x00u;
        uint8_t ipolb = 0x00u;
        (void)HAL_I2C_Mem_Write(&hi2c1, MCP23017_ADDR, MCP_IOCON, I2C_MEMADD_SIZE_8BIT, &iocon, 1, 20);
        (void)HAL_I2C_Mem_Write(&hi2c1, MCP23017_ADDR, MCP_IPOLA, I2C_MEMADD_SIZE_8BIT, &ipola, 1, 20);
        (void)HAL_I2C_Mem_Write(&hi2c1, MCP23017_ADDR, MCP_IPOLB, I2C_MEMADD_SIZE_8BIT, &ipolb, 1, 20);
    }
    MCP23017_SetDirections(&hi2c1, 0x87u, 0xFFu);
    MCP23017_SetPullups(&hi2c1, 0x87u, 0xFFu);
    MCP23017_WriteGPIOA(&hi2c1, MATRIX_COL_MASK);

    GPIOA->BSRR = (1U << (9 + 16));
    HAL_Delay(20);
    GPIOA->BSRR = (1U << 9);
    HAL_Delay(120);

    ST7789_Init();
    ST7789_DisplayOn();
    ST7789_Fill_Color(BLACK);
    s_matrix_candidate_ms = HAL_GetTick();

    while (1)
    {
        draw_start_screen(HAL_GetTick());
        HAL_Delay(20);
    }
}
