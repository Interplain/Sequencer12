#ifndef UI_SCREEN_BASE_H
#define UI_SCREEN_BASE_H

#include <stdint.h>
#include "ui_screen_types.h"

/* Screen lifecycle exit reasons. */
typedef enum {
    SCREEN_EXIT_NONE = 0,
    SCREEN_EXIT_BACK,
    SCREEN_EXIT_SAVE,
    SCREEN_EXIT_DISCARD
} ScreenExitReason;

/* Generic UI screen structure. */
typedef struct {
    const char* name;
    void (*on_enter)(void);
    void (*on_update)(void);
    void (*on_input)(InputType input_type, int8_t value);
    void (*on_exit)(ScreenExitReason reason);
    void (*on_draw)(void);
    UiRect owned_region;
    UiRenderTransaction render_plan;
    uint8_t is_active;
    uint8_t is_dirty;
} UiScreen;

static inline void UI_Screen_SetRenderPlan(UiScreen* screen,
                                           const UiRect* regions,
                                           uint8_t region_count,
                                           uint8_t* buffer,
                                           uint16_t buffer_size)
{
    if (screen == NULL) return;

    screen->render_plan.region_count = 0;
    screen->render_plan.pending = 1;
    screen->render_plan.has_buffer = (buffer != NULL && buffer_size > 0);
    screen->render_plan.buffer_ready = screen->render_plan.has_buffer;
    screen->render_plan.buffer_size = buffer_size;
    screen->render_plan.buffer = buffer;
    screen->render_plan.transaction_id++;

    if (regions != NULL && region_count > 0 && region_count <= 4)
    {
        for (uint8_t i = 0; i < region_count; i++)
        {
            screen->render_plan.rects[i] = regions[i];
        }
        screen->render_plan.region_count = region_count;
    }
}

#endif /* UI_SCREEN_BASE_H */
