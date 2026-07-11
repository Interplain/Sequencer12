#ifndef UI_CHORD_MENU_SCREEN_H
#define UI_CHORD_MENU_SCREEN_H

#include <stdint.h>
#include "ui_screen_base.h"
#include "ui_display.h"

/* State owned by the CHORD_MENU screen. */
typedef struct {
    uint8_t step;
    uint8_t selected_chord_idx;
    ChordParams chord;
} ChordMenuState;

UiScreen* UI_ChordMenuScreen_Get(void);
void UI_ChordMenuScreen_SetContext(uint8_t step, const ChordParams* chord);
void UI_ChordMenuScreen_SetSelection(uint8_t selection);
uint8_t UI_ChordMenuScreen_GetSelection(void);

#endif /* UI_CHORD_MENU_SCREEN_H */
