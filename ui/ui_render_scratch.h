#ifndef UI_RENDER_SCRATCH_H
#define UI_RENDER_SCRATCH_H

#include <stdint.h>
#include "lib/ST7789/st7789.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_RENDER_SCRATCH_OWNER_NONE = 0,
    UI_RENDER_SCRATCH_OWNER_PIANO_ROLL,
    UI_RENDER_SCRATCH_OWNER_MAIN_GRID,
    UI_RENDER_SCRATCH_OWNER_MENU,
    UI_RENDER_SCRATCH_OWNER_MODAL
} UIRenderScratchOwner;

/* Shared static RGB565 scratch: 320x24x2 = 15360 bytes under rotation=3. */
#define UI_RENDER_SCRATCH_ROWS 24u
#define UI_RENDER_SCRATCH_BYTES ((uint32_t)ST7789_WIDTH * UI_RENDER_SCRATCH_ROWS * 2u)

uint8_t* UI_RenderScratch_Acquire(UIRenderScratchOwner owner);
void UI_RenderScratch_Release(UIRenderScratchOwner owner);
uint32_t UI_RenderScratch_SizeBytes(void);
UIRenderScratchOwner UI_RenderScratch_GetOwner(void);

#ifdef __cplusplus
}
#endif

#endif
