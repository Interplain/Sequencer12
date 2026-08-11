/**
 * @file uClock.h (minimal stub for compile-only integration)
 * 
 * Stub interface to uClock library for STM32F405 Sequencer12.
 * Provides minimal API to allow compilation without full uClock library.
 * 
 * This stub can be replaced with the full midilab/uClock library later.
 */

#ifndef UCLOCK_H
#define UCLOCK_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Minimal uClock object (stub)
 * 
 * In full implementation, this would be the complete uClock instance.
 * For compile-only integration, this is a placeholder.
 */
typedef struct {
    // Placeholder - full library would have extensive state here
    unsigned char reserved;
} uClockType;

/**
 * @brief Global uClock instance (stub)
 * 
 * Will be provided by full library when integrated.
 */
extern uClockType uClock;

/**
 * @brief Handle one tick from hardware timer
 * 
 * Stub implementation for compile-only.
 * Full library will synchronize tempo tracking here.
 */
static inline void uClock_handleTimerTick(void)
{
    // Stub: no-op in compile-only mode
    // Full library: process BPM clock, handle sync, etc.
}

#ifdef __cplusplus
}
#endif

#endif /* UCLOCK_H */
