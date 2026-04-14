#ifndef UI_INPUT_H
#define UI_INPUT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* ── Init ────────────────────────────────────────────────────────────────── */
void UI_Input_Init(void);

/* ── Poll — call every 10ms from main loop ───────────────────────────────── */
void UI_Input_Poll(void);

/* ── Encoder state — read by ui_sequencer ───────────────────────────────── */
int8_t  UI_Input_GetEncoderDelta(void);   /* consume encoder movement        */
uint8_t UI_Input_IsShiftHeld(void);       /* 1 if shift currently held       */
uint8_t UI_Input_IsEncoderPressed(void);     /* 1 if encoder button just pressed */
uint8_t UI_Input_IsPlayPressed(void);        /* 1 if Play pressed (consume) */
uint8_t UI_Input_IsShiftPlayPressed(void);   /* 1 if Shift+Play pressed (consume) */
uint8_t UI_Input_IsRecPressed(void);         /* 1 if Rec pressed (consume) */
uint8_t UI_Input_IsShiftRecPressed(void);    /* 1 if Shift+Rec pressed (consume) */
uint8_t UI_Input_GetStepPressed(void);       /* consume next step press (1-12, 0=none) */
uint8_t UI_Input_GetShiftStepPressed(void);  /* consume next shift+step press (1-12, 0=none) */

#ifdef __cplusplus
}
#endif

#endif