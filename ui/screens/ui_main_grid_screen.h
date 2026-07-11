#ifndef UI_MAIN_GRID_SCREEN_H
#define UI_MAIN_GRID_SCREEN_H

#include <stdint.h>
#include "ui_screen_base.h"
#include "ui_display.h"

/* State owned by the main grid / step-screen view. */
typedef struct {
    uint8_t selected_step;
    uint8_t active_step;
    uint8_t has_chord[12];
} MainGridScreenState;

UiScreen* UI_MainGridScreen_Get(void);
void UI_MainGridScreen_SetContext(uint8_t selected_step, uint8_t active_step, const uint8_t* has_chord_flags);
void UI_MainGridScreen_DrawStep(uint8_t step, uint8_t selected, uint8_t active, uint8_t has_chord);
void UI_MainGridScreen_DrawHeader(uint16_t bpm, TransportState state, uint8_t rec_armed);
void UI_MainGridScreen_DrawStatusRow(uint8_t pattern, uint8_t step, uint32_t loops, uint32_t run_time_ms);

#endif /* UI_MAIN_GRID_SCREEN_H */
