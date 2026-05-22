/*
 * ui_screens/ui_chord_menu_state.h
 * Chord Menu screen state container.
 *
 * This file defines the state ownership model for the CHORD_MENU screen.
 * No functional integration yet; this is purely structural scaffolding.
 *
 * CHORD_MENU responsibilities:
 * - Allow user to select a chord type (0=clear, 1-16=predefined, 17=user chords)
 * - Display current step and selected chord
 * - Support toggle between predefined and user chord sources (Shift+Encoder Press)
 * - Transition to CHORD_PARAMS or USER_CHORD_MENU on selection
 */

#ifndef UI_CHORD_MENU_STATE_H
#define UI_CHORD_MENU_STATE_H

#include <stdint.h>
#include "ui_display.h"  /* For ChordParams struct */

typedef struct {
    /* The step being edited (1-12) */
    uint8_t step;

    /* Currently selected chord index (0-17) */
    uint8_t selected_chord_idx;

    /* Last predefined chord index, for Shift+Press toggle between sources */
    uint8_t last_predefined_chord;
} ChordMenuState;

/*
 * CHORD_MENU Screen Lifecycle (future implementation):
 *
 * on_enter():
 *   - Capture current step from parent context
 *   - Load current chord type for this step
 *   - Mark dirty for redraw
 *
 * on_update():
 *   - No periodic updates needed (animation handled by display module)
 *
 * on_input(type, value):
 *   - INPUT_ENCODER_DELTA: Navigate chord buttons
 *   - INPUT_ENCODER_PRESS: Select/confirm chord, may transition to CHORD_PARAMS or USER_CHORD_MENU
 *   - INPUT_ENCODER_PRESS + SHIFT: Toggle predefined/user source
 *
 * on_draw():
 *   - Render chord menu in owned region (MENU_LIST region)
 *   - Display step header, chord buttons, footer
 *
 * on_exit(reason):
 *   - If SAVE/SELECT: apply selected chord to step (state already applied)
 *   - If BACK/DISCARD: revert to previous chord type
 *
 * Owned UI Region: UI_REGION_MENU_TOP, UI_REGION_MENU_LIST, UI_REGION_MENU_FOOTER
 * Input Owner: Encoder (all rotations and presses)
 * State Owner: ChordMenuState (local to screen module in Phase 3)
 */

#endif /* UI_CHORD_MENU_STATE_H */
