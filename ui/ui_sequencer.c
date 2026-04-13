#include "ui_sequencer.h"
#include "ui_display.h"
#include "ui_input.h"
#include "ui_transport.h"
#include "sequencer_bridge.h"
#include "stm32f4xx_hal.h"

/* ── UI Modes ────────────────────────────────────────────────────────────── */
typedef enum {
    UI_MODE_GRID,           /* main 12-step grid view */
    UI_MODE_CHORD_MENU,     /* chord selection submenu for current step */
    UI_MODE_CHORD_PARAMS    /* chord parameter editor */
} UiMode;

/* ── State ───────────────────────────────────────────────────────────────── */
static UiMode      s_ui_mode        = UI_MODE_GRID;
static uint8_t     s_selected_step  = 1;   /* cursor position 1-12      */
static uint8_t     s_active_step    = 0;   /* currently playing step    */
static uint8_t     s_last_active    = 0;   /* previous playing step     */
static uint8_t     s_menu_step      = 1;   /* which step's chord we're editing */
static ChordParams s_step_chords[12];     /* chord parameters for each step */
static uint8_t     s_param_cursor   = 0;   /* which parameter we're editing (0=root, 1=type, 2=arp, 3=duration) */

/* ── Status row caching (prevent flicker) ─────────────────────────────────── */
static uint8_t  s_last_pattern    = 0xFF;
static uint8_t  s_last_step       = 0xFF;
static uint32_t s_last_loops      = 0xFFFFFFFF;
static uint32_t s_last_run_time   = 0xFFFFFFFF;

/* ── Init ────────────────────────────────────────────────────────────────── */
void UI_Sequencer_Init(void)
{
    s_ui_mode       = UI_MODE_GRID;  // Start in grid mode
    s_selected_step = 1;
    s_active_step   = 0;
    s_last_active   = 0;
    s_menu_step     = 1;
    s_param_cursor  = 0;

    /* Initialize all chords to clear (no chord) */
    for (uint8_t i = 0; i < 12; i++) {
        s_step_chords[i].root_key = 0;    /* C */
        s_step_chords[i].chord_type = 0;  /* Clear */
        s_step_chords[i].arp_pattern = 0; /* Block */
        s_step_chords[i].duration = 1;    /* 8th notes */
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
            HAL_Delay(5);
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
    /* ── Encoder press — enter/exit chord menu ─────────────────────────── */
    if (UI_Input_IsEncoderPressed())
    {
        if (s_ui_mode == UI_MODE_GRID)
        {
            /* Enter chord menu for currently selected step */
            s_ui_mode   = UI_MODE_CHORD_MENU;
            s_menu_step = s_selected_step;
            /* Pre-select current chord type (0=clear, 1-16=chord types) */
            uint8_t current_type = s_step_chords[s_selected_step - 1].chord_type;
            UI_Display_SetChordSelection(current_type);
            UI_Display_DrawChordMenu(s_menu_step, &s_step_chords[s_selected_step - 1]);
        }
        else if (s_ui_mode == UI_MODE_CHORD_MENU)
        {
            /* Confirm chord type selection and enter parameters */
            uint8_t selected_type = UI_Display_GetSelectedChord();
            s_step_chords[s_menu_step - 1].chord_type = selected_type;
            
            s_ui_mode = UI_MODE_CHORD_PARAMS;
            s_param_cursor = 0;  /* Start with root key */
            UI_Display_DrawChordParams(s_menu_step, &s_step_chords[s_menu_step - 1], s_param_cursor);
        }
        else if (s_ui_mode == UI_MODE_CHORD_PARAMS)
        {
            /* Cycle to next parameter */
            s_param_cursor = (s_param_cursor + 1) % 4;
            UI_Display_DrawChordParams(s_menu_step, &s_step_chords[s_menu_step - 1], s_param_cursor);
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
        if (s_ui_mode == UI_MODE_CHORD_PARAMS)
        {
            /* Navigate chord parameters */
            UI_Display_NavigateChordParams(delta, s_menu_step, &s_step_chords[s_menu_step - 1], s_param_cursor);
        }
        else if (s_ui_mode == UI_MODE_CHORD_MENU)
        {
            if (UI_Input_IsShiftHeld())
            {
                /* Shift + encoder in chord menu: change steps */
                int8_t  next = (int8_t)s_menu_step + delta;

                if (next < 1)  next = 12;
                if (next > 12) next = 1;

                s_menu_step = (uint8_t)next;
                s_selected_step = s_menu_step;  /* Keep cursor in sync */

                /* Pre-select current chord assignment for new step */
                uint8_t current_chord = s_step_chords[s_menu_step - 1].chord_type;
                UI_Display_SetChordSelection(current_chord);
                UI_Display_DrawChordMenu(s_menu_step, &s_step_chords[s_menu_step - 1]);
            }
            else
            {
                /* Encoder in chord menu: navigate chord options */
                UI_Display_NavigateChordMenu(delta, s_menu_step, &s_step_chords[s_menu_step - 1]);
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
                HAL_Delay(5);  /* Prevent flashing from consecutive redraws */
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
                  HAL_Delay(5);  /* Allow display to finish first redraw before second */
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