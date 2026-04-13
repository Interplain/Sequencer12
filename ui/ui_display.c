#include "ui_display.h"
#include "lib/ST7789/st7789.h"
#include <stdio.h>
#include "fonts_extra.h"
/* ── Layout constants ────────────────────────────────────────────────────── */
#define SCREEN_W    240
#define SCREEN_H    240
#define HEADER_H     24
#define GRID_X        8
#define GRID_Y       56
#define BOX_W        50
#define BOX_H        56
#define GAP_X         6
#define GAP_Y         8
#define BORDER_T      2

/* ── Font characters ─────────────────────────────────────────────────────── */
#define ICON_PLAY    91   /* ▶ triangle */
#define ICON_STOP    92   /* ■ square   */
#define ICON_REC     93   /* ● circle   */

/* ── Colours ─────────────────────────────────────────────────────────────── */
#define COLOR_BG           RGB565(2, 4, 8)
#define COLOR_PANEL        RGB565(6, 10, 16)
#define COLOR_PANEL_ALT    RGB565(10, 16, 26)
#define COLOR_BOX_BG       RGB565(8, 12, 20)
#define COLOR_BOX_BORDER   RGB565(18, 28, 42)
#define COLOR_BORDER_ACCENT RGB565(24, 40, 60)
#define COLOR_TEXT_MAIN    WHITE
define COLOR_TEXT_SECONDARY LIGHTBLUE
#define COLOR_TEXT_MUTED   LGRAY
#define COLOR_ACCENT       RGB565(40, 60, 63)
#define COLOR_ACTIVE       RGB565(16, 40, 63)
#define COLOR_SELECTED     RGB565(12, 50, 22)
#define COLOR_CHORD        RGB565(30, 12, 52)
#define COLOR_STATUS       RGB565(10, 16, 24)

/* ── Forward declarations ────────────────────────────────────────────────── */
static void DrawRectBorder(uint16_t x, uint16_t y,
                           uint16_t w, uint16_t h, uint16_t color);

/* ── Init ────────────────────────────────────────────────────────────────── */
void UI_Display_Init(void)
{
    ST7789_Fill_Color(BLACK);
    UI_Display_DrawHeader(120, TRANSPORT_STOPPED, 0);
    /* Don't draw grid here - caller will draw with chord info */
}

/* ── Header ──────────────────────────────────────────────────────────────── */
void UI_Display_DrawHeader(uint16_t bpm, TransportState state, uint8_t rec_armed)
{
    ST7789_FillRect(0, 0, SCREEN_W, HEADER_H, BLACK);

    /* BPM */
    char bpm_str[8];
    snprintf(bpm_str, sizeof(bpm_str), "%3u", bpm);
    ST7789_DrawStringScaled(6, 2, bpm_str, &Font8x12, 2, WHITE, BLACK);
    ST7789_DrawString(60, 2, "BPM", &Font16x24, WHITE, BLACK);

    /* Transport icon — square when stopped, triangle when playing */
    char transport_icon[2] = {
        (state == TRANSPORT_PLAYING || state == TRANSPORT_PLAYING_RECORDING)
            ? ICON_PLAY : ICON_STOP,
        0
    };
    uint16_t transport_col =
        (state == TRANSPORT_PLAYING || state == TRANSPORT_PLAYING_RECORDING)
            ? COL_PLAY : COL_STOP;

    ST7789_DrawString(153, 0, transport_icon, &Font16x24, transport_col, BLACK);

    /* Record icon */
    char rec_icon[2] = {ICON_REC, 0};
    uint16_t rec_col = rec_armed ? COL_REC_ARM : COL_REC_OFF;
    ST7789_DrawString(199, 0, rec_icon, &Font16x24, rec_col, BLACK);
}

/* ── Grid ────────────────────────────────────────────────────────────────── */
void UI_Display_DrawGrid(void)
{
    for (uint8_t i = 1; i <= 12; i++)
        UI_Display_DrawStepBox(i, 0, 0, 0);  /* No chords during grid init */
}

