#include "ui_display.h"
#include "ui_regions.h"
#include "ui_icons.h"
#include "lib/ST7789/st7789.h"
#include <stdio.h>
#include <string.h>
#include "fonts_extra.h"
#include "sequencer_bridge.h"
/* ── Layout constants ────────────────────────────────────────────────────── */
#define SCREEN_W    ST7789_WIDTH
#define SCREEN_H    ST7789_HEIGHT
#define MAIN_W      ((SCREEN_W > 240U) ? 240U : SCREEN_W)
#define SIDEBAR_X   MAIN_W
#define SIDEBAR_W   ((SCREEN_W > MAIN_W) ? (SCREEN_W - MAIN_W) : 0U)
#define HEADER_H     24
#define BOX_W        55
#define BOX_H        56
#define GAP_X         4
#define GAP_Y         8
#define BORDER_T      2

#define GRID_COLS      4
#define GRID_X         ((MAIN_W - ((GRID_COLS * BOX_W) + ((GRID_COLS - 1) * GAP_X))) / 2)
#define GRID_Y         56

#define HEADER_TRANSPORT_X (MAIN_W - 87)
#define HEADER_REC_X       (MAIN_W - 41)
#define STATUS_LOOPS_X     (MAIN_W - 116)
#define STATUS_TIME_X      (MAIN_W - 52)

#define MENU_TOP_Y        UI_REGION_MENU_TOP_H
#define MENU_LIST_H       (SCREEN_H - UI_REGION_MENU_TOP_H - UI_REGION_MENU_FOOTER_H)
#define MENU_FOOTER_Y     (SCREEN_H - UI_REGION_MENU_FOOTER_H)

#define CHORD_GRID_H      (CHORD_BTN_ROWS * CHORD_BTN_H + ((CHORD_BTN_ROWS - 1) * CHORD_BTN_GAP_Y))
#define CHORD_BTN_Y0      (MENU_TOP_Y + ((MENU_LIST_H > CHORD_GRID_H) ? ((MENU_LIST_H - CHORD_GRID_H) / 2) : 0))

#define PARAM_ROW_COUNT   5
#define PARAM_ROW_H       22
#define PARAM_ROW_STEP    24
#define PARAM_BLOCK_H     (PARAM_ROW_H + ((PARAM_ROW_COUNT - 1) * PARAM_ROW_STEP))
#define PARAM_ROW_Y0      (MENU_TOP_Y + ((MENU_LIST_H > PARAM_BLOCK_H) ? ((MENU_LIST_H - PARAM_BLOCK_H) / 2) : 0))

#define SONG_ROW_COUNT    4
#define SONG_ROW_H        22
#define SONG_ROW_STEP     24
#define SONG_BLOCK_H      (SONG_ROW_H + ((SONG_ROW_COUNT - 1) * SONG_ROW_STEP))
#define SONG_ROW_Y0       (MENU_TOP_Y + ((MENU_LIST_H > SONG_BLOCK_H) ? ((MENU_LIST_H - SONG_BLOCK_H) / 2) : 0))

#define FOOTER_TEXT_Y1    (MENU_FOOTER_Y + 8)
#define FOOTER_TEXT_Y2    (MENU_FOOTER_Y + 26)

/* ── Font characters ─────────────────────────────────────────────────────── */
#define ICON_PLAY    91   /* ▶ triangle */
#define ICON_STOP    92   /* ■ square   */
#define ICON_REC     93   /* ● circle   */

/* ── Colours ─────────────────────────────────────────────────────────────── */
#define COLOR_BG           RGB565(2, 4, 8)
#define COLOR_PANEL        RGB565(6, 10, 16)
#define COLOR_PANEL_ALT    RGB565(10, 16, 26)
#define COLOR_BOX_BG       BLACK
#define COLOR_BOX_BORDER   RGB565(18, 28, 42)
#define COLOR_BORDER_ACCENT RGB565(24, 40, 60)
#define COLOR_TEXT_MAIN    WHITE
#define COLOR_TEXT_SECONDARY LIGHTBLUE
#define COLOR_TEXT_MUTED   LGRAY
#define COLOR_ACCENT       RGB565(40, 60, 63)
#define COLOR_ACTIVE       RGB565(16, 40, 63)
#define COLOR_SELECTED     RGB565(12, 50, 22)
#define COLOR_CHORD        RGB565(30, 12, 52)
#define COLOR_STATUS       RGB565(10, 16, 24)

#define COL_PLAY     RGB565(20, 48, 12)   /* grass green */
#define COL_STOP     COLOR_TEXT_MAIN
#define COL_REC_ARM  RGB565(63, 0, 0)     /* red */
#define COL_REC_OFF  COLOR_TEXT_MUTED
#define COL_SHIFT    YELLOW
#define COL_ACTIVE   COLOR_ACTIVE
#define COL_SELECTED COLOR_SELECTED
#define COL_CHORD    COLOR_CHORD
#define COL_NORMAL   WHITE
#define COL_SELECTED_EMPTY COLOR_TEXT_MUTED

/* ── Forward declarations ────────────────────────────────────────────────── */
static void DrawRectBorder(uint16_t x, uint16_t y,
                           uint16_t w, uint16_t h, uint16_t color);
static void FillRegion(UiRect r, uint16_t color);
static void MenuTemplate_Begin(uint8_t frame_id);
static void MenuTemplate_DrawHeader(const char* title, const char* indicator, uint16_t indicator_color);
static void GetChordButtonXY(uint8_t slot, uint16_t* x, uint16_t* y);
static void DrawAllChordButtonFrames(void);
static void DrawChordButtonFrame(uint8_t slot);
static void DrawChordButtonText(uint8_t slot, uint8_t chord_idx, uint8_t selected, const ChordParams* chord);
static void DrawRoundedButton(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t fill, uint16_t border);
static void DrawParamFooterButton(uint8_t slot, const char* label, uint8_t selected);
static void DrawCenteredFooterButton(const char* label, uint8_t selected);
static void DrawPianoWhiteKey(uint8_t slot, uint8_t note);
static void DrawPianoBlackKey(uint8_t slot, uint8_t note);
static void DrawMainModeBox(void);
static void DrawSidebarInfo(void);

enum {
    MENU_FRAME_NONE = 0,
    MENU_FRAME_CHORD,
    MENU_FRAME_USER_CHORD,
    MENU_FRAME_PARAMS,
    MENU_FRAME_TIMING,
    MENU_FRAME_SONG
};

/* Status row cache shared with init so we can force redraw after menu exits */
static uint8_t  s_prev_pattern = 0xFF;
static uint8_t  s_prev_step = 0xFF;
static uint32_t s_prev_loops = 0xFFFFFFFF;
static uint32_t s_prev_mins = 0xFFFFFFFF;
static uint32_t s_prev_secs = 0xFFFFFFFF;
static uint8_t  s_force_status_redraw = 1;
static uint8_t  s_chord_footer_valid;
static uint8_t  s_param_footer_valid;
static uint8_t  s_timing_footer_valid;
static uint8_t  s_timing_footer_selection = 0; /* 0=MAIN, 1=SAVE */
static uint8_t  s_timing_footer_drawn = 0xFF;
static uint8_t  s_menu_frame = MENU_FRAME_NONE;
static uint8_t  s_menu_header_valid = 0;
static uint8_t  s_sidebar_valid = 0;
static uint8_t  s_prev_sidebar_div = 0xFF;
static uint8_t  s_prev_sidebar_swing = 0xFF;
static char     s_menu_header_title[28];
static char     s_menu_header_indicator[28];
static uint16_t s_menu_header_indicator_color = COLOR_TEXT_MAIN;

/* ── Parameter menu row cache (prevent list flashing) ─────────────────────── */
static uint8_t  s_param_row_cursor = 0xFF;

/* ── Timing menu row cache (prevent list flashing) ──────────────────────── */
static uint8_t  s_timing_row_cursor = 0xFF;
static UiMainMode s_main_mode = UI_MAIN_MODE_STEP;
static UiMainMode s_prev_main_mode = (UiMainMode)0xFF;
static uint8_t s_repeat_flash_enabled = 0;
static uint8_t s_repeat_flash_on = 0;
static uint8_t s_header_valid = 0;
static uint8_t s_chord_page_drawn = 0xFF;
static uint16_t s_prev_header_bpm = 0xFFFF;
static TransportState s_prev_header_state = (TransportState)0xFF;
static uint8_t s_prev_header_rec = 0xFF;
static uint8_t s_step_cache_valid[12] = {0};
static uint8_t s_step_cache_selected[12] = {0};
static uint8_t s_step_cache_active[12] = {0};
static uint8_t s_step_cache_has_chord[12] = {0};
static uint8_t s_step_cache_flash_on[12] = {0};
static uint8_t s_step_cache_flash_enabled[12] = {0};

/* ── Init ────────────────────────────────────────────────────────────────── */
void UI_Display_Init(void)
{
    ST7789_Fill_Color(COLOR_BG);
    ST7789_DisplayOn();
    s_header_valid = 0;
    memset(s_step_cache_valid, 0, sizeof(s_step_cache_valid));
    UI_Display_DrawHeader(120, TRANSPORT_STOPPED, 0);
    s_force_status_redraw = 1;
    s_chord_footer_valid = 0;
    s_timing_footer_valid = 0;
    s_timing_footer_drawn = 0xFF;
    s_menu_frame = MENU_FRAME_NONE;
    s_menu_header_valid = 0;
    s_menu_header_title[0] = 0;
    s_menu_header_indicator[0] = 0;
    s_param_row_cursor = 0xFF;
    s_timing_row_cursor = 0xFF;
    s_sidebar_valid = 0;
    s_prev_sidebar_div = 0xFF;
    s_prev_sidebar_swing = 0xFF;
    /* Don't draw grid here - caller will draw with chord info */
}

