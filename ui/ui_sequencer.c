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
    UI_MODE_STEP_PIANO,       /* per-step piano-roll note view */
    UI_MODE_CHORD_MENU,       /* chord selection submenu for current step */
    UI_MODE_CHORD_PARAMS,     /* chord parameter editor */
    UI_MODE_TIMING_MENU,      /* pattern timing editor */
    UI_MODE_SONG_CHAIN,       /* pattern chain editor */
    UI_MODE_USER_CHORD_MENU,  /* user chord: Create/Load/Name */
    UI_MODE_USER_CHORD_CREATE,/* user chord: piano keyboard editor */
    UI_MODE_USER_CHORD_LOAD,  /* user chord: load from library */
    UI_MODE_USER_CHORD_NAME   /* user chord: name editor */
} UiMode;

/* ── State ───────────────────────────────────────────────────────────────── */
static UiMode      s_ui_mode        = UI_MODE_GRID;
static uint8_t     s_selected_step  = 1;   /* cursor position 1-12      */
static uint8_t     s_active_step    = 0;   /* currently playing step    */
static uint8_t     s_last_active    = 0;   /* previous playing step     */
static uint8_t     s_menu_step      = 1;   /* which step's chord we're editing */
static ChordParams s_step_chords[12];     /* chord parameters for each step */
static ChordParams s_pattern_step_chords[32][12];
static uint8_t     s_cached_pattern = 0xFF;
static uint8_t     s_param_cursor   = 0;   /* which parameter we're editing (0=root, 1=type, 2=arp, 3=duration) */
static uint8_t     s_timing_cursor  = 0;   /* timing field cursor */
static uint8_t     s_timing_step_count = 12;
static uint8_t     s_timing_step_division = 4;
static uint8_t     s_timing_ts_num = 4;
static uint8_t     s_timing_ts_den = 4;
static uint8_t     s_timing_swing = 0;
static uint8_t     s_last_predefined_chord = 0;
static UiMainMode  s_main_mode = UI_MAIN_MODE_STEP;
static uint8_t     s_song_chain[32] = {0};
static uint8_t     s_song_length = 1;
static uint8_t     s_song_cursor = 0;
static uint8_t     s_song_blink_on = 1;
static uint32_t    s_song_blink_ms = 0;

/* ── User Chord State ────────────────────────────────────────────────────── */
static uint16_t    s_user_chord_note_mask = 0; /* note mask being created */
static uint8_t     s_last_saved_user_chord = 0xFF;
static char        s_user_chord_name_edit[17] = {0};
static uint8_t     s_user_chord_name_cursor = 0;

static uint8_t UI_Sequencer_TimingDraftIsDirty(void);
static void UI_Sequencer_CommitChordDraft(void);
static void UI_Sequencer_EnterStepPianoView(uint8_t step);
static uint8_t UI_Sequencer_StepHasNotes(uint8_t step);
static void UI_Sequencer_SetMainMode(UiMainMode mode);
static void UI_Sequencer_HandleChordParamAction(uint8_t action);
static void UI_Sequencer_LoadSongChainDraft(void);
static void UI_Sequencer_DrawSongChainMenu(void);
static void UI_Sequencer_SaveChordDraftForPattern(void);
static void UI_Sequencer_LoadChordDraftForPattern(uint8_t pattern);
static void UI_Sequencer_HydrateStepChordFromEngine(uint8_t step);

/* ── Status row caching (prevent flicker) ─────────────────────────────────── */
static uint8_t  s_last_pattern    = 0xFF;
static uint8_t  s_last_step       = 0xFF;
static uint32_t s_last_loops      = 0xFFFFFFFF;
static uint32_t s_last_run_time   = 0xFFFFFFFF;
static uint8_t  s_repeat_flash_on = 0;
static uint32_t s_repeat_flash_ms = 0;

static void UI_Sequencer_SaveChordDraftForPattern(void)
{
    if (s_cached_pattern >= 32) return;
    memcpy(s_pattern_step_chords[s_cached_pattern], s_step_chords, sizeof(s_step_chords));
}

static void UI_Sequencer_LoadChordDraftForPattern(uint8_t pattern)
{
    if (pattern >= 32) pattern = 0;
    memcpy(s_step_chords, s_pattern_step_chords[pattern], sizeof(s_step_chords));
    s_cached_pattern = pattern;
}

