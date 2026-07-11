#include "ui_chord_menu_screen.h"
#include <string.h>

static uint8_t s_chord_menu_render_buffer[128];
static ChordMenuState s_state;

static void ChordMenu_OnEnter(void)
{
    if (s_state.selected_chord_idx > 17)
    {
        s_state.selected_chord_idx = 0;
    }
}

static void ChordMenu_OnUpdate(void)
{
    /* No periodic animation needed for this screen yet. */
}

static void ChordMenu_OnInput(InputType input_type, int8_t value)
{
    if (input_type == INPUT_ENCODER_DELTA)
    {
        UI_Display_NavigateChordMenu(value, s_state.step, &s_state.chord, &s_state.selected_chord_idx);
    }
}

static void ChordMenu_OnExit(ScreenExitReason reason)
{
    (void)reason;
}

static void ChordMenu_OnDraw(void)
{
    UI_Display_DrawChordMenu(s_state.step, &s_state.chord, s_state.selected_chord_idx);
}

static UiScreen s_screen = {
    .name = "ChordMenu",
    .on_enter = ChordMenu_OnEnter,
    .on_update = ChordMenu_OnUpdate,
    .on_input = ChordMenu_OnInput,
    .on_exit = ChordMenu_OnExit,
    .on_draw = ChordMenu_OnDraw,
    .owned_region = {0, 0, UI_SCREEN_W, UI_SCREEN_H},
    .render_plan = {
        .rects = {UI_REGION_MENU_TOP, UI_REGION_MENU_FOOTER, UI_REGION_MENU_LIST},
        .region_count = 3,
        .pending = 1,
        .has_buffer = 1,
        .buffer_ready = 1,
        .buffer_size = sizeof(s_chord_menu_render_buffer),
        .buffer = s_chord_menu_render_buffer,
        .transaction_id = 1
    },
    .is_active = 0,
    .is_dirty = 1
};

UiScreen* UI_ChordMenuScreen_Get(void)
{
    return &s_screen;
}

void UI_ChordMenuScreen_SetContext(uint8_t step, const ChordParams* chord)
{
    s_state.step = step;
    s_state.chord = *chord;
    if (s_state.selected_chord_idx > 17)
    {
        s_state.selected_chord_idx = 0;
    }
}

void UI_ChordMenuScreen_SetSelection(uint8_t selection)
{
    if (selection <= 17)
    {
        s_state.selected_chord_idx = selection;
    }
}

uint8_t UI_ChordMenuScreen_GetSelection(void)
{
    return s_state.selected_chord_idx;
}
