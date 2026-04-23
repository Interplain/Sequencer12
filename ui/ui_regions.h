#ifndef UI_REGIONS_H
#define UI_REGIONS_H

#include <stdint.h>
#include "lib/ST7789/st7789.h"

typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;
} UiRect;

#define UI_SCREEN_W ST7789_WIDTH
#define UI_SCREEN_H ST7789_HEIGHT

#define UI_REGION_HEADER   (UiRect){0,   0, UI_SCREEN_W, 24}
#define UI_REGION_STATUS   (UiRect){0,  24, UI_SCREEN_W, 24}
#define UI_REGION_GRID     (UiRect){0,  48, UI_SCREEN_W, (uint16_t)(UI_SCREEN_H - 48)}

#define UI_REGION_MENU_TOP_H      40
#define UI_REGION_MENU_FOOTER_H   60
#define UI_REGION_MENU_TOP        (UiRect){0, 0, UI_SCREEN_W, UI_REGION_MENU_TOP_H}
#define UI_REGION_MENU_FOOTER     (UiRect){0, (uint16_t)(UI_SCREEN_H - UI_REGION_MENU_FOOTER_H), UI_SCREEN_W, UI_REGION_MENU_FOOTER_H}
#define UI_REGION_MENU_LIST       (UiRect){0, UI_REGION_MENU_TOP_H, UI_SCREEN_W, (uint16_t)(UI_SCREEN_H - UI_REGION_MENU_TOP_H - UI_REGION_MENU_FOOTER_H)}

#endif