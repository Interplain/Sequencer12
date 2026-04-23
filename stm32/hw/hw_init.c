#include "hw_init.h"

// ─────────────────────────────────────────────
// Peripheral handles
// ─────────────────────────────────────────────
SPI_HandleTypeDef hspi1;
SPI_HandleTypeDef hspi2;
I2C_HandleTypeDef hi2c1;
I2C_HandleTypeDef hi2c3;
TIM_HandleTypeDef htim2;
DMA_HandleTypeDef hdma_spi1_tx;

// ─────────────────────────────────────────────
// Forward declarations
// ─────────────────────────────────────────────
static void MX_GPIO_Init(void);
static void MX_SPI1_Init(void);
static void MX_SPI2_Init(void);
static void MX_I2C1_Init(void);
static void MX_I2C3_Init(void);
static void MX_TIM2_Init(void);
void Error_Handler(void);
// ─────────────────────────────────────────────
// Master hardware init
// ─────────────────────────────────────────────
void HW_Init(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_SPI1_Init();
    MX_SPI2_Init();
    MX_I2C1_Init();
    MX_I2C3_Init();
    MX_TIM2_Init();
}

// ─────────────────────────────────────────────
// System Clock — HSE 8MHz → PLL → 168MHz
// ─────────────────────────────────────────────
void SystemClock_Config(void)
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

// ─────────────────────────────────────────────
// GPIO
// PB0=CS  PA6=DC  PA9=RST
// PA0=ENC_A  PA1=ENC_B  PC12=ENC_SW
// PB12=DAC_CS PB14=DAC_LDAC PC4=DAC_CLR
// ─────────────────────────────────────────────
static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef g = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_DMA2_CLK_ENABLE();
    __DSB();

    // PA4 unused, PA6=DC HIGH
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, GPIO_PIN_SET);
    g.Pin   = GPIO_PIN_4 | GPIO_PIN_6;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(GPIOA, &g);

    // PA9=RST — hold HIGH with pull-up, low-speed to reduce line sensitivity
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_9, GPIO_PIN_SET);
    g.Pin   = GPIO_PIN_9;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_PULLUP;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &g);

    // PB0 = LCD CS idle HIGH (active-low)
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET);
    g.Pin   = GPIO_PIN_0;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &g);

    // DAC control pins: PB12=CS HIGH, PB14=LDAC HIGH, PC4=CLR HIGH
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12 | GPIO_PIN_14, GPIO_PIN_SET);
    g.Pin   = GPIO_PIN_12 | GPIO_PIN_14;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &g);

    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_4, GPIO_PIN_SET);
    g.Pin   = GPIO_PIN_4;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOC, &g);

    // PA5=SCK  PA7=MOSI — SPI1 AF5
    g.Pin       = GPIO_PIN_5 | GPIO_PIN_7;
    g.Mode      = GPIO_MODE_AF_PP;
    g.Pull      = GPIO_NOPULL;
    g.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Alternate = GPIO_AF5_SPI1;
    HAL_GPIO_Init(GPIOA, &g);

    // PC12 = encoder switch — input with pullup
    g.Pin   = GPIO_PIN_12;
    g.Mode  = GPIO_MODE_INPUT;
    g.Pull  = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOC, &g);

    /* I2C1 SDA/SCL — PB8/PB9 */
    __HAL_RCC_I2C1_CLK_ENABLE();
    g.Pin       = GPIO_PIN_8 | GPIO_PIN_9;
    g.Mode      = GPIO_MODE_AF_OD;
    g.Pull      = GPIO_PULLUP;
    g.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(GPIOB, &g);
}

// ─────────────────────────────────────────────
// SPI1 — ST7789 display
// ─────────────────────────────────────────────
static void MX_SPI1_Init(void)
{
    __HAL_RCC_SPI1_CLK_ENABLE();

    hspi1.Instance               = SPI1;
    hspi1.Init.Mode              = SPI_MODE_MASTER;
    hspi1.Init.Direction         = SPI_DIRECTION_2LINES;
    hspi1.Init.DataSize          = SPI_DATASIZE_8BIT;
     /* Mode 3 is the stable edge timing for this wiring; mode 0 regressed to all-white. */
     hspi1.Init.CLKPolarity       = SPI_POLARITY_HIGH;
     hspi1.Init.CLKPhase          = SPI_PHASE_2EDGE;
    hspi1.Init.NSS               = SPI_NSS_SOFT;
    /* 10.5 MHz (168/16): confirmed clean pixel output on this panel. */
    /* 10.5 MHz (168/16): confirmed clean pixel output on this panel. */
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16;
    hspi1.Init.FirstBit          = SPI_FIRSTBIT_MSB;
    hspi1.Init.TIMode            = SPI_TIMODE_DISABLE;
    hspi1.Init.CRCCalculation    = SPI_CRCCALCULATION_DISABLE;
    hspi1.Init.CRCPolynomial     = 10;
    if (HAL_SPI_Init(&hspi1) != HAL_OK) Error_Handler();

    /* DMA2 Stream3 Ch3 — SPI1 TX */
    hdma_spi1_tx.Instance                 = DMA2_Stream3;
    hdma_spi1_tx.Init.Channel             = DMA_CHANNEL_3;
    hdma_spi1_tx.Init.Direction           = DMA_MEMORY_TO_PERIPH;
    hdma_spi1_tx.Init.PeriphInc           = DMA_PINC_DISABLE;
    hdma_spi1_tx.Init.MemInc              = DMA_MINC_ENABLE;
    hdma_spi1_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_spi1_tx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
    hdma_spi1_tx.Init.Mode                = DMA_NORMAL;
    hdma_spi1_tx.Init.Priority            = DMA_PRIORITY_HIGH;
    hdma_spi1_tx.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&hdma_spi1_tx) != HAL_OK) Error_Handler();

    __HAL_LINKDMA(&hspi1, hdmatx, hdma_spi1_tx);

    HAL_NVIC_SetPriority(DMA2_Stream3_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(DMA2_Stream3_IRQn);
}

