#include "stm32f4xx_hal.h"
#include "st7789.h"
#include <stdio.h>

SPI_HandleTypeDef hspi1;

void Error_Handler(void)
{
    __disable_irq();
    while (1) {
    }
}

static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clk = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState       = RCC_HSE_ON;
    osc.PLL.PLLState   = RCC_PLL_ON;
    osc.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM       = 8;
    osc.PLL.PLLN       = 336;
    osc.PLL.PLLP       = RCC_PLLP_DIV2;
    osc.PLL.PLLQ       = 7;

    if (HAL_RCC_OscConfig(&osc) != HAL_OK) Error_Handler();

    clk.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                         RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV4;
    clk.APB2CLKDivider = RCC_HCLK_DIV2;

    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_5) != HAL_OK) Error_Handler();
}

static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef g = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_DMA2_CLK_ENABLE();
    __DSB();

    /* PA4=CS  PA6=DC  PA8=RST */
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4 | GPIO_PIN_6 | GPIO_PIN_8, GPIO_PIN_SET);
    g.Pin   = GPIO_PIN_4 | GPIO_PIN_6 | GPIO_PIN_8;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(GPIOA, &g);

    /* PB0 = backlight - OFF until display is ready */
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET);
    g.Pin   = GPIO_PIN_0;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &g);

    /* PA5=SCK  PA7=MOSI */
    g.Pin       = GPIO_PIN_5 | GPIO_PIN_7;
    g.Mode      = GPIO_MODE_AF_PP;
    g.Pull      = GPIO_NOPULL;
    g.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Alternate = GPIO_AF5_SPI1;
    HAL_GPIO_Init(GPIOA, &g);
}

static void MX_SPI1_Init(void)
{
    __HAL_RCC_SPI1_CLK_ENABLE();

    hspi1.Instance               = SPI1;
    hspi1.Init.Mode              = SPI_MODE_MASTER;
    hspi1.Init.Direction         = SPI_DIRECTION_2LINES;
    hspi1.Init.DataSize          = SPI_DATASIZE_8BIT;
    hspi1.Init.CLKPolarity       = SPI_POLARITY_HIGH;
    hspi1.Init.CLKPhase          = SPI_PHASE_2EDGE;
    hspi1.Init.NSS               = SPI_NSS_SOFT;
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
    hspi1.Init.FirstBit          = SPI_FIRSTBIT_MSB;
    hspi1.Init.TIMode            = SPI_TIMODE_DISABLE;
    hspi1.Init.CRCCalculation    = SPI_CRCCALCULATION_DISABLE;
    hspi1.Init.CRCPolynomial     = 10;

    if (HAL_SPI_Init(&hspi1) != HAL_OK) Error_Handler();
}

/* ------------------------------------------------------------------------- */
/* UI LAYOUT                                                                 */
/* ------------------------------------------------------------------------- */

/*
 * YOUR PHOTO ORIENTATION
 *
 * Red side   = LEFT
 * Blue side  = RIGHT  (pin side)
 * Yellow line = TOP
 *
 * For ST7789_SetRotation(1) on YOUR mounted panel, your testing shows:
 *
 *   X = left / right
 *   Y = up / down
 *
 * Therefore:
 *   col -> X
 *   row -> Y
 *
 * This gives the normal Sequencer-12 style layout:
 *   1  2  3  4
 *   5  6  7  8
 *   9 10 11 12
 */

/* Screen size */
#define SCREEN_W   240
#define SCREEN_H   240

/* Top header bar */
#define HEADER_X     0
#define HEADER_Y     0
#define HEADER_W   240
#define HEADER_H    24

/*
 * Grid origin:
 * GRID_X bigger = move whole grid RIGHT (toward blue side / pins)
 * GRID_X smaller = move whole grid LEFT  (toward red side)
 *
 * GRID_Y bigger = move whole grid DOWN
 * GRID_Y smaller = move whole grid UP
 *
 * These are the two values you should tweak first if needed.
 */