void UI_Display_DrawStepBox(uint8_t step, uint8_t selected, uint8_t active, uint8_t has_chord)
{
    uint8_t  index = step - 1;
    uint8_t  col   = index % 4;
    uint8_t  row   = index / 4;

    uint16_t x = GRID_X + col * (BOX_W + GAP_X);
    uint16_t y = GRID_Y + row * (BOX_H + GAP_Y);

    /* Priority: active (playing) > selected (cursor) > chord > normal */
    uint16_t colour;
    if      (active)   colour = COL_ACTIVE;
    else if (selected) colour = COL_SELECTED;
    else if (has_chord) colour = COL_CHORD;
    else               colour = COL_NORMAL;

    char s[4];
    snprintf(s, sizeof(s), "%u", step);

    ST7789_FillRect(x, y, BOX_W, BOX_H, BLACK);
    DrawRectBorder(x, y, BOX_W, BOX_H, colour);

    uint8_t len = 0;
    while (s[len]) len++;

    uint16_t textW = (uint16_t)(len * Font8x12.width * 2);
    uint16_t textH = (uint16_t)(Font8x12.height * 2);
    uint16_t tx    = x + ((BOX_W - textW) / 2);
    uint16_t ty    = y + ((BOX_H - textH) / 2);

    ST7789_DrawStringScaled(tx, ty, s, &Font8x12, 2, colour, BLACK);
}

/* ── Shift indicator ─────────────────────────────────────────────────────── */
void UI_Display_SetShiftIndicator(uint8_t active)
{
    ST7789_FillRect(0, 45, SCREEN_W, 4,
                    active ? COL_SHIFT : BLACK);
}

/* ── Internal ────────────────────────────────────────────────────────────── */
static void DrawRectBorder(uint16_t x, uint16_t y,
                           uint16_t w, uint16_t h, uint16_t color)
{
    ST7789_FillRect(x, y,             w,        BORDER_T, color);
    ST7789_FillRect(x, y + h - BORDER_T, w,     BORDER_T, color);
    ST7789_FillRect(x, y,             BORDER_T, h,        color);
    ST7789_FillRect(x + w - BORDER_T, y, BORDER_T, h,     color);
}

/* ── Time Display ────────────────────────────────────────────────────────────── */
void UI_Display_DrawStatusRow(uint8_t pattern,
                              uint8_t step,
                              uint32_t loops,
                              uint32_t run_time_ms)
{
    char buf[40];

    uint32_t total_sec = run_time_ms / 1000;
    uint32_t mins      = total_sec / 60;
    uint32_t secs      = total_sec % 60;

    snprintf(buf, sizeof(buf), "P%02u ST:%02u LP:%02lu %02lu:%02lu",
             pattern + 1,
             step + 1,
             (unsigned long)loops,
             (unsigned long)mins,
             (unsigned long)secs);

    /* DrawString already includes black background, no need to pre-clear */
    ST7789_DrawString(4, 26, buf, &Font10x16, CYAN, BLACK);
}

/* ── Chord Menu ───────────────────────────────────────────────────────────── */
static uint8_t s_chord_selection = 0;  /* currently selected chord */