static void UI_Sequencer_HydrateStepChordFromEngine(uint8_t step)
{
    if (step < 1 || step > 12) return;

    uint8_t step_index = (uint8_t)(step - 1);
    uint16_t note_mask = Bridge_GetStepNoteMask(step_index);

    if (note_mask == 0)
    {
        s_step_chords[step_index].chord_type = 0;
        return;
    }

    if (s_step_chords[step_index].chord_type != 0)
    {
        return;
    }

    uint8_t root_key = 0;
    uint8_t chord_type = 0;
    uint8_t duration = s_step_chords[step_index].duration;
    uint8_t repeat_count = s_step_chords[step_index].loop_count;

    if (Bridge_GetStepChordUiParams(step_index,
                                    &root_key,
                                    &chord_type,
                                    &duration,
                                    &repeat_count))
    {
        s_step_chords[step_index].root_key = root_key;
        s_step_chords[step_index].chord_type = chord_type;
        s_step_chords[step_index].duration = duration;
        s_step_chords[step_index].loop_count = repeat_count;
        UI_Sequencer_SaveChordDraftForPattern();
    }
}

static void UI_Sequencer_ExitToGrid(void)
{
    s_ui_mode = UI_MODE_GRID;
    UI_Display_Init();

    for (uint8_t i = 1; i <= 12; i++)
    {
        uint8_t is_selected = (i == s_selected_step);
        uint8_t is_active = (i == s_active_step);
        UI_Display_DrawStepBox(i, is_selected, is_active, UI_Sequencer_StepHasNotes(i));
    }

    uint8_t pattern = Bridge_GetCurrentPattern();
    uint8_t step = Bridge_GetCurrentStep();
    uint32_t loops = Bridge_GetCompletedLoops();
    uint32_t run_time = Bridge_GetRunTimeMs();
    UI_Display_DrawStatusRow(pattern, step, loops, run_time);
}

static uint8_t UI_Sequencer_StepHasNotes(uint8_t step)
{
    if (step < 1 || step > 12) return 0;
    if (Bridge_GetStepNoteMask((uint8_t)(step - 1)) != 0) return 1;
    return (s_step_chords[step - 1].chord_type != 0) ? 1 : 0;
}

static uint8_t UI_Sequencer_GetStepPianoInitialKey(uint8_t step)
{
    if (step < 1 || step > 12) return 0;

    const uint8_t step_index = (uint8_t)(step - 1);
    const uint16_t note_mask = Bridge_GetStepNoteMask(step_index);
    uint8_t root_key = 0;
    uint8_t chord_type = 0;
    uint8_t duration = 0;
    uint8_t repeat_count = 0;

    if (note_mask != 0 &&
        Bridge_GetStepChordUiParams(step_index, &root_key, &chord_type, &duration, &repeat_count) &&
        chord_type != 0)
    {
        return root_key;
    }

    for (uint8_t note = 0; note < 12; note++)
    {
        if (note_mask & (1u << note)) return note;
    }

    return 0;
}

static void UI_Sequencer_EnterStepPianoView(uint8_t step)
{
    if (step < 1 || step > 12) return;

    s_selected_step = step;
    s_ui_mode = UI_MODE_STEP_PIANO;
    UI_Display_DrawStepPianoRoll(step,
                                 Bridge_GetStepNoteMask((uint8_t)(step - 1)),
                                 UI_Sequencer_GetStepPianoInitialKey(step));
}

static void UI_Sequencer_SetMainMode(UiMainMode mode)
{
    s_main_mode = mode;
    UI_Display_SetMainMode(mode);
}

static void UI_Sequencer_LoadSongChainDraft(void)
{
    s_song_length = Bridge_GetChainLength();
    if (s_song_length < 1) s_song_length = 1;
    if (s_song_length > 32) s_song_length = 32;

    for (uint8_t i = 0; i < s_song_length; i++)
    {
        s_song_chain[i] = Bridge_GetChainPatternAt(i);
    }

    if (s_song_cursor >= s_song_length)
    {
        s_song_cursor = (uint8_t)(s_song_length - 1);
    }
}

static void UI_Sequencer_DrawSongChainMenu(void)
{
    uint8_t playing_slot = 0;
    uint8_t repeat_index = 0;
    uint8_t repeat_total = Bridge_GetPatternRepeatCount();

    if (Bridge_IsPlaying())
    {
        playing_slot = Bridge_GetChainCurrentPosition();
        repeat_index = Bridge_GetCurrentPatternRepeatProgress();
    }
    else
    {
        playing_slot = s_song_cursor;
    }

    UI_Display_DrawSongChainMenu(s_song_chain,
                                 s_song_length,
                                 s_song_cursor,
                                 playing_slot,
                                 repeat_index,
                                 repeat_total,
                                 s_song_blink_on);
}

