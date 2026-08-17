#include "ui_screen_piano_roll.h"
#include "ui_display.h"
#include "ui_screen_base.h"
#include "ui_screen_router.h"
#include "ui_regions.h"
#include "st7789.h"
#include "fonts_extra.h"
#include <stdio.h>
#include <string.h>

/* ── Layout constants ────────────────────────────────────────────────────── */
#define SCREEN_W    ST7789_WIDTH
#define SCREEN_H    ST7789_HEIGHT
#define MENU_TOP_Y  UI_REGION_MENU_TOP_H
#define MENU_LIST_H (SCREEN_H - UI_REGION_MENU_TOP_H - UI_REGION_MENU_FOOTER_H)
#define FOOTER_TEXT_Y1  (SCREEN_H - UI_REGION_MENU_FOOTER_H + 8)
#define MENU_FOOTER_Y   (SCREEN_H - UI_REGION_MENU_FOOTER_H)

/* ── Colours ─────────────────────────────────────────────────────────────── */
#define COLOR8(r,g,b) RGB565((r)>>3, (g)>>2, (b)>>3)
#define COLOR_BG           RGB565(2, 4, 8)
#define COLOR_PANEL        RGB565(6, 10, 16)
#define COLOR_PANEL_ALT    RGB565(10, 16, 26)
#define COLOR_TEXT_MAIN    WHITE
#define COLOR_ACTIVE       RGB565(16, 40, 63)
#define COLOR_KEY_HILITE   COLOR8(232, 206, 108)

/* ── Menu constants ──────────────────────────────────────────────────────── */
#define MENU_FRAME_CHORD 1

/* ── Forward declarations for ui_display functions ──────────────────────– */
extern void MenuTemplate_Begin(uint8_t frame_id);
extern void MenuTemplate_DrawHeader(const char* title, const char* indicator, uint16_t indicator_color);
extern void FillRegion(UiRect r, uint16_t color);

/* ── Piano Roll Screen State ─────────────────────────────────────────────── */

typedef enum {
    PIANO_VIEW_KEYBOARD = 0,
    PIANO_VIEW_ROLL = 1
} PianoViewMode;

static struct {
    /* Current rendering mode */
    PianoViewMode view_mode;
    PianoRollMode roll_mode;
    
    /* Context */
    uint8_t step;
    uint16_t note_mask;
    uint8_t selected_key;
    uint8_t slot_index;
    uint8_t slot_count;
    char title[32];
    char footer_label[24];
    
    /* Keyboard view state */
    char header_title[24];
    
    /* Step roll view state */
    uint8_t roll_layout_valid;
    uint8_t roll_last_step;
    uint16_t roll_last_note_mask;
    uint8_t roll_last_selected_key;
    uint8_t roll_last_slot_index;
    uint8_t roll_last_slot_count;
    
    /* Cache flags */
    uint8_t needs_redraw;
} s_piano_roll = {
    .view_mode = PIANO_VIEW_KEYBOARD,
    .roll_mode = PIANO_ROLL_MODE_KEYBOARD,
    .step = 1,
    .note_mask = 0,
    .selected_key = 0,
    .slot_index = 0,
    .slot_count = 1,
    .title = "Create Chord",
    .footer_label = "SAVE/DONE",
    .header_title = "Create Chord",
    .roll_layout_valid = 0,
    .roll_last_step = 0xFF,
    .roll_last_note_mask = 0,
    .roll_last_selected_key = 0xFF,
    .roll_last_slot_index = 0xFF,
    .roll_last_slot_count = 0xFF,
    .needs_redraw = 1
};

/* ── Constants & Lookup Tables ──────────────────────────────────────────── */

