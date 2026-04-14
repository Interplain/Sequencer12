#include "ui_sequencer.h"
#include "ui_display.h"
#include "ui_input.h"
#include "ui_transport.h"
#include "sequencer_bridge.h"
#include "stm32/user_chord_bridge.h"
#include "stm32f4xx_hal.h"
#include <string.h>
#include <stdio.h>

/* ── UI Modes ────────────────────────────────────────────────────────────── */
typedef enum {
    UI_MODE_GRID,             /* main 12-step grid view */
    UI_MODE_CHORD_MENU,       /* chord selection submenu for current step */
    UI_MODE_CHORD_PARAMS,     /* chord parameter editor */
    UI_MODE_TIMING_MENU,      /* pattern timing editor */
    UI_MODE_USER_CHORD_MENU,  /* user chord: Create/Load/Back */
    UI_MODE_USER_CHORD_CREATE,/* user chord: piano keyboard editor */
    UI_MODE_USER_CHORD_LOAD   /* user chord: load from library */
} UiMode;

/* ── State ───────────────────────────────────────────────────────────────── */
static UiMode      s_ui_mode        = UI_MODE_GRID;
static uint8_t     s_selected_step  = 1;   /* cursor position 1-12      */
static uint8_t     s_active_step    = 0;   /* currently playing step    */
static uint8_t     s_last_active    = 0;   /* previous playing step     */
static uint8_t     s_menu_step      = 1;   /* which step's chord we're editing */
static ChordParams s_step_chords[12];     /* chord parameters for each step */
static uint8_t     s_param_cursor   = 0;   /* which parameter we're editing (0=root, 1=type, 2=arp, 3=duration) */
static uint8_t     s_timing_cursor  = 0;   /* timing field cursor */
static uint8_t     s_timing_step_count = 12;
static uint8_t     s_timing_step_division = 4;
static uint8_t     s_timing_ts_num = 4;
static uint8_t     s_timing_ts_den = 4;
static uint8_t     s_timing_swing = 0;
static uint8_t     s_last_predefined_chord = 0;

/* ── User Chord State ────────────────────────────────────────────────────── */
static uint16_t    s_user_chord_note_mask = 0; /* note mask being created */

static uint8_t UI_Sequencer_TimingDraftIsDirty(void);
static void UI_Sequencer_CommitChordDraft(void);

/* ── Status row caching (prevent flicker) ─────────────────────────────────── */
static uint8_t  s_last_pattern    = 0xFF;
static uint8_t  s_last_step       = 0xFF;
static uint32_t s_last_loops      = 0xFFFFFFFF;
static uint32_t s_last_run_time   = 0xFFFFFFFF;

static void UI_Sequencer_ExitToGrid(void)
{
    s_ui_mode = UI_MODE_GRID;
    UI_Display_Init();

    for (uint8_t i = 1; i <= 12; i++)
    {
        uint8_t is_selected = (i == s_selected_step);
        uint8_t is_active = (i == s_active_step);
        UI_Display_DrawStepBox(i, is_selected, is_active, s_step_chords[i-1].chord_type > 0);
    }

    uint8_t pattern = Bridge_GetCurrentPattern();
    uint8_t step = Bridge_GetCurrentStep();
    uint32_t loops = Bridge_GetCompletedLoops();
    uint32_t run_time = Bridge_GetRunTimeMs();
    UI_Display_DrawStatusRow(pattern, step, loops, run_time);
}

static void UI_Sequencer_DrawTimingMenu(void)
{
    UI_Display_DrawTimingMenu(s_timing_step_count,
                              s_timing_step_division,
                              s_timing_ts_num,
                              s_timing_ts_den,
                              s_timing_swing,
                              s_timing_cursor,
                              UI_Display_GetTimingFooterAction(),
                              UI_Sequencer_TimingDraftIsDirty());
}