static void UI_Sequencer_HandleChordParamAction(uint8_t action)
{
    if (action >= PARAM_ACTION_COUNT) return;

    UI_Display_SetSelectedParamAction(action);
    UI_Display_DrawParamFooterActions(action);

    if (action == PARAM_ACTION_MAIN)
    {
        /* MAIN now behaves as save-and-exit for chord param workflow. */
        UI_Sequencer_CommitChordDraft();
        UI_Sequencer_SetMainMode(UI_MAIN_MODE_STEP);
        UI_Sequencer_ExitToGrid();
    }
    else if (action == PARAM_ACTION_SAVE)
    {
        UI_Sequencer_CommitChordDraft();
        /* Do not leave footer parked on SAVE; that locks encoder param edits. */
        UI_Display_SetSelectedParamAction(PARAM_ACTION_MAIN);
        UI_Display_DrawParamFooterActions(PARAM_ACTION_MAIN);
        UI_Display_DrawChordParams(s_menu_step, &s_step_chords[s_menu_step - 1], s_param_cursor);
    }
    else if (action == PARAM_ACTION_PREV)
    {
        uint8_t from_step = s_menu_step;
        UI_Sequencer_CommitChordDraft();
        s_menu_step = (s_menu_step <= 1) ? 12 : (s_menu_step - 1);
        s_selected_step = s_menu_step;

        /* Continue editing workflow: carry current step params into target step. */
        s_step_chords[s_menu_step - 1] = s_step_chords[from_step - 1];

        UI_Display_DrawChordParams(s_menu_step, &s_step_chords[s_menu_step - 1], s_param_cursor);
    }
    else if (action == PARAM_ACTION_NEXT)
    {
        uint8_t from_step = s_menu_step;
        UI_Sequencer_CommitChordDraft();
        s_menu_step = (s_menu_step >= 12) ? 1 : (s_menu_step + 1);
        s_selected_step = s_menu_step;

        /* Continue editing workflow: carry current step params into target step. */
        s_step_chords[s_menu_step - 1] = s_step_chords[from_step - 1];

        UI_Display_DrawChordParams(s_menu_step, &s_step_chords[s_menu_step - 1], s_param_cursor);
    }
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
    UI_Sequencer_SaveChordDraftForPattern();

    for (uint8_t i = 0; i < 12; i++)
    {
        Bridge_SetStepChordParams(i,
                                  s_step_chords[i].root_key,
                                  s_step_chords[i].chord_type,
                                  s_step_chords[i].arp_pattern,
                                  s_step_chords[i].duration,
                                  s_step_chords[i].loop_count);
    }
}

