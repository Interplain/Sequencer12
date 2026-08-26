#include "ui_screen_piano_roll.h"
#include "ui_display.h"
#include "ui_screen_base.h"
#include "ui_screen_router.h"
#include "ui_regions.h"
#include "ui_render_scratch.h"
#include "sequencer_bridge.h"
#include "core/pitch_mapping.h"
#include "st7789.h"
#include "s12_fonts.h"
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
static uint8_t RollRowsThatFitBody(void);
static void RollDrawFooter(void);
static void RollDrawAllRows(void);
static uint8_t RollCellHasSpanNote(const uint8_t slots[32][4],
                                   const uint8_t spans[32],
                                   uint8_t slot_count,
                                   uint8_t slot,
                                   uint8_t midi_note,
                                   uint8_t* out_pending);
static uint8_t RollCellHasSpanNoteLast(const uint8_t slots[32][4],
                                       const uint8_t spans[32],
                                       uint8_t slot_count,
                                       uint8_t slot,
                                       uint8_t midi_note);
static uint8_t RollBuildSpanDirtyRows(uint8_t* out_rows);

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
    uint8_t selected_midi_note;
    uint8_t slot_index;
    uint8_t slot_count;
    uint8_t grid_division;
    uint8_t slot_notes[4];
    uint8_t cv_absolute_valid;
    uint8_t arp_on;
    uint8_t length_mode_active;
    uint8_t pending_length;
    char key_label[16];
    char scale_label[24];
    uint8_t clear_confirm_active;
    uint8_t clear_confirm_yes_selected;
    uint8_t bar_skipped;
    char confirm_message[24];
    uint8_t roll_top_midi_note;
    char title[32];
    char footer_label[24];
    
    /* Keyboard view state */
    char header_title[24];
    
    /* Step roll view state */
    uint8_t roll_layout_valid;
    uint8_t roll_last_step;
    uint16_t roll_last_note_mask;
    uint8_t roll_last_selected_midi;
    uint8_t roll_last_top_midi_note;
    uint8_t roll_last_slot_index;
    uint8_t roll_last_slot_count;
    uint8_t roll_last_grid_division;
    uint8_t roll_last_bar_skipped;
    uint8_t roll_last_cv_absolute_valid;
    uint8_t roll_last_arp_on;
    uint8_t roll_last_length_mode_active;
    uint8_t roll_last_pending_length;
    uint8_t roll_last_slot_notes[4];
    uint8_t roll_event_span[32];
    uint8_t roll_last_event_span[32];
    char roll_last_key_label[16];
    char roll_last_scale_label[24];
    uint8_t roll_slots[32][4];
    uint8_t roll_last_slots[32][4];
    uint8_t roll_prev_selected_midi;
    uint16_t roll_dirty_flags;
    
    /* Cache flags */
    uint8_t needs_redraw;
} s_piano_roll = {
    .view_mode = PIANO_VIEW_KEYBOARD,
    .roll_mode = PIANO_ROLL_MODE_KEYBOARD,
    .step = 1,
    .note_mask = 0,
    .selected_key = 0,
    .selected_midi_note = (uint8_t)kLegacyPitchClassFallbackMidiBase,
    .slot_index = 0,
    .slot_count = 1,
    .grid_division = 4,
    .slot_notes = {0xFFu, 0xFFu, 0xFFu, 0xFFu},
    .cv_absolute_valid = 0,
    .arp_on = 0,
    .length_mode_active = 0,
    .pending_length = 1,
    .key_label = "C",
    .scale_label = "MAJ",
    .clear_confirm_active = 0,
    .clear_confirm_yes_selected = 0,
    .bar_skipped = 0,
    .confirm_message = "CLEAR BAR?",
    .roll_top_midi_note = 72,
    .title = "Create Chord",
    .footer_label = "SAVE/DONE",
    .header_title = "Create Chord",
    .roll_layout_valid = 0,
    .roll_last_step = 0xFF,
    .roll_last_note_mask = 0,
    .roll_last_selected_midi = 0xFF,
    .roll_last_top_midi_note = 0xFF,
    .roll_last_slot_index = 0xFF,
    .roll_last_slot_count = 0xFF,
    .roll_last_grid_division = 0xFF,
    .roll_last_bar_skipped = 0xFF,
    .roll_last_cv_absolute_valid = 0xFF,
    .roll_last_arp_on = 0xFF,
    .roll_last_length_mode_active = 0xFF,
    .roll_last_pending_length = 0xFF,
    .roll_last_slot_notes = {0xFFu, 0xFFu, 0xFFu, 0xFFu},
    .roll_event_span = {0},
    .roll_last_event_span = {0},
    .roll_last_key_label = "",
    .roll_last_scale_label = "",
    .roll_slots = {{{0}}},
    .roll_last_slots = {{{0}}},
    .roll_prev_selected_midi = 0xFF,
    .roll_dirty_flags = 0xFFFFu,
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
#define ROLL_HEADER_H   54u
#define ROLL_FOOTER_H   32u
#define ROLL_FOOTER_Y   ((uint16_t)(SCREEN_H - ROLL_FOOTER_H))
#define ROLL_GRID_Y     ROLL_HEADER_H
#define ROLL_ROW_H      12u
#define ROLL_VISIBLE_ROWS 19u
#define ROLL_LABEL_W    44u
#define ROLL_GRID_X     ROLL_LABEL_W
#define ROLL_GRID_W     ((uint16_t)(SCREEN_W - ROLL_LABEL_W))

/* Header sub-rectangles for field-level redraw. */
#define HDR_BAR_X       4u
#define HDR_BAR_Y       4u
#define HDR_BAR_W       48u
#define HDR_BAR_H       12u
#define HDR_SKIP_X      56u
#define HDR_SKIP_Y      4u
#define HDR_SKIP_W      42u
#define HDR_SKIP_H      12u
#define HDR_POS_X       104u
#define HDR_POS_Y       4u
#define HDR_POS_W       64u
#define HDR_POS_H       12u
#define HDR_NOTE_X      168u
#define HDR_NOTE_Y      4u
#define HDR_NOTE_W      72u
#define HDR_NOTE_H      12u
#define HDR_KEYSCALE_X  4u
#define HDR_KEYSCALE_Y  20u
#define HDR_KEYSCALE_W  120u
#define HDR_KEYSCALE_H  12u
#define HDR_RNG_X       132u
#define HDR_RNG_Y       20u
#define HDR_RNG_W       108u
#define HDR_RNG_H       12u
#define HDR_CV_X        4u
#define HDR_CV_Y        36u
#define HDR_CV_W        236u
#define HDR_CV_H        12u

enum
{
    ROLL_DIRTY_NONE      = 0u,
    ROLL_DIRTY_BODY_FULL = (1u << 0),
    ROLL_DIRTY_ROW_OLD   = (1u << 1),
    ROLL_DIRTY_ROW_NEW   = (1u << 2),
    ROLL_DIRTY_SLOT_BODY = (1u << 3),
    ROLL_DIRTY_BAR       = (1u << 4),
    ROLL_DIRTY_SKIP      = (1u << 5),
    ROLL_DIRTY_POS       = (1u << 6),
    ROLL_DIRTY_NOTE      = (1u << 7),
    ROLL_DIRTY_KEYSCALE  = (1u << 8),
    ROLL_DIRTY_RNG       = (1u << 9),
    ROLL_DIRTY_CV        = (1u << 10),
    ROLL_DIRTY_FOOTER    = (1u << 11),
    ROLL_DIRTY_SPAN_ROWS = (1u << 12)
};

/* Roll colour palette */
#define ROLL_COL_WHITE_KEY_BG   WHITE
#define ROLL_COL_WHITE_KEY_FG   BLACK
#define ROLL_COL_BLACK_KEY_BG   COLOR8(30, 30, 30)
#define ROLL_COL_BLACK_KEY_FG   WHITE
#define ROLL_COL_LANE_WHITE     COLOR8(230, 228, 129)
#define ROLL_COL_LANE_BLACK     COLOR8(121, 224, 152)
#define ROLL_COL_SEL_BORDER     COLOR8(237, 141, 24)
#define ROLL_COL_NOTE_BLOCK     COLOR8(224, 80, 40)
#define ROLL_COL_NOTE_BLOCK_PENDING COLOR8(64, 132, 255)
#define ROLL_COL_ROW_SEP        COLOR8(50, 55, 60)
#define ROLL_COL_KEY_DIVIDER    COLOR8(80, 80, 80)

/* Temporary diagnostic toggles for low-level render-pipeline isolation. */
#ifndef PIANO_ROLL_SYNTHETIC_RENDER_TEST
#define PIANO_ROLL_SYNTHETIC_RENDER_TEST 0
#endif

#ifndef PIANO_ROLL_DIAG_REDRAW_FOOTER_AFTER_BODY
#define PIANO_ROLL_DIAG_REDRAW_FOOTER_AFTER_BODY 0
#endif

/* Compromise path when a full visible-body buffer does not fit SRAM. */
#ifndef ROLL_BODY_STRIP_ROWS
#define ROLL_BODY_STRIP_ROWS 2u
#endif

/* Dynamic OCT redraw threshold derived from transaction cost:
 * dynamic body tx ~= 1(label upload) + 2*dirty_cells (+ up to 2 row refreshes)
 * strip body tx = 7 (1 clear + 6 strip uploads for 12 visible rows)
 * Use dynamic path for sparse/moderate viewport shifts only, and force strip
 * fallback once changed-cell count enters dense territory.
 */
#define ROLL_OCT_DYNAMIC_CELL_THRESHOLD 24u

/* Note name tables */
static const uint8_t k_piano_white_keys[PIANO_WHITE_COUNT] = {0, 2, 4, 5, 7, 9, 11};
static const uint8_t k_piano_black_keys[PIANO_BLACK_COUNT] = {1, 3, 6, 8, 10};
static const uint8_t k_piano_black_after_white_idx[PIANO_BLACK_COUNT] = {0, 1, 3, 4, 5};
static const char* k_note_names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

#define ROLL_ROW_BYTES ((uint32_t)SCREEN_W * (uint32_t)ROLL_ROW_H * 2u)

/* Shared display scratch aliases (owned by active renderer via ui_render_scratch). */
static uint8_t* s_roll_row_buf = 0;
static uint8_t* s_roll_body_strip_buf = 0;

typedef struct {
    uint8_t row;
    uint8_t slot;
    uint8_t new_on;
} RollDirtyCell;

static RollDirtyCell s_roll_oct_dirty_cells[ROLL_VISIBLE_ROWS * 32u];

/* ── Drawing Helpers ────────────────────────────────────────────────────── */

static uint8_t RollIsBlack(uint8_t note)
{
    return (note == 1u || note == 3u || note == 6u || note == 8u || note == 10u) ? 1u : 0u;
}

static uint8_t RollPitchClass(uint8_t midi_note)
{
    return (uint8_t)(midi_note % 12u);
}

static void RollRowSetPixel(uint16_t x, uint16_t y, uint16_t color)
{
    if (!s_roll_row_buf) return;
    if (x >= SCREEN_W || y >= ROLL_ROW_H) return;
    const uint32_t idx = ((uint32_t)y * (uint32_t)SCREEN_W + (uint32_t)x) * 2u;
    s_roll_row_buf[idx] = (uint8_t)(color >> 8);
    s_roll_row_buf[idx + 1u] = (uint8_t)(color & 0xFFu);
}

static void RollRowFillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    if (!s_roll_row_buf) return;
    if (x >= SCREEN_W || y >= ROLL_ROW_H) return;
    if (w == 0u || h == 0u) return;
    if ((uint16_t)(x + w) > SCREEN_W) w = (uint16_t)(SCREEN_W - x);
    if ((uint16_t)(y + h) > ROLL_ROW_H) h = (uint16_t)(ROLL_ROW_H - y);

    const uint8_t hi = (uint8_t)(color >> 8);
    const uint8_t lo = (uint8_t)(color & 0xFFu);
    for (uint16_t yy = y; yy < (uint16_t)(y + h); ++yy)
    {
        uint32_t idx = ((uint32_t)yy * (uint32_t)SCREEN_W + (uint32_t)x) * 2u;
        for (uint16_t xx = 0u; xx < w; ++xx)
        {
            s_roll_row_buf[idx] = hi;
            s_roll_row_buf[idx + 1u] = lo;
            idx += 2u;
        }
    }
}

