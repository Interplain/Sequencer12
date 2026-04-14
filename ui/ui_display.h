#ifndef UI_DISPLAY_H
#define UI_DISPLAY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Chord Parameters ────────────────────────────────────────────────────── */
typedef struct {
    uint8_t root_key;     /* 0-11: C=0, C#=1, D=2, ..., B=11 */
    uint8_t chord_type;   /* 0=clear, 1=major, 2=minor, etc. */
    uint8_t arp_pattern;  /* 0=block, 1=up, 2=down, 3=up-down, 4=random */
    uint8_t duration;     /* 0=16th, 1=8th, 2=quarter, 3=dotted 8th */
    uint8_t loop_count;   /* 1-16 pattern repeats before chain advance */
} ChordParams;

typedef enum
{
    PARAM_ACTION_MAIN = 0,
    PARAM_ACTION_PREV,
    PARAM_ACTION_NEXT,
    PARAM_ACTION_SAVE,
    PARAM_ACTION_COUNT
} ParamFooterAction;

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
void UI_Display_DrawParamFooterActions(uint8_t selected_action);
void UI_Display_NavigateParamFooterActions(int8_t delta, uint8_t step, const ChordParams* chord, uint8_t param_cursor);
uint8_t UI_Display_GetSelectedParamAction(void);
void UI_Display_SetSelectedParamAction(uint8_t action);

/* ── Timing Menu ─────────────────────────────────────────────────────────── */
void UI_Display_DrawTimingMenu(uint8_t step_count,
                               uint8_t step_division,
                               uint8_t ts_num,
                               uint8_t ts_den,
                               uint8_t swing,
                               uint8_t cursor,
                               uint8_t footer_action,
                               uint8_t has_unsaved_changes);
void UI_Display_NavigateTimingFooter(int8_t delta);
uint8_t UI_Display_GetTimingFooterAction(void);
void UI_Display_SetTimingFooterAction(uint8_t action);

/* ── User Chord Menu ─────────────────────────────────────────────────────── */
void UI_Display_DrawUserChordMenu(void);
void UI_Display_NavigateUserChordMenu(int8_t delta);
uint8_t UI_Display_GetUserChordMenuSelection(void);

/* ── User Chord Create (Piano keyboard) ────────────────────────────────── */
void UI_Display_DrawPianoKeyboard(const uint16_t note_mask, uint8_t selected_key);
void UI_Display_NavigatePianoKeyboard(int8_t delta);
uint8_t UI_Display_GetSelectedPianoKey(void);
void UI_Display_TogglePianoKey(uint8_t key);
uint16_t UI_Display_GetCurrentNoteMask(void);
void UI_Display_SetPianoNoteMask(uint16_t mask);

/* ── User Chord Load ─────────────────────────────────────────────────────── */
void UI_Display_DrawUserChordLoad(void);
void UI_Display_NavigateUserChordLoad(int8_t delta);
uint8_t UI_Display_GetSelectedUserChord(void);

#ifdef __cplusplus
}
#endif

#endif