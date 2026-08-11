#ifndef UI_SCREEN_ROUTER_H
#define UI_SCREEN_ROUTER_H

#include <stdint.h>
#include "screens/ui_screen_base.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Small fixed stack for incremental migration to push/pop navigation. */
#define UI_SCREEN_ROUTER_MAX_DEPTH 4u

void UI_ScreenRouter_Init(void);

/* Lifecycle-aware switch: mirrors existing set-active behavior. */
void UI_ScreenRouter_SwitchTo(UiScreen* screen);

/* Legacy path: detach active screen without calling on_exit. */
void UI_ScreenRouter_DetachActive(void);

/* Future stack navigation helpers (not yet used by sequencer routes). */
uint8_t UI_ScreenRouter_Push(UiScreen* screen);
uint8_t UI_ScreenRouter_Pop(ScreenExitReason reason);

UiScreen* UI_ScreenRouter_GetActive(void);
uint8_t UI_ScreenRouter_GetDepth(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_SCREEN_ROUTER_H */