static void RollRowDrawVLine(uint16_t x, uint16_t y, uint16_t h, uint16_t color)
{
    RollRowFillRect(x, y, 1u, h, color);
}

static void RollRowDrawHLine(uint16_t x, uint16_t y, uint16_t w, uint16_t color)
{
    RollRowFillRect(x, y, w, 1u, color);
}

static void RollRowDrawChar(uint16_t x, uint16_t y, char c,
                            const Font_t* font,
                            uint16_t fg,
                            uint16_t bg,
                            uint8_t transparent_bg)
{
    if (!font) return;
    if (x >= SCREEN_W || y >= ROLL_ROW_H) return;

    uint8_t ch = (uint8_t)c;
    if (ch < font->first || ch > font->last)
    {
        ch = (uint8_t)' ';
    }

    const uint8_t bytes_per_row = (uint8_t)((font->width + 7u) / 8u);
    const uint8_t* char_data = font->data + ((uint32_t)(ch - font->first) * font->height * bytes_per_row);

    for (uint8_t row = 0u; row < font->height; ++row)
    {
        const uint16_t yy = (uint16_t)(y + row);
        if (yy >= ROLL_ROW_H) break;

        for (uint8_t col = 0u; col < font->width; ++col)
        {
            const uint16_t xx = (uint16_t)(x + col);
            if (xx >= SCREEN_W) break;

            const uint8_t byte_index = (uint8_t)(col / 8u);
            const uint8_t bit_mask = (uint8_t)(0x80u >> (col % 8u));
            const uint8_t pixel_on = (char_data[(uint16_t)row * bytes_per_row + byte_index] & bit_mask) ? 1u : 0u;

            if (pixel_on)
            {
                RollRowSetPixel(xx, yy, fg);
            }
            else if (!transparent_bg)
            {
                RollRowSetPixel(xx, yy, bg);
            }
        }
    }
}

static void RollRowDrawString(uint16_t x, uint16_t y, const char* text,
                              const Font_t* font,
                              uint16_t fg,
                              uint16_t bg,
                              uint8_t transparent_bg)
{
    if (!text || !font) return;
    uint16_t cx = x;
    while (*text)
    {
        if (cx >= SCREEN_W) break;
        RollRowDrawChar(cx, y, *text, font, fg, bg, transparent_bg);
        cx = (uint16_t)(cx + font->width);
        ++text;
    }
}

static void RollBuildMidiLabel(uint8_t midi_note, char* out, uint8_t out_len)
{
    if (!out || out_len < 2u) return;
    const int8_t octave = (int8_t)(midi_note / 12u) - 1;
    snprintf(out, out_len, "%s%d", k_note_names[RollPitchClass(midi_note)], (int)octave);
}

static uint8_t RollClampTopMidi(int16_t top)
{
    const uint8_t rows = RollRowsThatFitBody();
    const uint8_t span = (rows > 0u) ? (uint8_t)(rows - 1u) : 0u;
    const int16_t min_top = (int16_t)kPitchMidiMin + (int16_t)span;
    if (top < min_top) top = min_top;
    if (top > (int16_t)kPitchMidiMax) top = (int16_t)kPitchMidiMax;
    return (uint8_t)top;
}

static uint8_t RollRowsThatFitBody(void)
{
    const uint16_t body_h = (ROLL_FOOTER_Y > ROLL_GRID_Y) ? (uint16_t)(ROLL_FOOTER_Y - ROLL_GRID_Y) : 0u;
    uint8_t rows = (uint8_t)(body_h / ROLL_ROW_H);
    if (rows == 0u) rows = 1u;
    if (rows > ROLL_VISIBLE_ROWS) rows = ROLL_VISIBLE_ROWS;
    return rows;
}

static uint16_t RollLedgerBottomY(void)
{
    return (uint16_t)(ROLL_GRID_Y + (uint16_t)RollRowsThatFitBody() * ROLL_ROW_H);
}

static void RollGetSlotGeometry(uint8_t slot, uint8_t slot_count, uint16_t* out_x, uint16_t* out_w)
{
    uint16_t base_w;
    uint16_t rem;
    uint16_t sx;
    uint16_t sw;

    if (!out_x || !out_w) return;
    if (slot_count < 1u) slot_count = 1u;
    if (slot_count > 32u) slot_count = 32u;
    if (slot >= slot_count) slot = (uint8_t)(slot_count - 1u);

    base_w = (uint16_t)(ROLL_GRID_W / slot_count);
    rem = (uint16_t)(ROLL_GRID_W % slot_count);

    sx = (uint16_t)(ROLL_GRID_X + (uint16_t)slot * base_w + ((slot < rem) ? slot : rem));
    sw = (uint16_t)(base_w + ((slot < rem) ? 1u : 0u));

    *out_x = sx;
    *out_w = sw;
}

static uint8_t RollClampSelectedToViewport(uint8_t selected, uint8_t top)
{
    const uint8_t rows = RollRowsThatFitBody();
    const uint8_t span = (rows > 0u) ? (uint8_t)(rows - 1u) : 0u;
    const uint8_t unclamped_bottom = (top >= span) ? (uint8_t)(top - span) : (uint8_t)kPitchMidiMin;
    const uint8_t bottom = (unclamped_bottom < (uint8_t)kPitchMidiMin) ? (uint8_t)kPitchMidiMin : unclamped_bottom;
    if (selected < (uint8_t)kPitchMidiMin) selected = (uint8_t)kPitchMidiMin;
    if (selected > (uint8_t)kPitchMidiMax) selected = (uint8_t)kPitchMidiMax;
    if (selected > top) return top;
    if (selected < bottom) return bottom;
    return selected;
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
                      k_note_names[note], &Font12x20, text_col, fill);

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
        ST7789_DrawString(4u, 22u, indicator, &Font12x20, color, COLOR_PANEL_ALT);
    }
}

