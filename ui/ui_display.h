#ifndef UI_DISPLAY_H
#define UI_DISPLAY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* ── Transport states ────────────────────────────────────────────────────── */
typedef enum
{
    TRANSPORT_STOPPED = 0,
    TRANSPORT_PLAYING,
    TRANSPORT_RECORDING,
    TRANSPORT_PLAYING_RECORDING
} TransportState;

/* ── Init ────────────────────────────────────────────────────────────────── */
void UI_Display_Init(void);

/* ── Grid ────────────────────────────────────────────────────────────────── */
void UI_Display_DrawGrid(void);
void UI_Display_DrawStepBox(uint8_t step, uint8_t selected, uint8_t active);

/* ── Header ──────────────────────────────────────────────────────────────── */
void UI_Display_DrawHeader(uint16_t bpm, TransportState state, uint8_t rec_armed);

/* ── Transport strip ─────────────────────────────────────────────────────── */
void UI_Display_SetShiftIndicator(uint8_t active);

/* ── Debug time ────────────────────────────────────────────────────────────── */
void UI_Display_DrawDebugRow(uint16_t bpm, uint8_t step, uint32_t elapsed_ms);

#ifdef __cplusplus
}
#endif

#endif