// ─────────────────────────────────────────────
// SPI2 — DAC8564
// PB13=SCK PB15=MOSI
// ─────────────────────────────────────────────
static void MX_SPI2_Init(void)
{
    __HAL_RCC_SPI2_CLK_ENABLE();

    hspi2.Instance               = SPI2;
    hspi2.Init.Mode              = SPI_MODE_MASTER;
    hspi2.Init.Direction         = SPI_DIRECTION_2LINES;
    hspi2.Init.DataSize          = SPI_DATASIZE_8BIT;
    hspi2.Init.CLKPolarity       = SPI_POLARITY_LOW;
    hspi2.Init.CLKPhase          = SPI_PHASE_1EDGE;
    hspi2.Init.NSS               = SPI_NSS_SOFT;
    hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8;
    hspi2.Init.FirstBit          = SPI_FIRSTBIT_MSB;
    hspi2.Init.TIMode            = SPI_TIMODE_DISABLE;
    hspi2.Init.CRCCalculation    = SPI_CRCCALCULATION_DISABLE;
    hspi2.Init.CRCPolynomial     = 10;

    if (HAL_SPI_Init(&hspi2) != HAL_OK) Error_Handler();
}

// ─────────────────────────────────────────────
// I2C1 — MCP23017
// PB8=SCL  PB9=SDA
// ─────────────────────────────────────────────
static void MX_I2C1_Init(void)
{
    __HAL_RCC_I2C1_CLK_ENABLE();

    hi2c1.Instance             = I2C1;
    hi2c1.Init.ClockSpeed      = 100000;
    hi2c1.Init.DutyCycle       = I2C_DUTYCYCLE_2;
    hi2c1.Init.OwnAddress1     = 0;
    hi2c1.Init.AddressingMode  = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2     = 0;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode   = I2C_NOSTRETCH_DISABLE;

    if (HAL_I2C_Init(&hi2c1) != HAL_OK) Error_Handler();
}

// ─────────────────────────────────────────────
// I2C3 — FRAM (dedicated bus)
// PA8=SCL  PC9=SDA
// ─────────────────────────────────────────────
static void MX_I2C3_Init(void)
{
    __HAL_RCC_I2C3_CLK_ENABLE();

    hi2c3.Instance             = I2C3;
    hi2c3.Init.ClockSpeed      = 100000;
    hi2c3.Init.DutyCycle       = I2C_DUTYCYCLE_2;
    hi2c3.Init.OwnAddress1     = 0;
    hi2c3.Init.AddressingMode  = I2C_ADDRESSINGMODE_7BIT;
    hi2c3.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c3.Init.OwnAddress2     = 0;
    hi2c3.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c3.Init.NoStretchMode   = I2C_NOSTRETCH_DISABLE;

    if (HAL_I2C_Init(&hi2c3) != HAL_OK) Error_Handler();
}

// ─────────────────────────────────────────────
// TIM2 — encoder
// PA0=CH1  PA1=CH2
// ─────────────────────────────────────────────
static void MX_TIM2_Init(void)
{
    TIM_Encoder_InitTypeDef enc  = {0};
    TIM_MasterConfigTypeDef mstr = {0};

    __HAL_RCC_TIM2_CLK_ENABLE();

    htim2.Instance               = TIM2;
    htim2.Init.Prescaler         = 0;
    htim2.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim2.Init.Period            = 0xFFFF;
    htim2.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    enc.EncoderMode  = TIM_ENCODERMODE_TI12;
    enc.IC1Polarity  = TIM_ICPOLARITY_RISING;
    enc.IC1Selection = TIM_ICSELECTION_DIRECTTI;
    enc.IC1Prescaler = TIM_ICPSC_DIV1;
    enc.IC1Filter    = 10;
    enc.IC2Polarity  = TIM_ICPOLARITY_RISING;
    enc.IC2Selection = TIM_ICSELECTION_DIRECTTI;
    enc.IC2Prescaler = TIM_ICPSC_DIV1;
    enc.IC2Filter    = 10;

    if (HAL_TIM_Encoder_Init(&htim2, &enc) != HAL_OK) Error_Handler();

    mstr.MasterOutputTrigger = TIM_TRGO_RESET;
    mstr.MasterSlaveMode     = TIM_MASTERSLAVEMODE_DISABLE;
    if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &mstr) != HAL_OK)
        Error_Handler();

    HAL_TIM_Encoder_Start(&htim2, TIM_CHANNEL_ALL);
}

// ─────────────────────────────────────────────
// Error handler
// ─────────────────────────────────────────────
void Error_Handler(void)
{
    __disable_irq();
    while (1) {}
}