/* ── Step Piano Roll Drawing ───────────────────────────────────────────── */

static void RollComposeRow(uint8_t row)
{
    if (row >= RollRowsThatFitBody()) return;

    const uint8_t midi_note = (uint8_t)(s_piano_roll.roll_top_midi_note - row);
    const uint8_t pitch_class = RollPitchClass(midi_note);
    const uint8_t sel = (s_piano_roll.selected_midi_note == midi_note) ? 1u : 0u;
    const uint8_t black = RollIsBlack(pitch_class);
    char midi_label[8];

    RollBuildMidiLabel(midi_note, midi_label, sizeof(midi_label));

    /* Layer 1: base */
    RollRowFillRect(0u, 0u, ROLL_LABEL_W, ROLL_ROW_H, ROLL_COL_WHITE_KEY_BG);
    if (black)
    {
        const uint16_t kw = (uint16_t)(ROLL_LABEL_W - 14u);
        RollRowFillRect(2u, 0u, kw, ROLL_ROW_H, ROLL_COL_BLACK_KEY_BG);
    }
    RollRowFillRect((uint16_t)(ROLL_LABEL_W - 1u), 0u, 1u, ROLL_ROW_H, ROLL_COL_KEY_DIVIDER);

    {
        const uint16_t lane_bg = black ? ROLL_COL_LANE_BLACK : ROLL_COL_LANE_WHITE;
        RollRowFillRect(ROLL_GRID_X, 0u, ROLL_GRID_W, ROLL_ROW_H, lane_bg);
    }

    /* Layer 4 (row-local text): note label */
    if (black)
    {
        RollRowDrawString(4u, 1u, midi_label, &Font8x12,
                          ROLL_COL_BLACK_KEY_FG, ROLL_COL_BLACK_KEY_BG, 0u);
    }
    else
    {
        RollRowDrawString(2u, 1u, midi_label, &Font8x12,
                          ROLL_COL_WHITE_KEY_FG, ROLL_COL_WHITE_KEY_BG, 0u);
    }

    if (sel)
    {
        RollRowFillRect(0u, 0u, 2u, ROLL_ROW_H, ROLL_COL_SEL_BORDER);
        RollRowFillRect(ROLL_GRID_X, 0u, ROLL_GRID_W, 1u, ROLL_COL_SEL_BORDER);
        RollRowFillRect(ROLL_GRID_X, (uint16_t)(ROLL_ROW_H - 2u), ROLL_GRID_W, 1u, ROLL_COL_SEL_BORDER);
    }

    /* Layer 2 + 3: slot grid, notes, cursor */
    {
        uint8_t slot_count = s_piano_roll.slot_count;
        if (slot_count < 1u) slot_count = 1u;
        if (slot_count > 32u) slot_count = 32u;

        const uint16_t bh = (ROLL_ROW_H > 6u) ? (uint16_t)(ROLL_ROW_H - 6u) : (uint16_t)(ROLL_ROW_H - 2u);
        const uint16_t by = (uint16_t)((ROLL_ROW_H - bh) / 2u);

        for (uint8_t s = 0u; s < slot_count; ++s)
        {
            uint16_t sx = 0u;
            uint16_t sw = 1u;
            RollGetSlotGeometry(s, slot_count, &sx, &sw);

            uint8_t pending_span = 0u;
            const uint8_t note_present = RollCellHasSpanNote(s_piano_roll.roll_slots,
                                                             s_piano_roll.roll_event_span,
                                                             slot_count,
                                                             s,
                                                             midi_note,
                                                             &pending_span);

            if (note_present)
            {
                const uint16_t pad = (sw > 6u) ? 2u : 1u;
                const uint16_t fill_w = (sw > (uint16_t)(2u * pad)) ? (uint16_t)(sw - (2u * pad)) : 1u;
                const uint16_t fill_color = pending_span ? ROLL_COL_NOTE_BLOCK_PENDING : ROLL_COL_NOTE_BLOCK;
                RollRowFillRect((uint16_t)(sx + pad), by, fill_w, bh, fill_color);
            }

            if (s > 0u)
            {
                RollRowDrawVLine(sx, 0u, ROLL_ROW_H, ROLL_COL_ROW_SEP);
            }

            if (s == s_piano_roll.slot_index && sw > 2u)
            {
                RollRowFillRect(sx, 0u, sw, 1u, ROLL_COL_SEL_BORDER);
                RollRowFillRect(sx, (uint16_t)(ROLL_ROW_H - 2u), sw, 1u, ROLL_COL_SEL_BORDER);
                RollRowDrawVLine(sx, 0u, ROLL_ROW_H, ROLL_COL_SEL_BORDER);
                RollRowDrawVLine((uint16_t)(sx + sw - 1u), 0u, ROLL_ROW_H, ROLL_COL_SEL_BORDER);
            }
        }
    }

    /* Layer 1 bottom separator */
    RollRowDrawHLine(0u, (uint16_t)(ROLL_ROW_H - 1u), SCREEN_W, ROLL_COL_ROW_SEP);

}

static void RollDrawRow(uint8_t row)
{
    const uint16_t y = (uint16_t)(ROLL_GRID_Y + (uint16_t)row * ROLL_ROW_H);
    RollComposeRow(row);
    ST7789_DrawRGB565Buffer(0u, y, SCREEN_W, ROLL_ROW_H,
                            s_roll_row_buf,
                            ROLL_ROW_BYTES);
}

static void RollDrawBodyFullComposedStrips(void)
{
    if (!s_roll_body_strip_buf) return;
    const uint8_t rows = RollRowsThatFitBody();
    const uint32_t row_bytes = ROLL_ROW_BYTES;

    for (uint8_t start_row = 0u; start_row < rows; start_row = (uint8_t)(start_row + ROLL_BODY_STRIP_ROWS))
    {
        uint8_t chunk_rows = ROLL_BODY_STRIP_ROWS;
        if ((uint8_t)(start_row + chunk_rows) > rows)
        {
            chunk_rows = (uint8_t)(rows - start_row);
        }

        for (uint8_t i = 0u; i < chunk_rows; ++i)
        {
            const uint8_t row = (uint8_t)(start_row + i);
            s_roll_row_buf = &s_roll_body_strip_buf[(uint32_t)i * row_bytes];
            RollComposeRow(row);
        }

        {
            const uint16_t y = (uint16_t)(ROLL_GRID_Y + (uint16_t)start_row * ROLL_ROW_H);
            const uint16_t h = (uint16_t)((uint16_t)chunk_rows * ROLL_ROW_H);
            const uint32_t bytes = (uint32_t)SCREEN_W * (uint32_t)h * 2u;
            ST7789_DrawRGB565Buffer(0u, y, SCREEN_W, h, s_roll_body_strip_buf, bytes);
        }
    }
}

static uint8_t RollCellHasSpanNote(const uint8_t slots[32][4],
                                   const uint8_t spans[32],
                                   uint8_t slot_count,
                                   uint8_t slot,
                                   uint8_t midi_note,
                                   uint8_t* out_pending)
{
    if (out_pending) *out_pending = 0u;

    for (uint8_t start = 0u; start < slot_count; ++start)
    {
        const uint8_t span = spans[start];
        if (span == 0u) continue;
        if (slot < start) continue;
        if (slot >= (uint8_t)(start + span)) continue;

        for (uint8_t i = 0u; i < 4u; ++i)
        {
            if (slots[start][i] == midi_note)
            {
                if (out_pending &&
                    s_piano_roll.length_mode_active &&
                    start == s_piano_roll.slot_index)
                {
                    *out_pending = 1u;
                }
                return 1u;
            }
        }
    }

    return 0u;
}

static uint8_t RollCellHasSpanNoteLast(const uint8_t slots[32][4],
                                       const uint8_t spans[32],
                                       uint8_t slot_count,
                                       uint8_t slot,
                                       uint8_t midi_note)
{
    for (uint8_t start = 0u; start < slot_count; ++start)
    {
        const uint8_t span = spans[start];
        if (span == 0u) continue;
        if (slot < start) continue;
        if (slot >= (uint8_t)(start + span)) continue;

        for (uint8_t i = 0u; i < 4u; ++i)
        {
            if (slots[start][i] == midi_note)
            {
                return 1u;
            }
        }
    }

    return 0u;
}

