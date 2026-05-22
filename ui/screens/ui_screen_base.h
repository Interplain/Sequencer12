/*
 * ui_screen_base.h
 * Core UI screen lifecycle and state handling infrastructure.
 *
 * This header defines the lifecycle, responsibilities, and contracts
 * for modular UI screens. It is the foundation for future modularisation
 * of the S12 UI architecture.
 */

#ifndef UI_SCREEN_BASE_H
#define UI_SCREEN_BASE_H

#include <stdint.h>
#include "ui_regions.h"  // For UiRect definition

/* Screen lifecycle exit reasons */
typedef enum {
    SCREEN_EXIT_NONE = 0,
    SCREEN_EXIT_BACK,       /* User exited without saving */
    SCREEN_EXIT_SAVE,       /* User explicitly saved changes */
    SCREEN_EXIT_DISCARD     /* User explicitly discarded changes */
} ScreenExitReason;

/* Generic UI screen structure */
typedef struct {
    const char *name;       /* Unique, descriptive name of the screen */

    /* Called once when the screen is entered */
    void (*on_enter)(void);

    /* Called from the UI scheduler/update loop while active */
    void (*on_update)(void);

    /* Called when user input is received (e.g., encoder/button press) */
    void (*on_input)(uint8_t input_type, int8_t value);

    /* Called when the screen is exited */
    void (*on_exit)(ScreenExitReason reason);

    /* Called when the screen needs to be (re)drawn */
    void (*on_draw)(void);

    /* The region of the UI owned by this screen */
    UiRect owned_region;

    /* Lifecycle flags */
    uint8_t is_active;      /* 1 if the screen is active, 0 otherwise */
    uint8_t is_dirty;       /* 1 if the screen needs redraw, 0 otherwise */
} UiScreen;

#endif /* UI_SCREEN_BASE_H */
