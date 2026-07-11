#ifndef UI_SCREEN_TYPES_H
#define UI_SCREEN_TYPES_H

#include <stdint.h>
#include "ui_regions.h"

/* Input events routed through screen modules. */
typedef enum {
    INPUT_ENCODER_DELTA,
    INPUT_ENCODER_PRESS,
    INPUT_PLAY,
    INPUT_REC,
    INPUT_SHIFT,
    INPUT_MATRIX_STEP,
    INPUT_SHIFT_MATRIX_STEP
} InputType;

/* Lightweight lifecycle flags that screen owners can track. */
typedef struct {
    uint8_t is_dirty;
    uint8_t is_active;
} ScreenLifecycleFlags;

/* A screen-owned draw plan for future DMA or staged redraw work. */
typedef struct {
    UiRect rects[4];
    uint8_t region_count;
    uint8_t pending;
    uint8_t has_buffer;
    uint8_t buffer_ready;
    uint16_t buffer_size;
    uint8_t* buffer;
    uint8_t transaction_id;
} UiRenderTransaction;

#endif /* UI_SCREEN_TYPES_H */