static uint8_t RollBuildSpanDirtyRows(uint8_t out_rows[ROLL_VISIBLE_ROWS])
{
    uint8_t row_count = 0u;
    const uint8_t rows = RollRowsThatFitBody();
    uint8_t slot_count_now = s_piano_roll.slot_count;
    uint8_t slot_count_prev = s_piano_roll.roll_last_slot_count;

    if (!out_rows) return 0u;
    if (slot_count_now < 1u) slot_count_now = 1u;
    if (slot_count_now > 32u) slot_count_now = 32u;
    if (slot_count_prev < 1u || slot_count_prev > 32u) slot_count_prev = slot_count_now;

    for (uint8_t row = 0u; row < rows; ++row)
    {
        const uint8_t midi_note = (uint8_t)(s_piano_roll.roll_top_midi_note - row);
        uint8_t changed = 0u;

        for (uint8_t slot = 0u; slot < slot_count_now; ++slot)
        {
            uint8_t pending_now = 0u;
            const uint8_t now_on = RollCellHasSpanNote(s_piano_roll.roll_slots,
                                                       s_piano_roll.roll_event_span,
                                                       slot_count_now,
                                                       slot,
                                                       midi_note,
                                                       &pending_now);
            const uint8_t old_on = RollCellHasSpanNoteLast(s_piano_roll.roll_last_slots,
                                                           s_piano_roll.roll_last_event_span,
                                                           slot_count_prev,
                                                           slot,
                                                           midi_note);

            uint8_t old_pending = 0u;
            if (old_on &&
                s_piano_roll.roll_last_length_mode_active &&
                s_piano_roll.roll_last_slot_index < slot_count_prev &&
                slot >= s_piano_roll.roll_last_slot_index &&
                slot < (uint8_t)(s_piano_roll.roll_last_slot_index + s_piano_roll.roll_last_event_span[s_piano_roll.roll_last_slot_index]))
            {
                old_pending = 1u;
            }

            if (now_on != old_on || pending_now != old_pending)
            {
                changed = 1u;
                break;
            }
        }

        if (changed)
        {
            out_rows[row_count++] = row;
            if (row_count >= rows) break;
        }
    }

    return row_count;
}

static void RollDrawLabelOnlyRow(uint8_t row, uint8_t midi_note)
{
    const uint16_t y = (uint16_t)(ROLL_GRID_Y + (uint16_t)row * ROLL_ROW_H);
    const uint8_t pitch_class = RollPitchClass(midi_note);
    const uint8_t black = RollIsBlack(pitch_class);
    char midi_label[8];

    RollBuildMidiLabel(midi_note, midi_label, sizeof(midi_label));

    ST7789_FillRect(0u, y, ROLL_LABEL_W, ROLL_ROW_H, ROLL_COL_WHITE_KEY_BG);
    if (black)
    {
        const uint16_t kw = (uint16_t)(ROLL_LABEL_W - 14u);
        ST7789_FillRect(2u, y, kw, ROLL_ROW_H, ROLL_COL_BLACK_KEY_BG);
        ST7789_DrawString(4u, (uint16_t)(y + 1u), midi_label, &Font8x12,
                          ROLL_COL_BLACK_KEY_FG, ROLL_COL_BLACK_KEY_BG);
    }
    else
    {
        ST7789_DrawString(2u, (uint16_t)(y + 1u), midi_label, &Font8x12,
                          ROLL_COL_WHITE_KEY_FG, ROLL_COL_WHITE_KEY_BG);
    }

    ST7789_FillRect((uint16_t)(ROLL_LABEL_W - 1u), y, 1u, ROLL_ROW_H, ROLL_COL_KEY_DIVIDER);
    ST7789_FillRect(0u, (uint16_t)(y + ROLL_ROW_H - 1u), ROLL_LABEL_W, 1u, ROLL_COL_ROW_SEP);
}

static void RollDrawLabelBandForTop(uint8_t top_midi)
{
    const uint8_t rows = RollRowsThatFitBody();
    for (uint8_t row = 0u; row < rows; ++row)
    {
        const uint8_t midi_note = (uint8_t)(top_midi - row);
        RollDrawLabelOnlyRow(row, midi_note);
    }
}

static void RollDrawGridCellDynamic(uint8_t row, uint8_t slot, uint8_t note_on)
{
    uint8_t slot_count = s_piano_roll.slot_count;
    const uint8_t midi_note = (uint8_t)(s_piano_roll.roll_top_midi_note - row);
    const uint8_t pitch_class = RollPitchClass(midi_note);
    const uint8_t black = RollIsBlack(pitch_class);
    const uint16_t lane_bg = black ? ROLL_COL_LANE_BLACK : ROLL_COL_LANE_WHITE;
    const uint16_t y = (uint16_t)(ROLL_GRID_Y + (uint16_t)row * ROLL_ROW_H);

    if (slot_count < 1u) slot_count = 1u;
    if (slot_count > 32u) slot_count = 32u;
    if (slot >= slot_count) return;

    {
        uint16_t sx = 0u;
        uint16_t sw = 1u;
        const uint16_t bh = (ROLL_ROW_H > 6u) ? (uint16_t)(ROLL_ROW_H - 6u) : (uint16_t)(ROLL_ROW_H - 2u);
        const uint16_t by = (uint16_t)(y + ((ROLL_ROW_H - bh) / 2u));
        uint16_t pad;
        uint16_t fill_w;

        RollGetSlotGeometry(slot, slot_count, &sx, &sw);

        pad = (sw > 6u) ? 2u : 1u;
        fill_w = (sw > (uint16_t)(2u * pad)) ? (uint16_t)(sw - (2u * pad)) : 1u;

        /* Restore previous note/block region to lane background first. */
        ST7789_FillRect((uint16_t)(sx + pad), by, fill_w, bh, lane_bg);

        if (note_on)
        {
            ST7789_FillRect((uint16_t)(sx + pad), by, fill_w, bh, ROLL_COL_NOTE_BLOCK);
        }
    }
}

static uint8_t RollFindVisibleRowForMidiWithTop(uint8_t midi_note, uint8_t top_midi)
{
    const uint8_t rows = RollRowsThatFitBody();
    const uint8_t span = (rows > 0u) ? (uint8_t)(rows - 1u) : 0u;
    const uint8_t bottom = (top_midi >= span) ? (uint8_t)(top_midi - span) : 0u;
    if (midi_note > top_midi || midi_note < bottom) return 0xFFu;
    return (uint8_t)(top_midi - midi_note);
}

static uint16_t RollBuildOctDirtyCells(uint8_t old_top, uint8_t new_top)
{
    uint16_t count = 0u;
    uint8_t slot_count = s_piano_roll.slot_count;
    uint8_t prev_slot_count = s_piano_roll.roll_last_slot_count;
    const uint8_t rows = RollRowsThatFitBody();

    if (slot_count < 1u) slot_count = 1u;
    if (slot_count > 32u) slot_count = 32u;
    if (prev_slot_count < 1u || prev_slot_count > 32u) prev_slot_count = slot_count;

    for (uint8_t row = 0u; row < rows; ++row)
    {
        const uint8_t old_midi = (uint8_t)(old_top - row);
        const uint8_t new_midi = (uint8_t)(new_top - row);

        for (uint8_t slot = 0u; slot < slot_count; ++slot)
        {
            const uint8_t old_on = RollCellHasSpanNoteLast(s_piano_roll.roll_last_slots,
                                                           s_piano_roll.roll_last_event_span,
                                                           prev_slot_count,
                                                           slot,
                                                           old_midi);
            uint8_t pending_dummy = 0u;
            const uint8_t new_on = RollCellHasSpanNote(s_piano_roll.roll_slots,
                                                       s_piano_roll.roll_event_span,
                                                       slot_count,
                                                       slot,
                                                       new_midi,
                                                       &pending_dummy);
            if (old_on != new_on)
            {
                if (count < (uint16_t)(ROLL_VISIBLE_ROWS * 32u))
                {
                    s_roll_oct_dirty_cells[count].row = row;
                    s_roll_oct_dirty_cells[count].slot = slot;
                    s_roll_oct_dirty_cells[count].new_on = new_on;
                    ++count;
                }
            }
        }
    }

    return count;
}

static uint8_t RollCanUseDynamicOctPath(uint16_t dirty, uint8_t need_layout)
{
    if (need_layout) return 0u;
    if ((dirty & ROLL_DIRTY_BODY_FULL) == 0u) return 0u;
    if (s_piano_roll.roll_last_step != s_piano_roll.step) return 0u;
    if (s_piano_roll.roll_last_slot_count != s_piano_roll.slot_count) return 0u;

    {
        const int16_t dt = (int16_t)s_piano_roll.roll_top_midi_note - (int16_t)s_piano_roll.roll_last_top_midi_note;
        if (dt == 0) return 0u;
        if (dt > 12 || dt < -12) return 0u;
    }

    return 1u;
}