/* ── Header ──────────────────────────────────────────────────────────────── */
void UI_Display_DrawHeader(uint16_t bpm, TransportState state, uint8_t rec_armed)
{
    if (s_header_valid &&
        s_prev_header_bpm == bpm &&
        s_prev_header_state == state &&
        s_prev_header_rec == rec_armed) {
        return;
    }

    ST7789_FillRect(0, 0, SCREEN_W, HEADER_H, BLACK);

    /* BPM */
    char bpm_str[8];
    snprintf(bpm_str, sizeof(bpm_str), "%3u", bpm);
    ST7789_DrawStringScaled(4, 2, bpm_str, &Font8x12, 2, COLOR_TEXT_MAIN, BLACK);
    ST7789_DrawString(52, 2, "BPM", &Font16x24, COLOR_TEXT_MAIN, BLACK);

    /* Transport icon — square when stopped, triangle when playing */
    char transport_icon[2] = {
        (state == TRANSPORT_PLAYING || state == TRANSPORT_PLAYING_RECORDING)
            ? ICON_PLAY : ICON_STOP,
        0
    };
    uint16_t transport_col =
        (state == TRANSPORT_PLAYING || state == TRANSPORT_PLAYING_RECORDING)
            ? COL_PLAY : COL_STOP;

    ST7789_DrawString(HEADER_TRANSPORT_X, 0, transport_icon, &Font16x24, transport_col, BLACK);

    /* Record icon */
    char rec_icon[2] = {ICON_REC, 0};
    uint16_t rec_col = rec_armed ? COL_REC_ARM : COL_REC_OFF;
    ST7789_DrawString(HEADER_REC_X, 0, rec_icon, &Font16x24, rec_col, BLACK);

    s_prev_header_bpm = bpm;
    s_prev_header_state = state;
    s_prev_header_rec = rec_armed;
    s_header_valid = 1;
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

    if (index < 12U &&
        s_step_cache_valid[index] &&
        s_step_cache_selected[index] == selected &&
        s_step_cache_active[index] == active &&
        s_step_cache_has_chord[index] == has_chord &&
        s_step_cache_flash_on[index] == s_repeat_flash_on &&
        s_step_cache_flash_enabled[index] == s_repeat_flash_enabled) {
        return;
    }

    uint16_t x = GRID_X + col * (BOX_W + GAP_X);
    uint16_t y = GRID_Y + row * (BOX_H + GAP_Y);

    /* Priority: active (playing) > selected+saved > selected+empty > saved > normal */
    uint16_t colour;
    if (active && s_repeat_flash_enabled)
        colour = s_repeat_flash_on ? WHITE : BLUE;
    else if (active)
        colour = COL_ACTIVE;
    else if (selected && has_chord)
        colour = COL_CHORD;
    else if (selected)
        colour = COL_SELECTED_EMPTY;
    else if (has_chord)
        colour = COL_CHORD;
    else
        colour = COL_NORMAL;

    char s[4];
    snprintf(s, sizeof(s), "%u", step);

    ST7789_FillRect(x, y, BOX_W, BOX_H, COLOR_BOX_BG);
    DrawRectBorder(x, y, BOX_W, BOX_H, colour);

    uint8_t len = 0;
    while (s[len]) len++;

    uint16_t textW = (uint16_t)(len * Font8x12.width * 2);
    uint16_t textH = (uint16_t)(Font8x12.height * 2);
    uint16_t tx    = x + ((BOX_W - textW) / 2);
    uint16_t ty    = y + ((BOX_H - textH) / 2);

    ST7789_DrawStringScaled(tx, ty, s, &Font8x12, 2, colour, COLOR_BOX_BG);

    if (index < 12U) {
        s_step_cache_selected[index] = selected;
        s_step_cache_active[index] = active;
        s_step_cache_has_chord[index] = has_chord;
        s_step_cache_flash_on[index] = s_repeat_flash_on;
        s_step_cache_flash_enabled[index] = s_repeat_flash_enabled;
        s_step_cache_valid[index] = 1;
    }
}

void UI_Display_SetRepeatFlash(uint8_t enabled, uint8_t on_phase)
{
    s_repeat_flash_enabled = enabled ? 1u : 0u;
    s_repeat_flash_on = on_phase ? 1u : 0u;
}

/* ── Shift indicator ─────────────────────────────────────────────────────── */
void UI_Display_SetShiftIndicator(uint8_t active)
{
    (void)active;
    ST7789_FillRect(0, 45, MAIN_W, 4, COLOR_BG);
}