/* Piano keyboard layout */
#define PIANO_WHITE_COUNT 7
#define PIANO_BLACK_COUNT 5
#define PIANO_KEY_GAP     3
#define PIANO_START_X     4
#define PIANO_START_Y     (MENU_TOP_Y + 6)
#define PIANO_TOTAL_W     (SCREEN_W - (PIANO_START_X * 2))
#define PIANO_KEY_W       ((PIANO_TOTAL_W - ((PIANO_WHITE_COUNT - 1) * PIANO_KEY_GAP)) / PIANO_WHITE_COUNT)
#define PIANO_KEY_H       128
#define PIANO_BLACK_W     20
#define PIANO_BLACK_H     84

/* Step piano roll layout */
#define ROLL_HEADER_H   48u
#define ROLL_FOOTER_H   35u
#define ROLL_FOOTER_Y   ((uint16_t)(SCREEN_H - ROLL_FOOTER_H))
#define ROLL_GRID_Y     ROLL_HEADER_H
#define ROLL_ROW_H      13u
#define ROLL_LABEL_W    44u
#define ROLL_GRID_X     ROLL_LABEL_W
#define ROLL_GRID_W     ((uint16_t)(SCREEN_W - ROLL_LABEL_W))

/* Roll colour palette */
#define ROLL_COL_WHITE_KEY_BG   WHITE
#define ROLL_COL_WHITE_KEY_FG   BLACK
#define ROLL_COL_BLACK_KEY_BG   COLOR8(30, 30, 30)
#define ROLL_COL_BLACK_KEY_FG   WHITE
#define ROLL_COL_LANE_WHITE     COLOR8(230, 228, 129)
#define ROLL_COL_LANE_BLACK     COLOR8(121, 224, 152)
#define ROLL_COL_SEL_BORDER     COLOR8(237, 141, 24)
#define ROLL_COL_NOTE_BLOCK     COLOR8(224, 80, 40)
#define ROLL_COL_ROW_SEP        COLOR8(50, 55, 60)
#define ROLL_COL_KEY_DIVIDER    COLOR8(80, 80, 80)

/* Note name tables */
static const uint8_t k_piano_white_keys[PIANO_WHITE_COUNT] = {0, 2, 4, 5, 7, 9, 11};
static const uint8_t k_piano_black_keys[PIANO_BLACK_COUNT] = {1, 3, 6, 8, 10};
static const uint8_t k_piano_black_after_white_idx[PIANO_BLACK_COUNT] = {0, 1, 3, 4, 5};
static const char* k_note_names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char* k_note_labels_oct5[12] = {"C5", "C#5", "D5", "D#5", "E5", "F5", "F#5", "G5", "G#5", "A5", "A#5", "B5"};

/* ── Drawing Helpers ────────────────────────────────────────────────────── */

extern void Bridge_GetStepChordDisplayName(uint8_t step, char* out, uint16_t max_len);
extern void Bridge_FindChordName(uint16_t note_mask, char* out, uint16_t max_len);
extern uint8_t Bridge_GetStepLedgerLength(uint8_t step_index);
extern uint16_t Bridge_GetStepLedgerSlot(uint8_t step_index, uint8_t slot_index);

static uint8_t RollIsBlack(uint8_t note)
{
    return (note == 1u || note == 3u || note == 6u || note == 8u || note == 10u) ? 1u : 0u;
}

static int8_t PianoWhiteSlotForNote(uint8_t note)
{
    for (uint8_t i = 0; i < PIANO_WHITE_COUNT; i++) 
        if (k_piano_white_keys[i] == note) return (int8_t)i;
    return -1;
}

static int8_t PianoBlackSlotForNote(uint8_t note)
{
    for (uint8_t i = 0; i < PIANO_BLACK_COUNT; i++) 
        if (k_piano_black_keys[i] == note) return (int8_t)i;
    return -1;
}

/* ── Piano Keyboard Drawing ───────────────────────────────────────────────– */