static uint8_t RollDrawOctHybridDynamic(uint16_t* io_dirty)
{
    uint16_t dirty = *io_dirty;
    const uint8_t old_top = s_piano_roll.roll_last_top_midi_note;
    const uint8_t new_top = s_piano_roll.roll_top_midi_note;
    const int16_t dt = (int16_t)new_top - (int16_t)old_top;
    const uint16_t changed_cells = RollBuildOctDirtyCells(old_top, new_top);

    /* For partial clamped edge shifts (|dt| < 12), force dynamic redraw to
     * avoid BODY_FULL clear/strip fallback that causes visible zig-zag at
     * MIDI range limits. Keep threshold fallback for full ±12 shifts.
     */
    if ((dt == 12 || dt == -12) && changed_cells > ROLL_OCT_DYNAMIC_CELL_THRESHOLD)
    {
        return 0u;
    }

    /* At clamped MIDI bounds, OCT +/- can become a partial shift (< 12 semitones).
     * In that case the black/white row pattern itself changes, so dirty-cell-only
     * repaint is insufficient. Redraw the visible rows in place without using the
     * clear/strip compositor, preserving the footer and OCT dynamic path behavior. */
    if (dt != 12 && dt != -12)
    {
        RollDrawAllRows();
        dirty |= (ROLL_DIRTY_NOTE | ROLL_DIRTY_RNG);
        dirty &= (uint16_t)~ROLL_DIRTY_BODY_FULL;
        *io_dirty = dirty;
        return 1u;
    }

    /* 1) Always redraw left note-label band for the new visible octave rows. */
    RollDrawLabelBandForTop(new_top);

    /* 2) Dirty cell updates only (restore old/background + draw new block). */
    for (uint16_t i = 0u; i < changed_cells; ++i)
    {
        RollDrawGridCellDynamic(s_roll_oct_dirty_cells[i].row,
                                s_roll_oct_dirty_cells[i].slot,
                                s_roll_oct_dirty_cells[i].new_on);
    }

    /* 3) Redraw selection/cursor visuals as a separate pass. */
    {
        const uint8_t old_row = RollFindVisibleRowForMidiWithTop(s_piano_roll.roll_last_selected_midi, old_top);
        const uint8_t new_row = RollFindVisibleRowForMidiWithTop(s_piano_roll.selected_midi_note, new_top);
        if (old_row != 0xFFu)
        {
            RollDrawRow(old_row);
        }
        if (new_row != 0xFFu && new_row != old_row)
        {
            RollDrawRow(new_row);
        }
    }

    /* 4) Ensure NOTE and RNG header fields are updated for octave movement. */
    dirty |= (ROLL_DIRTY_NOTE | ROLL_DIRTY_RNG);

    /* Do not run BODY_FULL clear/strip compositor on dynamic OCT path. */
    dirty &= (uint16_t)~ROLL_DIRTY_BODY_FULL;
    *io_dirty = dirty;
    return 1u;
}

static void RollHeaderClearRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    ST7789_FillRect(x, y, w, h, COLOR_PANEL_ALT);
}

static const char* RollGridDivisionLabel(uint8_t grid_division)
{
    switch (grid_division)
    {
        case 1u: return "1/4";
        case 2u: return "1/8";
        case 4u: return "1/16";
        case 8u: return "1/32";
        default: return "1/16";
    }
}

static void RollBuildCvLine(char* out, uint8_t out_len)
{
    char cv1[8] = "--";
    char cv2[8] = "--";
    char cv3[8] = "--";
    char cv4[8] = "--";

    if (!out || out_len == 0u) return;

    if (!s_piano_roll.arp_on && s_piano_roll.cv_absolute_valid)
    {
        if (s_piano_roll.slot_notes[0] <= 127u) RollBuildMidiLabel(s_piano_roll.slot_notes[0], cv1, sizeof(cv1));
        if (s_piano_roll.slot_notes[1] <= 127u) RollBuildMidiLabel(s_piano_roll.slot_notes[1], cv2, sizeof(cv2));
        if (s_piano_roll.slot_notes[2] <= 127u) RollBuildMidiLabel(s_piano_roll.slot_notes[2], cv3, sizeof(cv3));
        if (s_piano_roll.slot_notes[3] <= 127u) RollBuildMidiLabel(s_piano_roll.slot_notes[3], cv4, sizeof(cv4));
        snprintf(out, out_len, "CV1:%s CV2:%s CV3:%s CV4:%s", cv1, cv2, cv3, cv4);
        return;
    }

    snprintf(out, out_len, "CV1:ARP CV2:-- CV3:-- CV4:--");
}

static void RollDrawHeaderFields(uint16_t dirty)
{
    char line[96];

    if ((dirty & ROLL_DIRTY_BAR) != 0u)
    {
        RollHeaderClearRect(HDR_BAR_X, HDR_BAR_Y, HDR_BAR_W, HDR_BAR_H);
        snprintf(line, sizeof(line), "BAR%02u", (unsigned)s_piano_roll.step);
        ST7789_DrawString(HDR_BAR_X, HDR_BAR_Y, line, &Font8x12, COLOR_TEXT_MAIN, COLOR_PANEL_ALT);
    }

    if ((dirty & ROLL_DIRTY_SKIP) != 0u)
    {
        RollHeaderClearRect(HDR_SKIP_X, HDR_SKIP_Y, HDR_SKIP_W, HDR_SKIP_H);
        if (s_piano_roll.bar_skipped)
        {
            ST7789_DrawString(HDR_SKIP_X, HDR_SKIP_Y, "SKIP", &Font8x12, COLOR_TEXT_MAIN, COLOR_PANEL_ALT);
        }
    }

    if ((dirty & ROLL_DIRTY_POS) != 0u)
    {
        RollHeaderClearRect(HDR_POS_X, HDR_POS_Y, HDR_POS_W, HDR_POS_H);
        snprintf(line, sizeof(line), "POS:%02u/%02u",
                 (unsigned)(s_piano_roll.slot_index + 1u),
                 (unsigned)s_piano_roll.slot_count);
        ST7789_DrawString(HDR_POS_X, HDR_POS_Y, line, &Font8x12, COLOR_TEXT_MAIN, COLOR_PANEL_ALT);
    }

    if ((dirty & ROLL_DIRTY_NOTE) != 0u)
    {
        char note_label[8];
        RollBuildMidiLabel(s_piano_roll.selected_midi_note, note_label, sizeof(note_label));
        RollHeaderClearRect(HDR_NOTE_X, HDR_NOTE_Y, HDR_NOTE_W, HDR_NOTE_H);
        snprintf(line, sizeof(line), "NOTE:%s", note_label);
        ST7789_DrawString(HDR_NOTE_X, HDR_NOTE_Y, line, &Font8x12, COLOR_TEXT_MAIN, COLOR_PANEL_ALT);
    }

    if ((dirty & ROLL_DIRTY_KEYSCALE) != 0u)
    {
        RollHeaderClearRect(HDR_KEYSCALE_X, HDR_KEYSCALE_Y, HDR_KEYSCALE_W, HDR_KEYSCALE_H);
        snprintf(line, sizeof(line), "GRID %s", RollGridDivisionLabel(s_piano_roll.grid_division));
        ST7789_DrawString(HDR_KEYSCALE_X, HDR_KEYSCALE_Y, line, &Font8x12, COLOR_TEXT_MAIN, COLOR_PANEL_ALT);
    }

    if ((dirty & ROLL_DIRTY_RNG) != 0u)
    {
        char bottom_label[8];
        char top_label[8];
        const uint8_t top_note = s_piano_roll.roll_top_midi_note;
        const uint8_t rows = RollRowsThatFitBody();
        const uint8_t span = (rows > 0u) ? (uint8_t)(rows - 1u) : 0u;
        const uint8_t bottom_note = (top_note >= span) ? (uint8_t)(top_note - span) : 0u;
        RollBuildMidiLabel(bottom_note, bottom_label, sizeof(bottom_label));
        RollBuildMidiLabel(top_note, top_label, sizeof(top_label));

        RollHeaderClearRect(HDR_RNG_X, HDR_RNG_Y, HDR_RNG_W, HDR_RNG_H);
        snprintf(line, sizeof(line), "RNG:%s..%s", bottom_label, top_label);
        ST7789_DrawString(HDR_RNG_X, HDR_RNG_Y, line, &Font8x12, COLOR_TEXT_MAIN, COLOR_PANEL_ALT);
    }

    if ((dirty & ROLL_DIRTY_CV) != 0u)
    {
        RollHeaderClearRect(HDR_CV_X, HDR_CV_Y, HDR_CV_W, HDR_CV_H);
        RollBuildCvLine(line, sizeof(line));
        ST7789_DrawString(HDR_CV_X, HDR_CV_Y, line, &Font8x12, COLOR_ACTIVE, COLOR_PANEL_ALT);
    }
}