static char UI_Sequencer_CycleNameChar(char current, int8_t delta)
{
    static const char k_name_chars[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-";
    const int16_t count = (int16_t)(sizeof(k_name_chars) - 1);
    int16_t index = 0;

    for (int16_t i = 0; i < count; i++)
    {
        if (k_name_chars[i] == current)
        {
            index = i;
            break;
        }
    }

    index += delta;
    while (index < 0) index += count;
    while (index >= count) index -= count;
    return k_name_chars[index];
}

static void UI_Sequencer_StartUserChordNameEdit(void)
{
    if (s_last_saved_user_chord == 0xFF)
    {
        uint8_t count = Bridge_UserChord_GetCount();
        for (uint8_t i = 0; i < count; i++)
        {
            if (Bridge_UserChord_Get(i))
            {
                s_last_saved_user_chord = i;
                break;
            }
        }
        if (s_last_saved_user_chord == 0xFF) return;
    }

    const UserChordInfo* chord_info = Bridge_UserChord_Get(s_last_saved_user_chord);
    if (!chord_info)
    {
        return;
    }

    memset(s_user_chord_name_edit, 0, sizeof(s_user_chord_name_edit));
    strncpy(s_user_chord_name_edit, chord_info->name, 16);
    s_user_chord_name_edit[16] = '\0';

    for (uint8_t i = 0; i < 16; i++)
    {
        if (s_user_chord_name_edit[i] == '\0')
        {
            s_user_chord_name_edit[i] = ' ';
        }
    }

    s_user_chord_name_cursor = 0;
    s_ui_mode = UI_MODE_USER_CHORD_NAME;
    UI_Display_DrawUserChordNameEditor(s_user_chord_name_edit, s_user_chord_name_cursor);
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
    s_repeat_flash_on = 0;
    s_repeat_flash_ms = 0;

    /* Initialize per-pattern chord drafts (32 patterns x 12 steps). */
    for (uint8_t p = 0; p < 32; p++) {
        for (uint8_t i = 0; i < 12; i++) {
            s_pattern_step_chords[p][i].root_key = 0;    /* C */
            s_pattern_step_chords[p][i].chord_type = 0;  /* Clear */
            s_pattern_step_chords[p][i].arp_pattern = 0; /* Block */
            s_pattern_step_chords[p][i].duration = 1;    /* 8th notes */
            s_pattern_step_chords[p][i].loop_count = 1;
        }
    }

    UI_Sequencer_LoadChordDraftForPattern(Bridge_GetCurrentPattern());

    UI_Display_Init();    /* draws all boxes white, no selection */
    UI_Display_SetRepeatFlash(0, 0);
    UI_Sequencer_SetMainMode(s_main_mode);
    UI_Transport_Init();

    /* Draw initial grid with chord colors */
    for (uint8_t i = 1; i <= 12; i++)
    {
        uint8_t is_selected = (i == s_selected_step);
        uint8_t is_active = (i == s_active_step);
        UI_Display_DrawStepBox(i, is_selected, is_active, UI_Sequencer_StepHasNotes(i));
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

    s_selected_step = step;

    if (s_ui_mode == UI_MODE_GRID)
    {
        UI_Sequencer_EnterStepPianoView(step);
    }
    else if (s_ui_mode == UI_MODE_STEP_PIANO)
    {
        UI_Display_DrawStepPianoRoll(step,
                                     Bridge_GetStepNoteMask((uint8_t)(step - 1)),
                                     UI_Sequencer_GetStepPianoInitialKey(step));
    }
    else if (s_ui_mode == UI_MODE_CHORD_MENU)
    {
        /* In chord menu: switch to this step and load its current chord selection. */
        s_menu_step = step;
        UI_Sequencer_HydrateStepChordFromEngine(s_menu_step);
        s_last_predefined_chord = s_step_chords[s_menu_step - 1].chord_type;
        UI_Display_SetChordSelection(s_step_chords[s_menu_step - 1].chord_type);
        UI_Display_DrawChordMenu(s_menu_step, &s_step_chords[s_menu_step - 1]);
    }
    else if (s_ui_mode == UI_MODE_CHORD_PARAMS)
    {
        /* Preserve edits before switching target step, then continue in params view. */
        UI_Sequencer_CommitChordDraft();
        s_menu_step = step;
        UI_Sequencer_HydrateStepChordFromEngine(s_menu_step);
        UI_Display_DrawChordParams(s_menu_step, &s_step_chords[s_menu_step - 1], s_param_cursor);
    }
    else
    {
        /* Ignore matrix step selects in other non-grid menus. */
    }
}

/* ── Update — called every 10ms from main loop ───────────────────────────── */
void UI_Sequencer_Update(void)
{
    uint8_t current_pattern = Bridge_GetCurrentPattern();
    if (current_pattern != s_cached_pattern)
    {
        UI_Sequencer_SaveChordDraftForPattern();
        UI_Sequencer_LoadChordDraftForPattern(current_pattern);
    }

    if (UI_Input_GetShiftTap())
    {
        /* Only cycle main mode when in grid — ignore shift-tap inside menus */
        if (s_ui_mode == UI_MODE_GRID)
        {
            UI_Sequencer_SetMainMode((UiMainMode)(((uint8_t)s_main_mode + 1u) % 4u));
            UI_Display_DrawStatusRow(Bridge_GetCurrentPattern(),
                                     Bridge_GetCurrentStep(),
                                     Bridge_GetCompletedLoops(),
                                     Bridge_GetRunTimeMs());
        }
    }

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
                    Bridge_UserChord_EnsureLoaded();
                    s_ui_mode = UI_MODE_USER_CHORD_MENU;
                    UI_Display_ResetUserChordMenuCache();
                    UI_Display_DrawUserChordMenu();
                }
                else if (selected_idx == 0)
                {
                    s_step_chords[s_menu_step - 1].chord_type = 0;
                    UI_Sequencer_CommitChordDraft();
                    UI_Sequencer_SetMainMode(UI_MAIN_MODE_STEP);
                    UI_Sequencer_ExitToGrid();
                }
                else
                {
                    s_step_chords[s_menu_step - 1].chord_type = selected_idx;
                    s_ui_mode = UI_MODE_CHORD_PARAMS;
                    s_param_cursor = 0;
                    UI_Display_SetSelectedParamAction(PARAM_ACTION_MAIN);
                    UI_Display_DrawChordParams(s_menu_step, &s_step_chords[s_menu_step - 1], s_param_cursor);
                }
            }
            else if (s_ui_mode == UI_MODE_CHORD_PARAMS)
            {
                /* Save: commit chord params and return to grid */
                UI_Sequencer_CommitChordDraft();
                UI_Sequencer_SetMainMode(UI_MAIN_MODE_STEP);
                UI_Sequencer_ExitToGrid();
            }
            else if (s_ui_mode == UI_MODE_STEP_PIANO)
            {
                UI_Sequencer_ExitToGrid();
            }
            else if (s_ui_mode == UI_MODE_TIMING_MENU)
            {
                /* Save: commit timing and return to grid */
                UI_Sequencer_CommitTimingDraft();
                UI_Sequencer_ExitToGrid();
            }
            else if (s_ui_mode == UI_MODE_SONG_CHAIN)
            {
                UI_Sequencer_ExitToGrid();
            }
            else if (s_ui_mode == UI_MODE_USER_CHORD_MENU)
            {
                /* Save: activate the currently highlighted item */
                uint8_t selection = UI_Display_GetUserChordMenuSelection();
                if (selection == 0)
                {
                    s_ui_mode = UI_MODE_USER_CHORD_CREATE;
                    UI_Display_ResetUserChordMenuCache();
                    s_user_chord_note_mask = 0;
                    UI_Display_SetPianoNoteMask(0);
                    UI_Display_DrawPianoKeyboard(0, 0);
                }
                else if (selection == 1)
                {
                    Bridge_UserChord_EnsureLoaded();
                    s_ui_mode = UI_MODE_USER_CHORD_LOAD;
                    UI_Display_DrawUserChordLoad();
                }
                else
                {
                    UI_Sequencer_StartUserChordNameEdit();
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
                    s_last_saved_user_chord = Bridge_UserChord_Save(auto_name, s_user_chord_note_mask);
                }
                s_ui_mode = UI_MODE_USER_CHORD_MENU;
                UI_Display_ResetUserChordMenuCache();
                UI_Display_DrawUserChordMenu();
            }
            else if (s_ui_mode == UI_MODE_USER_CHORD_LOAD)
            {
                /* Save: load selected chord and apply to step */
                uint8_t chord_idx = UI_Display_GetSelectedUserChord();
                const UserChordInfo *chord_info = Bridge_UserChord_Get(chord_idx);
                if (chord_info)
                {
                    s_last_saved_user_chord = chord_idx;
                    s_step_chords[s_menu_step - 1].chord_type = 0;
                    Bridge_SetStepCustomUserChord((uint8_t)(s_menu_step - 1), chord_info->note_mask, chord_info->name);
                    UI_Sequencer_SetMainMode(UI_MAIN_MODE_STEP);
                    UI_Sequencer_ExitToGrid();
                }
            }
            else if (s_ui_mode == UI_MODE_USER_CHORD_NAME)
            {
                char final_name[17];
                memcpy(final_name, s_user_chord_name_edit, 16);
                final_name[16] = '\0';

                /* Trim trailing spaces before save */
                for (int8_t i = 15; i >= 0; i--)
                {
                    if (final_name[i] == ' ') final_name[i] = '\0';
                    else break;
                }

                if (final_name[0] == '\0')
                {
                    strncpy(final_name, "USER", sizeof(final_name));
                }

                Bridge_UserChord_Rename(s_last_saved_user_chord, final_name);
                s_ui_mode = UI_MODE_USER_CHORD_MENU;
                UI_Display_ResetUserChordMenuCache();
                UI_Display_DrawUserChordMenu();
            }
        }

        if (shift_rec_pressed && s_ui_mode == UI_MODE_USER_CHORD_LOAD)
        {
            uint8_t chord_idx = UI_Display_GetSelectedUserChord();
            uint8_t count = Bridge_UserChord_GetCount();

            if (count > 0)
            {
                Bridge_UserChord_Delete(chord_idx);

                count = Bridge_UserChord_GetCount();
                if (count == 0)
                    UI_Display_SetSelectedUserChord(0);
                else if (chord_idx >= count)
                    UI_Display_SetSelectedUserChord((uint8_t)(count - 1));

                UI_Display_DrawUserChordLoad();
            }
        }

        /* ── Record = Back ────────────────────────────────────────────── */
        if (rec_pressed && !shift_rec_pressed)
        {
            if (s_ui_mode == UI_MODE_CHORD_MENU || s_ui_mode == UI_MODE_TIMING_MENU)
            {
                UI_Sequencer_ExitToGrid();
            }
            else if (s_ui_mode == UI_MODE_SONG_CHAIN)
            {
                UI_Sequencer_ExitToGrid();
            }
            else if (s_ui_mode == UI_MODE_STEP_PIANO)
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
                /* MAIN STEPS footer behavior */
                UI_Sequencer_ExitToGrid();
            }
            else if (s_ui_mode == UI_MODE_USER_CHORD_CREATE)
            {
                /* Back: discard and return to user chord menu */
                s_ui_mode = UI_MODE_USER_CHORD_MENU;
                UI_Display_ResetUserChordMenuCache();
                UI_Display_DrawUserChordMenu();
            }
            else if (s_ui_mode == UI_MODE_USER_CHORD_LOAD)
            {
                s_ui_mode = UI_MODE_USER_CHORD_MENU;
                UI_Display_ResetUserChordMenuCache();
                UI_Display_DrawUserChordMenu();
            }
            else if (s_ui_mode == UI_MODE_USER_CHORD_NAME)
            {
                s_ui_mode = UI_MODE_USER_CHORD_MENU;
                UI_Display_ResetUserChordMenuCache();
                UI_Display_DrawUserChordMenu();
            }
        }
    }

    /* ── Direct step matrix presses (future hardware) ─────────────────── */
    uint8_t step_press = UI_Input_GetStepPressed();
    if (step_press >= 1 && step_press <= 12)
    {
        if (s_ui_mode == UI_MODE_CHORD_PARAMS)
        {
            /* Matrix footer shortcuts in chord params:
             * 1=MAIN, 2=PREV, 3=NEXT, 4=SAVE */
            if (step_press <= 4)
            {
                UI_Sequencer_HandleChordParamAction((uint8_t)(step_press - 1));
            }
            else
            {
                /* Steps 5-12 remain quick-jump targets while staying in params view. */
                UI_Sequencer_SelectStep(step_press);
            }
        }
        else if (s_ui_mode == UI_MODE_SONG_CHAIN)
        {
            if (step_press >= 1 && step_press <= 4)
            {
                uint8_t page_base = (uint8_t)((s_song_cursor / 4) * 4);
                uint8_t slot = (uint8_t)(page_base + (step_press - 1));
                if (slot < s_song_length)
                {
                    s_song_cursor = slot;
                    UI_Sequencer_DrawSongChainMenu();
                }
            }
            else if (step_press == 5)
            {
                if (s_song_length < 32)
                {
                    uint8_t fill = s_song_chain[s_song_cursor];
                    s_song_chain[s_song_length] = fill;
                    s_song_length++;
                    Bridge_SetChainLength(s_song_length);
                    Bridge_SetChainPatternAt((uint8_t)(s_song_length - 1), fill);
                    s_song_cursor = (uint8_t)(s_song_length - 1);
                    UI_Sequencer_DrawSongChainMenu();
                }
            }
            else if (step_press == 6)
            {
                if (s_song_length > 1)
                {
                    s_song_length--;
                    Bridge_SetChainLength(s_song_length);
                    if (s_song_cursor >= s_song_length)
                    {
                        s_song_cursor = (uint8_t)(s_song_length - 1);
                    }
                    UI_Sequencer_LoadSongChainDraft();
                    UI_Sequencer_DrawSongChainMenu();
                }
            }
        }
        else if (s_ui_mode == UI_MODE_GRID)
        {
            if (s_main_mode == UI_MAIN_MODE_CHORD)
            {
                s_selected_step = step_press;
                s_menu_step = step_press;
                UI_Sequencer_HydrateStepChordFromEngine(s_menu_step);
                s_ui_mode = UI_MODE_CHORD_MENU;
                s_last_predefined_chord = s_step_chords[step_press - 1].chord_type;
                UI_Display_SetChordSelection(s_step_chords[step_press - 1].chord_type);
                UI_Display_DrawChordMenu(s_menu_step, &s_step_chords[s_menu_step - 1]);
            }
            else if (s_main_mode == UI_MAIN_MODE_STEP)
            {
                UI_Sequencer_SelectStep(step_press);
            }
            else if (s_main_mode == UI_MAIN_MODE_TIMING)
            {
                /* Matrix key 1 = direct jump to timing duration/division editor. */
                if (step_press == 1)
                {
                    s_ui_mode = UI_MODE_TIMING_MENU;
                    s_timing_cursor = 1; /* duration/division */
                    UI_Sequencer_LoadTimingDraft();
                    UI_Display_SetTimingFooterAction(0);
                    UI_Sequencer_DrawTimingMenu();
                }
            }
            else if (s_main_mode == UI_MAIN_MODE_PATTERN)
            {
                if (step_press == 1)
                {
                    s_ui_mode = UI_MODE_SONG_CHAIN;
                    s_song_cursor = 0;
                    s_song_blink_on = 1;
                    s_song_blink_ms = HAL_GetTick();
                    UI_Sequencer_LoadSongChainDraft();
                    UI_Sequencer_DrawSongChainMenu();
                }
            }
            else
            {
                /* Timing and Pattern matrix mappings are introduced in later phases. */
            }
        }
        else
        {
            UI_Sequencer_SelectStep(step_press);
        }
    }

    /* Shift+step in grid: quick-load pattern P01..P12. */
    uint8_t shift_step_press = UI_Input_GetShiftStepPressed();
    if (shift_step_press >= 1 && shift_step_press <= 12)
    {
        if (s_ui_mode == UI_MODE_GRID && s_main_mode == UI_MAIN_MODE_TIMING)
        {
            /* Timing mode + Shift + step: jump directly to that step parameters. */
            s_selected_step = shift_step_press;
            s_menu_step = shift_step_press;
            UI_Sequencer_HydrateStepChordFromEngine(s_menu_step);
            s_ui_mode = UI_MODE_CHORD_PARAMS;
            s_param_cursor = 3; /* Gate (per-step timing length) */
            UI_Display_SetSelectedParamAction(PARAM_ACTION_MAIN);
            UI_Display_DrawChordParams(s_menu_step, &s_step_chords[s_menu_step - 1], s_param_cursor);
        }
        else if (s_ui_mode == UI_MODE_CHORD_PARAMS)
        {
            if (shift_step_press <= 4)
            {
                UI_Sequencer_HandleChordParamAction((uint8_t)(shift_step_press - 1));
            }
            else
            {
                UI_Sequencer_SelectStep(shift_step_press);
            }
        }
        else if (s_ui_mode == UI_MODE_GRID && s_main_mode == UI_MAIN_MODE_PATTERN)
        {
            Bridge_SetCurrentPattern((uint8_t)(shift_step_press - 1));
            s_last_pattern = 0xFF;
            s_last_step = 0xFF;
            s_last_loops = 0xFFFFFFFF;
            s_last_run_time = 0xFFFFFFFF;
        }
        else if (s_ui_mode != UI_MODE_GRID)
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
                UI_Sequencer_HydrateStepChordFromEngine(s_menu_step);
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
                    UI_Display_ResetUserChordMenuCache();
                    UI_Display_DrawUserChordMenu();
                }
                else if (selected_idx == 0)
                {
                    /* Clear chord and return to main steps */
                    s_step_chords[s_menu_step - 1].chord_type = 0;
                    UI_Sequencer_CommitChordDraft();
                    UI_Sequencer_SetMainMode(UI_MAIN_MODE_STEP);
                    UI_Sequencer_ExitToGrid();
                }
                else
                {
                    /* Confirm chord type selection and enter parameters */
                    uint8_t selected_type = selected_idx;
                    s_step_chords[s_menu_step - 1].chord_type = selected_type;

                    s_ui_mode = UI_MODE_CHORD_PARAMS;
                    s_param_cursor = 0;  /* Start with root key */
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
                UI_Sequencer_HandleChordParamAction(action);
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
                    UI_Display_ResetUserChordMenuCache();
                    s_user_chord_note_mask = 0;
                    UI_Display_SetPianoNoteMask(0);
                    UI_Display_DrawPianoKeyboard(0, 0);
                }
                else if (selection == 1)  /* LOAD */
                {
                    Bridge_UserChord_EnsureLoaded();
                    s_ui_mode = UI_MODE_USER_CHORD_LOAD;
                    UI_Display_DrawUserChordLoad();
                }
                else if (selection == 2)  /* NAME */
                {
                    UI_Sequencer_StartUserChordNameEdit();
                }
            }
        }
        else if (s_ui_mode == UI_MODE_USER_CHORD_CREATE)
        {
            /* Encoder press = toggle current note on/off */
            uint8_t key = UI_Display_GetSelectedPianoKey();
            UI_Display_TogglePianoKey(key);
        }
        else if (s_ui_mode == UI_MODE_USER_CHORD_NAME)
        {
            /* Encoder press = next character */
            s_user_chord_name_cursor = (uint8_t)((s_user_chord_name_cursor + 1) % 16);
            UI_Display_DrawUserChordNameEditor(s_user_chord_name_edit, s_user_chord_name_cursor);
        }
        else if (s_ui_mode == UI_MODE_USER_CHORD_LOAD)
        {
            if (UI_Input_IsShiftHeld())
            {
                /* Back to user chord menu */
                s_ui_mode = UI_MODE_USER_CHORD_MENU;
                UI_Display_ResetUserChordMenuCache();
                UI_Display_DrawUserChordMenu();
            }
            else
            {
                /* Load selected user chord and apply to current step */
                uint8_t chord_idx = UI_Display_GetSelectedUserChord();
                const UserChordInfo *chord_info = Bridge_UserChord_Get(chord_idx);
                if (chord_info)
                {
                    s_last_saved_user_chord = chord_idx;
                    s_step_chords[s_menu_step - 1].chord_type = 0;
                    Bridge_SetStepCustomUserChord((uint8_t)(s_menu_step - 1), chord_info->note_mask, chord_info->name);
                    UI_Sequencer_SetMainMode(UI_MAIN_MODE_STEP);
                    UI_Sequencer_ExitToGrid();
                }
            }
        }
        else if (s_ui_mode == UI_MODE_STEP_PIANO)
        {
            /* Encoder press reserved for future step-note edit. */
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
                    Bridge_SetPatternStepCount(s_timing_step_count);
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
        else if (s_ui_mode == UI_MODE_STEP_PIANO)
        {
            /* Encoder in step piano view: inspect notes. */
            UI_Display_NavigatePianoKeyboard(delta);
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
        else if (s_ui_mode == UI_MODE_USER_CHORD_NAME)
        {
            /* Encoder in name editor: cycle current character */
            s_user_chord_name_edit[s_user_chord_name_cursor] =
                UI_Sequencer_CycleNameChar(s_user_chord_name_edit[s_user_chord_name_cursor], delta);
            UI_Display_DrawUserChordNameEditor(s_user_chord_name_edit, s_user_chord_name_cursor);
        }
        else if (s_ui_mode == UI_MODE_SONG_CHAIN)
        {
            int16_t next = (int16_t)s_song_chain[s_song_cursor] + delta;
            while (next < 0) next += 32;
            while (next >= 32) next -= 32;
            s_song_chain[s_song_cursor] = (uint8_t)next;
            Bridge_SetChainPatternAt(s_song_cursor, s_song_chain[s_song_cursor]);
            UI_Sequencer_DrawSongChainMenu();
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
            /* Grid selection is matrix-driven in Phase 1; ignore plain encoder turns here. */
        }
    }

    /* ── Active step display update ───────────────────────────────────── */
    if (s_ui_mode == UI_MODE_GRID)
    {
        uint32_t now_ms = HAL_GetTick();
        uint8_t repeat_flash_enabled = 0;
        uint8_t active_has_notes = 0;
        static uint8_t s_repeat_flash_was_enabled = 0;

        if (s_active_step >= 1 && s_active_step <= 12 && Bridge_IsPlaying())
        {
            active_has_notes = UI_Sequencer_StepHasNotes(s_active_step);
            if (s_step_chords[s_active_step - 1].loop_count > 1)
            {
                repeat_flash_enabled = 1;
            }
        }

        if (repeat_flash_enabled)
        {
            if (!s_repeat_flash_was_enabled)
            {
                s_repeat_flash_on = 1;
                s_repeat_flash_ms = now_ms;
                UI_Display_SetRepeatFlash(1, s_repeat_flash_on);
                if (s_active_step > 0)
                {
                    uint8_t is_selected = (s_active_step == s_selected_step);
                    UI_Display_DrawStepBox(s_active_step, is_selected, 1, active_has_notes);
                }
            }

            if ((now_ms - s_repeat_flash_ms) >= 140)
            {
                s_repeat_flash_ms = now_ms;
                s_repeat_flash_on = (uint8_t)!s_repeat_flash_on;
                UI_Display_SetRepeatFlash(1, s_repeat_flash_on);
                if (s_active_step > 0)
                {
                    uint8_t is_selected = (s_active_step == s_selected_step);
                    UI_Display_DrawStepBox(s_active_step, is_selected, 1, active_has_notes);
                }
            }
        }
        else
        {
            if (s_repeat_flash_was_enabled)
            {
                s_repeat_flash_on = 0;
                UI_Display_SetRepeatFlash(0, 0);
                if (s_active_step > 0)
                {
                    uint8_t is_selected = (s_active_step == s_selected_step);
                    UI_Display_DrawStepBox(s_active_step, is_selected, 1, active_has_notes);
                }
            }
        }

        s_repeat_flash_was_enabled = repeat_flash_enabled;

        if (s_active_step != s_last_active)
        {
            if (s_last_active > 0)
            {
                uint8_t was_selected = (s_last_active == s_selected_step);
                uint8_t last_has_notes = UI_Sequencer_StepHasNotes(s_last_active);
                UI_Display_DrawStepBox(s_last_active, was_selected, 0, last_has_notes);
            }
            if (s_active_step > 0)
            {
                uint8_t is_selected = (s_active_step == s_selected_step);
                UI_Display_DrawStepBox(s_active_step, is_selected, 1, active_has_notes);
            }
            
            /* Update last active step for next comparison */
            s_last_active = s_active_step;
        }
    }

   
uint32_t now = HAL_GetTick();

if (s_ui_mode == UI_MODE_SONG_CHAIN)
{
    if ((now - s_song_blink_ms) >= 180)
    {
        s_song_blink_ms = now;
        s_song_blink_on = (uint8_t)!s_song_blink_on;
        UI_Sequencer_DrawSongChainMenu();
    }
}

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