void UI_Display_DrawChordMenu(uint8_t step, const ChordParams* chord)
{
    /* Clear entire screen for clean redraw */
    ST7789_Fill_Color(BLACK);

    /* Title */
    char title[20];
    snprintf(title, sizeof(title), "Step %u Chord", step);
    ST7789_DrawString(4, 4, title, &Font12x20, WHITE, BLACK);

    /* Current selected chord summary */
    const char* root_keys[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const char* chord_names[] = {
        "Clear", "Major", "Minor", "Dim", "Aug", "Sus4", "7th", "Maj7",
        "Min7", "Dim7", "7sus4", "6th", "Min6", "9th", "Maj9", "Min9", "11th"
    };

    char selected_label[24];
    if (s_chord_selection == 0)
    {
        snprintf(selected_label, sizeof(selected_label), "Selected: Clear");
    }
    else
    {
        uint8_t root = chord->root_key;
        snprintf(selected_label, sizeof(selected_label), "Selected: %s %s", root_keys[root], chord_names[s_chord_selection]);
    }
    ST7789_DrawString(4, 26, selected_label, &Font10x16, YELLOW, BLACK);

    /* Chord list - show all 17 options (Clear + 16 chord types) */
    /* Show 8 chords at a time, scrolling through pages */
    uint8_t start_idx = (s_chord_selection / 8) * 8;
    uint8_t end_idx = start_idx + 8;
    if (end_idx > 17) end_idx = 17;

    for (uint8_t i = start_idx; i < end_idx; i++)
    {
        uint8_t y = 44 + (i - start_idx) * 18;
        uint16_t color = (i == s_chord_selection) ? CYAN : WHITE;

        char chord_text[12];
        if (i == 0) {
            snprintf(chord_text, sizeof(chord_text), "%u: %s", i + 1, chord_names[i]);
        } else {
            /* Show chord with current root key */
            const char* root_keys[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
            uint8_t root = chord->root_key;
            snprintf(chord_text, sizeof(chord_text), "%u: %s%s", i + 1, root_keys[root], chord_names[i]);
        }
        ST7789_DrawString(4, y, chord_text, &Font10x16, color, BLACK);
    }

    /* Instructions at bottom */
    ST7789_DrawString(4, 185, "Select | Press: Confirm", &Font10x16, YELLOW, BLACK);
    ST7789_DrawString(4, 200, "Shift+Enc: Change Step", &Font10x16, YELLOW, BLACK);
}

void UI_Display_NavigateChordMenu(int8_t delta, uint8_t step, const ChordParams* chord)
{
    int8_t next = (int8_t)s_chord_selection + delta;
    if (next < 0)  next = 16;  /* Wrap around: 17 options total (0-16) */
    if (next > 16) next = 0;
    s_chord_selection = (uint8_t)next;

    /* Redraw entire menu for clean display */
    UI_Display_DrawChordMenu(step, chord);
}

uint8_t UI_Display_GetSelectedChord(void)
{
    return s_chord_selection;
}

void UI_Display_SetChordSelection(uint8_t selection)
{
    if (selection <= 16)  /* Valid range 0-16 */
        s_chord_selection = selection;
}

/* ── Chord Parameters ─────────────────────────────────────────────────────── */
void UI_Display_DrawChordParams(uint8_t step, const ChordParams* chord, uint8_t param_cursor)
{
    /* Clear entire screen */
    ST7789_Fill_Color(BLACK);

    /* Title */
    char title[20];
    snprintf(title, sizeof(title), "Step %u Parameters", step);
    ST7789_DrawString(4, 4, title, &Font12x20, WHITE, BLACK);

    /* Parameter names and values */
    const char* param_names[] = {"Root Key:", "Type:", "Arp:", "Duration:"};
    const char* root_keys[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const char* chord_types[] = {"Clear", "Major", "Minor", "Dim", "Aug", "Sus4", "7th", "Maj7", "Min7", "Dim7", "7sus4", "6th", "Min6", "9th", "Maj9", "Min9", "11th"};
    const char* arp_patterns[] = {"Block", "Up", "Down", "UpDn", "Rand"};
    const char* durations[] = {"16th", "8th", "Qtr", "8th."};

    uint16_t colors[4] = {WHITE, WHITE, WHITE, WHITE};
    colors[param_cursor] = CYAN;  /* Highlight current parameter */

    for (uint8_t i = 0; i < 4; i++)
    {
        uint8_t y = 32 + i * 20;

        /* Parameter name */
        ST7789_DrawString(4, y, param_names[i], &Font10x16, colors[i], BLACK);

        /* Parameter value */
        char value[10];
        switch (i) {
            case 0: snprintf(value, sizeof(value), "%s", root_keys[chord->root_key]); break;
            case 1: snprintf(value, sizeof(value), "%s", chord_types[chord->chord_type]); break;
            case 2: snprintf(value, sizeof(value), "%s", arp_patterns[chord->arp_pattern]); break;
            case 3: snprintf(value, sizeof(value), "%s", durations[chord->duration]); break;
        }
        ST7789_DrawString(120, y, value, &Font10x16, colors[i], BLACK);
    }

    /* Instructions */
    ST7789_DrawString(4, 200, "Select: Next Param | Press: Save", &Font10x16, YELLOW, BLACK);
}

void UI_Display_NavigateChordParams(int8_t delta, uint8_t step, ChordParams* chord, uint8_t param_cursor)
{
    /* Adjust the current parameter */
    switch (param_cursor) {
        case 0: /* Root key */
            chord->root_key = (chord->root_key + delta) % 12;
            if (chord->root_key > 11) chord->root_key = 11;
            break;
        case 1: /* Chord type */
            chord->chord_type = (chord->chord_type + delta) % 17;
            if (chord->chord_type > 16) chord->chord_type = 16;
            break;
        case 2: /* Arp pattern */
            chord->arp_pattern = (chord->arp_pattern + delta) % 5;
            if (chord->arp_pattern > 4) chord->arp_pattern = 4;
            break;
        case 3: /* Duration */
            chord->duration = (chord->duration + delta) % 4;
            if (chord->duration > 3) chord->duration = 3;
            break;
    }

    /* Redraw parameters */
    UI_Display_DrawChordParams(step, chord, param_cursor);
}