#define GRID_X       8 //Higher the number to the left.
#define GRID_Y      56 //Move all the Rows Down

/* Box geometry */
#define BOX_W       50
#define BOX_H       56

/* Gaps between boxes */
#define GAP_X        6
#define GAP_Y        8

/* Border thickness */
#define BORDER_T     2


static void DrawRectBorder(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    ST7789_FillRect(x, y, w, BORDER_T, color);                     /* top */
    ST7789_FillRect(x, y + h - BORDER_T, w, BORDER_T, color);     /* bottom */
    ST7789_FillRect(x, y, BORDER_T, h, color);                    /* left */
    ST7789_FillRect(x + w - BORDER_T, y, BORDER_T, h, color);     /* right */
}

static void DrawStepBox(uint8_t step, uint8_t selected)
{
    uint8_t index = step - 1;

    /*
     * Logical sequencer layout:
     *  1  2  3  4
     *  5  6  7  8
     *  9 10 11 12
     */
    uint8_t col = index % 4;
    uint8_t row = index / 4;

    /*
     * Based on YOUR panel behaviour:
     *   X = left/right
     *   Y = up/down
     *
     * So use a normal grid.
     */
    uint16_t x = GRID_X + col * (BOX_W + GAP_X);
    uint16_t y = GRID_Y + row * (BOX_H + GAP_Y);

    uint16_t borderColor = selected ? GREEN : WHITE;
    uint16_t textColor   = selected ? GREEN : WHITE;

    char s[4];
    snprintf(s, sizeof(s), "%u", step);

    /* clear box interior */
    ST7789_FillRect(x, y, BOX_W, BOX_H, BLACK);

    /* draw border */
    DrawRectBorder(x, y, BOX_W, BOX_H, borderColor);

    /* centre the step number */
    uint8_t len = 0;
    while (s[len] != 0) len++;

    uint16_t textW = len * Font16x24.width;
    uint16_t textH = Font16x24.height;

    uint16_t tx = x + ((BOX_W - textW) / 2);
    uint16_t ty = y + ((BOX_H - textH) / 2);

    ST7789_DrawStringScaled(tx, ty, s, &Font8x12, 2, textColor, BLACK);
}

static void DrawHeader(void)
{
    
    char play_big[2] = {91, 0};
    char stop_big[2] = {92, 0};
    char rec_big[2]  = {93, 0};
    
    /* real top bar across the yellow-line side */
    ST7789_FillRect(HEADER_X, HEADER_Y, HEADER_W, HEADER_H, BLACK);

    /*
     * Header content:
     * tempo on left,
     * transport to the right of it,
     * all ABOVE the grid.
     * 
     */
   
    ST7789_DrawStringScaled(6, 2, "120", &Font8x12, 2, WHITE, BLACK);
    ST7789_DrawString(60,  2, "BPM", &Font16x24, WHITE, BLACK);

   int MoveIconsX = 23; //Higher number moves them right
   int MoveIconsY = 5;//Higher number moves them down

    ST7789_DrawString(130 + MoveIconsX, 25 + MoveIconsY, play_big, &Font16x24, WHITE, BLACK);
    ST7789_DrawString(160 + MoveIconsX, 25 + MoveIconsY, stop_big, &Font16x24, WHITE, BLACK);
    ST7789_DrawString(190 + MoveIconsX, 25 + MoveIconsY, rec_big,  &Font16x24, WHITE, BLACK);





}


int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_SPI1_Init();
    ST7789_Init();
    ST7789_Fill_Color(BLACK);
    DrawHeader();
    for (uint8_t i = 1; i <= 12; i++) {
        DrawStepBox(i, (i == 1) ? 1 : 0);
    }
    GPIOB->BSRR = (1U << 0);

    while (1) {
        HAL_Delay(100);
    }
}
