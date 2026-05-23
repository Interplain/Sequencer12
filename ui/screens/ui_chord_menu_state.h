/*
 * ui_chord_menu_state.h
 * 
 * State container for CHORD_MENU screen.
 * 
 * This header defines the state owned by the CHORD_MENU screen.
 * It is intentionally renderer-agnostic and does not depend on display
 * or rendering infrastructure. This allows state ownership to remain
 * independent from rendering implementation.
 * 
 * Rendering is handled by the caller (ui_sequencer.c) via ui_display.h.
 */

#ifndef UI_CHORD_MENU_STATE_H
#define UI_CHORD_MENU_STATE_H

#include <stdint.h>

/* State owned by CHORD_MENU screen */
typedef struct {
    uint8_t step;                    /* which step (1-12) we're editing */
    uint8_t selected_chord_idx;      /* current selection (0-17) */
    uint8_t last_predefined_chord;   /* for Shift+Press toggle user/predefined */
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
 *   - Rendering handled by display module (ui_display.c)
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
