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
uint8_t UI_Input_IsEncoderPressed(void);  /* 1 if encoder button just pressed */

#ifdef __cplusplus
}
#endif

#endif