static void UI_Sequencer_LoadTimingDraft(void)
{
    s_timing_step_count = Bridge_GetPatternStepCount();
    s_timing_step_division = Bridge_GetPatternStepDivision();
    s_timing_ts_num = Bridge_GetTimeSigNumerator();
    s_timing_ts_den = Bridge_GetTimeSigDenominator();
    s_timing_swing = Bridge_GetSwing();
}

static void UI_Sequencer_CommitTimingDraft(void)
{
    Bridge_SetPatternStepCount(s_timing_step_count);
    Bridge_SetPatternStepDivision(s_timing_step_division);
    Bridge_SetTimeSignature(s_timing_ts_num, s_timing_ts_den);
    Bridge_SetSwing(s_timing_swing);
}

static uint8_t UI_Sequencer_TimingDraftIsDirty(void)
{
    if (s_timing_step_count != Bridge_GetPatternStepCount()) return 1;
    if (s_timing_step_division != Bridge_GetPatternStepDivision()) return 1;
    if (s_timing_ts_num != Bridge_GetTimeSigNumerator()) return 1;
    if (s_timing_ts_den != Bridge_GetTimeSigDenominator()) return 1;
    if (s_timing_swing != Bridge_GetSwing()) return 1;
    return 0;
}

static void UI_Sequencer_CommitChordDraft(void)
{
    for (uint8_t i = 0; i < 12; i++)
    {
        Bridge_SetStepChordParams(i,
                                  s_step_chords[i].root_key,
                                  s_step_chords[i].chord_type,
                                  s_step_chords[i].arp_pattern,
                                  s_step_chords[i].duration);
    }

    Bridge_SetPatternRepeatCount(s_step_chords[s_menu_step - 1].loop_count);
}

/* ── Init ────────────────────────────────────────────────────────────────── */
void UI_Sequencer_Init(void)
{
    s_ui_mode       = UI_MODE_GRID;  // Start in grid mode
    s_selected_step = 1;
    s_active_step   = 0;
    s_last_active   = 0;
    s_menu_step     = 1;
    s_param_cursor  = 0;
    s_timing_cursor = 0;

    /* Initialize all chords to clear (no chord) */
    for (uint8_t i = 0; i < 12; i++) {
        s_step_chords[i].root_key = 0;    /* C */
        s_step_chords[i].chord_type = 0;  /* Clear */
        s_step_chords[i].arp_pattern = 0; /* Block */
        s_step_chords[i].duration = 1;    /* 8th notes */
        s_step_chords[i].loop_count = Bridge_GetPatternRepeatCount();
    }

    UI_Display_Init();    /* draws all boxes white, no selection */
    UI_Transport_Init();

    /* Draw initial grid with chord colors */
    for (uint8_t i = 1; i <= 12; i++)
    {
        uint8_t is_selected = (i == s_selected_step);
        uint8_t is_active = (i == s_active_step);
        UI_Display_DrawStepBox(i, is_selected, is_active, s_step_chords[i-1].chord_type > 0);
    }

    // TEMP: Test chord menu on startup
    // s_ui_mode = UI_MODE_CHORD_MENU;
    // UI_Display_DrawChordMenu(1);
}

/* ── Called from SysTick — 1ms ───────────────────────────────────────────── */
void UI_Sequencer_Tick1ms(void)
{
    Bridge_Tick1ms();
}

/* ── Direct step access — for dedicated button matrix ───────────────────── */
void UI_Sequencer_SelectStep(uint8_t step)
{
    if (step < 1 || step > 12) return;

    /* Update selection */
    uint8_t prev = s_selected_step;
    s_selected_step = step;

    if (s_ui_mode == UI_MODE_GRID)
    {
        /* In grid mode: just move cursor */
        if (prev != s_selected_step)
        {
            UI_Display_DrawStepBox(prev,            0, (prev           == s_active_step), s_step_chords[prev-1].chord_type > 0);
            UI_Display_DrawStepBox(s_selected_step, 1, (s_selected_step == s_active_step), s_step_chords[s_selected_step-1].chord_type > 0);
        }
    }
    else
    {
        /* In chord menu: switch to editing this step's chord */
        s_menu_step = step;
        UI_Display_DrawChordMenu(s_menu_step, &s_step_chords[s_menu_step - 1]);
    }
}

