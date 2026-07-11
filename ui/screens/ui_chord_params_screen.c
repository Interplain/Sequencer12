#include "ui_chord_params_screen.h"
#include "ui_input.h"
#include <string.h>

static uint8_t s_chord_params_render_buffer[128];
static ChordParamsScreenState s_state;

static void ChordParams_OnEnter(void)
{
    s_state.footer_action = PARAM_ACTION_MAIN;
}

static void ChordParams_OnUpdate(void)
{
    /* No periodic work yet. */
}

static void ChordParams_OnInput(InputType input_type, int8_t value)
{
    if (input_type == INPUT_ENCODER_DELTA)
    {
        if (UI_Input_IsShiftHeld())
        {
            UI_Display_NavigateParamFooterActions(value, s_state.step, &s_state.chord, s_state.param_cursor, &s_state.footer_action);
        }
        else if (s_state.footer_action != PARAM_ACTION_SAVE)
        {
            UI_Display_NavigateChordParams(value, s_state.step, &s_state.chord, s_state.param_cursor, s_state.footer_action);
        }
    }
}

static void ChordParams_OnExit(ScreenExitReason reason)
{
    (void)reason;
}

static void ChordParams_OnDraw(void)
{
    UI_Display_DrawChordParams(s_state.step, &s_state.chord, s_state.param_cursor, s_state.footer_action);
}

static UiScreen s_screen = {
    .name = "ChordParams",
    .on_enter = ChordParams_OnEnter,
    .on_update = ChordParams_OnUpdate,
    .on_input = ChordParams_OnInput,
    .on_exit = ChordParams_OnExit,
    .on_draw = ChordParams_OnDraw,
    .owned_region = {0, 0, UI_SCREEN_W, UI_SCREEN_H},
    .render_plan = {
        .rects = {UI_REGION_MENU_TOP, UI_REGION_MENU_FOOTER, UI_REGION_MENU_LIST},
        .region_count = 3,
        .pending = 1,
        .has_buffer = 1,
        .buffer_ready = 1,
        .buffer_size = sizeof(s_chord_params_render_buffer),
        .buffer = s_chord_params_render_buffer,
        .transaction_id = 1
    },
    .is_active = 0,
    .is_dirty = 1
};

UiScreen* UI_ChordParamsScreen_Get(void)
{
    return &s_screen;
}

void UI_ChordParamsScreen_SetContext(uint8_t step, const ChordParams* chord, uint8_t param_cursor)
{
    s_state.step = step;
    s_state.chord = *chord;
    s_state.param_cursor = param_cursor;
}

void UI_ChordParamsScreen_SetFooterAction(uint8_t action)
{
    if (action < PARAM_ACTION_COUNT)
    {
        s_state.footer_action = action;
    }
}

uint8_t UI_ChordParamsScreen_GetFooterAction(void)
{
    return s_state.footer_action;
}

void UI_ChordParamsScreen_CopyToChord(ChordParams* chord)
{
    if (chord != NULL)
    {
        *chord = s_state.chord;
    }
}