static void DrawSidebarInfo(void)
{
    if (SIDEBAR_W == 0U) return;

    uint8_t div   = Bridge_GetPatternStepDivision();
    uint8_t swing = Bridge_GetSwing();

    if (s_sidebar_valid && div == s_prev_sidebar_div && swing == s_prev_sidebar_swing) return;

    const uint16_t panel_bg   = COLOR_BG;
 

    /* Fill only below the header+status band so we don't bleed into it */
    ST7789_FillRect(SIDEBAR_X, 44, SIDEBAR_W, SCREEN_H - 44U, panel_bg);

    {
        char q[12];
        snprintf(q, sizeof(q), "1/%u", (unsigned)(div * 4U));
        ST7789_DrawString((uint16_t)(SIDEBAR_X + 8), 52, "QUANT", &Font8x12, LIGHTBLUE, panel_bg);
        ST7789_DrawString((uint16_t)(SIDEBAR_X + 8), 66, q, &Font10x16, WHITE, panel_bg);

        snprintf(q, sizeof(q), "%u%%", (unsigned)swing);
        ST7789_DrawString((uint16_t)(SIDEBAR_X + 8), 100, "SWING", &Font8x12, LIGHTBLUE, panel_bg);
        ST7789_DrawString((uint16_t)(SIDEBAR_X + 8), 116, q, &Font10x16, WHITE, panel_bg);
    }

    s_prev_sidebar_div   = div;
    s_prev_sidebar_swing = swing;
    s_sidebar_valid      = 1;
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

static void FillRegion(UiRect r, uint16_t color)
{
    ST7789_FillRect(r.x, r.y, r.w, r.h, color);
}

static uint8_t StrEq(const char* a, const char* b)
{
    uint16_t i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return (a[i] == 0 && b[i] == 0) ? 1 : 0;
}

static void StrCopy(char* dst, uint16_t size, const char* src)
{
    if (size == 0) return;
    snprintf(dst, size, "%s", src);
}

static void MenuTemplate_Begin(uint8_t frame_id)
{
    if (s_menu_frame == frame_id) return;

    s_menu_frame = frame_id;
    s_menu_header_valid = 0;
    s_menu_header_title[0] = 0;
    s_menu_header_indicator[0] = 0;

    /* Each menu owns its footer strip and redraw cache. */
    s_chord_footer_valid = 0;
    s_param_footer_valid = 0;
    s_timing_footer_valid = 0;
    s_timing_footer_drawn = 0xFF;
    s_param_row_cursor = 0xFF;
    s_timing_row_cursor = 0xFF;
}

static void MenuTemplate_DrawHeader(const char* title, const char* indicator, uint16_t indicator_color)
{
    uint8_t need_redraw = !s_menu_header_valid;

    if (!need_redraw && !StrEq(s_menu_header_title, title)) need_redraw = 1;
    if (!need_redraw && !StrEq(s_menu_header_indicator, indicator)) need_redraw = 1;
    if (!need_redraw && s_menu_header_indicator_color != indicator_color) need_redraw = 1;

    if (!need_redraw) return;

    FillRegion(UI_REGION_MENU_TOP, COLOR_PANEL_ALT);
    ST7789_DrawString(4, 4, title, &Font12x20, COLOR_TEXT_MAIN, COLOR_PANEL_ALT);
    if (indicator[0]) {
        ST7789_DrawString(4, 22, indicator, &Font10x16, indicator_color, COLOR_PANEL_ALT);
    }

    StrCopy(s_menu_header_title, sizeof(s_menu_header_title), title);
    StrCopy(s_menu_header_indicator, sizeof(s_menu_header_indicator), indicator);
    s_menu_header_indicator_color = indicator_color;
    s_menu_header_valid = 1;
}

static void DrawMainModeBox(void)
{
    uint16_t box_x = 4;
    uint16_t box_y = 25;
    uint16_t box_w = 44;
    uint16_t box_h = 18;
    uint16_t fill = WHITE;
    uint16_t border = COLOR_BOX_BORDER;
    uint16_t text = BLACK;
    const char* label = "STEP";

    if (s_main_mode == UI_MAIN_MODE_CHORD)
    {
        fill = RGB565(70, 140, 255);
        text = WHITE;
        label = "CHRD";
    }
    else if (s_main_mode == UI_MAIN_MODE_TIMING)
    {
        fill = ORANGE;
        text = WHITE;
        label = "TIME";
    }
    else if (s_main_mode == UI_MAIN_MODE_PATTERN)
    {
        fill = RGB565(0, 135, 0);
        text = WHITE;
        label = "SONG";
    }

    ST7789_FillRect(box_x, box_y, box_w, box_h, fill);
    DrawRectBorder(box_x, box_y, box_w, box_h, border);
    ST7789_DrawString(8, 28, label, &Font8x12, text, fill);
    s_prev_main_mode = s_main_mode;
}

void UI_Display_SetMainMode(UiMainMode mode)
{
    if (mode > UI_MAIN_MODE_PATTERN)
    {
        mode = UI_MAIN_MODE_STEP;
    }
    if (s_main_mode != mode)
    {
        s_main_mode = mode;
        s_force_status_redraw = 1;
    }
}

/* ── Time Display ────────────────────────────────────────────────────────────── */
void UI_Display_DrawStatusRow(uint8_t pattern,
                              uint8_t step,
                              uint32_t loops,
                              uint32_t run_time_ms)
{
    uint32_t total_sec = run_time_ms / 1000;
    uint32_t mins      = total_sec / 60;
    uint32_t secs      = total_sec % 60;

    DrawSidebarInfo();

    if (s_force_status_redraw || s_main_mode != s_prev_main_mode || pattern != s_prev_pattern) {
        DrawMainModeBox();
        s_prev_pattern = pattern;
    }
    if (s_force_status_redraw || step != s_prev_step) {
        char st[8];
        snprintf(st, sizeof(st), "ST:%02u", step + 1);
        ST7789_DrawString(54, 26, st, &Font10x16, COLOR_TEXT_MAIN, COLOR_BG);
        s_prev_step = step;
    }
    if (s_force_status_redraw || loops != s_prev_loops) {
        char lp[12];
        snprintf(lp, sizeof(lp), "LP:%02lu", (unsigned long)loops);
        ST7789_DrawString(STATUS_LOOPS_X, 26, lp, &Font10x16, COLOR_TEXT_MAIN, COLOR_BG);
        s_prev_loops = loops;
    }
    if (s_force_status_redraw || (mins != s_prev_mins) || (secs != s_prev_secs)) {
        char tm[8];
        snprintf(tm, sizeof(tm), "%02lu:%02lu", (unsigned long)mins, (unsigned long)secs);
        ST7789_DrawString(STATUS_TIME_X, 26, tm, &Font10x16, COLOR_TEXT_MAIN, COLOR_BG);
        s_prev_mins = mins;
        s_prev_secs = secs;
    }

    s_force_status_redraw = 0;
}

/* ── Chord Menu ───────────────────────────────────────────────────────────── */
static uint8_t s_chord_selection = 0;  /* currently selected chord */
static uint8_t s_param_footer_selection = PARAM_ACTION_MAIN;
static const char* s_root_keys[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char* s_chord_names[] = {
    "Clear", "Major", "Minor", "Dim", "Aug", "Sus4", "7th", "Maj7",
    "Min7", "Dim7", "7sus4", "6th", "Min6", "9th", "Maj9", "Min9", "11th"
};

#define CHORD_BTN_COLS      3
#define CHORD_BTN_ROWS      2
#define CHORD_BTNS_PER_PAGE (CHORD_BTN_COLS * CHORD_BTN_ROWS)
#define CHORD_BTN_X0        ((SCREEN_W - ((CHORD_BTN_COLS * CHORD_BTN_W) + ((CHORD_BTN_COLS - 1) * CHORD_BTN_GAP_X))) / 2)
#define CHORD_BTN_W         70
#define CHORD_BTN_H         50
#define CHORD_BTN_GAP_X      8
#define CHORD_BTN_GAP_Y      8

#define FOOTER_BTN_COLS      4
#define FOOTER_BTN_X0        6
#define FOOTER_BTN_Y0        (MENU_FOOTER_Y + ((UI_REGION_MENU_FOOTER_H - FOOTER_BTN_H) / 2))
#define FOOTER_BTN_W         54
#define FOOTER_BTN_H         38
#define FOOTER_BTN_GAP_X      4

void UI_Display_DrawChordMenu(uint8_t step, const ChordParams* chord)
{
    MenuTemplate_Begin(MENU_FRAME_CHORD);
    s_chord_page_drawn = 0xFF;  /* Reset page cache when entering menu */

    /* Content region redraw only. */
    FillRegion(UI_REGION_MENU_LIST, COLOR_BG);
    FillRegion(UI_REGION_MENU_FOOTER, COLOR_PANEL);
    DrawCenteredFooterButton("USER CHORDS", (s_chord_selection == 17) ? 1 : 0);
    s_chord_footer_valid = 1;

    /* Title */
    char title[20];
    snprintf(title, sizeof(title), "Step %u Chord", step);
    ST7789_DrawString(4, 4, title, &Font12x20, COLOR_TEXT_MAIN, COLOR_PANEL_ALT);

    char selected_label[24];
    if (s_chord_selection == 0)
    {
        snprintf(selected_label, sizeof(selected_label), "Selected: Clear");
    }
    else if (s_chord_selection == 17)
    {
        snprintf(selected_label, sizeof(selected_label), "Selected: USER");
    }
    else
    {
        uint8_t root = chord->root_key;
        snprintf(selected_label, sizeof(selected_label), "Selected: %s %s", s_root_keys[root], s_chord_names[s_chord_selection]);
    }
    MenuTemplate_DrawHeader(title, selected_label, YELLOW);

    /* 3x2 rounded-button grid (6 buttons per page) */
    uint8_t menu_selection = (s_chord_selection == 17) ? 0 : s_chord_selection;
    uint8_t start_idx = (menu_selection / CHORD_BTNS_PER_PAGE) * CHORD_BTNS_PER_PAGE;
    uint8_t current_page = (uint8_t)(start_idx / CHORD_BTNS_PER_PAGE);

    /* Draw boxes only once per page */
    if (s_chord_page_drawn != current_page)
    {
        DrawAllChordButtonFrames();
        s_chord_page_drawn = current_page;
    }

    for (uint8_t slot = 0; slot < CHORD_BTNS_PER_PAGE; slot++)
    {
        uint8_t idx = start_idx + slot;
        if (idx >= 17) {
            break;
        }

        DrawChordButtonText(slot, idx, (idx == s_chord_selection), chord);
    }

}

static void DrawRoundedButton(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t fill, uint16_t border)
{
    /* Fill with clipped corners for a rounded look */
    ST7789_FillRect(x + 2, y,     w - 4, h,     fill);
    ST7789_FillRect(x,     y + 2, w,     h - 4, fill);

    /* Border ring */
    ST7789_FillRect(x + 2, y,         w - 4, 1, border);
    ST7789_FillRect(x + 2, y + h - 1, w - 4, 1, border);
    ST7789_FillRect(x,     y + 2,     1,     h - 4, border);
    ST7789_FillRect(x + w - 1, y + 2, 1,     h - 4, border);
    /* Corner pixels as 1x1 rects (avoids per-pixel SetAddressWindow overhead) */
    ST7789_DrawPixel(x + 1,     y + 1,     border);
    ST7789_DrawPixel(x + w - 2, y + 1,     border);
    ST7789_DrawPixel(x + 1,     y + h - 2, border);
    ST7789_DrawPixel(x + w - 2, y + h - 2, border);
}

static void GetChordButtonXY(uint8_t slot, uint16_t* x, uint16_t* y)
{
    uint8_t col = slot % CHORD_BTN_COLS;
    uint8_t row = slot / CHORD_BTN_COLS;
    *x = CHORD_BTN_X0 + col * (CHORD_BTN_W + CHORD_BTN_GAP_X);
    *y = CHORD_BTN_Y0 + row * (CHORD_BTN_H + CHORD_BTN_GAP_Y);
}

static void DrawAllChordButtonFrames(void)
{
    for (uint8_t slot = 0; slot < CHORD_BTNS_PER_PAGE; slot++)
    {
        DrawChordButtonFrame(slot);
    }
}

static void DrawChordButtonFrame(uint8_t slot)
{
    uint16_t x;
    uint16_t y;
    GetChordButtonXY(slot, &x, &y);
    DrawRoundedButton(x, y, CHORD_BTN_W, CHORD_BTN_H, COLOR_BOX_BG, COLOR_BOX_BORDER);
}

static void DrawChordButtonText(uint8_t slot, uint8_t chord_idx, uint8_t selected, const ChordParams* chord)
{
    uint16_t x;
    uint16_t y;
    const uint16_t text_col = selected ? COLOR_ACTIVE : COLOR_TEXT_MAIN;
    GetChordButtonXY(slot, &x, &y);

    /* Clear content area only; keep static button frame to avoid wipe-like redraws. */
    ST7789_FillRect((uint16_t)(x + 3), (uint16_t)(y + 3), (uint16_t)(CHORD_BTN_W - 6), (uint16_t)(CHORD_BTN_H - 6), COLOR_BOX_BG);

    if (chord_idx == 0) {
        ST7789_DrawString((uint16_t)(x + 14), (uint16_t)(y + 16), "Clear", &Font10x16, text_col, COLOR_BOX_BG);
    } else {
        char top[8];
        char bottom[10];
        uint8_t root = chord->root_key;
        snprintf(top, sizeof(top), "%s", s_root_keys[root]);
        snprintf(bottom, sizeof(bottom), "%s", s_chord_names[chord_idx]);

        ST7789_DrawString((uint16_t)(x + 8), (uint16_t)(y + 8), top, &Font10x16, COLOR_TEXT_SECONDARY, COLOR_BOX_BG);
        ST7789_DrawString((uint16_t)(x + 8), (uint16_t)(y + 26), bottom, &Font10x16, text_col, COLOR_BOX_BG);
    }
}

// static void DrawChordButton(uint8_t slot, uint8_t chord_idx, uint8_t selected, const ChordParams* chord)
// {
//     DrawChordButtonFrame(slot);
//     DrawChordButtonText(slot, chord_idx, selected, chord);
// }

static void DrawParamFooterButton(uint8_t slot, const char* label, uint8_t selected)
{
    uint16_t x = FOOTER_BTN_X0 + slot * (FOOTER_BTN_W + FOOTER_BTN_GAP_X);
    uint16_t y = FOOTER_BTN_Y0;
    uint16_t fill = selected ? COLOR_PANEL_ALT : COLOR_BOX_BG;
    uint16_t border = selected ? COLOR_ACTIVE : COLOR_BOX_BORDER;
    uint16_t text_col = selected ? WHITE : COLOR_TEXT_MAIN;

    DrawRoundedButton(x, y, FOOTER_BTN_W, FOOTER_BTN_H, fill, border);
    ST7789_DrawString(x + 8, y + 12, label, &Font10x16, text_col, fill);
}

static void DrawCenteredFooterButton(const char* label, uint8_t selected)
{
    uint16_t w = 150;
    uint16_t x = (SCREEN_W - w) / 2;
    uint16_t y = FOOTER_BTN_Y0;
    uint16_t fill = selected ? COLOR_PANEL_ALT : COLOR_BOX_BG;
    uint16_t border = selected ? COLOR_ACTIVE : COLOR_BOX_BORDER;
    uint16_t text_col = selected ? WHITE : COLOR_TEXT_MAIN;

    DrawRoundedButton(x, y, w, FOOTER_BTN_H, fill, border);
    ST7789_DrawString(x + 18, y + 12, label, &Font10x16, text_col, fill);
}

void UI_Display_NavigateChordMenu(int8_t delta, uint8_t step, const ChordParams* chord)
{
    uint8_t old = s_chord_selection;
    int16_t next = (int16_t)s_chord_selection + delta;

    while (next < 0)  next += 18;  // Now 18 options (0-16 chords + USER at 17)
    while (next > 17) next -= 18;
    s_chord_selection = (uint8_t)next;

    if (old == 17 || s_chord_selection == 17)
    {
        /* Moving to/from USER footer button — just update the footer highlight
         * and the previously selected chord button; no full redraw needed. */
        DrawCenteredFooterButton("USER CHORDS", (s_chord_selection == 17) ? 1 : 0);
        s_chord_footer_valid = 1;

        /* Deselect old button if it was a grid chord */
        uint8_t page_base = (uint8_t)(((old < 17 ? old : s_chord_selection) / CHORD_BTNS_PER_PAGE) * CHORD_BTNS_PER_PAGE);
        if (old < 17)
        {
            DrawChordButtonText((uint8_t)(old - page_base), old, 0, chord);
        }
        if (s_chord_selection < 17)
        {
            DrawChordButtonText((uint8_t)(s_chord_selection - page_base), s_chord_selection, 1, chord);
        }

        char title[20];
        char selected_label[24];
        snprintf(title, sizeof(title), "Step %u Chord", step);
        snprintf(selected_label, sizeof(selected_label), "Selected: USER");
        MenuTemplate_DrawHeader(title, selected_label, YELLOW);
        return;
    }

    if ((old / CHORD_BTNS_PER_PAGE) != (s_chord_selection / CHORD_BTNS_PER_PAGE))
    {
        /* Page changed: draw new page frames and text without full redraw */
        uint8_t new_page_base = (s_chord_selection / CHORD_BTNS_PER_PAGE) * CHORD_BTNS_PER_PAGE;
        
        DrawAllChordButtonFrames();
        s_chord_page_drawn = (uint8_t)(new_page_base / CHORD_BTNS_PER_PAGE);
        
        for (uint8_t slot = 0; slot < CHORD_BTNS_PER_PAGE; slot++)
        {
            uint8_t idx = new_page_base + slot;
            if (idx >= 17) break;
            DrawChordButtonText(slot, idx, (idx == s_chord_selection), chord);
        }
        
        char title[20];
        char selected_label[24];
        snprintf(title, sizeof(title), "Step %u Chord", step);
        snprintf(selected_label, sizeof(selected_label), "Selected: %s %s", s_root_keys[chord->root_key], s_chord_names[s_chord_selection]);
        MenuTemplate_DrawHeader(title, selected_label, YELLOW);
        return;
    }

    /* Same page: redraw only previous and current button */
    uint8_t page_base = (s_chord_selection / CHORD_BTNS_PER_PAGE) * CHORD_BTNS_PER_PAGE;
    
    if (old < 17)
    {
        DrawChordButtonText((uint8_t)(old - page_base), old, 0, chord);
    }
    
    DrawChordButtonText((uint8_t)(s_chord_selection - page_base), s_chord_selection, 1, chord);

    char title[20];
    char selected_label[24];
    snprintf(title, sizeof(title), "Step %u Chord", step);
    if (s_chord_selection == 0)
    {
        snprintf(selected_label, sizeof(selected_label), "Selected: Clear");
    }
    else if (s_chord_selection == 17)
    {
        snprintf(selected_label, sizeof(selected_label), "Selected: USER");
    }
    else
    {
        snprintf(selected_label, sizeof(selected_label), "Selected: %s %s", s_root_keys[chord->root_key], s_chord_names[s_chord_selection]);
    }
    MenuTemplate_DrawHeader(title, selected_label, YELLOW);
}

uint8_t UI_Display_GetSelectedChord(void)
{
    return s_chord_selection;
}

void UI_Display_SetChordSelection(uint8_t selection)
{
    if (selection <= 17)  /* Valid range 0-17 (17 = USER) */
        s_chord_selection = selection;
}

/* ── Chord Parameters ─────────────────────────────────────────────────────── */
static void DrawParamRow(uint8_t index, const ChordParams* chord, uint8_t is_selected)
{
    const char* param_names[] = {"Root Key:", "Type:", "Arp:", "Gate:", "Step Repeats:"};
    const uint16_t value_x = (uint16_t)(SCREEN_W - 56);
    const char* root_keys[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const char* chord_types[] = {"Clear", "Major", "Minor", "Dim", "Aug", "Sus4", "7th", "Maj7", "Min7", "Dim7", "7sus4", "6th", "Min6", "9th", "Maj9", "Min9", "11th"};
    const char* arp_patterns[] = {"Block", "Up", "Down", "UpDn", "Random"};
    const char* durations[] = {"16th", "8th", "Qtr", "8th."};

    uint8_t y = (uint8_t)(PARAM_ROW_Y0 + index * PARAM_ROW_STEP);
    uint16_t color = is_selected ? COLOR_ACTIVE : COLOR_TEXT_MAIN;

    /* Clear row */
    ST7789_FillRect(0, y, SCREEN_W, 22, COLOR_BG);

    /* Parameter name */
    ST7789_DrawString(4, y, param_names[index], &Font12x20, color, COLOR_BG);

    /* Parameter value */
    char value[12];
    switch (index) {
        case 0: snprintf(value, sizeof(value), "%s", root_keys[chord->root_key]); break;
        case 1: snprintf(value, sizeof(value), "%s", chord_types[chord->chord_type]); break;
        case 2: snprintf(value, sizeof(value), "%s", arp_patterns[chord->arp_pattern]); break;
        case 3: snprintf(value, sizeof(value), "%s", durations[chord->duration]); break;
        case 4: snprintf(value, sizeof(value), "%u", chord->loop_count); break;
        default: value[0] = 0; break;
    }
    ST7789_DrawString(value_x, y, value, &Font12x20, color, COLOR_BG);
}

void UI_Display_DrawChordParams(uint8_t step, const ChordParams* chord, uint8_t param_cursor)
{
    MenuTemplate_Begin(MENU_FRAME_PARAMS);

    if (s_param_row_cursor == 0xFF)
    {
        /* First draw: clear entire list and draw all rows */
        FillRegion(UI_REGION_MENU_LIST, COLOR_BG);
        for (uint8_t i = 0; i < 5; i++)
        {
            DrawParamRow(i, chord, (i == param_cursor));
        }
    }
    else if (s_param_row_cursor != param_cursor)
    {
        /* Cursor moved: redraw only old and new rows */
        DrawParamRow(s_param_row_cursor, chord, 0);
        DrawParamRow(param_cursor, chord, 1);
    }
    else
    {
        /* Cursor same: update values in place (value changed) */
        DrawParamRow(param_cursor, chord, 1);
    }

    s_param_row_cursor = param_cursor;

    /* Title */
    char title[20];
    snprintf(title, sizeof(title), "Step %u Parameters", step);
    MenuTemplate_DrawHeader(title, "Per-step Parameters", COLOR_TEXT_MUTED);

    /* Footer action buttons */
    if (!s_param_footer_valid)
    {
        FillRegion(UI_REGION_MENU_FOOTER, COLOR_PANEL);
        UI_Display_DrawParamFooterActions(s_param_footer_selection);
        s_param_footer_valid = 1;
    }
}

void UI_Display_NavigateChordParams(int8_t delta, uint8_t step, ChordParams* chord, uint8_t param_cursor)
{
    /* Adjust the current parameter */
    switch (param_cursor) {
        case 0: /* Root key */
        {
            int16_t next = (int16_t)chord->root_key + delta;
            while (next < 0) next += 12;
            while (next >= 12) next -= 12;
            chord->root_key = (uint8_t)next;
            break;
        }
        case 1: /* Chord type */
        {
            int16_t next = (int16_t)chord->chord_type + delta;
            while (next < 0) next += 17;
            while (next >= 17) next -= 17;
            chord->chord_type = (uint8_t)next;
            break;
        }
        case 2: /* Arp pattern */
        {
            int16_t next = (int16_t)chord->arp_pattern + delta;
            while (next < 0) next += 5;
            while (next >= 5) next -= 5;
            chord->arp_pattern = (uint8_t)next;
            break;
        }
        case 3: /* Duration */
        {
            int16_t next = (int16_t)chord->duration + delta;
            while (next < 0) next += 4;
            while (next >= 4) next -= 4;
            chord->duration = (uint8_t)next;
            break;
        }
        case 4: /* Pattern loop count */
        {
            int16_t next = (int16_t)chord->loop_count - 1 + delta;
            while (next < 0) next += 16;
            while (next >= 16) next -= 16;
            chord->loop_count = (uint8_t)(next + 1);
            break;
        }
        default:
            break;
    }

    /* Redraw parameters */
    UI_Display_DrawChordParams(step, chord, param_cursor);
}

void UI_Display_DrawParamFooterActions(uint8_t selected_action)
{
    static const char* labels[PARAM_ACTION_COUNT] = {"MAIN", "PREV", "NEXT", "SAVE"};
    FillRegion(UI_REGION_MENU_FOOTER, COLOR_PANEL);

    for (uint8_t i = 0; i < PARAM_ACTION_COUNT; i++)
    {
        DrawParamFooterButton(i, labels[i], i == selected_action);
    }
}

void UI_Display_NavigateParamFooterActions(int8_t delta, uint8_t step, const ChordParams* chord, uint8_t param_cursor)
{
    uint8_t old = s_param_footer_selection;
    int16_t next = (int16_t)s_param_footer_selection + delta;
    while (next < 0) next += PARAM_ACTION_COUNT;
    while (next >= PARAM_ACTION_COUNT) next -= PARAM_ACTION_COUNT;

    s_param_footer_selection = (uint8_t)next;

    DrawParamFooterButton(old, (old == PARAM_ACTION_MAIN) ? "1 MAIN" :
                               (old == PARAM_ACTION_PREV) ? "2 PREV" :
                               (old == PARAM_ACTION_NEXT) ? "3 NEXT" : "4 SAVE", 0);
    DrawParamFooterButton(s_param_footer_selection,
                          (s_param_footer_selection == PARAM_ACTION_MAIN) ? "1 MAIN" :
                          (s_param_footer_selection == PARAM_ACTION_PREV) ? "2 PREV" :
                          (s_param_footer_selection == PARAM_ACTION_NEXT) ? "3 NEXT" : "4 SAVE", 1);
}

uint8_t UI_Display_GetSelectedParamAction(void)
{
    return s_param_footer_selection;
}

void UI_Display_SetSelectedParamAction(uint8_t action)
{
    if (action < PARAM_ACTION_COUNT)
        s_param_footer_selection = action;
}

static void DrawTimingFooterButton(uint8_t slot, const char* label, uint8_t selected)
{
    uint16_t w = 92;
    uint16_t gap = 8;
    uint16_t total = (2 * w) + gap;
    uint16_t x0 = (SCREEN_W - total) / 2;
    uint16_t x = x0 + slot * (w + gap);
    uint16_t y = FOOTER_BTN_Y0;
    uint16_t fill = selected ? COLOR_PANEL_ALT : COLOR_BOX_BG;
    uint16_t border = selected ? COLOR_ACTIVE : COLOR_BOX_BORDER;
    uint16_t text_col = selected ? WHITE : COLOR_TEXT_MAIN;

    DrawRoundedButton(x, y, w, FOOTER_BTN_H, fill, border);
    ST7789_DrawString(x + 22, y + 12, label, &Font10x16, text_col, fill);
}

static void DrawTimingRow(uint8_t index, uint8_t step_count, uint8_t step_division, uint8_t ts_num, uint8_t ts_den, uint8_t swing, uint8_t is_selected)
{
    const uint16_t y_positions[] = {
        PARAM_ROW_Y0,
        PARAM_ROW_Y0 + PARAM_ROW_STEP,
        PARAM_ROW_Y0 + (PARAM_ROW_STEP * 2),
        PARAM_ROW_Y0 + (PARAM_ROW_STEP * 3),
        PARAM_ROW_Y0 + (PARAM_ROW_STEP * 4)
    };
    const uint16_t value_x = (uint16_t)(SCREEN_W - 56);
    uint16_t y = y_positions[index];
    uint16_t color = is_selected ? COLOR_ACTIVE : COLOR_TEXT_MAIN;

    /* Clear row */
    ST7789_FillRect(0, y, SCREEN_W, 22, COLOR_BG);

    const char* div_text = "1/16";
    switch (step_division)
    {
        case 1: div_text = "1/4"; break;
        case 2: div_text = "1/8"; break;
        case 3: div_text = "1/8T"; break;
        case 4: div_text = "1/16"; break;
        case 6: div_text = "1/16T"; break;
        case 8: div_text = "1/32"; break;
        default: break;
    }

    if (index == 0)
    {
        char b[12];
        snprintf(b, sizeof(b), "%u", step_count);
        ST7789_DrawString(4, y, "Pattern Steps", &Font12x20, color, COLOR_BG);
        ST7789_DrawString(value_x, y, b,      &Font12x20, color, COLOR_BG);
    }
    else if (index == 1)
    {
        ST7789_DrawString(4, y, "Step Grid",  &Font12x20, color, COLOR_BG);
        ST7789_DrawString(value_x, y, div_text, &Font12x20, color, COLOR_BG);
    }
    else if (index == 2)
    {
        char b[12];
        snprintf(b, sizeof(b), "%u", ts_num);
        ST7789_DrawString(4, y, "Beats Per Bar", &Font12x20, color, COLOR_BG);
        ST7789_DrawString(value_x, y, b,      &Font12x20, color, COLOR_BG);
    }
    else if (index == 3)
    {
        char b[12];
        snprintf(b, sizeof(b), "%u", ts_den);
        ST7789_DrawString(4, y, "Beat Unit",  &Font12x20, color, COLOR_BG);
        ST7789_DrawString(value_x, y, b,      &Font12x20, color, COLOR_BG);
    }
    else if (index == 4)
    {
        char b[12];
        snprintf(b, sizeof(b), "%u%%", swing);
        ST7789_DrawString(4, y, "Swing",      &Font12x20, color, COLOR_BG);
        ST7789_DrawString(value_x, y, b,      &Font12x20, color, COLOR_BG);
    }
}

void UI_Display_DrawTimingMenu(uint8_t step_count,
                               uint8_t step_division,
                               uint8_t ts_num,
                               uint8_t ts_den,
                               uint8_t swing,
                               uint8_t cursor,
                               uint8_t footer_action,
                               uint8_t has_unsaved_changes)
{
    MenuTemplate_Begin(MENU_FRAME_TIMING);

    if (s_timing_row_cursor == 0xFF)
    {
        /* First draw: clear entire list and draw all rows */
        FillRegion(UI_REGION_MENU_LIST, COLOR_BG);
        for (uint8_t i = 0; i < 5; i++)
        {
            DrawTimingRow(i, step_count, step_division, ts_num, ts_den, swing, (i == cursor));
        }
    }
    else if (s_timing_row_cursor != cursor)
    {
        /* Cursor moved: redraw only old and new rows */
        DrawTimingRow(s_timing_row_cursor, step_count, step_division, ts_num, ts_den, swing, 0);
        DrawTimingRow(cursor, step_count, step_division, ts_num, ts_den, swing, 1);
    }
    else
    {
        /* Cursor same: update values in place (value changed) */
        DrawTimingRow(cursor, step_count, step_division, ts_num, ts_den, swing, 1);
    }

    s_timing_row_cursor = cursor;

    MenuTemplate_DrawHeader("Pattern Timing", has_unsaved_changes ? "*UNSAVED" : "Global sequencer timing", YELLOW);

    /* Footer buttons */
    if (!s_timing_footer_valid || (s_timing_footer_drawn != footer_action))
    {
        FillRegion(UI_REGION_MENU_FOOTER, COLOR_PANEL);
        DrawTimingFooterButton(0, "MAIN", footer_action == 0);
        DrawTimingFooterButton(1, "SAVE", footer_action == 1);
        s_timing_footer_valid = 1;
        s_timing_footer_drawn = footer_action;
    }
}

void UI_Display_NavigateTimingFooter(int8_t delta)
{
    int16_t next = (int16_t)s_timing_footer_selection + delta;
    while (next < 0) next += 2;
    while (next >= 2) next -= 2;
    s_timing_footer_selection = (uint8_t)next;
}

uint8_t UI_Display_GetTimingFooterAction(void)
{
    return s_timing_footer_selection;
}

void UI_Display_SetTimingFooterAction(uint8_t action)
{
    if (action < 2) s_timing_footer_selection = action;
}

void UI_Display_DrawSongChainMenu(const uint8_t* chain,
                                  uint8_t length,
                                  uint8_t cursor,
                                  uint8_t playing_slot,
                                  uint8_t repeat_index,
                                  uint8_t repeat_total,
                                  uint8_t blink_on)
{
    MenuTemplate_Begin(MENU_FRAME_SONG);
    FillRegion(UI_REGION_MENU_LIST, COLOR_BG);

    if (length < 1) length = 1;
    if (length > 32) length = 32;
    if (cursor >= length) cursor = (uint8_t)(length - 1);

    uint8_t page_base = (uint8_t)((cursor / 4) * 4);
    for (uint8_t i = 0; i < 4; i++)
    {
        uint8_t pos = (uint8_t)(page_base + i);
        uint16_t y = (uint16_t)(SONG_ROW_Y0 + (i * SONG_ROW_STEP));
        uint8_t selected = (pos == cursor) ? 1 : 0;
        uint16_t color = selected ? COLOR_ACTIVE : COLOR_TEXT_MAIN;

        ST7789_FillRect(0, y, SCREEN_W, 22, COLOR_BG);

        if (pos < length)
        {
            char slot_label[20];
            char pat_label[16];
            snprintf(slot_label, sizeof(slot_label), "%u Slot %u", (unsigned)(i + 1), (unsigned)(pos + 1));
            snprintf(pat_label, sizeof(pat_label), "P%02u", (unsigned)(chain[pos] + 1));
            ST7789_DrawString(4, y, slot_label, &Font12x20, color, COLOR_BG);
            ST7789_DrawString((uint16_t)(SCREEN_W - 56), y, pat_label, &Font12x20, color, COLOR_BG);

            if (pos == playing_slot)
            {
                uint16_t outline = blink_on ? LIGHTBLUE : RGB565(12, 22, 36);
                DrawRectBorder(0, y, SCREEN_W, 22, outline);
            }
        }
        else
        {
            char slot_label[20];
            snprintf(slot_label, sizeof(slot_label), "%u Slot --", (unsigned)(i + 1));
            ST7789_DrawString(4, y, slot_label, &Font12x20, COLOR_TEXT_MUTED, COLOR_BG);
        }
    }

    {
        char indicator[32];
        if (repeat_total < 1) repeat_total = 1;
        if (repeat_index >= repeat_total) repeat_index = (uint8_t)(repeat_total - 1);
        snprintf(indicator, sizeof(indicator), "Len:%u  Rep:%u/%u", (unsigned)length,
                 (unsigned)(repeat_index + 1), (unsigned)repeat_total);
        MenuTemplate_DrawHeader("Song Chain", indicator, LIGHTBLUE);
    }

    FillRegion(UI_REGION_MENU_FOOTER, COLOR_PANEL);
    ST7789_DrawString(6, FOOTER_TEXT_Y1, "1-4 Slot", &Font10x16, COLOR_TEXT_MAIN, COLOR_PANEL);
    ST7789_DrawString(6, FOOTER_TEXT_Y2, "5 +Slot 6 -Slot", &Font10x16, COLOR_TEXT_MAIN, COLOR_PANEL);
    ST7789_DrawString(142, MENU_FOOTER_Y + 17, "REC MAIN", &Font10x16, COLOR_ACTIVE, COLOR_PANEL);
}

/* ── User Chord Menu ─────────────────────────────────────────────────────── */

static uint8_t s_user_chord_menu_selection = 0;  // 0=Create, 1=Load, 2=Name
static uint8_t s_user_chord_menu_frames_valid = 0;  // Cache flag for button frames
static uint8_t s_user_chord_menu_prev_selection = 0xFF;  // Track prev selection for text update
static uint8_t s_selected_piano_key = 0;         // Currently selected key (0-11)
static uint16_t s_piano_note_mask = 0;           // Currently selected notes
static char s_piano_header_title[24] = "Create Chord";
static uint8_t s_step_roll_step = 1;
typedef enum {
    PIANO_VIEW_KEYBOARD = 0,
    PIANO_VIEW_ROLL = 1
} PianoViewMode;
static PianoViewMode s_piano_view_mode = PIANO_VIEW_KEYBOARD;


static void DrawAllUserChordButtonFrames(void)
{
    /* Draw all 3 button frames once; content only updates on selection change */
    const uint16_t btn_w = 70;
    const uint16_t btn_h = 50;
    const uint16_t gap_x = 8;
    const uint16_t x0 = (uint16_t)((SCREEN_W - ((3 * btn_w) + (2 * gap_x))) / 2);
    const uint16_t y0 = CHORD_BTN_Y0 + 8;

    for (uint8_t i = 0; i < 3; i++)
    {
        uint16_t x = x0 + i * (btn_w + gap_x);
        uint16_t y = y0;
        DrawRoundedButton(x, y, btn_w, btn_h, COLOR_BOX_BG, COLOR_BOX_BORDER);
    }
}

static void DrawUserChordButtonText(uint8_t slot, uint8_t selected)
{
    /* Draw only text for a single button; frame persists from DrawAllUserChordButtonFrames() */
    const char* labels[] = {"MAKE", "LOAD", "NAME"};
    const uint16_t btn_w = 70;
    const uint16_t btn_h = 50;
    const uint16_t gap_x = 8;
    const uint16_t x0 = (uint16_t)((SCREEN_W - ((3 * btn_w) + (2 * gap_x))) / 2);
    const uint16_t y0 = CHORD_BTN_Y0 + 8;

    uint16_t x = x0 + slot * (btn_w + gap_x);
    uint16_t y = y0;
    uint16_t bg       = selected ? COLOR_ACTIVE     : COLOR_BOX_BG;
    uint16_t border   = selected ? YELLOW           : COLOR_BOX_BORDER;
    uint16_t text_col = selected ? WHITE            : COLOR_TEXT_MUTED;

    /* Redraw entire button with correct highlight state */
    DrawRoundedButton(x, y, btn_w, btn_h, bg, border);
    ST7789_DrawString((uint16_t)(x + (btn_w - 50) / 2), (uint16_t)(y + 18), labels[slot], &Font10x16, text_col, bg);
}

void UI_Display_DrawUserChordMenu(void)
{
    MenuTemplate_Begin(MENU_FRAME_USER_CHORD);

    /* Title / header */
    const char* title = "Custom Chord";
    MenuTemplate_DrawHeader(title, "USER CHORDS", YELLOW);

    /* Draw button frames + all text on first entry; frames persist after that */
    if (!s_user_chord_menu_frames_valid)
    {
        FillRegion(UI_REGION_MENU_LIST, COLOR_BG);
        DrawAllUserChordButtonFrames();
        /* Draw all 3 button texts immediately so none are blank */
        for (uint8_t i = 0; i < 3; i++)
        {
            DrawUserChordButtonText(i, (i == s_user_chord_menu_selection));
        }
        s_user_chord_menu_frames_valid = 1;
        s_user_chord_menu_prev_selection = s_user_chord_menu_selection;
    }
    else if (s_user_chord_menu_prev_selection != s_user_chord_menu_selection)
    {
        /* Only update the two changed buttons; frames and other buttons persist */
        DrawUserChordButtonText(s_user_chord_menu_prev_selection, 0);
        DrawUserChordButtonText(s_user_chord_menu_selection, 1);
        s_user_chord_menu_prev_selection = s_user_chord_menu_selection;
    }

    /* Footer */
    FillRegion(UI_REGION_MENU_FOOTER, COLOR_PANEL);
    DrawCenteredFooterButton(" MAIN STEPS", 1);
    s_chord_footer_valid = 1;
}

void UI_Display_NavigateUserChordMenu(int8_t delta)
{
    int16_t next = (int16_t)s_user_chord_menu_selection + delta;
    while (next < 0) next += 3;
    while (next >= 3) next -= 3;
    s_user_chord_menu_selection = (uint8_t)next;
    /* Text will update on next draw call when selection mismatch detected */
}

uint8_t UI_Display_GetUserChordMenuSelection(void)
{
    return s_user_chord_menu_selection;
}

void UI_Display_ResetUserChordMenuCache(void)
{
    s_user_chord_menu_frames_valid = 0;
    s_user_chord_menu_prev_selection = 0xFF;
    s_user_chord_menu_selection = 0;
}

/* ── Piano Keyboard for Chord Creation ──────────────────────────────────── */

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

/* White: C D E F G A B. Black: C# D# F# G# A# */
static const uint8_t k_piano_white_keys[PIANO_WHITE_COUNT] = {0, 2, 4, 5, 7, 9, 11};
static const uint8_t k_piano_black_keys[PIANO_BLACK_COUNT] = {1, 3, 6, 8, 10};
static const uint8_t k_piano_black_after_white_idx[PIANO_BLACK_COUNT] = {0, 1, 3, 4, 5};
static const char* k_note_names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char* k_note_labels_oct5[12] = {"C5", "C#5", "D5", "D#5", "E5", "F5", "F#5", "G5", "G#5", "A5", "A#5", "B5"};

#define ROLL_LABEL_X     0
#define ROLL_LABEL_W     34
#define ROLL_GRID_X      36
#define ROLL_HEADER_H    32
#define ROLL_FOOTER_H    48
#define ROLL_FOOTER_Y    (SCREEN_H - ROLL_FOOTER_H)
#define ROLL_GRID_Y      (ROLL_HEADER_H + 8)
#define ROLL_KEY_H       12
#define ROLL_ROW_H       13
#define ROLL_GRID_W      (SCREEN_W - ROLL_GRID_X - 4)
#define ROLL_BLOCK_X     (ROLL_GRID_X + 4)
#define ROLL_BLOCK_W     22

static uint8_t PianoRollIsBlackKey(uint8_t note)
{
    return (note == 1 || note == 3 || note == 6 || note == 8 || note == 10) ? 1u : 0u;
}

static uint16_t PianoRollLaneBg(uint8_t row)
{
    return (row & 1u) ? RGB565(54, 56, 60) : RGB565(46, 48, 52);
}

static void DrawPianoRollLabelKey(uint8_t note, uint16_t y, uint8_t selected)
{
    const uint8_t is_black_key = PianoRollIsBlackKey(note);
    const uint16_t base_bg = RGB565(18, 22, 28);
    const uint16_t white_key_bg = WHITE;
    const uint16_t white_key_fg = BLACK;
    const uint16_t black_key_bg = BLACK;
    const uint16_t black_key_fg = WHITE;
    const uint16_t focus_col = RGB565(184, 172, 142);
    const uint16_t border_col = is_black_key ? RGB565(78, 84, 92) : RGB565(152, 156, 160);

    uint16_t key_x = ROLL_LABEL_X;
    uint16_t key_w = ROLL_LABEL_W;
    uint16_t label_bg = white_key_bg;
    uint16_t label_fg = white_key_fg;

    ST7789_FillRect(ROLL_LABEL_X, y, ROLL_LABEL_W, ROLL_KEY_H, base_bg);

    if (is_black_key)
    {
        key_x = ROLL_LABEL_X + 9;
        key_w = (uint16_t)(ROLL_LABEL_W - 9);
        label_bg = black_key_bg;
        label_fg = black_key_fg;
    }

    ST7789_FillRect(key_x, y, key_w, ROLL_KEY_H, label_bg);
    ST7789_FillRect(key_x, y, key_w, 1, border_col);
    ST7789_FillRect(key_x, y + ROLL_KEY_H - 1, key_w, 1, border_col);

    if (!is_black_key)
    {
        ST7789_FillRect(key_x + key_w - 1, y, 1, ROLL_KEY_H, RGB565(188, 190, 194));
    }

    if (selected)
    {
        ST7789_FillRect(key_x, y, 2, ROLL_KEY_H, focus_col);
        ST7789_FillRect(key_x + key_w - 2, y, 2, ROLL_KEY_H, focus_col);
    }

    ST7789_DrawString((uint16_t)(key_x + 2), y + 1, k_note_labels_oct5[note], &Font8x12, label_fg, label_bg);
}

static void BuildStepPianoHeaderText(char* step_buf,
                                     uint8_t step_buf_len,
                                     char* chord_buf,
                                     uint8_t chord_buf_len)
{
    if (!step_buf || step_buf_len == 0 || !chord_buf || chord_buf_len == 0) return;

    snprintf(step_buf, step_buf_len, "STEP %u", s_step_roll_step);
    Bridge_GetStepChordDisplayName((uint8_t)(s_step_roll_step - 1), chord_buf, chord_buf_len);
}

static void DrawPianoRollRow(uint8_t note)
{
    uint8_t row = (uint8_t)(11 - note);
    uint16_t y = (uint16_t)(ROLL_GRID_Y + row * ROLL_ROW_H);
    uint8_t on = (s_piano_note_mask & (1U << note)) ? 1 : 0;
    uint8_t selected = (s_selected_piano_key == note);
    uint16_t lane_bg = PianoRollLaneBg(row);
    uint16_t focus_col = RGB565(184, 172, 142);

    DrawPianoRollLabelKey(note, y, selected);
    ST7789_FillRect(ROLL_GRID_X, y, ROLL_GRID_W, ROLL_KEY_H, lane_bg);
    ST7789_FillRect(ROLL_GRID_X, y + ROLL_KEY_H - 1, ROLL_GRID_W, 1, RGB565(64, 68, 72));

    if (selected)
    {
        ST7789_FillRect(ROLL_GRID_X, y, 2, ROLL_KEY_H, focus_col);
        ST7789_FillRect(ROLL_GRID_X + ROLL_GRID_W - 2, y, 2, ROLL_KEY_H, focus_col);
    }

    if (on)
    {
        ST7789_FillRect(ROLL_BLOCK_X, y + 2, ROLL_BLOCK_W, ROLL_KEY_H - 4, RGB565(118, 34, 34));
        ST7789_FillRect(ROLL_BLOCK_X, y + 2, 2, ROLL_KEY_H - 4, RGB565(162, 74, 66));
    }
}

static void DrawStepPianoRollHeader(void)
{
    char step_label[12];
    char chord_name[20];
    int16_t chord_x;
    int16_t chord_w;
    int16_t min_chord_x;

    BuildStepPianoHeaderText(step_label, sizeof(step_label), chord_name, sizeof(chord_name));
    chord_w = (int16_t)(strlen(chord_name) * Font10x16.width);
    min_chord_x = (int16_t)(8 + (strlen(step_label) * Font10x16.width) + 12);
    chord_x = (int16_t)(min_chord_x + ((SCREEN_W - min_chord_x - 4 - chord_w) / 2));
    if (chord_x < min_chord_x) chord_x = min_chord_x;

    ST7789_FillRect(0, 0, SCREEN_W, ROLL_HEADER_H, COLOR_PANEL_ALT);
    ST7789_FillRect(0, ROLL_HEADER_H - 1, SCREEN_W, 1, COLOR_BORDER_ACCENT);
    ST7789_DrawString(8, 8, step_label, &Font10x16, COLOR_TEXT_MAIN, COLOR_PANEL_ALT);
    ST7789_DrawString((uint16_t)chord_x, 8, chord_name, &Font10x16, COLOR_TEXT_MAIN, COLOR_PANEL_ALT);
}

static int8_t PianoWhiteSlotForNote(uint8_t note)
{
    for (uint8_t i = 0; i < PIANO_WHITE_COUNT; i++) if (k_piano_white_keys[i] == note) return (int8_t)i;
    return -1;
}

static int8_t PianoBlackSlotForNote(uint8_t note)
{
    for (uint8_t i = 0; i < PIANO_BLACK_COUNT; i++) if (k_piano_black_keys[i] == note) return (int8_t)i;
    return -1;
}

static void DrawPianoWhiteKey(uint8_t slot, uint8_t note)
{
    uint16_t x = PIANO_START_X + slot * (PIANO_KEY_W + PIANO_KEY_GAP);
    uint16_t y = PIANO_START_Y;
    uint8_t selected = (s_selected_piano_key == note);
    uint8_t on = (s_piano_note_mask & (1U << note)) ? 1 : 0;
    uint16_t fill = on ? RGB565(244, 244, 244) : WHITE;
    uint16_t border = selected ? YELLOW : COLOR_BOX_BORDER;
    uint8_t border_thickness = selected ? 3 : 1;

    ST7789_FillRect(x, y, PIANO_KEY_W, PIANO_KEY_H, fill);
    for (uint8_t t = 0; t < border_thickness; t++)
    {
        ST7789_FillRect(x + t, y + t, PIANO_KEY_W - (2 * t), 1, border);
        ST7789_FillRect(x + t, y + PIANO_KEY_H - 1 - t, PIANO_KEY_W - (2 * t), 1, border);
        ST7789_FillRect(x + t, y + t, 1, PIANO_KEY_H - (2 * t), border);
        ST7789_FillRect(x + PIANO_KEY_W - 1 - t, y + t, 1, PIANO_KEY_H - (2 * t), border);
    }

    if (selected)
    {
        ST7789_FillRect(x + (PIANO_KEY_W / 2) - 2, y + (PIANO_KEY_H / 2) - 2, 4, 4, BLACK);
    }
}

static void DrawPianoBlackKey(uint8_t slot, uint8_t note)
{
    uint16_t white_right = PIANO_START_X + (k_piano_black_after_white_idx[slot] + 1) * (PIANO_KEY_W + PIANO_KEY_GAP) - PIANO_KEY_GAP;
    uint16_t x = white_right - (PIANO_BLACK_W / 2);
    uint16_t y = PIANO_START_Y;
    uint8_t selected = (s_selected_piano_key == note);
    uint8_t on = (s_piano_note_mask & (1U << note)) ? 1 : 0;
    uint16_t fill = on ? RGB565(116, 32, 32) : BLACK;
    uint16_t border = selected ? YELLOW : GRAY;
    uint8_t border_thickness = selected ? 2 : 1;

    ST7789_FillRect(x, y, PIANO_BLACK_W, PIANO_BLACK_H, fill);
    for (uint8_t t = 0; t < border_thickness; t++)
    {
        ST7789_FillRect(x + t, y + t, PIANO_BLACK_W - (2 * t), 1, border);
        ST7789_FillRect(x + t, y + PIANO_BLACK_H - 1 - t, PIANO_BLACK_W - (2 * t), 1, border);
        ST7789_FillRect(x + t, y + t, 1, PIANO_BLACK_H - (2 * t), border);
        ST7789_FillRect(x + PIANO_BLACK_W - 1 - t, y + t, 1, PIANO_BLACK_H - (2 * t), border);
    }

    if (on)
    {
        ST7789_FillRect(x + (PIANO_BLACK_W / 2) - 3, y + (PIANO_BLACK_H / 2) - 3, 6, 6, YELLOW);
    }
}

static void DrawPianoKeyByNote(uint8_t note)
{
    int8_t white_slot = PianoWhiteSlotForNote(note);
    if (white_slot >= 0)
    {
        DrawPianoWhiteKey((uint8_t)white_slot, note);
        /* White keys sit below black keys, so redraw black overlay keys. */
        for (uint8_t i = 0; i < PIANO_BLACK_COUNT; i++)
        {
            DrawPianoBlackKey(i, k_piano_black_keys[i]);
        }
        return;
    }

    int8_t black_slot = PianoBlackSlotForNote(note);
    if (black_slot >= 0)
    {
        DrawPianoBlackKey((uint8_t)black_slot, note);
    }
}

static void DrawPianoKeyboardScreen(const char* title, const char* footer_label, const uint16_t note_mask, uint8_t selected_key)
{
    MenuTemplate_Begin(MENU_FRAME_CHORD);
    FillRegion(UI_REGION_MENU_LIST, COLOR_BG);

    ST7789_DrawString(4, 4, title, &Font12x20, COLOR_TEXT_MAIN, COLOR_PANEL_ALT);
    MenuTemplate_DrawHeader(title, k_note_names[(selected_key < 12) ? selected_key : 0], YELLOW);
    StrCopy(s_piano_header_title, sizeof(s_piano_header_title), title);

    s_piano_note_mask = note_mask;
    s_selected_piano_key = (selected_key < 12) ? selected_key : 0;

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
    s_chord_footer_valid = 1;
}

static void DrawStepPianoRollScreen(uint8_t step, const uint16_t note_mask, uint8_t selected_key)
{
    MenuTemplate_Begin(MENU_FRAME_CHORD);
    ST7789_FillRect(0, ROLL_HEADER_H, SCREEN_W, SCREEN_H - ROLL_HEADER_H, BLACK);

    s_piano_note_mask = note_mask;
    s_selected_piano_key = (selected_key < 12) ? selected_key : 0;
    s_step_roll_step = step;
    DrawStepPianoRollHeader();

    ST7789_FillRect(ROLL_GRID_X + (ROLL_GRID_W / 4), ROLL_GRID_Y, 1, 12 * ROLL_ROW_H, RGB565(64, 66, 70));
    ST7789_FillRect(ROLL_GRID_X + (ROLL_GRID_W / 2), ROLL_GRID_Y, 1, 12 * ROLL_ROW_H, RGB565(64, 66, 70));
    ST7789_FillRect(ROLL_GRID_X + ((ROLL_GRID_W * 3) / 4), ROLL_GRID_Y, 1, 12 * ROLL_ROW_H, RGB565(64, 66, 70));

    for (uint8_t note = 0; note < 12; note++)
    {
        DrawPianoRollRow(note);
    }

    ST7789_FillRect(0, ROLL_FOOTER_Y, SCREEN_W, ROLL_FOOTER_H, COLOR_PANEL);
    ST7789_DrawString(6, ROLL_FOOTER_Y + 12, "PLAY SAVE", &Font10x16, COLOR_ACTIVE, COLOR_PANEL);
    ST7789_DrawString(126, ROLL_FOOTER_Y + 12, "REC BACK", &Font10x16, COLOR_TEXT_MAIN, COLOR_PANEL);
    s_chord_footer_valid = 1;
}

void UI_Display_DrawPianoKeyboard(const uint16_t note_mask, uint8_t selected_key)
{
    s_piano_view_mode = PIANO_VIEW_KEYBOARD;
    DrawPianoKeyboardScreen("Create Chord", "SAVE/DONE", note_mask, selected_key);
}

void UI_Display_DrawStepPianoRoll(uint8_t step, const uint16_t note_mask, uint8_t selected_key)
{
    s_piano_view_mode = PIANO_VIEW_ROLL;
    DrawStepPianoRollScreen(step, note_mask, selected_key);
}

void UI_Display_NavigatePianoKeyboard(int8_t delta)
{
    uint8_t old = s_selected_piano_key;
    int16_t next = (int16_t)s_selected_piano_key + delta;
    while (next < 0) next += 12;
    while (next >= 12) next -= 12;
    s_selected_piano_key = (uint8_t)next;

    if (old != s_selected_piano_key)
    {
        if (s_piano_view_mode == PIANO_VIEW_ROLL)
        {
            DrawPianoRollRow(old);
            DrawPianoRollRow(s_selected_piano_key);
        }
        else
        {
            DrawPianoKeyByNote(old);
            DrawPianoKeyByNote(s_selected_piano_key);
        }
        if (s_piano_view_mode == PIANO_VIEW_ROLL)
            DrawStepPianoRollHeader();
        else
            MenuTemplate_DrawHeader(s_piano_header_title, k_note_names[s_selected_piano_key], YELLOW);
    }
}

uint8_t UI_Display_GetSelectedPianoKey(void)
{
    return s_selected_piano_key;
}

void UI_Display_TogglePianoKey(uint8_t key)
{
    if (key < 12)
    {
        s_piano_note_mask ^= (1U << key);
        if (s_piano_view_mode == PIANO_VIEW_ROLL)
            DrawPianoRollRow(key);
        else
            DrawPianoKeyByNote(key);

        if (s_piano_view_mode == PIANO_VIEW_KEYBOARD)
        {
            char chord_name[20];
            Bridge_FindChordName(s_piano_note_mask, chord_name, sizeof(chord_name));
            MenuTemplate_DrawHeader(s_piano_header_title, chord_name, YELLOW);
        }
    }
}

uint16_t UI_Display_GetCurrentNoteMask(void)
{
    return s_piano_note_mask;
}

void UI_Display_SetPianoNoteMask(uint16_t mask)
{
    s_piano_note_mask = mask;
}

void UI_Display_DrawUserChordNameEditor(const char* name, uint8_t cursor)
{
    MenuTemplate_Begin(MENU_FRAME_CHORD);
    FillRegion(UI_REGION_MENU_LIST, COLOR_BG);

    const char* title = "Name Chord";
    ST7789_DrawString(4, 4, title, &Font12x20, COLOR_TEXT_MAIN, COLOR_PANEL_ALT);
    MenuTemplate_DrawHeader(title, "EDIT NAME", YELLOW);

    ST7789_DrawString(10, 58, "Encoder: Char", &Font8x12, COLOR_TEXT_MUTED, COLOR_BG);
    ST7789_DrawString(10, 74, "Press: Next", &Font8x12, COLOR_TEXT_MUTED, COLOR_BG);

    uint16_t box_x = 8;
    uint16_t box_y = 98;
    uint16_t box_w = SCREEN_W - 16;
    uint16_t box_h = 34;
    DrawRoundedButton(box_x, box_y, box_w, box_h, COLOR_BOX_BG, COLOR_BOX_BORDER);

    ST7789_DrawString(16, 108, name, &Font10x16, COLOR_TEXT_MAIN, COLOR_BOX_BG);

    if (cursor < 16)
    {
        uint16_t cx = 16 + (uint16_t)cursor * 10;
        ST7789_FillRect(cx, 126, 9, 2, YELLOW);
    }

    FillRegion(UI_REGION_MENU_FOOTER, COLOR_PANEL);
    ST7789_DrawString(6, FOOTER_TEXT_Y1, "PLAY SAVE", &Font10x16, COLOR_ACTIVE, COLOR_PANEL);
    ST7789_DrawString(126, FOOTER_TEXT_Y1, "REC BACK", &Font10x16, COLOR_TEXT_MAIN, COLOR_PANEL);
    s_chord_footer_valid = 1;
}

/* ── User Chord Load ─────────────────────────────────────────────────────── */

#include "stm32/user_chord_bridge.h"

static uint8_t s_selected_user_chord = 0;

void UI_Display_DrawUserChordLoad(void)
{
    MenuTemplate_Begin(MENU_FRAME_CHORD);
    FillRegion(UI_REGION_MENU_LIST, COLOR_BG);

    const char* title = "Load Chord";
    ST7789_DrawString(4, 4, title, &Font12x20, COLOR_TEXT_MAIN, COLOR_PANEL_ALT);
    MenuTemplate_DrawHeader(title, "SELECT CHORD", YELLOW);

    /* Draw user chord list */
    uint8_t count = Bridge_UserChord_GetCount();
    uint16_t y = (uint16_t)(PARAM_ROW_Y0 + 4);
    
    for (uint8_t i = 0; i < count && i < 7; i++)
    {
        const UserChordInfo* chord = Bridge_UserChord_Get(i);
        if (!chord) continue;
        
        uint16_t text_col = (i == s_selected_user_chord) ? COLOR_ACTIVE : COLOR_TEXT_MAIN;
        
        char line[24];
        snprintf(line, sizeof(line), "%2u: %s", i + 1, chord->name);
        ST7789_DrawString(10, y, line, &Font10x16, text_col, COLOR_BG);
        
        y += 20;
    }

    if (count == 0)
    {
        ST7789_DrawString(10, 100, "No saved chords", &Font10x16, GRAY, COLOR_BG);
    }

    /* Footer */
    FillRegion(UI_REGION_MENU_FOOTER, COLOR_PANEL);
    ST7789_DrawString(6, FOOTER_TEXT_Y1, "PLAY LOAD", &Font10x16, COLOR_ACTIVE, COLOR_PANEL);
    ST7789_DrawString(126, FOOTER_TEXT_Y1, "REC BACK", &Font10x16, COLOR_TEXT_MAIN, COLOR_PANEL);
    ST7789_DrawString(6, FOOTER_TEXT_Y2, "SHIFT+REC DELETE", &Font10x16, ORANGE, COLOR_PANEL);
    s_chord_footer_valid = 1;
}

void UI_Display_NavigateUserChordLoad(int8_t delta)
{
    uint8_t count = Bridge_UserChord_GetCount();
    if (count == 0) return;
    
    int16_t next = (int16_t)s_selected_user_chord + delta;
    while (next < 0) next += count;
    while (next >= (int16_t)count) next -= count;
    s_selected_user_chord = (uint8_t)next;
}

uint8_t UI_Display_GetSelectedUserChord(void)
{
    return s_selected_user_chord;
}

void UI_Display_SetSelectedUserChord(uint8_t index)
{
    s_selected_user_chord = index;
}