static void DrawPianoWhiteKey(uint8_t slot, uint8_t note)
{
    uint16_t x = PIANO_START_X + slot * (PIANO_KEY_W + PIANO_KEY_GAP);
    uint16_t y = PIANO_START_Y;
    uint8_t selected = (s_piano_roll.selected_key == note);
    uint8_t on = (s_piano_roll.note_mask & (1U << note)) ? 1 : 0;
    uint16_t fill = (on || selected) ? COLOR_KEY_HILITE : WHITE;
    uint16_t border = on ? RED : (selected ? COLOR_KEY_HILITE : BLACK);
    uint16_t text_col = on ? BLACK : (selected ? BLACK : BLACK);
    int16_t t = on ? 0 : 2;

    ST7789_FillRect(x, y, PIANO_KEY_W, PIANO_KEY_H, fill);

    if (border != fill)
    {
        ST7789_FillRect(x + t, y + t, PIANO_KEY_W - (2 * t), 1, border);
        ST7789_FillRect(x + t, y + PIANO_KEY_H - 1 - t, PIANO_KEY_W - (2 * t), 1, border);
        ST7789_FillRect(x + t, y + t, 1, PIANO_KEY_H - (2 * t), border);
        ST7789_FillRect(x + PIANO_KEY_W - 1 - t, y + t, 1, PIANO_KEY_H - (2 * t), border);
    }

    ST7789_DrawString(x + (PIANO_KEY_W / 2) - 8, y + (PIANO_KEY_H / 2) - 8,
                      k_note_names[note], &Font12x20, text_col, fill);

}

static void DrawPianoBlackKey(uint8_t slot, uint8_t note)
{
    uint16_t white_right = PIANO_START_X + (k_piano_black_after_white_idx[slot] + 1) * (PIANO_KEY_W + PIANO_KEY_GAP) - PIANO_KEY_GAP;
    uint16_t x = white_right - (PIANO_BLACK_W / 2);
    uint16_t y = PIANO_START_Y;
    uint8_t selected = (s_piano_roll.selected_key == note);
    uint8_t on = (s_piano_roll.note_mask & (1U << note)) ? 1 : 0;
    uint16_t fill = (on || selected) ? COLOR_KEY_HILITE : COLOR8(30, 30, 30);
    uint16_t border = on ? RED : (selected ? COLOR_KEY_HILITE : WHITE);
    uint16_t text_col = (on || selected) ? BLACK : WHITE;
    int16_t t = on ? 0 : 2;

    ST7789_FillRect(x, y, PIANO_BLACK_W, PIANO_BLACK_H, fill);

    if (border != fill)
    {
        ST7789_FillRect(x + t, y + t, PIANO_BLACK_W - (2 * t), 1, border);
        ST7789_FillRect(x + t, y + PIANO_BLACK_H - 1 - t, PIANO_BLACK_W - (2 * t), 1, border);
        ST7789_FillRect(x + t, y + t, 1, PIANO_BLACK_H - (2 * t), border);
        ST7789_FillRect(x + PIANO_BLACK_W - 1 - t, y + t, 1, PIANO_BLACK_H - (2 * t), border);
    }

    ST7789_DrawString(x + (PIANO_BLACK_W / 2) - 8, y + (PIANO_BLACK_H / 2) - 8,
                      k_note_names[note], &Font10x16, text_col, fill);

}

static void DrawPianoKeyByNote(uint8_t note)
{
    int8_t white_slot = PianoWhiteSlotForNote(note);
    if (white_slot >= 0)
    {
        DrawPianoWhiteKey((uint8_t)white_slot, note);
        for (uint8_t i = 0; i < PIANO_BLACK_COUNT; i++)
        {
            DrawPianoBlackKey(i, k_piano_black_keys[i]);
        }
    }
    else
    {
        int8_t black_slot = PianoBlackSlotForNote(note);
        if (black_slot >= 0)
        {
            DrawPianoBlackKey((uint8_t)black_slot, note);
        }
    }
}

static void DrawKeyboardIndicator(const char* indicator, uint16_t color)
{
    /* Update only indicator line to avoid full header repaint flicker. */
    ST7789_FillRect(0u, 22u, SCREEN_W, 18u, COLOR_PANEL_ALT);
    if (indicator && indicator[0])
    {
        ST7789_DrawString(4u, 22u, indicator, &Font10x16, color, COLOR_PANEL_ALT);
    }
}

