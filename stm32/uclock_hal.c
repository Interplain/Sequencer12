/**
 * @brief uClock STM32Cube HAL implementation for Sequencer12
 * 
 * Provides TIM1 hardware timer interface to uClock library.
 * Compile-only integration; no sequencer behavior changes.
 */

#include "uclock_hal.h"
#include "uClock.h"

// ─────────────────────────────────────────────────────────────
// TIM1 Handle
// ─────────────────────────────────────────────────────────────
static TIM_HandleTypeDef htim1_uclock;

/**
 * @brief Initialize TIM1 for uClock 1kHz timing
 * 
 * APB2 clock = 168 MHz
 * Prescaler = 16800 - 1 → base clock = 10 kHz
 * Period = 10 - 1 → 1 kHz update event
 */
void uClock_HAL_Init(void)
{
    // Enable TIM1 clock on APB2
    __HAL_RCC_TIM1_CLK_ENABLE();
    
    // Configure TIM1 base
    htim1_uclock.Instance = TIM1;
    htim1_uclock.Init.Prescaler = 16800 - 1;      // 168MHz / 16800 = 10kHz base
    htim1_uclock.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim1_uclock.Init.Period = 10 - 1;            // 10kHz / 10 = 1kHz update event
    htim1_uclock.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim1_uclock.Init.RepetitionCounter = 0;
    htim1_uclock.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    
    if (HAL_TIM_Base_Init(&htim1_uclock) != HAL_OK) {
        // Error: Let it fail silently in compile-only mode
        // In production, call Error_Handler()
    }
    
    // Configure TIM1 interrupt priority
    // Use lower priority than critical peripherals (e.g., SPI/I2C)
    HAL_NVIC_SetPriority(TIM1_UP_TIM10_IRQn, 8, 0);
    HAL_NVIC_EnableIRQ(TIM1_UP_TIM10_IRQn);
}

/**
 * @brief Start TIM1 update interrupt and timer
 */
void uClock_HAL_Start(void)
{
    // Enable update interrupt
    __HAL_TIM_ENABLE_IT(&htim1_uclock, TIM_IT_UPDATE);
    
    // Start timer counter
    HAL_TIM_Base_Start_IT(&htim1_uclock);
}

/**
 * @brief Stop TIM1 timer and interrupt
 */
void uClock_HAL_Stop(void)
{
    // Stop timer
    HAL_TIM_Base_Stop_IT(&htim1_uclock);
    
    // Disable update interrupt
    __HAL_TIM_DISABLE_IT(&htim1_uclock, TIM_IT_UPDATE);
}

/**
 * @brief Handle TIM1 update interrupt
 * 
 * Called from TIM1_UP_TIM10_IRQHandler.
 * Routes 1kHz tick to uClock library.
 */
void uClock_HAL_TimerTick(void)
{
    // Call uClock's internal timer handler
    // uClock expects a tick approximately every 1ms for standard tempo tracking
    uClock_handleTimerTick();
}

/**
 * @brief TIM1 Update Interrupt Handler
 * 
 * HAL callback for TIM1 update (overflow) interrupt.
 * Declared as weak in HAL so we can override it here.
 */
void TIM1_UP_TIM10_IRQHandler(void)
{
    // Let HAL clear the interrupt flag
    HAL_TIM_IRQHandler(&htim1_uclock);
    
    // Route to uClock
    uClock_HAL_TimerTick();
}

/**
 * @brief TIM1 Base MSP Initialization callback (weak override)
 * 
 * Called by HAL_TIM_Base_Init() to configure peripherals.
 */
void HAL_TIM_Base_MspInit(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM1) {
        // Clock already enabled in uClock_HAL_Init() above
        // Additional MSP config can go here if needed
    }
}

/**
 * @brief TIM1 Base MSP Deinitialization callback (weak override)
 * 
 * Called by HAL_TIM_Base_DeInit() during cleanup.
 */
void HAL_TIM_Base_MspDeInit(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM1) {
        __HAL_RCC_TIM1_CLK_DISABLE();
        HAL_NVIC_DisableIRQ(TIM1_UP_TIM10_IRQn);
    }
}
