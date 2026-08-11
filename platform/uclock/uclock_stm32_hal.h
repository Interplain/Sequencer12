/*!
 * @file    uclock_stm32_hal.h
 * @brief   STM32Cube/HAL compatibility layer for uClock v2.3.0
 * 
 * Provides Arduino-compatible functions and STM32-specific implementations
 * for the real uClock library without Arduino/STM32Duino dependencies.
 */

#ifndef __UCLOCK_STM32_HAL_H__
#define __UCLOCK_STM32_HAL_H__

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "stm32f4xx_hal.h"
#include "core_cm4.h"

// ============================================================================
// TIME FUNCTIONS - Required by uClock core
// ============================================================================

/**
 * @brief Get elapsed milliseconds from HAL_GetTick()
 * @return Milliseconds since startup
 */
static inline uint32_t millis(void) {
    return HAL_GetTick();
}

/**
 * @brief Get elapsed microseconds from DWT cycle counter
 * 
 * STM32F405: SystemCoreClock = 168 MHz
 * - Each cycle = 1/168MHz ≈ 5.95 nanoseconds
 * - DWT_CYCCNT increments every CPU cycle
 * - Returns 32-bit microseconds (wraps every ~25.6 seconds)
 * 
 * LIMITATION: External clock sync with phase measurement requires
 * wrap-safe microsecond tracking (deferred to runtime phase).
 * For compile-only phase, this simple version is sufficient.
 * 
 * @return Microseconds since DWT_CYCCNT reset
 */
static inline uint32_t micros(void) {
    uint32_t cycles = DWT->CYCCNT;
    // Convert cycles to microseconds at 168 MHz
    // Wraps every 2^32 / 168,000,000 ≈ 25.6 seconds
    return cycles / 168U;
}

// ============================================================================
// INTERRUPT CONTROL - Critical section protection with PRIMASK preservation
// ============================================================================

/**
 * @brief Execute code block atomically with interrupt state preservation
 * 
 * Saves current interrupt state (PRIMASK), disables interrupts,
 * executes statement, then restores PRIMASK. Proper for nested interrupt
 * contexts.
 * 
 * Usage: ATOMIC(shared_var = new_value)
 */
#define ATOMIC(X) \
    { \
        uint32_t _primask = __get_PRIMASK(); \
        __disable_irq(); \
        X; \
        __set_PRIMASK(_primask); \
    }

// ============================================================================
// TIMER BACKEND INTERFACE - Called by uClock core
// ============================================================================

/**
 * @brief Initialize hardware timer for uClock (called by uClock.init())
 * 
 * Configures TIM1 on APB2 (168 MHz) with dynamic microsecond intervals.
 * In compile-only phase, this is NOT called.
 * 
 * @param us_interval Desired update interval in microseconds
 *                    (PSC=168-1 creates 1MHz base, ARR=us_interval-1)
 */
void initTimer(uint32_t us_interval);

/**
 * @brief Update timer interval dynamically (called on BPM changes)
 * 
 * Changes TIM1 period without stopping timer.
 * In compile-only phase, this is NOT called.
 * 
 * @param us_interval New interval in microseconds
 */
void setTimer(uint32_t us_interval);

/**
 * @brief Forward declaration: interrupt handler entry point
 * 
 * Defined in uClock.cpp; called from TIM1_UP_TIM10_IRQHandler
 * when interrupt is enabled (not in compile-only phase).
 */
extern void uClockHandler(void);

#endif /* __UCLOCK_STM32_HAL_H__ */