/* ── Step Piano Roll Drawing ───────────────────────────────────────────── */

static void RollDrawRow(uint8_t note)
{
    uint8_t  row    = (uint8_t)(11u - note);
    uint16_t y      = (uint16_t)(ROLL_GRID_Y + (uint16_t)row * ROLL_ROW_H);
    uint8_t  sel    = (s_piano_roll.selected_key == note) ? 1u : 0u;
    uint8_t  black  = RollIsBlack(note);

    /* Label area background */
    ST7789_FillRect(0u, y, ROLL_LABEL_W, ROLL_ROW_H, ROLL_COL_WHITE_KEY_BG);

    /* Key graphic */
    if (black)
    {
        uint16_t kw = (uint16_t)(ROLL_LABEL_W - 14u);
        ST7789_FillRect(2u, y, kw, ROLL_ROW_H, ROLL_COL_BLACK_KEY_BG);
        ST7789_DrawString(4u, (uint16_t)(y + 1u),
                          k_note_labels_oct5[note], &Font8x12,
                          ROLL_COL_BLACK_KEY_FG, ROLL_COL_BLACK_KEY_BG);
    }
    else
    {
        ST7789_DrawString(2u, (uint16_t)(y + 1u),
                          k_note_labels_oct5[note], &Font8x12,
                          ROLL_COL_WHITE_KEY_FG, ROLL_COL_WHITE_KEY_BG);
    }

    /* Selection indicator on label */
    if (sel)
    {
        ST7789_FillRect(0u, y, 2u, ROLL_ROW_H, ROLL_COL_SEL_BORDER);
    }

    /* Key right-edge divider */
    ST7789_FillRect((uint16_t)(ROLL_LABEL_W - 1u), y, 1u, ROLL_ROW_H, ROLL_COL_KEY_DIVIDER);

    /* Grid lane background */
    uint16_t lane_bg = black ? ROLL_COL_LANE_BLACK : ROLL_COL_LANE_WHITE;
    ST7789_FillRect(ROLL_GRID_X, y, ROLL_GRID_W, ROLL_ROW_H, lane_bg);

    /* Selection highlight */
    if (sel)
    {
        ST7789_FillRect(ROLL_GRID_X, y, ROLL_GRID_W, 1u, ROLL_COL_SEL_BORDER);
        ST7789_FillRect(ROLL_GRID_X, (uint16_t)(y + ROLL_ROW_H - 2u), ROLL_GRID_W, 1u, ROLL_COL_SEL_BORDER);
    }

    /* Draw per-slot note blocks so sub-step rhythm is visible in the bar. */
    {
        uint8_t slot_count = s_piano_roll.slot_count;
        if (slot_count < 1u) slot_count = 1u;
        if (slot_count > 16u) slot_count = 16u;

        const uint16_t bh = (ROLL_ROW_H > 6u) ? (uint16_t)(ROLL_ROW_H - 6u) : (uint16_t)(ROLL_ROW_H - 2u);
        const uint16_t by = (uint16_t)(y + (ROLL_ROW_H - bh) / 2u);
        const uint16_t slot_w = (uint16_t)(ROLL_GRID_W / slot_count);

        for (uint8_t s = 0u; s < slot_count; ++s)
        {
            const uint16_t sx = (uint16_t)(ROLL_GRID_X + (uint16_t)s * slot_w);
            const uint16_t sw = (s == (uint8_t)(slot_count - 1u)) ?
                                (uint16_t)(ROLL_GRID_W - (uint16_t)s * slot_w) :
                                slot_w;

            uint16_t slot_mask = 0u;
            if (s_piano_roll.roll_mode == PIANO_ROLL_MODE_STEP_ROLL && s_piano_roll.step >= 1u)
            {
                slot_mask = Bridge_GetStepLedgerSlot((uint8_t)(s_piano_roll.step - 1u), s);
            }
            if (slot_count == 1u && slot_mask == 0u)
            {
                slot_mask = s_piano_roll.note_mask;
            }

            if ((slot_mask & (uint16_t)(1u << note)) != 0u)
            {
                const uint16_t pad = (sw > 6u) ? 2u : 1u;
                const uint16_t fill_w = (sw > (uint16_t)(2u * pad)) ? (uint16_t)(sw - (2u * pad)) : 1u;
                ST7789_FillRect((uint16_t)(sx + pad), by, fill_w, bh, ROLL_COL_NOTE_BLOCK);
            }

            /* Vertical separators between sub-slots. */
            if (s > 0u)
            {
                ST7789_FillRect(sx, y, 1u, ROLL_ROW_H, ROLL_COL_ROW_SEP);
            }

            /* Highlight active slot with a full slot box so width is obvious. */
            if (s == s_piano_roll.slot_index)
            {
                if (sw > 2u)
                {
                    ST7789_FillRect(sx, y, sw, 1u, ROLL_COL_SEL_BORDER);
                    ST7789_FillRect(sx, (uint16_t)(y + ROLL_ROW_H - 2u), sw, 1u, ROLL_COL_SEL_BORDER);
                    ST7789_FillRect(sx, y, 1u, ROLL_ROW_H, ROLL_COL_SEL_BORDER);
                    ST7789_FillRect((uint16_t)(sx + sw - 1u), y, 1u, ROLL_ROW_H, ROLL_COL_SEL_BORDER);
                }
            }
        }
    }

    /* Row separator */
    ST7789_FillRect(0u, (uint16_t)(y + ROLL_ROW_H - 1u), SCREEN_W, 1u, ROLL_COL_ROW_SEP);
}

