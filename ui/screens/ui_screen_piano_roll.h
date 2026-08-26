#ifndef UI_SCREEN_PIANO_ROLL_H
#define UI_SCREEN_PIANO_ROLL_H

#include <stdint.h>
#include "ui_screen_base.h"

/* ── Piano Roll Screen Types ─────────────────────────────────────────────── */

typedef enum {
    PIANO_ROLL_MODE_KEYBOARD = 0,  /* Standalone piano keyboard (e.g., create chord) */
    PIANO_ROLL_MODE_STEP_ROLL = 1  /* Per-step piano roll (inspect/edit notes) */
} PianoRollMode;

/* Screen context: what the piano roll should display. */
typedef struct {
    PianoRollMode mode;
    uint8_t step;              /* Which step we're editing (1-12 for step roll) */
    uint16_t note_mask;        /* Which notes are selected (bit 0=C, ..., bit 11=B) */
    uint8_t selected_key;      /* Currently selected note (0-11) */
    uint8_t selected_midi_note;/* Currently selected MIDI note (0-127) */
    uint8_t top_visible_midi;  /* Top row MIDI note for the fixed 19-row viewport */
    uint8_t slot_index;        /* Active sub-step slot within the step */
    uint8_t slot_count;        /* Number of sub-step slots in this step */
    uint8_t grid_division;     /* 1=1/4, 2=1/8, 4=1/16, 8=1/32 */
    uint8_t slot_notes[4];     /* CV1..CV4 absolute MIDI notes, 0xFF when empty */
    uint8_t cv_absolute_valid; /* 1 when slot_notes are authoritative absolute CV lanes */
    uint8_t arp_on;            /* 1 when arp mode is active for current pattern */
    uint8_t length_mode_active; /* 1 while M8 LENGTH mode is active */
    uint8_t pending_length;     /* Pending grid length while in LENGTH mode */
    const char* key_label;     /* Key label for step editor header */
    const char* scale_label;   /* Scale label for step editor header */
    uint8_t clear_confirm_active;       /* 1 when YES/NO confirmation is visible */
    uint8_t clear_confirm_yes_selected; /* 1 when YES is selected, otherwise NO */
    uint8_t bar_skipped;      /* 1 when current bar is marked Skip */
    const char* confirm_message;        /* Confirmation prompt, e.g. CLEAR BAR05? */
    const char* title;         /* Screen title (e.g., "Create Chord" or "CH 1: Piano Roll") */
    const char* footer_label;  /* Footer action label (e.g., "SAVE/DONE") */
} PianoRollContext;

/* ── Get the screen instance ─────────────────────────────────────────────– */
UiScreen* UI_PianoRollScreen_Get(void);

/* ── Context management ──────────────────────────────────────────────────– */
void UI_PianoRollScreen_SetContext(const PianoRollContext* ctx);

/* ── State queries ───────────────────────────────────────────────────────– */
uint8_t UI_PianoRollScreen_GetSelectedKey(void);
uint16_t UI_PianoRollScreen_GetNoteMask(void);
void UI_PianoRollScreen_SetNoteMask(uint16_t mask);

/* ── Navigation (called by parent UI) ────────────────────────────────────– */
void UI_PianoRollScreen_NavigateKey(int8_t delta);
void UI_PianoRollScreen_ToggleKey(uint8_t key);

/* ── Mode management ────────────────────────────────────────────────────── */
PianoRollMode UI_PianoRollScreen_GetMode(void);

/* ── Force a full redraw ────────────────────────────────────────────────── */
void UI_PianoRollScreen_ForceRedraw(void);

/* ── Reset cached layout/state for guaranteed clean re-entry ────────────── */
void UI_PianoRollScreen_ResetCache(void);

#endif /* UI_SCREEN_PIANO_ROLL_H */
