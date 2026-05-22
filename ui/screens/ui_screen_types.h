/*
 * ui_screen_types.h
 * Shared lightweight types for screen state and ownership.
 *
 * This header defines screen-agnostic types that are independent from
 * rendering, display drivers, or animation systems. Screen state containers
 * should depend on this, not on display/render modules.
 *
 * The goal is to maintain clean separation between:
 * - State ownership (what a screen controls)
 * - Rendering (how a screen is drawn)
 * - Display drivers (hardware/SPI communication)
 *
 * This prevents coupling between state management and display infrastructure.
 */

#ifndef UI_SCREEN_TYPES_H
#define UI_SCREEN_TYPES_H

#include <stdint.h>

/* ── Generic coordinate/region (already in ui_regions.h, re-exported here) ── */
typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;
} UiRect;

/* ── Input event types ── */
typedef enum {
    INPUT_ENCODER_DELTA,      /* int8_t value: encoder rotation delta */
    INPUT_ENCODER_PRESS,      /* single press; no value */
    INPUT_PLAY,               /* play button pressed */
    INPUT_REC,                /* record button pressed */
    INPUT_SHIFT,              /* shift button short tap */
    INPUT_MATRIX_STEP,        /* int8_t value: 1-12 step button pressed */
    INPUT_SHIFT_MATRIX_STEP,  /* int8_t value: 1-12 shift+step pressed */
} InputType;

/* ── Screen exit reasons (lifecycle) ── */
typedef enum {
    SCREEN_EXIT_NONE = 0,
    SCREEN_EXIT_BACK,         /* User cancelled without saving */
    SCREEN_EXIT_SAVE,         /* User explicitly saved changes */
    SCREEN_EXIT_DISCARD       /* User discarded changes */
} ScreenExitReason;

/* ── Lightweight composition mode (not UI mode) ── */
typedef enum {
    COMPOSITION_MODE_STEP = 0,
    COMPOSITION_MODE_CHORD,
    COMPOSITION_MODE_TIMING,
    COMPOSITION_MODE_PATTERN
} CompositionMode;

/* ── Lightweight chord parameters (pure data, no display logic) ── */
typedef struct {
    uint8_t root_key;         /* 0-11: C=0, C#=1, ..., B=11 */
    uint8_t chord_type;       /* 0=clear, 1-16=chord types, etc */
    uint8_t arp_pattern;      /* 0=block, 1=up, 2=down, 3=up-down, 4=random */
    uint8_t duration;         /* 0=16th, 1=8th, 2=quarter, 3=dotted 8th */
    uint8_t loop_count;       /* 1-16 repeats */
} ChordParamsData;

/* ── Dirty flag tracking (minimal, no coupling to render system) ── */
typedef struct {
    uint8_t is_dirty;         /* Screen needs redraw */
    uint8_t is_active;        /* Screen is currently active */
} ScreenLifecycleFlags;

#endif /* UI_SCREEN_TYPES_H */
