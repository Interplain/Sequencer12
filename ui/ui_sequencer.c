#include "ui_sequencer.h"
#include "ui_display.h"
#include "ui_input.h"
#include "ui_transport.h"
#include "sequencer_bridge.h"

/* ── State ───────────────────────────────────────────────────────────────── */
static uint8_t s_selected_step = 1;   /* cursor position 1-12      */
static uint8_t s_active_step   = 0;   /* currently playing step    */
static uint8_t s_last_active   = 0;   /* previous playing step     */

/* ── Init ────────────────────────────────────────────────────────────────── */
void UI_Sequencer_Init(void)
{
    s_selected_step = 1;
    s_active_step   = 0;
    s_last_active   = 0;

    UI_Display_Init();    /* draws all boxes white, no selection */
    UI_Transport_Init();

    /* Draw initial cursor position */
    UI_Display_DrawStepBox(1, 1, 0);
}

/* ── Tick 1ms — called from SysTick ─────────────────────────────────────── */
void UI_Sequencer_Tick1ms(void)
{
    Bridge_Tick1ms();
}

/* ── Update — called every 10ms from main loop ───────────────────────────── */
void UI_Sequencer_Update(void)
{
    /* ── Active step from engine ──────────────────────────────────────── */
    uint8_t engine_step = (uint8_t)(Bridge_GetCurrentStep() + 1); /* 0-based → 1-based */
  
    if (Bridge_IsPlaying())
        s_active_step = engine_step;
    else
        s_active_step = 0;

    /* ── Encoder — move cursor or adjust BPM ─────────────────────────── */
    int8_t delta = UI_Input_GetEncoderDelta();

    if (delta != 0)
    {
        if (UI_Input_IsShiftHeld())
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
                UI_Display_DrawStepBox(prev,            0, (prev           == s_active_step));
                UI_Display_DrawStepBox(s_selected_step, 1, (s_selected_step == s_active_step));
            }
        }
    }

    /* ── Active step display update ───────────────────────────────────── */
    if (s_active_step != s_last_active)
    {
        if (s_last_active > 0)
        {
            uint8_t was_selected = (s_last_active == s_selected_step);
            UI_Display_DrawStepBox(s_last_active, was_selected, 0);
        }

        if (s_active_step > 0)
        {
            uint8_t is_selected = (s_active_step == s_selected_step);
            UI_Display_DrawStepBox(s_active_step, is_selected, 1);
        }

        s_last_active = s_active_step;
    }

    /* Debug row */
    UI_Display_DrawDebugRow(
    UI_Transport_GetBPM(),
    Bridge_GetCurrentStep() + 1,
    Bridge_GetElapsedMs()
    );

    /* ── Process engine dirty flags ───────────────────────────────────── */
    Bridge_Process();
}