#include "ui_screen_router.h"

typedef struct {
    UiScreen* stack[UI_SCREEN_ROUTER_MAX_DEPTH];
    uint8_t depth;
} UiScreenRouterState;

static UiScreenRouterState s_router;

static void Router_Activate(UiScreen* screen)
{
    if (screen == NULL)
    {
        return;
    }

    screen->is_active = 1;
    screen->is_dirty = 1;
    screen->render_plan.pending = 1;

    if (screen->on_enter)
    {
        screen->on_enter();
    }
    if (screen->on_draw)
    {
        screen->on_draw();
    }

    screen->render_plan.pending = 0;
}

static void Router_Deactivate(UiScreen* screen, uint8_t call_exit, ScreenExitReason reason)
{
    if (screen == NULL)
    {
        return;
    }

    if (call_exit && screen->on_exit)
    {
        screen->on_exit(reason);
    }

    screen->is_active = 0;
}

void UI_ScreenRouter_Init(void)
{
    s_router.depth = 0;
    for (uint8_t i = 0; i < UI_SCREEN_ROUTER_MAX_DEPTH; i++)
    {
        s_router.stack[i] = NULL;
    }
}

void UI_ScreenRouter_SwitchTo(UiScreen* screen)
{
    UiScreen* active = UI_ScreenRouter_GetActive();

    if (active != NULL)
    {
        Router_Deactivate(active, 1u, SCREEN_EXIT_NONE);
    }

    if (screen == NULL)
    {
        s_router.depth = 0;
        for (uint8_t i = 0; i < UI_SCREEN_ROUTER_MAX_DEPTH; i++)
        {
            s_router.stack[i] = NULL;
        }
        return;
    }

    for (uint8_t i = 0; i < UI_SCREEN_ROUTER_MAX_DEPTH; i++)
    {
        s_router.stack[i] = NULL;
    }
    s_router.depth = 1;
    s_router.stack[0] = screen;

    Router_Activate(screen);
}

void UI_ScreenRouter_DetachActive(void)
{
    UiScreen* active = UI_ScreenRouter_GetActive();

    if (active != NULL)
    {
        Router_Deactivate(active, 0u, SCREEN_EXIT_NONE);
    }

    s_router.depth = 0;
}

uint8_t UI_ScreenRouter_Push(UiScreen* screen)
{
    UiScreen* active;

    if (screen == NULL)
    {
        return 0u;
    }
    if (s_router.depth >= UI_SCREEN_ROUTER_MAX_DEPTH)
    {
        return 0u;
    }

    active = UI_ScreenRouter_GetActive();
    if (active != NULL)
    {
        active->is_active = 0;
    }

    s_router.stack[s_router.depth] = screen;
    s_router.depth++;
    Router_Activate(screen);
    return 1u;
}

uint8_t UI_ScreenRouter_Pop(ScreenExitReason reason)
{
    UiScreen* active;
    UiScreen* next;

    if (s_router.depth == 0)
    {
        return 0u;
    }

    active = s_router.stack[s_router.depth - 1];
    Router_Deactivate(active, 1u, reason);
    s_router.stack[s_router.depth - 1] = NULL;
    s_router.depth--;

    next = UI_ScreenRouter_GetActive();
    if (next != NULL)
    {
        next->is_active = 1;
        if (next->on_draw)
        {
            next->on_draw();
        }
    }

    return 1u;
}

UiScreen* UI_ScreenRouter_GetActive(void)
{
    if (s_router.depth == 0)
    {
        return NULL;
    }

    return s_router.stack[s_router.depth - 1];
}

uint8_t UI_ScreenRouter_GetDepth(void)
{
    return s_router.depth;
}