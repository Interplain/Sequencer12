#ifndef UI_TRANSPORT_H
#define UI_TRANSPORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ui_display.h"

/* ── Transport API ───────────────────────────────────────────────────────── */
void           UI_Transport_Init(void);
void           UI_Transport_PlayStop(void);      /* Button 1 — toggle */
void           UI_Transport_RecArm(void);        /* Button 2 — toggle */
void           UI_Transport_Reset(void);         /* Shift + Button 1  */
void           UI_Transport_RecClear(void);      /* Shift + Button 2  */
TransportState UI_Transport_GetState(void);
uint8_t        UI_Transport_IsRecArmed(void);
uint16_t       UI_Transport_GetBPM(void);
void           UI_Transport_SetBPM(uint16_t bpm);  /* Shift + encoder   */

#ifdef __cplusplus
}
#endif

#endif