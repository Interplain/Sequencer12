#ifndef UI_DISPLAY_H
#define UI_DISPLAY_H

#include <stdint.h>

/* ── Chord Parameters ────────────────────────────────────────────────────── */
typedef struct {
    uint8_t root_key;     /* 0-11: C=0, C#=1, D=2, ..., B=11 */
    uint8_t chord_type;   /* 0=clear, 1=major, 2=minor, etc. */
    uint8_t arp_pattern;  /* 0=block, 1=up, 2=down, 3=up-down, 4=random */
    uint8_t duration;     /* 0=16th, 1=8th, 2=quarter, 3=dotted 8th */
} ChordParams;

/* ── Transport states ────────────────────────────────────────────────────── */

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
void UI_Display_DrawStepBox(uint8_t step, uint8_t selected, uint8_t active, uint8_t has_chord);

/* ── Header ──────────────────────────────────────────────────────────────── */
void UI_Display_DrawHeader(uint16_t bpm, TransportState state, uint8_t rec_armed);

/* ── Transport strip ─────────────────────────────────────────────────────── */
void UI_Display_SetShiftIndicator(uint8_t active);

/* ── Time Display ────────────────────────────────────────────────────────────── */
void UI_Display_DrawStatusRow(uint8_t pattern,
                              uint8_t step,
                              uint32_t loops,
                              uint32_t run_time_ms);

/* ── Chord Menu ───────────────────────────────────────────────────────────── */
void UI_Display_DrawChordMenu(uint8_t step, const ChordParams* chord);
void UI_Display_NavigateChordMenu(int8_t delta, uint8_t step, const ChordParams* chord);
uint8_t UI_Display_GetSelectedChord(void);
void UI_Display_SetChordSelection(uint8_t selection);

/* ── Chord Parameters ─────────────────────────────────────────────────────── */
void UI_Display_DrawChordParams(uint8_t step, const ChordParams* chord, uint8_t param_cursor);
void UI_Display_NavigateChordParams(int8_t delta, uint8_t step, ChordParams* chord, uint8_t param_cursor);

#ifdef __cplusplus
}
#endif

#endif