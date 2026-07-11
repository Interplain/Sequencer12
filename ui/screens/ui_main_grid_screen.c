#include "ui_main_grid_screen.h"
#include "ui_transport.h"
#include "sequencer_bridge.h"
#include <string.h>

static MainGridScreenState s_state;
static uint8_t s_grid_render_buffer[128];

static void MainGrid_OnEnter(void)
{
    /* Nothing special yet. */
}

static void MainGrid_OnUpdate(void)
{
    /* No periodic work yet. */
}

static void MainGrid_OnInput(InputType input_type, int8_t value)
{
    (void)input_type;
    (void)value;
}

static void MainGrid_OnExit(ScreenExitReason reason)
{
    (void)reason;
}

static void MainGrid_OnDraw(void)
{
    UI_Display_FastReturnToGrid();
    UI_Display_DrawMainGridComposed(s_state.selected_step,
                                    s_state.active_step,
                                    s_state.has_chord,
                                    UI_Transport_GetBPM(),
                                    UI_Transport_GetState(),
                                    UI_Transport_IsRecArmed(),
                                    Bridge_GetCurrentPattern(),
                                    Bridge_GetCurrentStep(),
                                    Bridge_GetCompletedLoops(),
                                    Bridge_GetRunTimeMs());
}

static UiScreen s_screen = {
    .name = "MainGrid",
    .on_enter = MainGrid_OnEnter,
    .on_update = MainGrid_OnUpdate,
    .on_input = MainGrid_OnInput,
    .on_exit = MainGrid_OnExit,
    .on_draw = MainGrid_OnDraw,
    .owned_region = {0, 0, UI_SCREEN_W, UI_SCREEN_H},
    .render_plan = {
        .rects = {UI_REGION_HEADER, UI_REGION_STATUS, UI_REGION_GRID},
        .region_count = 3,
        .pending = 1,
        .has_buffer = 1,
        .buffer_ready = 1,
        .buffer_size = sizeof(s_grid_render_buffer),
        .buffer = s_grid_render_buffer,
        .transaction_id = 1
    },
    .is_active = 0,
    .is_dirty = 1
};

UiScreen* UI_MainGridScreen_Get(void)
{
    return &s_screen;
}

void UI_MainGridScreen_SetContext(uint8_t selected_step, uint8_t active_step, const uint8_t* has_chord_flags)
{
    s_state.selected_step = selected_step;
    s_state.active_step = active_step;
    if (has_chord_flags != NULL)
    {
        memcpy(s_state.has_chord, has_chord_flags, sizeof(s_state.has_chord));
    }
}

void UI_MainGridScreen_DrawStep(uint8_t step, uint8_t selected, uint8_t active, uint8_t has_chord)
{
    if (step < 1 || step > 12) return;
    UI_Display_DrawStepBox(step, selected, active, has_chord);
}

void UI_MainGridScreen_DrawHeader(uint16_t bpm, TransportState state, uint8_t rec_armed)
{
    UI_Display_DrawHeader(bpm, state, rec_armed);
}

void UI_MainGridScreen_DrawStatusRow(uint8_t pattern, uint8_t step, uint32_t loops, uint32_t run_time_ms)
{
    UI_Display_DrawStatusRow(pattern, step, loops, run_time_ms);
}
