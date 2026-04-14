#ifndef UI_REGIONS_H
#define UI_REGIONS_H

#include <stdint.h>

typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;
} UiRect;

#define UI_REGION_HEADER   (UiRect){0,   0, 240, 24}
#define UI_REGION_STATUS   (UiRect){0,  24, 240, 24}
#define UI_REGION_GRID     (UiRect){0,  48, 240, 192}

#define UI_REGION_MENU_TOP    (UiRect){0,   0, 240, 40}
#define UI_REGION_MENU_LIST   (UiRect){0,  40, 240, 140}
#define UI_REGION_MENU_FOOTER (UiRect){0, 180, 240, 60}

#endif