static void RollDrawHeader(void)
{
    char step_label[12];
    char chord_name[20];
    const uint8_t is_rest_slot = (s_piano_roll.note_mask == 0u) ? 1u : 0u;
    const char* slot_state = is_rest_slot ? "REST" : "NOTE";
    const uint16_t slot_state_color = is_rest_slot ? ROLL_COL_ROW_SEP : COLOR_ACTIVE;

    snprintf(step_label, sizeof(step_label), "BAR %u", s_piano_roll.step);
    Bridge_GetStepChordDisplayName((uint8_t)(s_piano_roll.step - 1u), chord_name, sizeof(chord_name));

    ST7789_FillRect(0u, 0u, SCREEN_W, ROLL_HEADER_H, COLOR_PANEL_ALT);
    ST7789_DrawString(8u, 8u,  step_label, &Font10x16, COLOR_TEXT_MAIN, COLOR_PANEL_ALT);
    ST7789_DrawString(8u, 28u, chord_name, &Font8x12,  COLOR_ACTIVE,    COLOR_PANEL_ALT);
    ST7789_DrawString(132u, 8u, s_piano_roll.title, &Font8x12, COLOR_TEXT_MAIN, COLOR_PANEL_ALT);
    ST7789_DrawString(132u, 28u, slot_state, &Font8x12, slot_state_color, COLOR_PANEL_ALT);
    ST7789_FillRect(0u, (uint16_t)(ROLL_HEADER_H - 1u), SCREEN_W, 1u, ROLL_COL_ROW_SEP);
}

static void RollDrawFooter(void)
{
    ST7789_FillRect(0u, ROLL_FOOTER_Y, SCREEN_W, ROLL_FOOTER_H, COLOR_PANEL);
    ST7789_DrawString(6u,   (uint16_t)(ROLL_FOOTER_Y + 12u),
                      "PLAY SAVE", &Font10x16, COLOR_ACTIVE,    COLOR_PANEL);
    ST7789_DrawString(126u, (uint16_t)(ROLL_FOOTER_Y + 12u),
                      "REC BACK",  &Font10x16, COLOR_TEXT_MAIN, COLOR_PANEL);
}

