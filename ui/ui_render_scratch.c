#include "ui_render_scratch.h"

/* Ownership rule:
 * Only the currently active renderer may hold this shared scratch at a time.
 * Acquire fails when another owner is active.
 */
static uint8_t s_ui_render_scratch[UI_RENDER_SCRATCH_BYTES];
static UIRenderScratchOwner s_owner = UI_RENDER_SCRATCH_OWNER_NONE;

uint8_t* UI_RenderScratch_Acquire(UIRenderScratchOwner owner)
{
    if (owner == UI_RENDER_SCRATCH_OWNER_NONE)
    {
        return 0;
    }

    if (s_owner == UI_RENDER_SCRATCH_OWNER_NONE)
    {
        s_owner = owner;
        return s_ui_render_scratch;
    }

    if (s_owner == owner)
    {
        return s_ui_render_scratch;
    }

    return 0;
}

void UI_RenderScratch_Release(UIRenderScratchOwner owner)
{
    if (owner == UI_RENDER_SCRATCH_OWNER_NONE)
    {
        return;
    }

    if (s_owner == owner)
    {
        s_owner = UI_RENDER_SCRATCH_OWNER_NONE;
    }
}

uint32_t UI_RenderScratch_SizeBytes(void)
{
    return (uint32_t)UI_RENDER_SCRATCH_BYTES;
}

UIRenderScratchOwner UI_RenderScratch_GetOwner(void)
{
    return s_owner;
}