static void RollDrawFooter(void)
{
    const uint16_t footer_y = RollLedgerBottomY();
    const uint16_t footer_h = (footer_y < SCREEN_H) ? (uint16_t)(SCREEN_H - footer_y) : 1u;
    const uint16_t line1_y = (uint16_t)(footer_y + 2u);
    const uint16_t line2_y = (uint16_t)(footer_y + 16u);

    ST7789_FillRect(0u, footer_y, SCREEN_W, footer_h, COLOR_PANEL);
    ST7789_DrawString(6u, line1_y,
                      "M5 SKIP M6 -- M7 -- M8 --", &Font8x12, COLOR_ACTIVE, COLOR_PANEL);
    ST7789_DrawString(6u, line2_y,
                      "M9 NOTE M10 CLR M11 OCT+ M12 OCT-", &Font8x12, COLOR_TEXT_MAIN, COLOR_PANEL);
}

static void RollDrawClearConfirmOverlay(void)
{
    if (!s_piano_roll.clear_confirm_active) return;

    const uint16_t box_w = 176u;
    const uint16_t box_h = 86u;
    const uint16_t box_x = (uint16_t)((SCREEN_W - box_w) / 2u);
    const uint16_t box_y = (uint16_t)((SCREEN_H - box_h) / 2u);
    const uint16_t yes_bg = s_piano_roll.clear_confirm_yes_selected ? COLOR_ACTIVE : COLOR_PANEL;
    const uint16_t no_bg = s_piano_roll.clear_confirm_yes_selected ? COLOR_PANEL : COLOR_ACTIVE;
    const uint16_t yes_fg = s_piano_roll.clear_confirm_yes_selected ? WHITE : COLOR_TEXT_MAIN;
    const uint16_t no_fg = s_piano_roll.clear_confirm_yes_selected ? COLOR_TEXT_MAIN : WHITE;

    ST7789_FillRect(box_x, box_y, box_w, box_h, COLOR_PANEL_ALT);
    ST7789_FillRect((uint16_t)(box_x + 1u), (uint16_t)(box_y + 1u), (uint16_t)(box_w - 2u), 1u, WHITE);
    ST7789_FillRect((uint16_t)(box_x + 1u), (uint16_t)(box_y + box_h - 2u), (uint16_t)(box_w - 2u), 1u, WHITE);
    ST7789_FillRect((uint16_t)(box_x + 1u), (uint16_t)(box_y + 1u), 1u, (uint16_t)(box_h - 2u), WHITE);
    ST7789_FillRect((uint16_t)(box_x + box_w - 2u), (uint16_t)(box_y + 1u), 1u, (uint16_t)(box_h - 2u), WHITE);

    ST7789_DrawString((uint16_t)(box_x + 12u), (uint16_t)(box_y + 14u),
                      s_piano_roll.confirm_message, &Font12x20, COLOR_TEXT_MAIN, COLOR_PANEL_ALT);

    ST7789_FillRect((uint16_t)(box_x + 20u), (uint16_t)(box_y + 50u), 58u, 24u, no_bg);
    ST7789_FillRect((uint16_t)(box_x + 98u), (uint16_t)(box_y + 50u), 58u, 24u, yes_bg);
    ST7789_DrawString((uint16_t)(box_x + 34u), (uint16_t)(box_y + 56u), "NO", &Font8x12, no_fg, no_bg);
    ST7789_DrawString((uint16_t)(box_x + 112u), (uint16_t)(box_y + 56u), "YES", &Font8x12, yes_fg, yes_bg);
}

static void RollDrawAllRows(void)
{
    if (!s_roll_row_buf) return;
    const uint8_t rows = RollRowsThatFitBody();
    for (uint8_t n = 0u; n < rows; n++)
    {
        RollDrawRow(n);
    }
}

static int8_t RollFindVisibleRowForMidi(uint8_t midi_note)
{
    const uint8_t rows = RollRowsThatFitBody();
    const uint8_t span = (rows > 0u) ? (uint8_t)(rows - 1u) : 0u;
    const uint8_t top = s_piano_roll.roll_top_midi_note;
    const uint8_t bottom = (top >= span) ? (uint8_t)(top - span) : 0u;
    if (midi_note > top || midi_note < bottom)
    {
        return -1;
    }
    return (int8_t)(top - midi_note);
}

#if PIANO_ROLL_SYNTHETIC_RENDER_TEST
static void RollBuildSolidRow(uint16_t color)
{
    if (!s_roll_row_buf) return;
    const uint8_t hi = (uint8_t)(color >> 8);
    const uint8_t lo = (uint8_t)(color & 0xFFu);
    for (uint32_t i = 0u; i < ROLL_ROW_BYTES; i += 2u)
    {
        s_roll_row_buf[i] = hi;
        s_roll_row_buf[i + 1u] = lo;
    }
}

static void RollRunSyntheticRenderDiagnostics(void)
{
    static const uint16_t k_diag_rows[] = {54u, 66u, 270u, 276u};

    ST7789_DebugSetPianoEvent(ST7789_PIANO_EVENT_SYNTHETIC);

    ST7789_FillRect(0u, 0u, SCREEN_W, ROLL_GRID_Y, RGB565(0u, 40u, 0u));
    ST7789_FillRect(0u, ROLL_GRID_Y, SCREEN_W,
                    (uint16_t)(ROLL_FOOTER_Y - ROLL_GRID_Y), RGB565(0u, 0u, 42u));
    ST7789_FillRect(0u, ROLL_FOOTER_Y, SCREEN_W, ROLL_FOOTER_H, RGB565(30u, 8u, 0u));

    for (uint8_t i = 0u; i < (uint8_t)(sizeof(k_diag_rows) / sizeof(k_diag_rows[0])); ++i)
    {
        const uint16_t y = k_diag_rows[i];
        RollBuildSolidRow((i & 1u) ? RGB565(0u, 63u, 0u) : RGB565(31u, 0u, 0u));
        ST7789_DrawRGB565Buffer(0u, y, SCREEN_W, ROLL_ROW_H,
                                s_roll_row_buf,
                                ROLL_ROW_BYTES);
    }

    for (uint8_t row = 0u; row < ROLL_VISIBLE_ROWS; ++row)
    {
        const uint16_t y = (uint16_t)(ROLL_GRID_Y + (uint16_t)row * ROLL_ROW_H);
        const uint16_t color = (row & 1u) ? RGB565(0u, 24u, 31u) : RGB565(31u, 24u, 0u);
        RollBuildSolidRow(color);
        ST7789_DrawRGB565Buffer(0u, y, SCREEN_W, ROLL_ROW_H,
                                s_roll_row_buf,
                                ROLL_ROW_BYTES);
    }

    /* Explicit footer redraw probe to detect downstream corruption. */
    RollDrawFooter();
}
#endif

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
    ST7789_DrawString(6, FOOTER_TEXT_Y1, "PLAY SAVE", &Font12x20, COLOR_ACTIVE, COLOR_PANEL);
    ST7789_DrawString(126, FOOTER_TEXT_Y1, "REC BACK", &Font12x20, COLOR_TEXT_MAIN, COLOR_PANEL);
}