static void RollDrawAllRows(void)
{
    for (uint8_t n = 0u; n < 12u; n++)
    {
        RollDrawRow(n);
    }
}

static void RollDrawSlotCursor(void)
{
    uint8_t slot_count = s_piano_roll.slot_count;
    if (slot_count < 1u) slot_count = 1u;
    if (slot_count > 16u) slot_count = 16u;

    if (s_piano_roll.slot_index >= slot_count) return;

    const uint16_t slot_w = (uint16_t)(ROLL_GRID_W / slot_count);
    const uint16_t sx = (uint16_t)(ROLL_GRID_X + (uint16_t)s_piano_roll.slot_index * slot_w);
    const uint16_t sw = (s_piano_roll.slot_index == (uint8_t)(slot_count - 1u)) ?
                        (uint16_t)(ROLL_GRID_W - (uint16_t)s_piano_roll.slot_index * slot_w) :
                        slot_w;
    const uint16_t y = ROLL_GRID_Y;
    const uint16_t h = (uint16_t)(ROLL_ROW_H * 12u);

    if (sw > 2u)
    {
        /* Full-height cursor column makes slot position movement unambiguous. */
        ST7789_FillRect(sx, y, 1u, h, ROLL_COL_SEL_BORDER);
        ST7789_FillRect((uint16_t)(sx + sw - 1u), y, 1u, h, ROLL_COL_SEL_BORDER);
        ST7789_FillRect(sx, y, sw, 1u, ROLL_COL_SEL_BORDER);
        ST7789_FillRect(sx, (uint16_t)(y + h - 1u), sw, 1u, ROLL_COL_SEL_BORDER);
    }
}

/* ── Screen Screens ────────────────────────────────────────────────────── */

static void DrawPianoKeyboardScreen(void)
{
    MenuTemplate_Begin(MENU_FRAME_CHORD);
    FillRegion(UI_REGION_MENU_LIST, COLOR_BG);

    ST7789_DrawString(4, 4, s_piano_roll.title, &Font12x20, COLOR_TEXT_MAIN, COLOR_PANEL_ALT);
    MenuTemplate_DrawHeader(s_piano_roll.title, k_note_names[s_piano_roll.selected_key], YELLOW);

    for (uint8_t i = 0; i < PIANO_WHITE_COUNT; i++)
    {
        DrawPianoWhiteKey(i, k_piano_white_keys[i]);
    }

    for (uint8_t i = 0; i < PIANO_BLACK_COUNT; i++)
    {
        DrawPianoBlackKey(i, k_piano_black_keys[i]);
    }

    FillRegion(UI_REGION_MENU_FOOTER, COLOR_PANEL);
    ST7789_DrawString(6, FOOTER_TEXT_Y1, "PLAY SAVE", &Font10x16, COLOR_ACTIVE, COLOR_PANEL);
    ST7789_DrawString(126, FOOTER_TEXT_Y1, "REC BACK", &Font10x16, COLOR_TEXT_MAIN, COLOR_PANEL);
}

