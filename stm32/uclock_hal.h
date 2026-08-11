#ifndef UCLOCK_HAL_H
#define UCLOCK_HAL_H

/**
 * @file uclock_hal.h
 * @brief uClock STM32Cube HAL integration for Sequencer12
 * 
 * Provides hardware timer (TIM1) abstraction for uClock library.
 * Compile-only integration; no impact on existing sequencer behavior.
 */

#include "stm32f4xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize uClock hardware timer (TIM1 on STM32F405)
 * 
 * Sets up TIM1 as a 1kHz timer for uClock timing/clock synchronization.
 * - TIM1 clock: 168 MHz (APB2)
 * - Prescaler: 16800-1 to get 10 kHz base
 * - Period: 9 for 1 kHz interrupt (10000 / 10 = 1000 Hz)
 * - Interrupt: TIM1_UP_TIM10_IRQn
 * 
 * Call this after SystemClock_Config() has completed.
 */
void uClock_HAL_Init(void);

/**
 * @brief Start uClock hardware timer
 * 
 * Enables TIM1 update interrupt and starts counter.
 */
void uClock_HAL_Start(void);

/**
 * @brief Stop uClock hardware timer
 * 
 * Disables TIM1 interrupt and stops counter.
 */
void uClock_HAL_Stop(void);

/**
 * @brief Handler for TIM1 update interrupt
 * 
 * Called by TIM1_UP_TIM10_IRQHandler in interrupt context.
 * Routes timing tick to uClock library.
 */
void uClock_HAL_TimerTick(void);

#ifdef __cplusplus
}
#endif

#endif /* UCLOCK_HAL_H */