static void DrawStepPianoRollScreen(void)
{
    uint8_t* scratch = UI_RenderScratch_Acquire(UI_RENDER_SCRATCH_OWNER_PIANO_ROLL);
    if (!scratch) return;

    s_roll_body_strip_buf = scratch;
    s_roll_row_buf = scratch;

    uint16_t dirty = s_piano_roll.roll_dirty_flags;
    const uint8_t need_layout = (s_piano_roll.roll_layout_valid == 0u);
#if PIANO_ROLL_DIAG_REDRAW_FOOTER_AFTER_BODY
    uint8_t body_drawn = 0u;
#endif

    ST7789_DebugBeginPianoRenderFrame("step-roll");

    MenuTemplate_Begin(MENU_FRAME_CHORD);

#if PIANO_ROLL_SYNTHETIC_RENDER_TEST
    RollRunSyntheticRenderDiagnostics();
    s_piano_roll.roll_layout_valid = 1u;
    s_piano_roll.roll_dirty_flags = ROLL_DIRTY_NONE;
    s_piano_roll.needs_redraw = 0u;
    ST7789_DebugEndPianoRenderFrame();
    s_roll_row_buf = 0;
    s_roll_body_strip_buf = 0;
    UI_RenderScratch_Release(UI_RENDER_SCRATCH_OWNER_PIANO_ROLL);
    return;
#endif

    if (need_layout)
    {
        ST7789_FillRect(0u, 0u, SCREEN_W, ROLL_HEADER_H, COLOR_PANEL_ALT);
        ST7789_FillRect(0u, (uint16_t)(ROLL_HEADER_H - 1u), SCREEN_W, 1u, ROLL_COL_ROW_SEP);
        dirty |= (ROLL_DIRTY_BODY_FULL |
                  ROLL_DIRTY_BAR |
                  ROLL_DIRTY_SKIP |
                  ROLL_DIRTY_POS |
                  ROLL_DIRTY_NOTE |
                  ROLL_DIRTY_KEYSCALE |
                  ROLL_DIRTY_RNG |
                  ROLL_DIRTY_CV |
                  ROLL_DIRTY_FOOTER);
        s_piano_roll.roll_layout_valid = 1u;
    }

    if (RollCanUseDynamicOctPath(dirty, need_layout))
    {
        (void)RollDrawOctHybridDynamic(&dirty);
    }

    if ((dirty & ROLL_DIRTY_BODY_FULL) != 0u)
    {
        ST7789_FillRect(0u, ROLL_GRID_Y, SCREEN_W,
                        (uint16_t)(ROLL_FOOTER_Y - ROLL_GRID_Y), COLOR_BG);
        RollDrawBodyFullComposedStrips();
#if PIANO_ROLL_DIAG_REDRAW_FOOTER_AFTER_BODY
        body_drawn = 1u;
#endif
    }
    else
    {
        if ((dirty & ROLL_DIRTY_SLOT_BODY) != 0u)
        {
            RollDrawAllRows();
#if PIANO_ROLL_DIAG_REDRAW_FOOTER_AFTER_BODY
            body_drawn = 1u;
#endif
        }
        else if ((dirty & ROLL_DIRTY_SPAN_ROWS) != 0u)
        {
            uint8_t rows[ROLL_VISIBLE_ROWS];
            const uint8_t count = RollBuildSpanDirtyRows(rows);
            for (uint8_t i = 0u; i < count; ++i)
            {
                RollDrawRow(rows[i]);
            }
        }
        else
        {
            if ((dirty & ROLL_DIRTY_ROW_OLD) != 0u)
            {
                const int8_t old_row = RollFindVisibleRowForMidi(s_piano_roll.roll_prev_selected_midi);
                if (old_row >= 0)
                {
                    RollDrawRow((uint8_t)old_row);
#if PIANO_ROLL_DIAG_REDRAW_FOOTER_AFTER_BODY
                    body_drawn = 1u;
#endif
                }
            }
            if ((dirty & ROLL_DIRTY_ROW_NEW) != 0u)
            {
                const int8_t new_row = RollFindVisibleRowForMidi(s_piano_roll.selected_midi_note);
                if (new_row >= 0)
                {
                    RollDrawRow((uint8_t)new_row);
#if PIANO_ROLL_DIAG_REDRAW_FOOTER_AFTER_BODY
                    body_drawn = 1u;
#endif
                }
            }
        }
    }

#if PIANO_ROLL_DIAG_REDRAW_FOOTER_AFTER_BODY
    if (body_drawn)
    {
        RollDrawFooter();
    }
#endif

    if ((dirty & (ROLL_DIRTY_BAR |
                  ROLL_DIRTY_SKIP |
                  ROLL_DIRTY_POS |
                  ROLL_DIRTY_NOTE |
                  ROLL_DIRTY_KEYSCALE |
                  ROLL_DIRTY_RNG |
                  ROLL_DIRTY_CV)) != 0u)
    {
        RollDrawHeaderFields(dirty);
    }

    if ((dirty & ROLL_DIRTY_FOOTER) != 0u)
    {
        RollDrawFooter();
    }

    s_piano_roll.roll_last_step = s_piano_roll.step;
    s_piano_roll.roll_last_note_mask = s_piano_roll.note_mask;
    s_piano_roll.roll_last_selected_midi = s_piano_roll.selected_midi_note;
    s_piano_roll.roll_last_top_midi_note = s_piano_roll.roll_top_midi_note;
    s_piano_roll.roll_last_slot_index = s_piano_roll.slot_index;
    s_piano_roll.roll_last_slot_count = s_piano_roll.slot_count;
    s_piano_roll.roll_last_grid_division = s_piano_roll.grid_division;
    s_piano_roll.roll_last_bar_skipped = s_piano_roll.bar_skipped;
    s_piano_roll.roll_last_cv_absolute_valid = s_piano_roll.cv_absolute_valid;
    s_piano_roll.roll_last_arp_on = s_piano_roll.arp_on;
    s_piano_roll.roll_last_length_mode_active = s_piano_roll.length_mode_active;
    s_piano_roll.roll_last_pending_length = s_piano_roll.pending_length;
    for (uint8_t i = 0u; i < 4u; ++i)
    {
        s_piano_roll.roll_last_slot_notes[i] = s_piano_roll.slot_notes[i];
    }
    strncpy(s_piano_roll.roll_last_key_label, s_piano_roll.key_label, sizeof(s_piano_roll.roll_last_key_label) - 1u);
    s_piano_roll.roll_last_key_label[sizeof(s_piano_roll.roll_last_key_label) - 1u] = '\0';
    strncpy(s_piano_roll.roll_last_scale_label, s_piano_roll.scale_label, sizeof(s_piano_roll.roll_last_scale_label) - 1u);
    s_piano_roll.roll_last_scale_label[sizeof(s_piano_roll.roll_last_scale_label) - 1u] = '\0';
    for (uint8_t s = 0u; s < 32u; ++s)
    {
        s_piano_roll.roll_last_event_span[s] = s_piano_roll.roll_event_span[s];
        for (uint8_t i = 0u; i < 4u; ++i)
        {
            s_piano_roll.roll_last_slots[s][i] = s_piano_roll.roll_slots[s][i];
        }
    }

    s_piano_roll.roll_dirty_flags = ROLL_DIRTY_NONE;

    RollDrawClearConfirmOverlay();
    ST7789_DebugEndPianoRenderFrame();

    s_roll_row_buf = 0;
    s_roll_body_strip_buf = 0;
    UI_RenderScratch_Release(UI_RENDER_SCRATCH_OWNER_PIANO_ROLL);
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

    const PianoViewMode prev_view = s_piano_roll.view_mode;
    const uint8_t prev_step = s_piano_roll.step;
    const uint8_t prev_selected = s_piano_roll.selected_midi_note;
    const uint8_t prev_top = s_piano_roll.roll_top_midi_note;

    s_piano_roll.roll_mode = ctx->mode;
    s_piano_roll.step = ctx->step;
    s_piano_roll.note_mask = ctx->note_mask;
    s_piano_roll.selected_key = (ctx->selected_key < 12) ? ctx->selected_key : 0;
    s_piano_roll.selected_midi_note = (ctx->selected_midi_note <= 127u) ? ctx->selected_midi_note : (uint8_t)kLegacyPitchClassFallbackMidiBase;
    s_piano_roll.roll_top_midi_note = RollClampTopMidi((int16_t)ctx->top_visible_midi);
    s_piano_roll.selected_midi_note = RollClampSelectedToViewport(s_piano_roll.selected_midi_note,
                                                                  s_piano_roll.roll_top_midi_note);
    s_piano_roll.slot_index = ctx->slot_index;
    s_piano_roll.slot_count = ctx->slot_count;
    s_piano_roll.grid_division = ctx->grid_division;
    if (s_piano_roll.slot_count < 1u) s_piano_roll.slot_count = 1u;
    if (s_piano_roll.slot_count > 32u) s_piano_roll.slot_count = 32u;
    if (s_piano_roll.slot_index >= s_piano_roll.slot_count) s_piano_roll.slot_index = 0u;

    for (uint8_t i = 0u; i < 4u; ++i)
    {
        s_piano_roll.slot_notes[i] = ctx->slot_notes[i];
    }
    s_piano_roll.cv_absolute_valid = ctx->cv_absolute_valid ? 1u : 0u;
    s_piano_roll.arp_on = ctx->arp_on ? 1u : 0u;
    s_piano_roll.length_mode_active = ctx->length_mode_active ? 1u : 0u;
    s_piano_roll.pending_length = (ctx->pending_length < 1u) ? 1u : ctx->pending_length;

    if (ctx->key_label)
    {
        strncpy(s_piano_roll.key_label, ctx->key_label, sizeof(s_piano_roll.key_label) - 1u);
        s_piano_roll.key_label[sizeof(s_piano_roll.key_label) - 1u] = '\0';
    }
    if (ctx->scale_label)
    {
        strncpy(s_piano_roll.scale_label, ctx->scale_label, sizeof(s_piano_roll.scale_label) - 1u);
        s_piano_roll.scale_label[sizeof(s_piano_roll.scale_label) - 1u] = '\0';
    }

    s_piano_roll.clear_confirm_active = ctx->clear_confirm_active;
    s_piano_roll.clear_confirm_yes_selected = ctx->clear_confirm_yes_selected ? 1u : 0u;
    s_piano_roll.bar_skipped = ctx->bar_skipped ? 1u : 0u;
    if (ctx->confirm_message)
    {
        strncpy(s_piano_roll.confirm_message, ctx->confirm_message, sizeof(s_piano_roll.confirm_message) - 1u);
        s_piano_roll.confirm_message[sizeof(s_piano_roll.confirm_message) - 1u] = '\0';
    }
    
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
        s_piano_roll.roll_dirty_flags |= ROLL_DIRTY_NONE;
    }
    else
    {
        uint16_t dirty = ROLL_DIRTY_NONE;
        uint8_t changed_slots = 0u;
        uint8_t changed_current_slot_only = 1u;
        uint8_t changed_spans = 0u;

        s_piano_roll.view_mode = PIANO_VIEW_ROLL;
        for (uint8_t s = 0u; s < 32u; ++s)
        {
            for (uint8_t i = 0u; i < 4u; ++i)
            {
                s_piano_roll.roll_slots[s][i] = 0xFFu;
            }
            s_piano_roll.roll_event_span[s] = 0u;

            if (s < s_piano_roll.slot_count && s_piano_roll.step >= 1u)
            {
                const BridgeLedgerSlot slot = Bridge_GetStepLedgerSlotMidi((uint8_t)(s_piano_roll.step - 1u), s);
                for (uint8_t i = 0u; i < 4u; ++i)
                {
                    s_piano_roll.roll_slots[s][i] = slot.notes[i];
                }

                uint8_t span_len = Bridge_GetStepEventLength((uint8_t)(s_piano_roll.step - 1u), s);
                if (s_piano_roll.length_mode_active && s == s_piano_roll.slot_index && span_len > 0u)
                {
                    uint8_t pending = s_piano_roll.pending_length;
                    const uint8_t max_len = Bridge_GetStepEventMaxLength((uint8_t)(s_piano_roll.step - 1u), s);
                    if (max_len > 0u)
                    {
                        if (pending < 1u) pending = 1u;
                        if (pending > max_len) pending = max_len;
                        span_len = pending;
                    }
                }

                if (span_len > (uint8_t)(s_piano_roll.slot_count - s))
                {
                    span_len = (uint8_t)(s_piano_roll.slot_count - s);
                }
                s_piano_roll.roll_event_span[s] = span_len;
            }

            for (uint8_t i = 0u; i < 4u; ++i)
            {
                if (s_piano_roll.roll_slots[s][i] != s_piano_roll.roll_last_slots[s][i])
                {
                    ++changed_slots;
                    if (s != s_piano_roll.slot_index)
                    {
                        changed_current_slot_only = 0u;
                    }
                    break;
                }
            }

            if (s_piano_roll.roll_event_span[s] != s_piano_roll.roll_last_event_span[s])
            {
                ++changed_spans;
            }
        }

        if (prev_view != PIANO_VIEW_ROLL || prev_step != s_piano_roll.step)
        {
            dirty |= (ROLL_DIRTY_BODY_FULL |
                      ROLL_DIRTY_BAR |
                      ROLL_DIRTY_SKIP |
                      ROLL_DIRTY_POS |
                      ROLL_DIRTY_NOTE |
                      ROLL_DIRTY_KEYSCALE |
                      ROLL_DIRTY_RNG |
                      ROLL_DIRTY_CV |
                      ROLL_DIRTY_FOOTER);
        }
        else
        {
            const uint8_t selected_changed = (prev_selected != s_piano_roll.selected_midi_note) ? 1u : 0u;
            const uint8_t top_changed = (prev_top != s_piano_roll.roll_top_midi_note) ? 1u : 0u;
            const uint8_t slot_index_changed = (s_piano_roll.roll_last_slot_index != s_piano_roll.slot_index) ? 1u : 0u;
            const uint8_t slot_count_changed = (s_piano_roll.roll_last_slot_count != s_piano_roll.slot_count) ? 1u : 0u;
            const uint8_t grid_division_changed = (s_piano_roll.roll_last_grid_division != s_piano_roll.grid_division) ? 1u : 0u;

            if (selected_changed)
            {
                s_piano_roll.roll_prev_selected_midi = prev_selected;
                dirty |= ROLL_DIRTY_NOTE;
                if (top_changed)
                {
                    dirty |= (ROLL_DIRTY_BODY_FULL | ROLL_DIRTY_RNG);
                }
                else
                {
                    dirty |= (ROLL_DIRTY_ROW_OLD | ROLL_DIRTY_ROW_NEW);
                }
            }
            else if (top_changed)
            {
                dirty |= (ROLL_DIRTY_BODY_FULL | ROLL_DIRTY_RNG);
            }

            if (slot_index_changed)
            {
                dirty |= (ROLL_DIRTY_SLOT_BODY | ROLL_DIRTY_POS | ROLL_DIRTY_CV);
            }

            if (slot_count_changed)
            {
                dirty |= (ROLL_DIRTY_BODY_FULL | ROLL_DIRTY_POS | ROLL_DIRTY_CV);
            }

            if (changed_slots > 0u)
            {
                dirty |= ROLL_DIRTY_CV;
                if ((dirty & ROLL_DIRTY_BODY_FULL) == 0u)
                {
                    if (changed_slots == 1u && changed_current_slot_only)
                    {
                        dirty |= ROLL_DIRTY_ROW_NEW;
                    }
                    else
                    {
                        dirty |= ROLL_DIRTY_SLOT_BODY;
                    }
                }
            }

            if (changed_spans > 0u ||
                s_piano_roll.roll_last_length_mode_active != s_piano_roll.length_mode_active ||
                s_piano_roll.roll_last_pending_length != s_piano_roll.pending_length)
            {
                if ((dirty & ROLL_DIRTY_BODY_FULL) == 0u)
                {
                    dirty |= ROLL_DIRTY_SPAN_ROWS;
                }
            }

            if (s_piano_roll.roll_last_bar_skipped != s_piano_roll.bar_skipped)
            {
                dirty |= ROLL_DIRTY_SKIP;
            }

            if ((strncmp(s_piano_roll.roll_last_key_label, s_piano_roll.key_label, sizeof(s_piano_roll.roll_last_key_label)) != 0) ||
                (strncmp(s_piano_roll.roll_last_scale_label, s_piano_roll.scale_label, sizeof(s_piano_roll.roll_last_scale_label)) != 0) ||
                grid_division_changed)
            {
                dirty |= ROLL_DIRTY_KEYSCALE;
            }

            if (s_piano_roll.roll_last_cv_absolute_valid != s_piano_roll.cv_absolute_valid ||
                s_piano_roll.roll_last_arp_on != s_piano_roll.arp_on ||
                s_piano_roll.roll_last_slot_notes[0] != s_piano_roll.slot_notes[0] ||
                s_piano_roll.roll_last_slot_notes[1] != s_piano_roll.slot_notes[1] ||
                s_piano_roll.roll_last_slot_notes[2] != s_piano_roll.slot_notes[2] ||
                s_piano_roll.roll_last_slot_notes[3] != s_piano_roll.slot_notes[3])
            {
                dirty |= ROLL_DIRTY_CV;
            }
        }

        if ((dirty & ROLL_DIRTY_BODY_FULL) != 0u)
        {
            dirty &= (uint16_t)~(ROLL_DIRTY_ROW_OLD | ROLL_DIRTY_ROW_NEW | ROLL_DIRTY_SLOT_BODY);
        }

        s_piano_roll.roll_dirty_flags |= dirty;
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
    s_piano_roll.roll_last_selected_midi = 0xFF;
    s_piano_roll.roll_last_top_midi_note = 0xFF;
    s_piano_roll.roll_last_slot_index = 0xFF;
    s_piano_roll.roll_last_slot_count = 0xFF;
    s_piano_roll.roll_last_grid_division = 0xFF;
    s_piano_roll.roll_last_bar_skipped = 0xFF;
    s_piano_roll.roll_last_cv_absolute_valid = 0xFF;
    s_piano_roll.roll_last_arp_on = 0xFF;
    s_piano_roll.roll_last_length_mode_active = 0xFF;
    s_piano_roll.roll_last_pending_length = 0xFF;
    s_piano_roll.roll_last_slot_notes[0] = 0xFFu;
    s_piano_roll.roll_last_slot_notes[1] = 0xFFu;
    s_piano_roll.roll_last_slot_notes[2] = 0xFFu;
    s_piano_roll.roll_last_slot_notes[3] = 0xFFu;
    s_piano_roll.roll_last_key_label[0] = '\0';
    s_piano_roll.roll_last_scale_label[0] = '\0';
    for (uint8_t s = 0u; s < 32u; ++s)
    {
        s_piano_roll.roll_event_span[s] = 0u;
        s_piano_roll.roll_last_event_span[s] = 0u;
        for (uint8_t i = 0u; i < 4u; ++i)
        {
            s_piano_roll.roll_last_slots[s][i] = 0xFFu;
            s_piano_roll.roll_slots[s][i] = 0xFFu;
        }
    }
    s_piano_roll.roll_prev_selected_midi = 0xFFu;
    s_piano_roll.roll_dirty_flags = 0xFFFFu;
    s_piano_roll.needs_redraw = 1;
}
