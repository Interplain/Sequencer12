#ifndef TIMING_PROBE_H
#define TIMING_PROBE_H

/* Temporary, removable timing instrumentation (build-flag gated).
 * Uses the Cortex-M4 DWT free-running cycle counter (already enabled
 * elsewhere by uclock_stm32_hal.cpp, but (re-)initialised here too so this
 * module works independently of uClock's init order).
 *
 * Zero cost / zero behavior change when S12_TIMING_PROBE_ENABLE is not
 * defined: TIMING_PROBE_BEGIN()/END() collapse to a no-op constant and a
 * cast-to-void, no function call at all. */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern volatile uint32_t g_timing_max_ui_update_us;
extern volatile uint32_t g_timing_max_on_draw_us;
extern volatile uint32_t g_timing_max_draw_rgb565_us;
extern volatile uint32_t g_timing_max_service_persistence_us;
extern volatile uint32_t g_timing_max_save_song_us;
extern volatile uint32_t g_timing_max_pattern_write_us;

#if defined(S12_TIMING_PROBE_ENABLE)

void TimingProbe_Init(void);
uint32_t TimingProbe_Begin(void);
void TimingProbe_End(volatile uint32_t* max_us_slot, uint32_t start_cycles);

#define TIMING_PROBE_BEGIN()        TimingProbe_Begin()
#define TIMING_PROBE_END(slot, t)   TimingProbe_End(&(slot), (t))

#else

#define TIMING_PROBE_BEGIN()        ((uint32_t)0u)
#define TIMING_PROBE_END(slot, t)   ((void)(t))

#endif

#ifdef __cplusplus
}
#endif

#endif
