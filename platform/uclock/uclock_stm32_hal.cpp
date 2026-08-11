/*!
 * @file    uclock_stm32_hal.cpp
 * @brief   STM32Cube/HAL backend for uClock v2.3.0
 * 
 * Implements TIM1-based timer control and linker reference for compile-only
 * footprint measurement. TIM1 is configured but NOT started in this phase.
 */

#include "uclock_stm32_hal.h"
#include "uClock.h"

// Global TIM1 handle (used if initTimer is called)
static TIM_HandleTypeDef htim1_uclock;

// ============================================================================
// INTERNAL: Initialize DWT Cycle Counter
// ============================================================================

/**
 * @brief Enable Data Watchpoint and Trace (DWT) cycle counter
 * 
 * DWT provides high-resolution cycle counter for microsecond precision
 * via the micros() function. Runs at SystemCoreClock (168 MHz).
 */
static void initDWT(void) {
    // Enable debug module (required for DWT access)
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    
    // Enable DWT cycle counter
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    
    // Reset counter to zero
    DWT->CYCCNT = 0;
}

// ============================================================================
// TIMER BACKEND: Dynamic TIM1 Configuration (Dormant in compile-only phase)
// ============================================================================

/**
 * @brief Initialize TIM1 for uClock timing
 * 
 * Configures TIM1 for dynamic microsecond intervals:
 * - Prescaler: 168-1 (creates 1 MHz base from 168 MHz APB2)
 * - Period: us_interval-1 (updates every us_interval microseconds)
 * - Interrupt: TIM1_UP_TIM10_IRQn (priority 8, non-critical)
 * 
 * COMPILE-ONLY PHASE: Function is present but NOT CALLED.
 * TIM1 is configured but NOT started (HAL_TIM_Base_Start_IT not called).
 * This measures footprint with zero runtime impact.
 * 
 * @param us_interval Timer update interval in microseconds
 */
void initTimer(uint32_t us_interval) {
    initDWT();  // Enable cycle counter for micros()
    
    // Enable TIM1 clock on APB2
    __HAL_RCC_TIM1_CLK_ENABLE();
    
    // Configure TIM1 for dynamic microsecond intervals
    // APB2 clock: 168 MHz
    // PSC: 168-1 → divides to 1 MHz (each tick = 1 µs)
    // ARR: us_interval-1 → period in microseconds
    htim1_uclock.Instance = TIM1;
    htim1_uclock.Init.Prescaler = 168 - 1;
    htim1_uclock.Init.Period = us_interval - 1;
    htim1_uclock.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim1_uclock.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim1_uclock.Init.RepetitionCounter = 0;
    htim1_uclock.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    
    HAL_TIM_Base_Init(&htim1_uclock);
    
    // Set interrupt priority (8 = low, below critical SPI/I2C at 1-3)
    HAL_NVIC_SetPriority(TIM1_UP_TIM10_IRQn, 8, 0);
    
    // Enable interrupt in NVIC (but timer not started)
    HAL_NVIC_EnableIRQ(TIM1_UP_TIM10_IRQn);
    
    // COMPILE-ONLY: Timer configured but NOT started
    // In runtime phase, would call: HAL_TIM_Base_Start_IT(&htim1_uclock);
}

/**
 * @brief Update TIM1 interval dynamically
 * 
 * Changes timer period without stopping. Used when BPM/timing changes.
 * COMPILE-ONLY PHASE: NOT CALLED.
 * 
 * @param us_interval New interval in microseconds
 */
void setTimer(uint32_t us_interval) {
    // Update period configuration
    htim1_uclock.Init.Period = us_interval - 1;
    
    // Write to ARR (auto-reload register)
    TIM1->ARR = us_interval - 1;
    
    // Force update event to apply new period immediately
    TIM1->EGR |= TIM_EGR_UG;
}

// ============================================================================
// INTERRUPT HANDLER (Defined but dormant in compile-only phase)
// ============================================================================

/**
 * @brief TIM1 Update/TIM10 interrupt handler
 * 
 * Triggered when TIM1 counter reaches period value.
 * Routes interrupt to uClockHandler() in uClock core.
 * 
 * COMPILE-ONLY PHASE: Handler is present for completeness but
 * TIM1 is not started, so this is never called.
 */
extern "C" void TIM1_UP_TIM10_IRQHandler(void) {
    if (__HAL_TIM_GET_FLAG(&htim1_uclock, TIM_FLAG_UPDATE)) {
        __HAL_TIM_CLEAR_FLAG(&htim1_uclock, TIM_FLAG_UPDATE);
        
        // Call uClock's main interrupt handler (defined in uClock.cpp)
        uClockHandler();
    }
}

// ============================================================================
// LINKER REFERENCE: Ensure uClock library is linked
// ============================================================================

/**
 * @brief Dummy function to ensure uClock object is linked
 * 
 * With function-section garbage collection (-ffunction-sections),
 * unused uClock and its methods could be stripped even if compiled.
 * This function calls a uClock method, forcing the linker to include
 * the entire uClock object and its vtable.
 * 
 * COMPILE-ONLY PHASE: Called once from main() to ensure linking.
 * Calls uClock.init() which configures TIM1 but doesn't start it.
 * Returns immediately; no functional impact at runtime since timer
 * was not started (no interrupt enable, no timer start).
 * 
 * @return Address of global uClock object (never used)
 */
extern "C" int uClock_linkage_check(void) {
    // Forward declare and reference uClock to force linking
    using namespace umodular::clock;
    
    // Call init() to force uClock methods to be linked.
    // init() configures TIM1 but does NOT start it or enable interrupts,
    // so this has zero runtime impact in compile-only phase.
    // This forces the compiler to keep all uClock symbols during linking.
    uClock.init();
    
    // Return address for measurement/verification (never used)
    return (int)&uClock;
}