/* ── Update — called every 10ms from main loop ───────────────────────────── */
void UI_Sequencer_Update(void)
{
    /* ── Play / Record buttons ─────────────────────────────────────────── */
    uint8_t play_pressed       = UI_Input_IsPlayPressed();
    uint8_t shift_play_pressed = UI_Input_IsShiftPlayPressed();
    uint8_t rec_pressed        = UI_Input_IsRecPressed();
    uint8_t shift_rec_pressed  = UI_Input_IsShiftRecPressed();

    if (s_ui_mode == UI_MODE_GRID)
    {
        /* Transport controls only on the main grid */
        if (play_pressed)       UI_Transport_PlayStop();
        if (shift_play_pressed) UI_Transport_Reset();
        if (rec_pressed)        UI_Transport_RecArm();
        if (shift_rec_pressed)  UI_Transport_RecClear();
    }
    else
    {
        /* Repurposed in submenus:
         *   Play   = SAVE / commit and move forward
         *   Record = BACK / cancel and go back one level
         *   Encoder rotate = navigate
         *   Encoder press  = select / enter
         */

        /* ── Play = Save ──────────────────────────────────────────────── */
        if (play_pressed)
        {
            if (s_ui_mode == UI_MODE_CHORD_MENU)
            {
                /* Save: apply currently highlighted chord (same as select) */
                uint8_t selected_idx = UI_Display_GetSelectedChord();
                if (selected_idx == 17)
                {
                    s_ui_mode = UI_MODE_USER_CHORD_MENU;
                    UI_Display_DrawUserChordMenu();
                }
                else if (selected_idx == 0)
                {
                    s_step_chords[s_menu_step - 1].chord_type = 0;
                    UI_Sequencer_CommitChordDraft();
                    UI_Sequencer_ExitToGrid();
                }
                else
                {
                    s_step_chords[s_menu_step - 1].chord_type = selected_idx;
                    s_ui_mode = UI_MODE_CHORD_PARAMS;
                    s_param_cursor = 0;
                    s_step_chords[s_menu_step - 1].loop_count = Bridge_GetPatternRepeatCount();
                    UI_Display_SetSelectedParamAction(PARAM_ACTION_MAIN);
                    UI_Display_DrawChordParams(s_menu_step, &s_step_chords[s_menu_step - 1], s_param_cursor);
                }
            }
            else if (s_ui_mode == UI_MODE_CHORD_PARAMS)
            {
                /* Save: commit chord params and return to grid */
                UI_Sequencer_CommitChordDraft();
                UI_Sequencer_ExitToGrid();
            }
            else if (s_ui_mode == UI_MODE_TIMING_MENU)
            {
                /* Save: commit timing and return to grid */
                UI_Sequencer_CommitTimingDraft();
                UI_Sequencer_ExitToGrid();
            }
            else if (s_ui_mode == UI_MODE_USER_CHORD_MENU)
            {
                /* Save: activate the currently highlighted item */
                uint8_t selection = UI_Display_GetUserChordMenuSelection();
                if (selection == 0)
                {
                    s_ui_mode = UI_MODE_USER_CHORD_CREATE;
                    s_user_chord_note_mask = 0;
                    UI_Display_SetPianoNoteMask(0);
                    UI_Display_DrawPianoKeyboard(0, 0);
                }
                else if (selection == 1)
                {
                    s_ui_mode = UI_MODE_USER_CHORD_LOAD;
                    UI_Display_DrawUserChordLoad();
                }
                else
                {
                    s_ui_mode = UI_MODE_CHORD_MENU;
                    uint8_t current_type = s_step_chords[s_menu_step - 1].chord_type;
                    UI_Display_SetChordSelection(current_type);
                    UI_Display_DrawChordMenu(s_menu_step, &s_step_chords[s_menu_step - 1]);
                }
            }
            else if (s_ui_mode == UI_MODE_USER_CHORD_CREATE)
            {
                /* Save: store chord and return to user chord menu */
                s_user_chord_note_mask = UI_Display_GetCurrentNoteMask();
                if (s_user_chord_note_mask != 0)
                {
                    char auto_name[17];
                    uint8_t idx = Bridge_UserChord_GetCount();
                    snprintf(auto_name, sizeof(auto_name), "USER%02u", (unsigned)(idx + 1));
                    Bridge_UserChord_Save(auto_name, s_user_chord_note_mask);
                }
                s_ui_mode = UI_MODE_USER_CHORD_MENU;
                UI_Display_DrawUserChordMenu();
            }
            else if (s_ui_mode == UI_MODE_USER_CHORD_LOAD)
            {
                /* Save: load selected chord and apply to step */
                uint8_t chord_idx = UI_Display_GetSelectedUserChord();
                const UserChordInfo *chord_info = Bridge_UserChord_Get(chord_idx);
                if (chord_info)
                {
                    s_step_chords[s_menu_step - 1].chord_type = 0;
                    UI_Sequencer_CommitChordDraft();
                    UI_Sequencer_ExitToGrid();
                }
            }
        }

        /* ── Record = Back ────────────────────────────────────────────── */
        if (rec_pressed)
        {
            if (s_ui_mode == UI_MODE_CHORD_MENU || s_ui_mode == UI_MODE_TIMING_MENU)
            {
                UI_Sequencer_ExitToGrid();
            }
            else if (s_ui_mode == UI_MODE_CHORD_PARAMS)
            {
                /* Back to chord menu without committing */
                s_ui_mode = UI_MODE_CHORD_MENU;
                uint8_t current_type = s_step_chords[s_menu_step - 1].chord_type;
                UI_Display_SetChordSelection(current_type);
                UI_Display_DrawChordMenu(s_menu_step, &s_step_chords[s_menu_step - 1]);
            }
            else if (s_ui_mode == UI_MODE_USER_CHORD_MENU)
            {
                s_ui_mode = UI_MODE_CHORD_MENU;
                uint8_t current_type = s_step_chords[s_menu_step - 1].chord_type;
                UI_Display_SetChordSelection(current_type);
                UI_Display_DrawChordMenu(s_menu_step, &s_step_chords[s_menu_step - 1]);
            }
            else if (s_ui_mode == UI_MODE_USER_CHORD_CREATE)
            {
                /* Back: discard and return to user chord menu */
                s_ui_mode = UI_MODE_USER_CHORD_MENU;
                UI_Display_DrawUserChordMenu();
            }
            else if (s_ui_mode == UI_MODE_USER_CHORD_LOAD)
            {
                s_ui_mode = UI_MODE_USER_CHORD_MENU;
                UI_Display_DrawUserChordMenu();
            }
        }
    }

    /* ── Direct step matrix presses (future hardware) ─────────────────── */
    uint8_t step_press = UI_Input_GetStepPressed();
    if (step_press >= 1 && step_press <= 12)
    {
        UI_Sequencer_SelectStep(step_press);
    }

    /* Shift+step in grid: quick-load pattern P01..P12. */
    uint8_t shift_step_press = UI_Input_GetShiftStepPressed();
    if (shift_step_press >= 1 && shift_step_press <= 12)
    {
        if (s_ui_mode == UI_MODE_GRID)
        {
            Bridge_SetCurrentPattern((uint8_t)(shift_step_press - 1));
            s_last_pattern = 0xFF;
            s_last_step = 0xFF;
            s_last_loops = 0xFFFFFFFF;
            s_last_run_time = 0xFFFFFFFF;
        }
        else
        {
            UI_Sequencer_SelectStep(shift_step_press);
        }
    }

    /* ── Encoder press — enter/exit chord menu ─────────────────────────── */
    if (UI_Input_IsEncoderPressed())
    {
        if (s_ui_mode == UI_MODE_GRID)
        {
            if (UI_Input_IsShiftHeld())
            {
                /* Shift+press from grid enters timing menu */
                s_ui_mode = UI_MODE_TIMING_MENU;
                s_timing_cursor = 0;
                UI_Sequencer_LoadTimingDraft();
                UI_Display_SetTimingFooterAction(0);
                UI_Sequencer_DrawTimingMenu();
            }
            else
            {
                /* Enter chord menu for currently selected step */
                s_ui_mode   = UI_MODE_CHORD_MENU;
                s_menu_step = s_selected_step;
                /* Pre-select current chord type (0=clear, 1-16=chord types) */
                uint8_t current_type = s_step_chords[s_selected_step - 1].chord_type;
                s_last_predefined_chord = current_type;
                UI_Display_SetChordSelection(current_type);
                UI_Display_DrawChordMenu(s_menu_step, &s_step_chords[s_selected_step - 1]);
            }
        }
        else if (s_ui_mode == UI_MODE_CHORD_MENU)
        {
            if (UI_Input_IsShiftHeld())
            {
                /* Shift toggles predefined/user source in chord menu */
                uint8_t selected_idx = UI_Display_GetSelectedChord();
                if (selected_idx == 17)
                {
                    UI_Display_SetChordSelection(s_last_predefined_chord);
                }
                else
                {
                    s_last_predefined_chord = selected_idx;
                    UI_Display_SetChordSelection(17);
                }
                UI_Display_DrawChordMenu(s_menu_step, &s_step_chords[s_menu_step - 1]);
            }
            else
            {
                /* Check if USER button (index 17) is selected */
                uint8_t selected_idx = UI_Display_GetSelectedChord();
                
                if (selected_idx == 17)  /* USER button */
                {
                    s_ui_mode = UI_MODE_USER_CHORD_MENU;
                    UI_Display_DrawUserChordMenu();
                }
                else if (selected_idx == 0)
                {
                    /* Clear chord and return to main steps */
                    s_step_chords[s_menu_step - 1].chord_type = 0;
                    UI_Sequencer_CommitChordDraft();
                    UI_Sequencer_ExitToGrid();
                }
                else
                {
                    /* Confirm chord type selection and enter parameters */
                    uint8_t selected_type = selected_idx;
                    s_step_chords[s_menu_step - 1].chord_type = selected_type;

                    s_ui_mode = UI_MODE_CHORD_PARAMS;
                    s_param_cursor = 0;  /* Start with root key */
                    s_step_chords[s_menu_step - 1].loop_count = Bridge_GetPatternRepeatCount();
                    UI_Display_SetSelectedParamAction(PARAM_ACTION_MAIN);
                    UI_Display_DrawChordParams(s_menu_step, &s_step_chords[s_menu_step - 1], s_param_cursor);
                }
            }
        }
        else if (s_ui_mode == UI_MODE_CHORD_PARAMS)
        {
            if (UI_Input_IsShiftHeld())
            {
                /* Execute selected footer action */
                uint8_t action = UI_Display_GetSelectedParamAction();

                if (action == PARAM_ACTION_MAIN)
                {
                    UI_Sequencer_ExitToGrid();
                }
                else if (action == PARAM_ACTION_SAVE)
                {
                    UI_Sequencer_CommitChordDraft();
                    UI_Display_DrawChordParams(s_menu_step, &s_step_chords[s_menu_step - 1], s_param_cursor);
                }
                else if (action == PARAM_ACTION_PREV)
                {
                    /* Persist current step edits before switching step. */
                    UI_Sequencer_CommitChordDraft();
                    s_menu_step = (s_menu_step <= 1) ? 12 : (s_menu_step - 1);
                    s_selected_step = s_menu_step;
                    s_step_chords[s_menu_step - 1].loop_count = Bridge_GetPatternRepeatCount();
                    UI_Display_DrawChordParams(s_menu_step, &s_step_chords[s_menu_step - 1], s_param_cursor);
                }
                else if (action == PARAM_ACTION_NEXT)
                {
                    /* Persist current step edits before switching step. */
                    UI_Sequencer_CommitChordDraft();
                    s_menu_step = (s_menu_step >= 12) ? 1 : (s_menu_step + 1);
                    s_selected_step = s_menu_step;
                    s_step_chords[s_menu_step - 1].loop_count = Bridge_GetPatternRepeatCount();
                    UI_Display_DrawChordParams(s_menu_step, &s_step_chords[s_menu_step - 1], s_param_cursor);
                }
            }
            else
            {
                /* Cycle to next parameter */
                s_param_cursor = (s_param_cursor + 1) % 5;
                UI_Display_DrawChordParams(s_menu_step, &s_step_chords[s_menu_step - 1], s_param_cursor);
            }
        }
        else if (s_ui_mode == UI_MODE_TIMING_MENU)
        {
            if (UI_Input_IsShiftHeld())
            {
                uint8_t action = UI_Display_GetTimingFooterAction();
                if (action == 1)
                {
                    UI_Sequencer_CommitTimingDraft();
                }

                /* MAIN exits without applying draft changes */
                UI_Sequencer_ExitToGrid();
            }
            else
            {
                s_timing_cursor = (s_timing_cursor + 1) % 5;
                UI_Sequencer_DrawTimingMenu();
            }
        }
        else if (s_ui_mode == UI_MODE_USER_CHORD_MENU)
        {
            if (UI_Input_IsShiftHeld())
            {
                /* Back to chord menu */
                s_ui_mode = UI_MODE_CHORD_MENU;
                uint8_t current_type = s_step_chords[s_menu_step - 1].chord_type;
                UI_Display_SetChordSelection(current_type);
                UI_Display_DrawChordMenu(s_menu_step, &s_step_chords[s_menu_step - 1]);
            }
            else
            {
                /* Process user chord menu selection (Create, Load, Back) */
                uint8_t selection = UI_Display_GetUserChordMenuSelection();
                
                if (selection == 0)  /* CREATE */
                {
                    s_ui_mode = UI_MODE_USER_CHORD_CREATE;
                    s_user_chord_note_mask = 0;
                    UI_Display_SetPianoNoteMask(0);
                    UI_Display_DrawPianoKeyboard(0, 0);
                }
                else if (selection == 1)  /* LOAD */
                {
                    s_ui_mode = UI_MODE_USER_CHORD_LOAD;
                    UI_Display_DrawUserChordLoad();
                }
                else if (selection == 2)  /* BACK */
                {
                    s_ui_mode = UI_MODE_CHORD_MENU;
                    uint8_t current_type = s_step_chords[s_menu_step - 1].chord_type;
                    UI_Display_SetChordSelection(current_type);
                    UI_Display_DrawChordMenu(s_menu_step, &s_step_chords[s_menu_step - 1]);
                }
            }
        }
        else if (s_ui_mode == UI_MODE_USER_CHORD_CREATE)
        {
            /* Encoder press = toggle current note on/off */
            uint8_t key = UI_Display_GetSelectedPianoKey();
            UI_Display_TogglePianoKey(key);
        }
        else if (s_ui_mode == UI_MODE_USER_CHORD_LOAD)
        {
            if (UI_Input_IsShiftHeld())
            {
                /* Back to user chord menu */
                s_ui_mode = UI_MODE_USER_CHORD_MENU;
                UI_Display_DrawUserChordMenu();
            }
            else
            {
                /* Load selected user chord and apply to current step */
                uint8_t chord_idx = UI_Display_GetSelectedUserChord();
                const UserChordInfo *chord_info = Bridge_UserChord_Get(chord_idx);
                if (chord_info)
                {
                    /* Apply loaded chord to current step */
                    s_step_chords[s_menu_step - 1].chord_type = 0;  /* External chord */
                    /* Store note mask somehow - we may need to extend ChordParams */
                    
                    UI_Sequencer_CommitChordDraft();
                    UI_Sequencer_ExitToGrid();
                }
            }
        }
        else
        {
            /* Confirm chord parameters and exit to grid */
            s_ui_mode = UI_MODE_GRID;
            UI_Display_Init();  /* Redraw full grid */
            
            /* Redraw all step boxes with correct chord colors */
            for (uint8_t i = 1; i <= 12; i++)
            {
                uint8_t is_selected = (i == s_selected_step);
                uint8_t is_active = (i == s_active_step);
                UI_Display_DrawStepBox(i, is_selected, is_active, s_step_chords[i-1].chord_type > 0);
            }
            
            /* Force status row redraw since it was disabled during chord menu */
            uint8_t pattern = Bridge_GetCurrentPattern();
            uint8_t step = Bridge_GetCurrentStep();
            uint32_t loops = Bridge_GetCompletedLoops();
            uint32_t run_time = Bridge_GetRunTimeMs();
            UI_Display_DrawStatusRow(pattern, step, loops, run_time);
        }
    }

    /* ── Active step from engine ──────────────────────────────────────── */
    uint8_t engine_step = (uint8_t)(Bridge_GetCurrentStep() + 1); /* 0-based → 1-based */
    static uint32_t s_last_status_ms = 0;
    if (Bridge_IsPlaying())
        s_active_step = engine_step;
    else
        s_active_step = 0;

    /* ── Encoder — move cursor or navigate chord menu ──────────────────── */
    int8_t delta = UI_Input_GetEncoderDelta();

    if (delta != 0)
    {
        if (s_ui_mode == UI_MODE_TIMING_MENU)
        {
            if (UI_Input_IsShiftHeld())
            {
                UI_Display_NavigateTimingFooter(delta);
            }
            else
            {
                static const uint8_t divisions[] = {1, 2, 3, 4, 6, 8};

                if (s_timing_cursor == 0)
                {
                    int16_t v = (int16_t)s_timing_step_count + delta;
                    if (v < 1) v = 1;
                    if (v > 12) v = 12;
                    s_timing_step_count = (uint8_t)v;
                }
                else if (s_timing_cursor == 1)
                {
                    uint8_t idx = 0;
                    for (uint8_t i = 0; i < 6; i++) if (divisions[i] == s_timing_step_division) { idx = i; break; }
                    int16_t n = (int16_t)idx + delta;
                    while (n < 0) n += 6;
                    while (n >= 6) n -= 6;
                    s_timing_step_division = divisions[n];
                }
                else if (s_timing_cursor == 2)
                {
                    int16_t v = (int16_t)s_timing_ts_num + delta;
                    if (v < 1) v = 1;
                    if (v > 12) v = 12;
                    s_timing_ts_num = (uint8_t)v;
                }
                else if (s_timing_cursor == 3)
                {
                    uint8_t dens[] = {2, 4, 8};
                    uint8_t idx = (s_timing_ts_den == 2) ? 0 : (s_timing_ts_den == 8) ? 2 : 1;
                    int16_t n = (int16_t)idx + delta;
                    while (n < 0) n += 3;
                    while (n >= 3) n -= 3;
                    s_timing_ts_den = dens[n];
                }
                else if (s_timing_cursor == 4)
                {
                    int16_t v = (int16_t)s_timing_swing + delta;
                    if (v < 0) v = 0;
                    if (v > 75) v = 75;
                    s_timing_swing = (uint8_t)v;
                }
            }

            UI_Sequencer_DrawTimingMenu();
        }
        else if (s_ui_mode == UI_MODE_CHORD_PARAMS && UI_Input_IsShiftHeld())
        {
            /* Shift + encoder in params: select footer actions */
            UI_Display_NavigateParamFooterActions(delta, s_menu_step, &s_step_chords[s_menu_step - 1], s_param_cursor);
        }
        else if (s_ui_mode == UI_MODE_CHORD_MENU)
        {
            /* Encoder in chord menu: navigate chord buttons */
            UI_Display_NavigateChordMenu(delta, s_menu_step, &s_step_chords[s_menu_step - 1]);
        }
        else if (s_ui_mode == UI_MODE_USER_CHORD_MENU)
        {
            /* Encoder in user chord menu: navigate Create/Load/Back */
            UI_Display_NavigateUserChordMenu(delta);
            UI_Display_DrawUserChordMenu();
        }
        else if (s_ui_mode == UI_MODE_USER_CHORD_CREATE)
        {
            /* Encoder in piano keyboard: navigate keys */
            UI_Display_NavigatePianoKeyboard(delta);
        }
        else if (s_ui_mode == UI_MODE_USER_CHORD_LOAD)
        {
            /* Encoder in user chord load: navigate chord list */
            UI_Display_NavigateUserChordLoad(delta);
            UI_Display_DrawUserChordLoad();
        }
        else if (s_ui_mode == UI_MODE_CHORD_PARAMS)
        {
            /* While SAVE is selected in footer, lock parameter edits. */
            if (UI_Display_GetSelectedParamAction() != PARAM_ACTION_SAVE)
            {
                /* Navigate chord parameters */
                UI_Display_NavigateChordParams(delta, s_menu_step, &s_step_chords[s_menu_step - 1], s_param_cursor);
            }
        }
        else if (UI_Input_IsShiftHeld())
        {
            /* Shift + encoder = BPM */
            uint16_t bpm = UI_Transport_GetBPM();
            bpm = ((int16_t)bpm + delta < 30)  ? 30  :
                  ((int16_t)bpm + delta > 300) ? 300 :
                  bpm + delta;
            UI_Transport_SetBPM(bpm);
        }
        else
        {
            /* Encoder = move step cursor */
            uint8_t prev = s_selected_step;
            int8_t  next = (int8_t)s_selected_step + delta;

            if (next < 1)  next = 12;
            if (next > 12) next = 1;

            s_selected_step = (uint8_t)next;

            if (prev != s_selected_step)
            {
                UI_Display_DrawStepBox(prev,            0, (prev           == s_active_step), s_step_chords[prev-1].chord_type > 0);
                UI_Display_DrawStepBox(s_selected_step, 1, (s_selected_step == s_active_step), s_step_chords[s_selected_step-1].chord_type > 0);
            }
        }
    }

    /* ── Active step display update ───────────────────────────────────── */
    if (s_ui_mode == UI_MODE_GRID)
    {
        if (s_active_step != s_last_active)
        {
            if (s_last_active > 0)
            {
                uint8_t was_selected = (s_last_active == s_selected_step);
                  UI_Display_DrawStepBox(s_last_active, was_selected, 0, s_step_chords[s_last_active-1].chord_type > 0);
              }
            uint8_t is_selected = (s_active_step == s_selected_step);
            UI_Display_DrawStepBox(s_active_step, is_selected, 1, s_step_chords[s_active_step-1].chord_type > 0);
            
            /* Update last active step for next comparison */
            s_last_active = s_active_step;
        }
    }

   
uint32_t now = HAL_GetTick();

if ((now - s_last_status_ms) >= 100)
{
    s_last_status_ms = now;

    /* Only redraw status row if values changed and not in chord menu */
    if (s_ui_mode == UI_MODE_GRID)
    {
        uint8_t  pattern   = Bridge_GetCurrentPattern();
        uint8_t  step      = Bridge_GetCurrentStep();
        uint32_t loops     = Bridge_GetCompletedLoops();
        uint32_t run_time  = Bridge_GetRunTimeMs();

        if ((pattern != s_last_pattern) || (step != s_last_step) || 
            (loops != s_last_loops) || (run_time != s_last_run_time))
        {
            s_last_pattern   = pattern;
            s_last_step      = step;
            s_last_loops     = loops;
            s_last_run_time  = run_time;

            UI_Display_DrawStatusRow(pattern, step, loops, run_time);
        }
    }
}

/* ── Process engine dirty flags ───────────────────────────────────── */
Bridge_Process();
}