static void DrawStepPianoRollScreen(void)
{
    uint8_t need_layout = (s_piano_roll.roll_layout_valid == 0u);
    uint8_t step_changed = (s_piano_roll.roll_last_step != s_piano_roll.step);
    uint8_t slot_count_changed = (s_piano_roll.roll_last_slot_count != s_piano_roll.slot_count);
    uint8_t slot_index_changed = (s_piano_roll.roll_last_slot_index != s_piano_roll.slot_index);
    uint8_t selected_changed = (s_piano_roll.roll_last_selected_key != s_piano_roll.selected_key);
    uint16_t changed_mask = (uint16_t)(s_piano_roll.roll_last_note_mask ^ s_piano_roll.note_mask);

    MenuTemplate_Begin(MENU_FRAME_CHORD);

    if (need_layout)
    {
        ST7789_FillRect(0u, ROLL_HEADER_H, SCREEN_W,
                        (uint16_t)(SCREEN_H - ROLL_HEADER_H), COLOR_BG);
        RollDrawHeader();
        RollDrawAllRows();
        RollDrawSlotCursor();
        RollDrawFooter();
        s_piano_roll.roll_layout_valid = 1u;
    }
    else
    {
        if (step_changed || slot_count_changed || changed_mask != 0u || slot_index_changed)
        {
            RollDrawHeader();
        }

        if (step_changed || slot_count_changed || slot_index_changed)
        {
            /* Slot guides change across all rows when slot geometry or position changes. */
            RollDrawAllRows();
        }
        else
        {
            if (selected_changed)
            {
                if (s_piano_roll.roll_last_selected_key < 12u)
                {
                    RollDrawRow(s_piano_roll.roll_last_selected_key);
                }
                RollDrawRow(s_piano_roll.selected_key);
            }

            if (changed_mask != 0u)
            {
                for (uint8_t n = 0u; n < 12u; ++n)
                {
                    if ((changed_mask & (uint16_t)(1u << n)) != 0u)
                    {
                        RollDrawRow(n);
                    }
                }
            }
        }

        /* Keep the full-height slot cursor visible after partial updates. */
        RollDrawSlotCursor();
    }

    s_piano_roll.roll_last_step         = s_piano_roll.step;
    s_piano_roll.roll_last_note_mask    = s_piano_roll.note_mask;
    s_piano_roll.roll_last_selected_key = s_piano_roll.selected_key;
    s_piano_roll.roll_last_slot_index   = s_piano_roll.slot_index;
    s_piano_roll.roll_last_slot_count   = s_piano_roll.slot_count;
}

/* ── UiScreen Lifecycle ─────────────────────────────────────────────────── */

static void OnEnter(void)
{
    s_piano_roll.needs_redraw = 1;
    if (s_piano_roll.view_mode == PIANO_VIEW_ROLL)
    {
        s_piano_roll.roll_layout_valid = 0;
    }
}

static void OnUpdate(void)
{
    /* Nothing to do on every frame update; drawing is event-driven */
}

static void OnInput(InputType input_type, int8_t value)
{
    /* Input routing is handled by parent UI; this screen is passive */
    (void)input_type;
    (void)value;
}

static void OnExit(ScreenExitReason reason)
{
    /* Clean up if needed */
    (void)reason;
}

static void OnDraw(void)
{
    if (!s_piano_roll.needs_redraw) return;

    if (s_piano_roll.view_mode == PIANO_VIEW_KEYBOARD)
    {
        DrawPianoKeyboardScreen();
    }
    else
    {
        DrawStepPianoRollScreen();
    }

    s_piano_roll.needs_redraw = 0;
}

/* ── UiScreen Instance ──────────────────────────────────────────────────– */

static UiScreen s_screen = {
    .name = "Piano Roll",
    .on_enter = OnEnter,
    .on_update = OnUpdate,
    .on_input = OnInput,
    .on_exit = OnExit,
    .on_draw = OnDraw,
    .owned_region = {0, 0, SCREEN_W, SCREEN_H},
    .is_active = 0,
    .is_dirty = 1
};

/* ── Public API ─────────────────────────────────────────────────────────– */

UiScreen* UI_PianoRollScreen_Get(void)
{
    return &s_screen;
}

