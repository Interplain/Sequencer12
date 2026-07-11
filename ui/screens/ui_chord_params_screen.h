#ifndef UI_CHORD_PARAMS_SCREEN_H
#define UI_CHORD_PARAMS_SCREEN_H

#include <stdint.h>
#include "ui_screen_base.h"
#include "ui_display.h"

/* State owned by the CHORD_PARAMS screen. */
typedef struct {
    uint8_t step;
    uint8_t param_cursor;
    uint8_t footer_action;
    ChordParams chord;
} ChordParamsScreenState;

UiScreen* UI_ChordParamsScreen_Get(void);
void UI_ChordParamsScreen_SetContext(uint8_t step, const ChordParams* chord, uint8_t param_cursor);
void UI_ChordParamsScreen_SetFooterAction(uint8_t action);
uint8_t UI_ChordParamsScreen_GetFooterAction(void);
void UI_ChordParamsScreen_CopyToChord(ChordParams* chord);

#endif /* UI_CHORD_PARAMS_SCREEN_H */
