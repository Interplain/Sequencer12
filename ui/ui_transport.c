#include "ui_transport.h"
#include "ui_display.h"

/* ── State ───────────────────────────────────────────────────────────────── */
static TransportState s_state     = TRANSPORT_STOPPED;
static uint8_t        s_rec_armed = 0;
static uint16_t       s_bpm       = 120;

/* ── Init ────────────────────────────────────────────────────────────────── */
void UI_Transport_Init(void)
{
    s_state     = TRANSPORT_STOPPED;
    s_rec_armed = 0;
    s_bpm       = 120;
    UI_Display_DrawHeader(s_bpm, s_state, s_rec_armed);
}

/* ── Play/Stop toggle ────────────────────────────────────────────────────── */
void UI_Transport_PlayStop(void)
{
    if (s_state == TRANSPORT_STOPPED)
    {
        s_state = s_rec_armed ? TRANSPORT_PLAYING_RECORDING : TRANSPORT_PLAYING;
    }
    else
    {
        s_state = TRANSPORT_STOPPED;
    }

    UI_Display_DrawHeader(s_bpm, s_state, s_rec_armed);
}

/* ── Record arm toggle ───────────────────────────────────────────────────── */
void UI_Transport_RecArm(void)
{
    s_rec_armed = !s_rec_armed;
    UI_Display_DrawHeader(s_bpm, s_state, s_rec_armed);
}

/* ── Reset — Shift + Button 1 ────────────────────────────────────────────── */
void UI_Transport_Reset(void)
{
    s_state     = TRANSPORT_STOPPED;
    s_rec_armed = 0;
    UI_Display_DrawHeader(s_bpm, s_state, s_rec_armed);
}

/* ── Rec clear — Shift + Button 2 ───────────────────────────────────────── */
void UI_Transport_RecClear(void)
{
    s_rec_armed = 0;
    UI_Display_DrawHeader(s_bpm, s_state, s_rec_armed);
}

/* ── BPM ─────────────────────────────────────────────────────────────────── */
void UI_Transport_SetBPM(uint16_t bpm)
{
    if (bpm < 30)  bpm = 30;
    if (bpm > 300) bpm = 300;
    s_bpm = bpm;
    UI_Display_DrawHeader(s_bpm, s_state, s_rec_armed);
}

uint16_t UI_Transport_GetBPM(void)
{
    return s_bpm;
}

/* ── Getters ─────────────────────────────────────────────────────────────── */
TransportState UI_Transport_GetState(void)
{
    return s_state;
}

uint8_t UI_Transport_IsRecArmed(void)
{
    return s_rec_armed;
}