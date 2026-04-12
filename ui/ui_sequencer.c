#include "ui_sequencer.h"
#include "ui_display.h"
#include "ui_input.h"
#include "ui_transport.h"

/* ── State ───────────────────────────────────────────────────────────────── */
static uint8_t  s_selected_step  = 1;    /* cursor position 1-12      */
static uint8_t  s_active_step    = 0;    /* currently playing step    */
static uint8_t  s_last_active    = 0;    /* previous playing step     */
static uint8_t  s_last_selected  = 1;    /* previous cursor position  */

/* ── Init ────────────────────────────────────────────────────────────────── */
void UI_Sequencer_Init(void)
{
    s_selected_step = 1;
    s_active_step   = 0;
    s_last_active   = 0;
    s_last_selected = 1;

    UI_Display_Init();
    UI_Transport_Init();
}

/* ── Tick 1ms — called from SysTick ─────────────────────────────────────── */
void UI_Sequencer_Tick1ms(void)
{
    /* Engine tick will go here once wired */
}

/* ── Update — called every 10ms ─────────────────────────────────────────── */
void UI_Sequencer_Update(void)
{
    /* ── Encoder — move cursor or adjust BPM ─────────────────────────── */
    int8_t delta = UI_Input_GetEncoderDelta();

    if (delta != 0)
    {
        if (UI_Input_IsShiftHeld())
        {
            /* Shift + encoder = BPM */
            uint16_t bpm = UI_Transport_GetBPM();
            bpm = (int16_t)bpm + delta < 30  ? 30  :
                  (int16_t)bpm + delta > 300 ? 300 :
                  bpm + delta;
            UI_Transport_SetBPM(bpm);
        }
        else
        {
            /* Encoder = move step cursor */
            uint8_t prev    = s_selected_step;
            int8_t  next    = (int8_t)s_selected_step + delta;

            if (next < 1)  next = 12;
            if (next > 12) next = 1;

            s_selected_step = (uint8_t)next;

            /* Redraw only changed boxes */
            if (prev != s_selected_step)
            {
                uint8_t prev_active = (prev == s_active_step);
                uint8_t next_active = (s_selected_step == s_active_step);

                UI_Display_DrawStepBox(prev,            0, prev_active);
                UI_Display_DrawStepBox(s_selected_step, 1, next_active);
            }
        }
    }

    /* ── Active step — update when engine advances ───────────────────── */
    if (s_active_step != s_last_active)
    {
        /* Clear old active step */
        if (s_last_active > 0)
        {
            uint8_t was_selected = (s_last_active == s_selected_step);
            UI_Display_DrawStepBox(s_last_active, was_selected, 0);
        }

        /* Draw new active step */
        if (s_active_step > 0)
        {
            uint8_t is_selected = (s_active_step == s_selected_step);
            UI_Display_DrawStepBox(s_active_step, is_selected, 1);
        }

        s_last_active = s_active_step;
    }
}