void UI_PianoRollScreen_SetContext(const PianoRollContext* ctx)
{
    if (!ctx) return;

    s_piano_roll.roll_mode = ctx->mode;
    s_piano_roll.step = ctx->step;
    s_piano_roll.note_mask = ctx->note_mask;
    s_piano_roll.selected_key = (ctx->selected_key < 12) ? ctx->selected_key : 0;
    s_piano_roll.slot_index = ctx->slot_index;
    s_piano_roll.slot_count = ctx->slot_count;
    if (s_piano_roll.slot_count < 1u) s_piano_roll.slot_count = 1u;
    if (s_piano_roll.slot_count > 16u) s_piano_roll.slot_count = 16u;
    if (s_piano_roll.slot_index >= s_piano_roll.slot_count) s_piano_roll.slot_index = 0u;
    
    if (ctx->title)
    {
        strncpy(s_piano_roll.title, ctx->title, sizeof(s_piano_roll.title) - 1u);
        s_piano_roll.title[sizeof(s_piano_roll.title) - 1u] = '\0';
    }
    if (ctx->footer_label)
    {
        strncpy(s_piano_roll.footer_label, ctx->footer_label, sizeof(s_piano_roll.footer_label) - 1u);
        s_piano_roll.footer_label[sizeof(s_piano_roll.footer_label) - 1u] = '\0';
    }

    if (ctx->mode == PIANO_ROLL_MODE_KEYBOARD)
    {
        s_piano_roll.view_mode = PIANO_VIEW_KEYBOARD;
    }
    else
    {
        s_piano_roll.view_mode = PIANO_VIEW_ROLL;
    }

    s_piano_roll.needs_redraw = 1;
}

uint8_t UI_PianoRollScreen_GetSelectedKey(void)
{
    return s_piano_roll.selected_key;
}

uint16_t UI_PianoRollScreen_GetNoteMask(void)
{
    return s_piano_roll.note_mask;
}

void UI_PianoRollScreen_SetNoteMask(uint16_t mask)
{
    s_piano_roll.note_mask = mask;
    s_piano_roll.needs_redraw = 1;

    if (s_piano_roll.view_mode == PIANO_VIEW_KEYBOARD)
    {
        UiScreen* active = UI_ScreenRouter_GetActive();
        if (active == &s_screen && active->on_draw)
        {
            active->on_draw();
        }
    }
}

void UI_PianoRollScreen_NavigateKey(int8_t delta)
{
    if (delta == 0) return;

    const int8_t step = (delta > 0) ? 1 : -1;
    uint8_t old = s_piano_roll.selected_key;
    int16_t next = (int16_t)s_piano_roll.selected_key + step;
    while (next < 0)   next += 12;
    while (next >= 12) next -= 12;
    s_piano_roll.selected_key = (uint8_t)next;

    if (old == s_piano_roll.selected_key) return;

    if (s_piano_roll.view_mode == PIANO_VIEW_ROLL)
    {
        s_piano_roll.needs_redraw = 1u;

        {
            UiScreen* active = UI_ScreenRouter_GetActive();
            if (active == &s_screen && active->on_draw)
            {
                active->on_draw();
            }
        }
    }
    else
    {
        DrawPianoKeyByNote(old);
        DrawPianoKeyByNote(s_piano_roll.selected_key);
        DrawKeyboardIndicator(k_note_names[s_piano_roll.selected_key], YELLOW);
    }
}

void UI_PianoRollScreen_ToggleKey(uint8_t key)
{
    if (key < 12)
    {
        s_piano_roll.note_mask ^= (1U << key);
        
        if (s_piano_roll.view_mode == PIANO_VIEW_ROLL)
        {
            s_piano_roll.needs_redraw = 1u;
        }
        else
        {
            DrawPianoKeyByNote(key);
            char chord_name[20];
            Bridge_FindChordName(s_piano_roll.note_mask, chord_name, sizeof(chord_name));
            DrawKeyboardIndicator(chord_name, YELLOW);
        }
    }
}

PianoRollMode UI_PianoRollScreen_GetMode(void)
{
    return s_piano_roll.roll_mode;
}

void UI_PianoRollScreen_ForceRedraw(void)
{
    s_piano_roll.needs_redraw = 1;
}

void UI_PianoRollScreen_ResetCache(void)
{
    s_piano_roll.roll_layout_valid = 0;
    s_piano_roll.roll_last_step = 0xFF;
    s_piano_roll.roll_last_note_mask = 0;
    s_piano_roll.roll_last_selected_key = 0xFF;
    s_piano_roll.roll_last_slot_index = 0xFF;
    s_piano_roll.roll_last_slot_count = 0xFF;
    s_piano_roll.needs_redraw = 1;
}
