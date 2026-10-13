#include "ui_display.h"
#include "ui_regions.h"
#include "ui_icons.h"
#include "lib/ST7789/st7789.h"
#include <stdio.h>
#include <string.h>
#include "s12_fonts.h"
#include "sequencer_bridge.h"
#include "screens/ui_screen_piano_roll.h"
#include "ui_render_scratch.h"
#include "ui_frame_job.h"
#include "stm32/time_commit_probe.h"
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
#define STATUS_TEXT_X      54u
#define STATUS_TEXT_Y      26u
#define STATUS_CHAR_W      8u
#define STATUS_CHAR_H      12u

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
/* RGB565(r,g,b) takes native 5-6-5 bit values (R:0-31, G:0-63, B:0-31).    */
/* COLOR8(r,g,b) accepts standard 8-bit values (0-255) and scales them.      */
#define COLOR8(r,g,b) RGB565((r)>>3, (g)>>2, (b)>>3)

#define COLOR_BG           RGB565(2, 4, 8)
#define COLOR_PANEL        RGB565(6, 10, 16)
#define COLOR_PANEL_ALT    RGB565(10, 16, 26)
#define COLOR_BOX_BG       BLACK
#define COLOR_BOX_BORDER   RGB565(18, 28, 42)
#define COLOR_BORDER_ACCENT RGB565(24, 40, 60)
#define COLOR_TEXT_MAIN    WHITE
#define COLOR_TEXT_SECONDARY LIGHTBLUE
#define COLOR_TEXT_MUTED   LGRAY
#define COLOR_ACCENT       COLOR8(40, 60, 63)
#define COLOR_ACTIVE       RGB565(16, 40, 63)
#define COLOR_SELECTED     RGB565(12, 50, 22)
#define COLOR_CHORD        RGB565(30, 12, 52)
#define COLOR_STATUS       RGB565(10, 16, 24)

#define COL_PLAY     COLOR8(12, 235, 5)   /* grass green */
#define COL_STOP     COLOR_TEXT_MAIN
#define COL_REC_ARM  COLOR8(235, 20, 5)     /* red */
#define COL_REC_OFF  COLOR_TEXT_MUTED
#define COL_SHIFT    YELLOW
#define COL_ACTIVE   COLOR_ACTIVE
#define COL_SELECTED COLOR_SELECTED
#define COL_CHORD    COLOR_CHORD
#define COL_NORMAL   WHITE
#define COL_SELECTED_EMPTY COLOR_TEXT_MUTED
#define COL_MODE_QUAN COLOR8(220, 55, 95)

/* ── Forward declarations ────────────────────────────────────────────────── */
static void DrawRectBorder(uint16_t x, uint16_t y,
                           uint16_t w, uint16_t h, uint16_t color);
void FillRegion(UiRect r, uint16_t color);
void MenuTemplate_Begin(uint8_t frame_id);
void MenuTemplate_DrawHeader(const char* title, const char* indicator, uint16_t indicator_color);
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
static void DrawSongChainRow(const uint8_t* chain,
                             uint8_t length,
                             uint8_t page_base,
                             uint8_t row,
                             uint8_t cursor,
                             uint8_t playing_slot,
                             uint8_t blink_on);
static void DrawUserChordLoadRow(uint8_t row,
                                 uint8_t count,
                                 uint8_t selected_index);
static void MainGridStripFillRect(uint8_t* buffer,
                                  uint16_t strip_y,
                                  uint16_t strip_h,
                                  uint16_t x,
                                  uint16_t y,
                                  uint16_t w,
                                  uint16_t h,
                                  uint16_t color);
static void MainGridStripDrawChar(uint8_t* buffer,
                                  uint16_t strip_y,
                                  uint16_t strip_h,
                                  uint16_t x,
                                  uint16_t y,
                                  char c,
                                  const Font_t* font,
                                  uint8_t scale,
                                  uint16_t fg,
                                  uint16_t bg);
static void MainGridStripDrawString(uint8_t* buffer,
                                    uint16_t strip_y,
                                    uint16_t strip_h,
                                    uint16_t x,
                                    uint16_t y,
                                    const char* str,
                                    const Font_t* font,
                                    uint8_t scale,
                                    uint16_t fg,
                                    uint16_t bg);
static void MainGridStripDrawBorderedRect(uint8_t* buffer,
                                          uint16_t strip_y,
                                          uint16_t strip_h,
                                          uint16_t x,
                                          uint16_t y,
                                          uint16_t w,
                                          uint16_t h,
                                          uint16_t fill,
                                          uint16_t border,
                                          uint8_t border_t);
/* Everything one Main Grid frame shows, captured once so all strips agree. */
typedef struct
{
    uint8_t selected_step;
    uint8_t active_step;
    uint8_t has_chord[12];
    uint8_t flash_enabled;
    uint8_t flash_on;
    UiMainMode main_mode;
    TransportState state;
    uint8_t rec_armed;
    uint16_t bpm;
    uint8_t pattern;
    uint8_t step;
    uint8_t current_bar;
    uint8_t current_substep;
    uint8_t div;
    uint8_t swing;
    uint8_t chain_len;
    uint32_t loops;
    uint32_t run_time_ms;
    char bpm_text[8];
    char status_text[32];
    char quant_text[12];
    char swing_text[12];
    char pattern_text[12];
} MainGridFrame;

static void MainGridComposeStrip(uint8_t* buffer,
                                 uint16_t strip_y,
                                 uint16_t strip_h,
                                 const MainGridFrame* frame);
static uint16_t MainGridBoxColourFor(uint8_t selected, uint8_t active, uint8_t has_chord,
                                     uint8_t flash_enabled, uint8_t flash_on);
static uint16_t MainGridBoxColour(uint8_t selected, uint8_t active, uint8_t has_chord);
static void MainGridComposeBox(uint8_t* buffer,
                               uint16_t strip_y,
                               uint16_t strip_h,
                               uint8_t step_no,
                               uint16_t colour);

enum {
    MENU_FRAME_NONE = 0,
    MENU_FRAME_CHORD,
    MENU_FRAME_USER_CHORD,
    MENU_FRAME_PARAMS,
    MENU_FRAME_TIMING,
    MENU_FRAME_QUANT_TIMING,
    MENU_FRAME_QUAN_ROUTER,
    MENU_FRAME_SONG
};

/* Status row cache shared with init so we can force redraw after menu exits */
static uint8_t  s_prev_pattern = 0xFF;
static uint8_t  s_prev_step = 0xFF;
static uint8_t  s_prev_substep = 0xFF;
static uint8_t  s_prev_bar = 0xFF;
static uint32_t s_prev_loops = 0xFFFFFFFF;
static uint32_t s_prev_mins = 0xFFFFFFFF;
static uint32_t s_prev_secs = 0xFFFFFFFF;
static char     s_prev_bar_digits[3] = {'0', '0', '\0'};
static char     s_prev_step_digits[3] = {'0', '0', '\0'};
static char     s_prev_timer_digits[10] = {'0', '0', ':', '0', '0', '.', '0', '0', '0', '\0'};
static uint8_t  s_force_status_redraw = 1;
static uint8_t  s_chord_footer_valid;
static uint8_t  s_param_footer_valid;
static uint8_t  s_timing_footer_valid;
static uint8_t  s_menu_frame = MENU_FRAME_NONE;
static uint8_t  s_menu_header_valid = 0;
static uint8_t  s_sidebar_valid = 0;
static uint8_t  s_prev_sidebar_div = 0xFF;
static uint8_t  s_prev_sidebar_swing = 0xFF;
static uint8_t  s_prev_sidebar_pattern = 0xFF;
static uint8_t  s_prev_sidebar_chain_len = 0xFF;
static char     s_menu_header_title[28];
static char     s_menu_header_indicator[28];
static uint16_t s_menu_header_indicator_color = COLOR_TEXT_MAIN;

/* ── Parameter menu row cache (prevent list flashing) ─────────────────────── */
static uint8_t  s_param_row_cursor = 0xFF;

/* ── Timing menu row cache (prevent list flashing) ──────────────────────── */
static uint8_t  s_timing_row_cursor = 0xFF;
static uint8_t  s_timing_prev_step_count = 0xFF;
static uint8_t  s_timing_prev_step_division = 0xFF;
static uint8_t  s_timing_prev_ts_num = 0xFF;
static uint8_t  s_timing_prev_ts_den = 0xFF;
static uint8_t  s_timing_prev_swing = 0xFF;
static uint8_t  s_timing_prev_unsaved = 0xFF;
static uint8_t  s_arp_prev_mode = 0xFF;
static uint8_t  s_arp_prev_rate = 0xFF;
static uint8_t  s_quan_router_prev_cursor = 0xFF;
static uint8_t  s_quan_router_prev_edit_field = 0xFF;
static uint8_t  s_quan_router_prev_unsaved = 0xFF;
static uint8_t  s_quan_router_prev_source[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static uint8_t  s_quan_router_prev_target[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static uint8_t  s_song_footer_valid = 0;
static uint8_t  s_song_page_valid = 0;
static uint8_t  s_song_prev_page_base = 0xFF;
static uint8_t  s_song_prev_cursor = 0xFF;
static uint8_t  s_song_prev_playing_slot = 0xFF;
static uint8_t  s_song_prev_blink_on = 0xFF;
static uint8_t  s_song_prev_length = 0xFF;
static uint8_t  s_song_prev_chain_page[4] = {0xFF, 0xFF, 0xFF, 0xFF};
static uint8_t  s_user_chord_load_valid = 0;
static uint8_t  s_user_chord_load_prev_selection = 0xFF;
static uint8_t  s_user_chord_load_prev_count = 0xFF;
static uint8_t  s_user_chord_load_footer_valid = 0;
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
static uint16_t s_step_cache_colour[12] = {0};

/* Composition target: strips span the full width; a single Bar box spans only itself. */
static uint16_t s_compose_x0 = 0u;
static uint16_t s_compose_w = SCREEN_W;

static UiFrameJob s_main_grid_job;
static MainGridFrame s_main_grid_frame;

typedef struct
{
    uint8_t step_count, division, numerator, denominator, swing, cursor, unsaved, rejected;
} TimingFrame;
static UiFrameJob s_timing_frame_job;
static TimingFrame s_timing_frame, s_timing_latest;
static uint8_t s_timing_followup;
#define TIMING_STRIP_H 8u
static uint8_t s_mcp_dbg_valid = 0;
static uint8_t s_mcp_dbg_online = 0xFF;
static uint8_t s_mcp_dbg_read_ok = 0xFF;
static uint8_t s_mcp_dbg_a = 0xFF;
static uint8_t s_mcp_dbg_b = 0xFF;
static uint8_t s_mcp_dbg_addr7 = 0xFF;
static uint8_t s_mcp_dbg_scan_mask = 0xFF;

#define MAIN_GRID_STRIP_H 16u
#define MAIN_GRID_STRIP_COUNT ((SCREEN_H + MAIN_GRID_STRIP_H - 1u) / MAIN_GRID_STRIP_H)

/* ── Init ────────────────────────────────────────────────────────────────── */
void UI_Display_Init(void)
{
    /* Fill GRAM to black BEFORE enabling display output.
     * The panel's default GRAM state is white; calling DisplayOn first
     * is what causes the visible white flash on boot.
     * Order must be: fill GRAM → DisplayOn → draw UI → BacklightSet(1). */
    ST7789_Fill_Color(BLACK);   /* overwrite GRAM while display output is still off */
    ST7789_DisplayOn();         /* panel now shows black */
    ST7789_FillRect(0, 0, SCREEN_W, HEADER_H, BLACK);
    ST7789_FillRect(0, HEADER_H, MAIN_W, (uint16_t)(SCREEN_H - HEADER_H), COLOR_BG);
    if (SIDEBAR_W != 0U)
    {
        ST7789_FillRect(SIDEBAR_X, HEADER_H, SIDEBAR_W, (uint16_t)(SCREEN_H - HEADER_H), COLOR_BG);
    }
    s_header_valid = 0;
    memset(s_step_cache_valid, 0, sizeof(s_step_cache_valid));
    UI_Display_DrawHeader(120, TRANSPORT_STOPPED, 0);
    s_force_status_redraw = 1;
    s_chord_footer_valid = 0;
    s_timing_footer_valid = 0;
    s_menu_frame = MENU_FRAME_NONE;
    s_menu_header_valid = 0;
    s_menu_header_title[0] = 0;
    s_menu_header_indicator[0] = 0;
    s_param_row_cursor = 0xFF;
    s_timing_row_cursor = 0xFF;
    s_timing_prev_step_count = 0xFF;
    s_timing_prev_step_division = 0xFF;
    s_timing_prev_ts_num = 0xFF;
    s_timing_prev_ts_den = 0xFF;
    s_timing_prev_swing = 0xFF;
    s_timing_prev_unsaved = 0xFF;
    s_arp_prev_mode = 0xFF;
    s_arp_prev_rate = 0xFF;
    s_song_footer_valid = 0;
    s_song_page_valid = 0;
    s_song_prev_page_base = 0xFF;
    s_song_prev_cursor = 0xFF;
    s_song_prev_playing_slot = 0xFF;
    s_song_prev_blink_on = 0xFF;
    s_song_prev_length = 0xFF;
    s_song_prev_chain_page[0] = 0xFF;
    s_song_prev_chain_page[1] = 0xFF;
    s_song_prev_chain_page[2] = 0xFF;
    s_song_prev_chain_page[3] = 0xFF;
    s_user_chord_load_valid = 0;
    s_user_chord_load_prev_selection = 0xFF;
    s_user_chord_load_prev_count = 0xFF;
    s_user_chord_load_footer_valid = 0;
    s_sidebar_valid = 0;
    s_prev_sidebar_div = 0xFF;
    s_prev_sidebar_swing = 0xFF;
    s_mcp_dbg_valid = 0;
    /* Don't draw grid here - caller will draw with chord info */
}

void UI_Display_FastReturnToGrid(void)
{
    /* Reset caches only. The main grid is now composed and pushed in larger strips. */
    s_header_valid = 0;
    memset(s_step_cache_valid, 0, sizeof(s_step_cache_valid));
    s_force_status_redraw = 1;

    s_prev_pattern = 0xFF;
    s_prev_step = 0xFF;
    s_prev_substep = 0xFF;
    s_prev_loops = 0xFFFFFFFF;
    s_prev_mins = 0xFFFFFFFF;
    s_prev_secs = 0xFFFFFFFF;
    s_prev_main_mode = (UiMainMode)0xFF;

    s_chord_footer_valid = 0;
    s_param_footer_valid = 0;
    s_timing_footer_valid = 0;
    s_menu_frame = MENU_FRAME_NONE;
    s_menu_header_valid = 0;
    s_menu_header_title[0] = 0;
    s_menu_header_indicator[0] = 0;
    s_param_row_cursor = 0xFF;
    s_timing_row_cursor = 0xFF;
    s_timing_prev_step_count = 0xFF;
    s_timing_prev_step_division = 0xFF;
    s_timing_prev_ts_num = 0xFF;
    s_timing_prev_ts_den = 0xFF;
    s_timing_prev_swing = 0xFF;
    s_timing_prev_unsaved = 0xFF;
    s_arp_prev_mode = 0xFF;
    s_arp_prev_rate = 0xFF;
    s_song_footer_valid = 0;
    s_song_page_valid = 0;
    s_song_prev_page_base = 0xFF;
    s_song_prev_cursor = 0xFF;
    s_song_prev_playing_slot = 0xFF;
    s_song_prev_blink_on = 0xFF;
    s_song_prev_length = 0xFF;
    s_song_prev_chain_page[0] = 0xFF;
    s_song_prev_chain_page[1] = 0xFF;
    s_song_prev_chain_page[2] = 0xFF;
    s_song_prev_chain_page[3] = 0xFF;
    s_user_chord_load_valid = 0;
    s_user_chord_load_prev_selection = 0xFF;
    s_user_chord_load_prev_count = 0xFF;
    s_user_chord_load_footer_valid = 0;

    s_sidebar_valid = 0;
    s_prev_sidebar_div = 0xFF;
    s_prev_sidebar_swing = 0xFF;
    s_mcp_dbg_valid = 0;
}

void UI_Display_BeginMainGridFrame(uint8_t selected_step,
                                   uint8_t active_step,
                                   const uint8_t* has_chord_flags,
                                   uint16_t bpm,
                                   TransportState state,
                                   uint8_t rec_armed,
                                   uint8_t pattern,
                                   uint8_t step,
                                   uint32_t loops,
                                   uint32_t run_time_ms)
{
    MainGridFrame* f = &s_main_grid_frame;
    uint8_t step_total = Bridge_GetStepLedgerLength(step);
    if (step_total == 0u) step_total = 1u;

    f->selected_step = selected_step;
    f->active_step = active_step;
    for (uint8_t i = 0u; i < 12u; ++i)
    {
        f->has_chord[i] = has_chord_flags ? has_chord_flags[i] : 0u;
    }
    f->flash_enabled = s_repeat_flash_enabled;
    f->flash_on = s_repeat_flash_on;
    f->main_mode = s_main_mode;
    f->state = state;
    f->rec_armed = rec_armed;
    f->bpm = bpm;
    f->pattern = pattern;
    f->step = step;
    f->current_bar = (uint8_t)(step + 1u);
    f->current_substep = (uint8_t)(Bridge_GetCurrentStepSubIndex() + 1u);
    f->div = Bridge_GetPatternStepDivision();
    f->swing = Bridge_GetSwing();
    f->chain_len = Bridge_GetChainLength();
    f->loops = loops;
    f->run_time_ms = run_time_ms;

    snprintf(f->bpm_text, sizeof(f->bpm_text), "%3u", bpm);
    snprintf(f->status_text, sizeof(f->status_text), "%02u/%02u S%02u/%02u %02lu:%02lu.%03lu",
             (unsigned)f->current_bar,
             (unsigned)Bridge_GetPatternStepCount(),
             (unsigned)f->current_substep,
             (unsigned)step_total,
             (unsigned long)(run_time_ms / 60000u),
             (unsigned long)((run_time_ms % 60000u) / 1000u),
             (unsigned long)(run_time_ms % 1000u));
    snprintf(f->quant_text, sizeof(f->quant_text), "1/%u", (unsigned)(f->div * 4u));
    snprintf(f->swing_text, sizeof(f->swing_text), "%u%%", (unsigned)f->swing);
    snprintf(f->pattern_text, sizeof(f->pattern_text), "%02u/%02u",
             (unsigned)(pattern + 1u),
             (unsigned)((f->chain_len < 1u) ? 1u : f->chain_len));

    TimeCommitProbe_RecordGridFull();
    TimeCommitProbe_FrameBegin(UiFrameJob_Begin(&s_main_grid_job, (uint8_t)MAIN_GRID_STRIP_COUNT));
}

static void MainGridFinalizeFrame(void)
{
    const MainGridFrame* f = &s_main_grid_frame;
    const uint32_t ms = f->run_time_ms;
    char timer_digits[10];

    s_prev_header_bpm = f->bpm;
    s_prev_header_state = f->state;
    s_prev_header_rec = f->rec_armed;
    s_header_valid = 1;

    s_prev_pattern = f->pattern;
    s_prev_step = f->step;
    s_prev_bar = f->current_bar;
    s_prev_substep = f->current_substep;
    s_prev_loops = f->loops;
    s_prev_mins = ms / 60000u;
    s_prev_secs = (ms % 60000u) / 1000u;
    s_prev_main_mode = f->main_mode;
    s_force_status_redraw = 0;
    snprintf(s_prev_bar_digits, sizeof(s_prev_bar_digits), "%02u", (unsigned)f->current_bar);
    snprintf(s_prev_step_digits, sizeof(s_prev_step_digits), "%02u", (unsigned)f->current_substep);
    snprintf(timer_digits, sizeof(timer_digits), "%02lu:%02lu.%03lu",
             (unsigned long)(ms / 60000u),
             (unsigned long)((ms % 60000u) / 1000u),
             (unsigned long)(ms % 1000u));
    memcpy(s_prev_timer_digits, timer_digits, sizeof(s_prev_timer_digits));

    s_sidebar_valid = 1;
    s_prev_sidebar_div = f->div;
    s_prev_sidebar_swing = f->swing;
    s_prev_sidebar_pattern = f->pattern;
    s_prev_sidebar_chain_len = f->chain_len;

    for (uint8_t i = 0; i < 12; i++)
    {
        s_step_cache_colour[i] = MainGridBoxColourFor(((i + 1u) == f->selected_step) ? 1u : 0u,
                                                      ((i + 1u) == f->active_step) ? 1u : 0u,
                                                      f->has_chord[i],
                                                      f->flash_enabled,
                                                      f->flash_on);
        s_step_cache_valid[i] = 1u;
    }
}

uint8_t UI_Display_ServiceMainGridFrame(void)
{
    uint8_t strip = 0u;
    if (!UiFrameJob_NextStrip(&s_main_grid_job, &strip)) return 0u;

    uint8_t* grid_strip_buf = UI_RenderScratch_Acquire(UI_RENDER_SCRATCH_OWNER_MAIN_GRID);
    if (!grid_strip_buf) return 0u;

    const uint16_t strip_y = (uint16_t)(strip * MAIN_GRID_STRIP_H);
    uint16_t strip_h = MAIN_GRID_STRIP_H;
    if ((uint16_t)(strip_y + strip_h) > SCREEN_H)
    {
        strip_h = (uint16_t)(SCREEN_H - strip_y);
    }

    TimeCommitProbe_FrameStripBegin();
    MainGridComposeStrip(grid_strip_buf, strip_y, strip_h, &s_main_grid_frame);
    ST7789_DrawRGB565Buffer(0,
                            strip_y,
                            SCREEN_W,
                            strip_h,
                            grid_strip_buf,
                            (uint32_t)SCREEN_W * (uint32_t)strip_h * 2u);
    UI_RenderScratch_Release(UI_RENDER_SCRATCH_OWNER_MAIN_GRID);
    TimeCommitProbe_FrameStripEnd();

    if (!UiFrameJob_CompleteStrip(&s_main_grid_job)) return 0u;

    MainGridFinalizeFrame();
    TimeCommitProbe_FrameComplete();
    return 1u;
}

uint8_t UI_Display_IsMainGridFrameActive(void)
{
    return s_main_grid_job.active;
}

void UI_Display_CancelMainGridFrame(void)
{
    if (s_main_grid_job.active) TimeCommitProbe_FrameCancel();
    UiFrameJob_Cancel(&s_main_grid_job);
}

uint8_t UI_Display_TakeMainGridDeferred(void)
{
    return UiFrameJob_TakeDeferred(&s_main_grid_job);
}

static uint8_t MainGridDefer(uint8_t bits)
{
    if (!UiFrameJob_Defer(&s_main_grid_job, bits)) return 0u;
    TimeCommitProbe_FrameDeferred();
    return 1u;
}

/* ── Header ──────────────────────────────────────────────────────────────── */
void UI_Display_DrawHeader(uint16_t bpm, TransportState state, uint8_t rec_armed)
{
    if (MainGridDefer(UI_FRAME_DEFER_HEADER)) return;

    if (s_header_valid &&
        s_prev_header_bpm == bpm &&
        s_prev_header_state == state &&
        s_prev_header_rec == rec_armed) {
        return;
    }

    if (!s_header_valid)
    {
        ST7789_FillRect(0, 0, SCREEN_W, HEADER_H, BLACK);
    }

    if (!s_header_valid || s_prev_header_bpm != bpm)
    {
        char bpm_str[8];
        ST7789_FillRect(0, 0, 120, HEADER_H, BLACK);
        snprintf(bpm_str, sizeof(bpm_str), "%3u", bpm);
        ST7789_DrawString(4, 0, bpm_str, &Font16x24, COLOR_TEXT_MAIN, BLACK);
        ST7789_DrawString(52, 2, "BPM", &Font16x24, COLOR_TEXT_MAIN, BLACK);
    }

    if (!s_header_valid || s_prev_header_state != state)
    {
        char transport_icon[2] = {
            (state == TRANSPORT_PLAYING || state == TRANSPORT_PLAYING_RECORDING)
                ? ICON_PLAY : ICON_STOP,
            0
        };
        uint16_t transport_col =
            (state == TRANSPORT_PLAYING || state == TRANSPORT_PLAYING_RECORDING)
                ? COL_PLAY : COL_STOP;

        ST7789_FillRect(HEADER_TRANSPORT_X, 0, 24, HEADER_H, BLACK);
        ST7789_DrawString(HEADER_TRANSPORT_X, 0, transport_icon, &Font16x24, transport_col, BLACK);
    }

    if (!s_header_valid || s_prev_header_rec != rec_armed)
    {
        char rec_icon[2] = {ICON_REC, 0};
        uint16_t rec_col = rec_armed ? COL_REC_ARM : COL_REC_OFF;
        ST7789_FillRect(HEADER_REC_X, 0, 24, HEADER_H, BLACK);
        ST7789_DrawString(HEADER_REC_X, 0, rec_icon, &Font16x24, rec_col, BLACK);
    }

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

_Static_assert((uint32_t)BOX_W * BOX_H * 2u <= UI_RENDER_SCRATCH_BYTES, "Bar box must fit render scratch");

void UI_Display_DrawStepBox(uint8_t step, uint8_t selected, uint8_t active, uint8_t has_chord)
{
    if (step < 1u || step > 12u) return;
    if (MainGridDefer(UI_FRAME_DEFER_BOXES)) return;

    const uint8_t index = (uint8_t)(step - 1u);
    const uint16_t colour = MainGridBoxColour(selected, active, has_chord);
    if (s_step_cache_valid[index] && s_step_cache_colour[index] == colour) return;

    const uint16_t x = (uint16_t)(GRID_X + (index % 4u) * (BOX_W + GAP_X));
    const uint16_t y = (uint16_t)(GRID_Y + (index / 4u) * (BOX_H + GAP_Y));
    uint8_t* box_buf = UI_RenderScratch_Acquire(UI_RENDER_SCRATCH_OWNER_MAIN_GRID);
    if (!box_buf) return;

    /* Same compositor as the full Main Grid, so both paths produce identical pixels. */
    s_compose_x0 = x;
    s_compose_w = BOX_W;
    MainGridComposeBox(box_buf, y, BOX_H, step, colour);
    s_compose_x0 = 0u;
    s_compose_w = SCREEN_W;

    ST7789_DrawRGB565Buffer(x, y, BOX_W, BOX_H, box_buf, (uint32_t)BOX_W * BOX_H * 2u);
    UI_RenderScratch_Release(UI_RENDER_SCRATCH_OWNER_MAIN_GRID);

    s_step_cache_colour[index] = colour;
    s_step_cache_valid[index] = 1u;
    TimeCommitProbe_RecordGridBox();
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
    if (s_menu_frame != MENU_FRAME_NONE) return;
    if (s_main_grid_job.active) return;
    ST7789_FillRect(0, 47, MAIN_W, 1, COLOR_BG);
}

static void DrawSidebarInfo(void)
{
    if (SIDEBAR_W == 0U) return;
    if (s_menu_frame != MENU_FRAME_NONE) return;  /* don't paint sidebar over any menu */

    uint8_t div   = Bridge_GetPatternStepDivision();
    uint8_t swing = Bridge_GetSwing();
    uint8_t pattern = Bridge_GetCurrentPattern();
    uint8_t chain_len = Bridge_GetChainLength();

    if (s_sidebar_valid && div == s_prev_sidebar_div && swing == s_prev_sidebar_swing &&
        pattern == s_prev_sidebar_pattern && chain_len == s_prev_sidebar_chain_len) return;

    const uint16_t panel_bg   = COLOR_BG;
 

    /* Fill only below the header+status band so we don't bleed into it */
    ST7789_FillRect(SIDEBAR_X, 44, SIDEBAR_W, SCREEN_H - 44U, panel_bg);

    {
        char q[12];
        const char* div_text = "1/16";
        switch (div)
        {
            case 1: div_text = "1/4"; break;
            case 2: div_text = "1/8"; break;
            case 4: div_text = "1/16"; break;
            case 8: div_text = "1/32"; break;
            default: break;
        }
        snprintf(q, sizeof(q), "%s", div_text);
        ST7789_DrawString((uint16_t)(SIDEBAR_X + 8), 52, "QUANT", &Font8x12, LIGHTBLUE, panel_bg);
        ST7789_DrawString((uint16_t)(SIDEBAR_X + 8), 66, q, &Font12x20, WHITE, panel_bg);

        snprintf(q, sizeof(q), "%u%%", (unsigned)swing);
        ST7789_DrawString((uint16_t)(SIDEBAR_X + 8), 100, "SWING", &Font8x12, LIGHTBLUE, panel_bg);
        ST7789_DrawString((uint16_t)(SIDEBAR_X + 8), 116, q, &Font12x20, WHITE, panel_bg);

        /* Second number is chain.length, display-only at this stage. */
        snprintf(q, sizeof(q), "%02u/%02u",
                 (unsigned)(pattern + 1u),
                 (unsigned)((chain_len < 1u) ? 1u : chain_len));
        ST7789_DrawString((uint16_t)(SIDEBAR_X + 8), 148, "PATTERN", &Font8x12, LIGHTBLUE, panel_bg);
        ST7789_DrawString((uint16_t)(SIDEBAR_X + 8), 164, q, &Font12x20, WHITE, panel_bg);
    }

    s_prev_sidebar_div       = div;
    s_prev_sidebar_swing     = swing;
    s_prev_sidebar_pattern   = pattern;
    s_prev_sidebar_chain_len = chain_len;
    s_sidebar_valid          = 1;
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

static void MainGridStripFillRect(uint8_t* buffer,
                                  uint16_t strip_y,
                                  uint16_t strip_h,
                                  uint16_t x,
                                  uint16_t y,
                                  uint16_t w,
                                  uint16_t h,
                                  uint16_t color)
{
    if (buffer == NULL) return;
    if (x >= SCREEN_W || y >= SCREEN_H || w == 0u || h == 0u) return;

    uint16_t y0 = (y > strip_y) ? y : strip_y;
    uint16_t y1 = ((uint32_t)y + h < (uint32_t)strip_y + strip_h) ? (uint16_t)(y + h) : (uint16_t)(strip_y + strip_h);
    if (y0 >= y1) return;

    const uint32_t target_x1 = (uint32_t)s_compose_x0 + s_compose_w;
    const uint16_t x0 = (x > s_compose_x0) ? x : s_compose_x0;
    const uint32_t x1 = ((uint32_t)x + w < target_x1) ? (uint32_t)x + w : target_x1;
    if ((uint32_t)x0 >= x1) return;
    w = (uint16_t)(x1 - x0);

    uint8_t hi = (uint8_t)(color >> 8);
    uint8_t lo = (uint8_t)(color & 0xFF);

    for (uint16_t py = y0; py < y1; py++)
    {
        uint32_t row_off = (uint32_t)(py - strip_y) * s_compose_w * 2u;
        uint32_t px_off = row_off + (uint32_t)(x0 - s_compose_x0) * 2u;
        for (uint16_t px = 0; px < w; px++)
        {
            buffer[px_off + px * 2u] = hi;
            buffer[px_off + px * 2u + 1u] = lo;
        }
    }
}

static void MainGridStripDrawBorderedRect(uint8_t* buffer,
                                          uint16_t strip_y,
                                          uint16_t strip_h,
                                          uint16_t x,
                                          uint16_t y,
                                          uint16_t w,
                                          uint16_t h,
                                          uint16_t fill,
                                          uint16_t border,
                                          uint8_t border_t)
{
    MainGridStripFillRect(buffer, strip_y, strip_h, x, y, w, h, fill);

    for (uint8_t t = 0; t < border_t; t++)
    {
        uint16_t ox = (uint16_t)(x + t);
        uint16_t oy = (uint16_t)(y + t);
        uint16_t ow = (uint16_t)(w - 2u * t);
        uint16_t oh = (uint16_t)(h - 2u * t);
        if (ow == 0u || oh == 0u) break;

        MainGridStripFillRect(buffer, strip_y, strip_h, ox, oy, ow, 1u, border);
        MainGridStripFillRect(buffer, strip_y, strip_h, ox, (uint16_t)(oy + oh - 1u), ow, 1u, border);
        MainGridStripFillRect(buffer, strip_y, strip_h, ox, oy, 1u, oh, border);
        MainGridStripFillRect(buffer, strip_y, strip_h, (uint16_t)(ox + ow - 1u), oy, 1u, oh, border);
    }
}

static void MainGridStripDrawChar(uint8_t* buffer,
                                  uint16_t strip_y,
                                  uint16_t strip_h,
                                  uint16_t x,
                                  uint16_t y,
                                  char c,
                                  const Font_t* font,
                                  uint8_t scale,
                                  uint16_t fg,
                                  uint16_t bg)
{
    if (buffer == NULL || font == NULL || scale == 0u) return;

    if ((uint8_t)c < font->first || (uint8_t)c > font->last) c = ' ';

    uint16_t srcW = font->width;
    uint16_t srcH = font->height;
    uint16_t bytesPerRow = (uint16_t)((srcW + 7u) / 8u);
    const uint8_t* charData = font->data + (((uint8_t)c - font->first) * srcH * bytesPerRow);
    const uint16_t target_x0 = s_compose_x0;
    const uint32_t target_x1 = (uint32_t)s_compose_x0 + s_compose_w;
    const uint32_t row_stride = (uint32_t)s_compose_w * 2u;

    for (uint16_t srcRow = 0; srcRow < srcH; srcRow++)
    {
        for (uint8_t sy = 0; sy < scale; sy++)
        {
            uint16_t py = (uint16_t)(y + srcRow * scale + sy);
            if (py < strip_y || py >= (uint16_t)(strip_y + strip_h)) continue;

            for (uint16_t srcCol = 0; srcCol < srcW; srcCol++)
            {
                uint8_t byteIndex = (uint8_t)(srcCol / 8u);
                uint8_t bitMask = (uint8_t)(0x80u >> (srcCol % 8u));
                uint16_t colour = (charData[srcRow * bytesPerRow + byteIndex] & bitMask) ? fg : bg;
                uint8_t hi = (uint8_t)(colour >> 8);
                uint8_t lo = (uint8_t)(colour & 0xFF);

                for (uint8_t sx = 0; sx < scale; sx++)
                {
                    uint16_t px = (uint16_t)(x + srcCol * scale + sx);
                    if (px < target_x0 || px >= target_x1) continue;
                    uint32_t off = (uint32_t)(py - strip_y) * row_stride + (uint32_t)(px - target_x0) * 2u;
                    buffer[off] = hi;
                    buffer[off + 1u] = lo;
                }
            }
        }
    }
}

static void MainGridStripDrawString(uint8_t* buffer,
                                    uint16_t strip_y,
                                    uint16_t strip_h,
                                    uint16_t x,
                                    uint16_t y,
                                    const char* str,
                                    const Font_t* font,
                                    uint8_t scale,
                                    uint16_t fg,
                                    uint16_t bg)
{
    if (buffer == NULL || str == NULL || font == NULL || scale == 0u) return;

    uint16_t stepX = (uint16_t)(font->width * scale);
    while (*str)
    {
        MainGridStripDrawChar(buffer, strip_y, strip_h, x, y, *str, font, scale, fg, bg);
        x = (uint16_t)(x + stepX);
        str++;
    }
}

static void MainGridComposeStrip(uint8_t* buffer,
                                 uint16_t strip_y,
                                 uint16_t strip_h,
                                 const MainGridFrame* f)
{
    MainGridStripFillRect(buffer, strip_y, strip_h, 0u, strip_y, SCREEN_W, strip_h, COLOR_BG);
    MainGridStripFillRect(buffer, strip_y, strip_h, 0u, 0u, SCREEN_W, HEADER_H, BLACK);

    MainGridStripDrawString(buffer, strip_y, strip_h, 4u, 2u, f->bpm_text, &Font8x12, 2u, COLOR_TEXT_MAIN, BLACK);
    MainGridStripDrawString(buffer, strip_y, strip_h, 52u, 2u, "BPM", &Font16x24, 1u, COLOR_TEXT_MAIN, BLACK);

    {
        const TransportState state = f->state;
        char transport_icon[2] = {
            (state == TRANSPORT_PLAYING || state == TRANSPORT_PLAYING_RECORDING) ? ICON_PLAY : ICON_STOP,
            0
        };
        uint16_t transport_col =
            (state == TRANSPORT_PLAYING || state == TRANSPORT_PLAYING_RECORDING) ? COL_PLAY : COL_STOP;
        char rec_icon[2] = {ICON_REC, 0};
        uint16_t rec_col = f->rec_armed ? COL_REC_ARM : COL_REC_OFF;

        MainGridStripDrawString(buffer, strip_y, strip_h, HEADER_TRANSPORT_X, 0u, transport_icon, &Font16x24, 1u, transport_col, BLACK);
        MainGridStripDrawString(buffer, strip_y, strip_h, HEADER_REC_X, 0u, rec_icon, &Font16x24, 1u, rec_col, BLACK);
    }

    {
        uint16_t fill = WHITE;
        uint16_t border = COLOR_BOX_BORDER;
        uint16_t text_col = BLACK;
        const char* label = "BAR";

        if (f->main_mode == UI_MAIN_MODE_CHORD)
        {
            fill = COLOR8(70, 140, 255);
            text_col = WHITE;
            label = "CHRD";
        }
        else if (f->main_mode == UI_MAIN_MODE_TIMING)
        {
            fill = ORANGE;
            text_col = WHITE;
            label = "TIME";
        }
        else if (f->main_mode == UI_MAIN_MODE_PATTERN)
        {
            fill = COLOR8(0, 135, 0);
            text_col = WHITE;
            label = "PAT";
        }
        else if (f->main_mode == UI_MAIN_MODE_QUAN)
        {
            fill = COL_MODE_QUAN;
            text_col = WHITE;
            label = "QUAN";
        }

        MainGridStripDrawBorderedRect(buffer, strip_y, strip_h, 4u, 25u, 44u, 18u, fill, border, BORDER_T);
        MainGridStripDrawString(buffer, strip_y, strip_h, 8u, 28u, label, &Font8x12, 1u, text_col, fill);
    }

    MainGridStripDrawString(buffer, strip_y, strip_h, 54u, 26u, f->status_text, &Font8x12, 1u, COLOR_TEXT_MAIN, COLOR_BG);

    if (SIDEBAR_W != 0U)
    {
        MainGridStripDrawString(buffer, strip_y, strip_h, (uint16_t)(SIDEBAR_X + 8u), 52u, "QUANT", &Font8x12, 1u, LIGHTBLUE, COLOR_BG);
        MainGridStripDrawString(buffer, strip_y, strip_h, (uint16_t)(SIDEBAR_X + 8u), 66u, f->quant_text, &Font12x20, 1u, WHITE, COLOR_BG);
        MainGridStripDrawString(buffer, strip_y, strip_h, (uint16_t)(SIDEBAR_X + 8u), 100u, "SWING", &Font8x12, 1u, LIGHTBLUE, COLOR_BG);
        MainGridStripDrawString(buffer, strip_y, strip_h, (uint16_t)(SIDEBAR_X + 8u), 116u, f->swing_text, &Font12x20, 1u, WHITE, COLOR_BG);
        MainGridStripDrawString(buffer, strip_y, strip_h, (uint16_t)(SIDEBAR_X + 8u), 148u, "PATTERN", &Font8x12, 1u, LIGHTBLUE, COLOR_BG);
        MainGridStripDrawString(buffer, strip_y, strip_h, (uint16_t)(SIDEBAR_X + 8u), 164u, f->pattern_text, &Font12x20, 1u, WHITE, COLOR_BG);
    }

    for (uint8_t step_no = 1u; step_no <= 12u; step_no++)
    {
        uint8_t index = (uint8_t)(step_no - 1u);
        uint16_t colour = MainGridBoxColourFor((f->selected_step == step_no) ? 1u : 0u,
                                               (f->active_step == step_no) ? 1u : 0u,
                                               f->has_chord[index],
                                               f->flash_enabled,
                                               f->flash_on);

        MainGridComposeBox(buffer, strip_y, strip_h, step_no, colour);
    }
}

/* Priority: active (playing) > selected+saved > selected+empty > saved > normal */
static uint16_t MainGridBoxColourFor(uint8_t selected, uint8_t active, uint8_t has_chord,
                                     uint8_t flash_enabled, uint8_t flash_on)
{
    if (active && flash_enabled) return flash_on ? WHITE : BLUE;
    if (active) return COL_ACTIVE;
    if (selected && has_chord) return COL_CHORD;
    if (selected) return COL_SELECTED_EMPTY;
    if (has_chord) return COL_CHORD;
    return COL_NORMAL;
}

static uint16_t MainGridBoxColour(uint8_t selected, uint8_t active, uint8_t has_chord)
{
    return MainGridBoxColourFor(selected, active, has_chord, s_repeat_flash_enabled, s_repeat_flash_on);
}

static void MainGridComposeBox(uint8_t* buffer,
                               uint16_t strip_y,
                               uint16_t strip_h,
                               uint8_t step_no,
                               uint16_t colour)
{
    const uint8_t index = (uint8_t)(step_no - 1u);
    const uint16_t x = (uint16_t)(GRID_X + (index % 4u) * (BOX_W + GAP_X));
    const uint16_t y = (uint16_t)(GRID_Y + (index / 4u) * (BOX_H + GAP_Y));
    char text[4];

    if (y >= (uint32_t)strip_y + strip_h || (uint32_t)y + BOX_H <= strip_y) return;

    MainGridStripDrawBorderedRect(buffer, strip_y, strip_h, x, y, BOX_W, BOX_H, COLOR_BOX_BG, colour, BORDER_T);

    snprintf(text, sizeof(text), "%u", (unsigned)step_no);
    {
        uint8_t len = 0u;
        while (text[len]) len++;
        uint16_t textW = (uint16_t)(len * Font8x12.width * 2u);
        uint16_t textH = (uint16_t)(Font8x12.height * 2u);
        uint16_t tx = (uint16_t)(x + ((BOX_W - textW) / 2u));
        uint16_t ty = (uint16_t)(y + ((BOX_H - textH) / 2u));
        MainGridStripDrawString(buffer, strip_y, strip_h, tx, ty, text, &Font8x12, 2u, colour, COLOR_BOX_BG);
    }
}

void FillRegion(UiRect r, uint16_t color)
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

void MenuTemplate_Begin(uint8_t frame_id)
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
    s_song_footer_valid = 0;
    s_param_row_cursor = 0xFF;
    s_timing_row_cursor = 0xFF;
    s_arp_prev_mode = 0xFF;
    s_arp_prev_rate = 0xFF;
    s_quan_router_prev_cursor = 0xFF;
    s_quan_router_prev_edit_field = 0xFF;
    s_quan_router_prev_unsaved = 0xFF;
    s_quan_router_prev_source[0] = 0xFF;
    s_quan_router_prev_source[1] = 0xFF;
    s_quan_router_prev_source[2] = 0xFF;
    s_quan_router_prev_source[3] = 0xFF;
    s_quan_router_prev_source[4] = 0xFF;
    s_quan_router_prev_source[5] = 0xFF;
    s_quan_router_prev_source[6] = 0xFF;
    s_quan_router_prev_source[7] = 0xFF;
    s_quan_router_prev_target[0] = 0xFF;
    s_quan_router_prev_target[1] = 0xFF;
    s_quan_router_prev_target[2] = 0xFF;
    s_quan_router_prev_target[3] = 0xFF;
    s_quan_router_prev_target[4] = 0xFF;
    s_quan_router_prev_target[5] = 0xFF;
    s_quan_router_prev_target[6] = 0xFF;
    s_quan_router_prev_target[7] = 0xFF;
    s_song_page_valid = 0;
    s_song_prev_page_base = 0xFF;
    s_song_prev_cursor = 0xFF;
    s_song_prev_playing_slot = 0xFF;
    s_song_prev_blink_on = 0xFF;
    s_song_prev_length = 0xFF;
    s_song_prev_chain_page[0] = 0xFF;
    s_song_prev_chain_page[1] = 0xFF;
    s_song_prev_chain_page[2] = 0xFF;
    s_song_prev_chain_page[3] = 0xFF;
    s_user_chord_load_valid = 0;
    s_user_chord_load_prev_selection = 0xFF;
    s_user_chord_load_prev_count = 0xFF;
    s_user_chord_load_footer_valid = 0;
}

void MenuTemplate_DrawHeader(const char* title, const char* indicator, uint16_t indicator_color)
{
    uint8_t need_redraw = !s_menu_header_valid;

    if (!need_redraw && !StrEq(s_menu_header_title, title)) need_redraw = 1;
    if (!need_redraw && !StrEq(s_menu_header_indicator, indicator)) need_redraw = 1;
    if (!need_redraw && s_menu_header_indicator_color != indicator_color) need_redraw = 1;

    if (!need_redraw) return;

    FillRegion(UI_REGION_MENU_TOP, COLOR_PANEL_ALT);
    ST7789_DrawString(4, 4, title, &Font12x20, COLOR_TEXT_MAIN, COLOR_PANEL_ALT);
    if (indicator[0]) {
        ST7789_DrawString(4, 22, indicator, &Font12x20, indicator_color, COLOR_PANEL_ALT);
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
    const char* label = "BAR";

    if (s_main_mode == UI_MAIN_MODE_CHORD)
    {
        fill = COLOR8(70, 140, 255);
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
        fill = COLOR8(0, 135, 0);
        text = WHITE;
        label = "PAT";
    }
    else if (s_main_mode == UI_MAIN_MODE_QUAN)
    {
        fill = COL_MODE_QUAN;
        text = WHITE;
        label = "QUAN";
    }

    ST7789_FillRectBordered(box_x, box_y, box_w, box_h, fill, border, BORDER_T);
    ST7789_DrawString(8, 28, label, &Font8x12, text, fill);
    s_prev_main_mode = s_main_mode;
}

void UI_Display_SetMainMode(UiMainMode mode)
{
    if (mode > UI_MAIN_MODE_QUAN)
    {
        mode = UI_MAIN_MODE_STEP;
    }
    if (s_main_mode != mode)
    {
        s_main_mode = mode;
        s_force_status_redraw = 1;
    }
}

static void DrawStatusCell(uint16_t x, uint16_t y, char ch, uint16_t fg, uint16_t bg)
{
    ST7789_FillRect(x, y, STATUS_CHAR_W, STATUS_CHAR_H, bg);
    ST7789_DrawChar(x, y, ch, &Font8x12, fg, bg);
}

static void DrawStatusBarDigitsDirty(const char* bar_digits, char prev_digits[2], uint16_t x, uint16_t y, uint16_t fg, uint16_t bg)
{
    for (uint8_t i = 0u; i < 2u; ++i)
    {
        char ch = bar_digits[i];
        if (prev_digits[i] != ch)
        {
            prev_digits[i] = ch;
            DrawStatusCell((uint16_t)(x + (uint16_t)i * STATUS_CHAR_W), y, ch, fg, bg);
        }
    }
}

static void DrawStatusStepDigitsDirty(const char* step_digits, char prev_digits[2], uint16_t x, uint16_t y, uint16_t fg, uint16_t bg)
{
    for (uint8_t i = 0u; i < 2u; ++i)
    {
        char ch = step_digits[i];
        if (prev_digits[i] != ch)
        {
            prev_digits[i] = ch;
            DrawStatusCell((uint16_t)(x + (uint16_t)i * STATUS_CHAR_W), y, ch, fg, bg);
        }
    }
}

static void DrawStatusTimerDigitsDirty(const char* timer_text, char prev_digits[9], uint16_t x, uint16_t y, uint16_t fg, uint16_t bg)
{
    static const uint8_t timer_digit_positions[7] = { 0u, 1u, 3u, 4u, 6u, 7u, 8u };

    for (uint8_t idx = 0u; idx < 7u; ++idx)
    {
        const uint8_t pos = timer_digit_positions[idx];
        const char ch = timer_text[pos];
        if (prev_digits[pos] != ch)
        {
            prev_digits[pos] = ch;
            DrawStatusCell((uint16_t)(x + (uint16_t)pos * STATUS_CHAR_W), y, ch, fg, bg);
        }
    }
}

/* ── Time Display ────────────────────────────────────────────────────────────── */
void UI_Display_DrawStatusRow(uint8_t pattern,
                              uint8_t step,
                              uint32_t loops,
                              uint32_t run_time_ms)
{
    (void)loops;
    (void)run_time_ms;

    if (MainGridDefer(UI_FRAME_DEFER_STATUS)) return;

    DrawSidebarInfo();

    const uint8_t current_bar = (uint8_t)(Bridge_GetCurrentStep() + 1u);
    const uint8_t current_bar_total = Bridge_GetPatternStepCount();
    const uint8_t current_substep = (uint8_t)(Bridge_GetCurrentStepSubIndex() + 1u);
    const uint8_t current_step_total = Bridge_GetStepLedgerLength(Bridge_GetCurrentStep());
    const uint32_t elapsed_ms = Bridge_GetRunTimeMs();
    const uint32_t elapsed_mins = elapsed_ms / 60000u;
    const uint32_t elapsed_secs = (elapsed_ms % 60000u) / 1000u;
    const uint32_t elapsed_millis = elapsed_ms % 1000u;

    if (s_force_status_redraw || s_main_mode != s_prev_main_mode || pattern != s_prev_pattern)
    {
        DrawMainModeBox();
        s_prev_pattern = pattern;

        char st[32];
        snprintf(st, sizeof(st), "%02u/%02u S%02u/%02u %02lu:%02lu.%03lu",
                 (unsigned)current_bar,
                 (unsigned)current_bar_total,
                 (unsigned)current_substep,
                 (unsigned)((current_step_total == 0u) ? 1u : current_step_total),
                 (unsigned long)elapsed_mins,
                 (unsigned long)elapsed_secs,
                 (unsigned long)elapsed_millis);
        ST7789_DrawString(STATUS_TEXT_X, STATUS_TEXT_Y, st, &Font8x12, COLOR_TEXT_MAIN, COLOR_BG);

        s_prev_bar = current_bar;
        s_prev_step = step;
        s_prev_substep = current_substep;

        char bar_digits[3];
        char step_digits[3];
        char timer_digits[10];
        snprintf(bar_digits, sizeof(bar_digits), "%02u", (unsigned)current_bar);
        snprintf(step_digits, sizeof(step_digits), "%02u", (unsigned)current_substep);
        snprintf(timer_digits, sizeof(timer_digits), "%02lu:%02lu.%03lu",
                 (unsigned long)elapsed_mins,
                 (unsigned long)elapsed_secs,
                 (unsigned long)elapsed_millis);
        s_prev_bar_digits[0] = bar_digits[0];
        s_prev_bar_digits[1] = bar_digits[1];
        s_prev_bar_digits[2] = '\0';
        s_prev_step_digits[0] = step_digits[0];
        s_prev_step_digits[1] = step_digits[1];
        s_prev_step_digits[2] = '\0';
        memcpy(s_prev_timer_digits, timer_digits, sizeof(s_prev_timer_digits));
    }
    else
    {
        char bar_digits[3];
        char step_digits[3];
        char timer_text[10];

        snprintf(bar_digits, sizeof(bar_digits), "%02u", (unsigned)current_bar);
        snprintf(step_digits, sizeof(step_digits), "%02u", (unsigned)current_substep);
        snprintf(timer_text, sizeof(timer_text), "%02lu:%02lu.%03lu",
                 (unsigned long)elapsed_mins,
                 (unsigned long)elapsed_secs,
                 (unsigned long)elapsed_millis);

        if (current_bar != s_prev_bar)
        {
            s_prev_bar = current_bar;
            DrawStatusBarDigitsDirty(bar_digits, s_prev_bar_digits, STATUS_TEXT_X, STATUS_TEXT_Y, COLOR_TEXT_MAIN, COLOR_BG);
        }

        if (step != s_prev_step || current_substep != s_prev_substep)
        {
            s_prev_step = step;
            s_prev_substep = current_substep;
            DrawStatusStepDigitsDirty(step_digits, s_prev_step_digits, (uint16_t)(STATUS_TEXT_X + 8u * 6u + 8u), STATUS_TEXT_Y, COLOR_TEXT_MAIN, COLOR_BG);
        }

        if (memcmp(timer_text, s_prev_timer_digits, sizeof(s_prev_timer_digits)) != 0)
        {
            DrawStatusTimerDigitsDirty(timer_text, s_prev_timer_digits, (uint16_t)(STATUS_TEXT_X + 8u * 13u), STATUS_TEXT_Y, COLOR_TEXT_MAIN, COLOR_BG);
        }
    }

    s_force_status_redraw = 0;
}

void UI_Display_DrawMcpDebug(uint8_t online, uint8_t read_ok, uint8_t gpio_a, uint8_t gpio_b, uint8_t addr7, uint8_t scan_mask)
{
    if (s_menu_frame != MENU_FRAME_NONE)
    {
        return;
    }

    if (s_mcp_dbg_valid &&
        online == s_mcp_dbg_online &&
        read_ok == s_mcp_dbg_read_ok &&
        gpio_a == s_mcp_dbg_a &&
        gpio_b == s_mcp_dbg_b &&
        addr7 == s_mcp_dbg_addr7 &&
        scan_mask == s_mcp_dbg_scan_mask)
    {
        return;
    }

    {
        char l1[20];
        char l2[20];
        uint16_t state_col = online ? (read_ok ? COL_PLAY : YELLOW) : COL_REC_ARM;

        snprintf(l1, sizeof(l1), "MCP:%s R:%u %02X", online ? "ON" : "OFF", (unsigned)read_ok, (unsigned)addr7);
        snprintf(l2, sizeof(l2), "A:%02X B:%02X M:%02X", (unsigned)gpio_a, (unsigned)gpio_b, (unsigned)scan_mask);

        ST7789_FillRect(56, 44, 148, 12, COLOR_BG);
        ST7789_DrawString(56, 44, l1, &Font8x12, state_col, COLOR_BG);

        ST7789_FillRect(56, 56, 148, 12, COLOR_BG);
        ST7789_DrawString(56, 56, l2, &Font8x12, COLOR_TEXT_SECONDARY, COLOR_BG);
    }

    s_mcp_dbg_valid = 1;
    s_mcp_dbg_online = online;
    s_mcp_dbg_read_ok = read_ok;
    s_mcp_dbg_a = gpio_a;
    s_mcp_dbg_b = gpio_b;
    s_mcp_dbg_addr7 = addr7;
    s_mcp_dbg_scan_mask = scan_mask;
}

/* ── Chord Menu ───────────────────────────────────────────────────────────── */
static uint8_t s_param_footer_selection = PARAM_ACTION_MAIN;
static uint8_t s_chord_params_direct_mode = 0u;
static uint8_t s_direct_chord_bar_number = 0u;
static uint8_t s_direct_chord_pos_number = 0u;
static uint8_t s_direct_chord_grid_division = 4u;
static uint8_t s_direct_chord_notes[4] = {0xFFu, 0xFFu, 0xFFu, 0xFFu};
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

void UI_Display_DrawChordMenu(uint8_t step, const ChordParams* chord, uint8_t selection)
{
    MenuTemplate_Begin(MENU_FRAME_CHORD);
    s_chord_page_drawn = 0xFF;  /* Reset page cache when entering menu */

    /* Content region redraw only. */
    FillRegion(UI_REGION_MENU_LIST, COLOR_BG);
    FillRegion(UI_REGION_MENU_FOOTER, COLOR_PANEL);
    DrawCenteredFooterButton("USER CHORDS", (selection == 17) ? 1 : 0);
    s_chord_footer_valid = 1;

    /* Title */
    char title[20];
    snprintf(title, sizeof(title), "Bar %u Chord", step);
    ST7789_DrawString(4, 4, title, &Font12x20, COLOR_TEXT_MAIN, COLOR_PANEL_ALT);

    char selected_label[24];
    if (selection == 0)
    {
        snprintf(selected_label, sizeof(selected_label), "Selected: Clear");
    }
    else if (selection == 17)
    {
        snprintf(selected_label, sizeof(selected_label), "Selected: USER");
    }
    else
    {
        uint8_t root = chord->root_key;
        snprintf(selected_label, sizeof(selected_label), "Selected: %s %s", s_root_keys[root], s_chord_names[selection]);
    }
    MenuTemplate_DrawHeader(title, selected_label, YELLOW);

    /* 3x2 rounded-button grid (6 buttons per page) */
    uint8_t menu_selection = (selection == 17) ? 0 : selection;
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

        DrawChordButtonText(slot, idx, (idx == selection), chord);
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
        ST7789_DrawString((uint16_t)(x + 14), (uint16_t)(y + 16), "Clear", &Font12x20, text_col, COLOR_BOX_BG);
    } else {
        char top[8];
        char bottom[10];
        uint8_t root = chord->root_key;
        snprintf(top, sizeof(top), "%s", s_root_keys[root]);
        snprintf(bottom, sizeof(bottom), "%s", s_chord_names[chord_idx]);

        ST7789_DrawString((uint16_t)(x + 8), (uint16_t)(y + 8), top, &Font12x20, COLOR_TEXT_SECONDARY, COLOR_BOX_BG);
        ST7789_DrawString((uint16_t)(x + 8), (uint16_t)(y + 26), bottom, &Font12x20, text_col, COLOR_BOX_BG);
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
    ST7789_DrawString(x + 8, y + 12, label, &Font12x20, text_col, fill);
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
    ST7789_DrawString(x + 18, y + 12, label, &Font12x20, text_col, fill);
}

void UI_Display_NavigateChordMenu(int8_t delta, uint8_t step, const ChordParams* chord, uint8_t* selection)
{
    uint8_t old = *selection;
    int16_t next = (int16_t)old + delta;

    while (next < 0)  next += 18;  /* 18 options: 0-16 chords + USER at 17 */
    while (next > 17) next -= 18;
    *selection = (uint8_t)next;

    if (old == 17 || *selection == 17)
    {
        /* Moving to/from USER footer button — update footer highlight and button states. */
        DrawCenteredFooterButton("USER CHORDS", (*selection == 17) ? 1 : 0);
        s_chord_footer_valid = 1;

        uint8_t page_base = (uint8_t)(((old < 17 ? old : *selection) / CHORD_BTNS_PER_PAGE) * CHORD_BTNS_PER_PAGE);
        if (old < 17)
        {
            DrawChordButtonText((uint8_t)(old - page_base), old, 0, chord);
        }
        if (*selection < 17)
        {
            DrawChordButtonText((uint8_t)(*selection - page_base), *selection, 1, chord);
        }

        char title[20];
        char selected_label[24];
        snprintf(title, sizeof(title), "Bar %u Chord", step);
        snprintf(selected_label, sizeof(selected_label), "Selected: USER");
        MenuTemplate_DrawHeader(title, selected_label, YELLOW);
        return;
    }

    if ((old / CHORD_BTNS_PER_PAGE) != (*selection / CHORD_BTNS_PER_PAGE))
    {
        /* Page changed: draw new buttons and header only. */
        uint8_t new_page_base = (*selection / CHORD_BTNS_PER_PAGE) * CHORD_BTNS_PER_PAGE;
        DrawAllChordButtonFrames();
        s_chord_page_drawn = (uint8_t)(new_page_base / CHORD_BTNS_PER_PAGE);

        for (uint8_t slot = 0; slot < CHORD_BTNS_PER_PAGE; slot++)
        {
            uint8_t idx = new_page_base + slot;
            if (idx >= 17) break;
            DrawChordButtonText(slot, idx, (idx == *selection), chord);
        }

        char title[20];
        char selected_label[24];
        snprintf(title, sizeof(title), "Bar %u Chord", step);
        snprintf(selected_label, sizeof(selected_label), "Selected: %s %s", s_root_keys[chord->root_key], s_chord_names[*selection]);
        MenuTemplate_DrawHeader(title, selected_label, YELLOW);
        return;
    }

    uint8_t page_base = (*selection / CHORD_BTNS_PER_PAGE) * CHORD_BTNS_PER_PAGE;
    if (old < 17)
    {
        DrawChordButtonText((uint8_t)(old - page_base), old, 0, chord);
    }

    DrawChordButtonText((uint8_t)(*selection - page_base), *selection, 1, chord);

    char title[20];
    char selected_label[24];
    snprintf(title, sizeof(title), "Bar %u Chord", step);
    if (*selection == 0)
    {
        snprintf(selected_label, sizeof(selected_label), "Selected: Clear");
    }
    else if (*selection == 17)
    {
        snprintf(selected_label, sizeof(selected_label), "Selected: USER");
    }
    else
    {
        snprintf(selected_label, sizeof(selected_label), "Selected: %s %s", s_root_keys[chord->root_key], s_chord_names[*selection]);
    }
    MenuTemplate_DrawHeader(title, selected_label, YELLOW);
}

/* ── Chord Parameters ─────────────────────────────────────────────────────── */
static void DrawParamRow(uint8_t index, const ChordParams* chord, uint8_t is_selected)
{
    const char* param_names[] = {"Root Key:", "Type:", "Arp:", "Gate:"};
    const uint16_t value_x = (uint16_t)(SCREEN_W - 96);
    const char* root_keys[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const char* chord_types[] = {"Clear", "Major", "Minor", "Dim", "Aug", "Sus4", "7th", "Maj7", "Min7", "Dim7", "7sus4", "6th", "Min6", "9th", "Maj9", "Min9", "11th"};
    const char* arp_patterns[] = {"Block", "Up", "Down", "UpDn", "Random"};
    const char* durations[] = {"16th", "8th", "Qtr", "8th."};

    uint8_t y = (uint8_t)(PARAM_ROW_Y0 + index * PARAM_ROW_STEP);
    uint16_t color = is_selected ? COLOR_ACTIVE : COLOR_TEXT_MAIN;

    /* Selection stripe + value area only (labels stay static after first draw). */
    ST7789_FillRect(0, y, 3, 22, is_selected ? COLOR_ACTIVE : COLOR_BG);
    ST7789_FillRect(value_x, y, (uint16_t)(SCREEN_W - value_x), 22, COLOR_BG);

    /* Parameter name (static tone). */
    if (s_param_row_cursor == 0xFF)
    {
        ST7789_DrawString(8, y, param_names[index], &Font12x20, COLOR_TEXT_MAIN, COLOR_BG);
    }

    /* Parameter value */
    char value[12];
    switch (index) {
        case 0: snprintf(value, sizeof(value), "%s", root_keys[chord->root_key]); break;
        case 1: snprintf(value, sizeof(value), "%s", chord_types[chord->chord_type]); break;
        case 2: snprintf(value, sizeof(value), "%s", arp_patterns[chord->arp_pattern]); break;
        case 3: snprintf(value, sizeof(value), "%s", durations[chord->duration]); break;
        default: value[0] = 0; break;
    }
    ST7789_DrawString(value_x, y, value, &Font12x20, color, COLOR_BG);
}

static void MidiToNoteLabel(uint8_t midi_note, char* out, size_t out_len)
{
    static const char* note_names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    if (!out || out_len == 0u)
    {
        return;
    }

    if (midi_note == 0xFFu)
    {
        out[0] = '\0';
        return;
    }

    const uint8_t note = (uint8_t)(midi_note % 12u);
    const int8_t octave = (int8_t)((midi_note / 12u) - 1u);
    snprintf(out, out_len, "%s%d", note_names[note], (int)octave);
}

static const char* GridDivisionLabel(uint8_t grid_division)
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

typedef struct
{
    uint8_t root, type, cursor, bar, position, grid, notes[4];
    char values[2][12], subtitle[24], grid_text[24], preview[48];
} DirectChordFrame;

enum { DIRECT_FULL, DIRECT_ROOT, DIRECT_TYPE, DIRECT_MARK_ROOT, DIRECT_MARK_TYPE,
       DIRECT_GRID, DIRECT_PREVIEW, DIRECT_HEADER, DIRECT_REGION_COUNT };
static DirectChordFrame s_direct_latest, s_direct_snapshot, s_direct_drawn;
static UiFrameJob s_direct_job;
static UiRect s_direct_region;
static uint8_t s_direct_valid, s_direct_kind, s_direct_character, s_direct_requested;
static uint8_t s_direct_value_selected[2], s_direct_marker_selected[2];
#define DIRECT_STRIP_H 8u
static const char* k_direct_types[] = {"Clear", "Major", "Minor", "Dim", "Aug", "Sus4",
    "7th", "Maj7", "Min7", "Dim7", "7sus4", "6th", "Min6", "9th", "Maj9", "Min9", "11th"};

static void DirectCapture(const ChordParams* chord, uint8_t cursor)
{
    DirectChordFrame* frame = &s_direct_latest;
    memset(frame, 0, sizeof(*frame));
    frame->root = chord->root_key;
    frame->type = chord->chord_type;
    frame->cursor = cursor;
    frame->bar = s_direct_chord_bar_number;
    frame->position = s_direct_chord_pos_number;
    frame->grid = s_direct_chord_grid_division;
    memcpy(frame->notes, s_direct_chord_notes, sizeof(frame->notes));
    snprintf(frame->values[0], sizeof(frame->values[0]), "%s", s_root_keys[frame->root]);
    snprintf(frame->values[1], sizeof(frame->values[1]), "%s", k_direct_types[frame->type]);
    snprintf(frame->subtitle, sizeof(frame->subtitle), "BAR%02u POS%02u",
             (unsigned)frame->bar, (unsigned)frame->position);
    snprintf(frame->grid_text, sizeof(frame->grid_text), "GRID %s", GridDivisionLabel(frame->grid));
    for (uint8_t note = 0u; note < 4u; ++note)
    {
        if (frame->notes[note] == 0xFFu) continue;
        char label[8];
        MidiToNoteLabel(frame->notes[note], label, sizeof(label));
        if (frame->preview[0]) strncat(frame->preview, "  ", sizeof(frame->preview) - strlen(frame->preview) - 1u);
        strncat(frame->preview, label, sizeof(frame->preview) - strlen(frame->preview) - 1u);
    }
}

static char DirectPreviewCharacter(const DirectChordFrame* frame, uint8_t index)
{
    return index < strlen(frame->preview) ? frame->preview[index] : ' ';
}

static uint8_t DirectFindDirty(void)
{
    if (!s_direct_valid) return DIRECT_FULL;
    for (uint8_t row = 0u; row < 2u; ++row)
    {
        if (strcmp(s_direct_latest.values[row], s_direct_drawn.values[row]) != 0 ||
            s_direct_value_selected[row] != (s_direct_latest.cursor == row))
            return row == 0u ? DIRECT_ROOT : DIRECT_TYPE;
        if (s_direct_marker_selected[row] != (s_direct_latest.cursor == row))
            return row == 0u ? DIRECT_MARK_ROOT : DIRECT_MARK_TYPE;
    }
    if (strcmp(s_direct_latest.grid_text, s_direct_drawn.grid_text)) return DIRECT_GRID;
    for (uint8_t index = 0u; index < (SCREEN_W - 16u) / Font12x20.width; ++index)
    {
        if (DirectPreviewCharacter(&s_direct_latest, index) != DirectPreviewCharacter(&s_direct_drawn, index))
        {
            s_direct_character = index;
            return DIRECT_PREVIEW;
        }
    }
    if (strcmp(s_direct_latest.subtitle, s_direct_drawn.subtitle)) return DIRECT_HEADER;
    return DIRECT_REGION_COUNT;
}

void UI_Display_CancelDirectChordFrame(void)
{
    if (s_direct_job.active) TimeCommitProbe_DirectCancel();
    UiFrameJob_Cancel(&s_direct_job);
    s_direct_valid = 0u;
    s_direct_requested = 0u;
}

void UI_Display_ResetDirectChordFrame(void)
{
    if (!s_direct_job.active) s_direct_valid = 0u;
}

uint8_t UI_Display_IsDirectChordFrameActive(void)
{
    return s_chord_params_direct_mode && s_direct_requested &&
        (s_direct_job.active || DirectFindDirty() != DIRECT_REGION_COUNT);
}

static void DirectBegin(uint8_t kind)
{
    s_direct_kind = kind;
    s_direct_snapshot = s_direct_latest;
    if (kind == DIRECT_FULL) s_direct_region = (UiRect){0u, 0u, SCREEN_W, SCREEN_H};
    else if (kind == DIRECT_ROOT || kind == DIRECT_TYPE)
        s_direct_region = (UiRect){SCREEN_W - 96u,
            (uint16_t)(PARAM_ROW_Y0 + (kind == DIRECT_TYPE ? PARAM_ROW_STEP : 0u)), 96u, 22u};
    else if (kind == DIRECT_MARK_ROOT || kind == DIRECT_MARK_TYPE)
        s_direct_region = (UiRect){0u,
            (uint16_t)(PARAM_ROW_Y0 + (kind == DIRECT_MARK_TYPE ? PARAM_ROW_STEP : 0u)), 3u, 22u};
    else if (kind == DIRECT_GRID) s_direct_region = (UiRect){8u, 104u, SCREEN_W - 16u, 20u};
    else if (kind == DIRECT_PREVIEW) s_direct_region = (UiRect){
        (uint16_t)(8u + s_direct_character * Font12x20.width), 128u, Font12x20.width, 20u};
    else s_direct_region = (UiRect){0u, 0u, SCREEN_W, 42u};
    (void)UiFrameJob_Begin(&s_direct_job,
        (uint8_t)((s_direct_region.h + DIRECT_STRIP_H - 1u) / DIRECT_STRIP_H));
    uint8_t category = 5u;
    if (kind == DIRECT_FULL) category = 0u;
    else if (kind == DIRECT_MARK_ROOT || kind == DIRECT_MARK_TYPE) category = 1u;
    else if (kind == DIRECT_ROOT || kind == DIRECT_TYPE)
    {
        const uint8_t row = kind == DIRECT_ROOT ? 0u : 1u;
        category = strcmp(s_direct_snapshot.values[row], s_direct_drawn.values[row]) ?
            (uint8_t)(row + 2u) : 1u;
    }
    else if (kind == DIRECT_PREVIEW) category = 4u;
    TimeCommitProbe_DirectBegin(category);
}

static void DirectCompose(uint8_t* buffer, uint16_t strip_y, uint16_t strip_h)
{
    const DirectChordFrame* frame = &s_direct_snapshot;
    MainGridStripFillRect(buffer, strip_y, strip_h, s_direct_region.x, strip_y,
                         s_direct_region.w, strip_h, COLOR_BG);
    MainGridStripFillRect(buffer, strip_y, strip_h, 0u, 0u, SCREEN_W, MENU_TOP_Y, COLOR_PANEL_ALT);
    for (uint8_t row = 0u; row < 2u; ++row)
    {
        const uint16_t y = (uint16_t)(PARAM_ROW_Y0 + row * PARAM_ROW_STEP);
        const uint16_t colour = frame->cursor == row ? COLOR_ACTIVE : COLOR_TEXT_MAIN;
        MainGridStripFillRect(buffer, strip_y, strip_h, 0u, y, 3u, 22u,
                             frame->cursor == row ? COLOR_ACTIVE : COLOR_BG);
        MainGridStripDrawString(buffer, strip_y, strip_h, 8u, y, row == 0u ? "Root Key:" : "Type:",
                               &Font12x20, 1u, COLOR_TEXT_MAIN, COLOR_BG);
        MainGridStripDrawString(buffer, strip_y, strip_h, SCREEN_W - 96u, y, frame->values[row],
                               &Font12x20, 1u, colour, COLOR_BG);
    }
    MainGridStripDrawString(buffer, strip_y, strip_h, 4u, 4u, "DIRECT CHORD",
                           &Font12x20, 1u, COLOR_TEXT_MAIN, COLOR_PANEL_ALT);
    MainGridStripDrawString(buffer, strip_y, strip_h, 4u, 22u, frame->subtitle,
                           &Font12x20, 1u, YELLOW, COLOR_PANEL_ALT);
    MainGridStripDrawString(buffer, strip_y, strip_h, 8u, 104u, frame->grid_text,
                           &Font12x20, 1u, COLOR_ACTIVE, COLOR_BG);
    MainGridStripDrawString(buffer, strip_y, strip_h, 8u, 128u, frame->preview,
                           &Font12x20, 1u, COLOR_TEXT_SECONDARY, COLOR_BG);
    MainGridStripFillRect(buffer, strip_y, strip_h, 0u, MENU_FOOTER_Y, SCREEN_W,
                         UI_REGION_MENU_FOOTER_H, COLOR_PANEL);
    const uint16_t button_x = (SCREEN_W - 150u) / 2u;
    MainGridStripFillRect(buffer, strip_y, strip_h, button_x + 2u, FOOTER_BTN_Y0,
                         146u, FOOTER_BTN_H, COLOR_PANEL_ALT);
    MainGridStripFillRect(buffer, strip_y, strip_h, button_x, FOOTER_BTN_Y0 + 2u,
                         150u, FOOTER_BTN_H - 4u, COLOR_PANEL_ALT);
    MainGridStripFillRect(buffer, strip_y, strip_h, button_x + 2u, FOOTER_BTN_Y0, 146u, 1u, COLOR_ACTIVE);
    MainGridStripFillRect(buffer, strip_y, strip_h, button_x + 2u, FOOTER_BTN_Y0 + FOOTER_BTN_H - 1u,
                         146u, 1u, COLOR_ACTIVE);
    MainGridStripFillRect(buffer, strip_y, strip_h, button_x, FOOTER_BTN_Y0 + 2u, 1u, FOOTER_BTN_H - 4u, COLOR_ACTIVE);
    MainGridStripFillRect(buffer, strip_y, strip_h, button_x + 149u, FOOTER_BTN_Y0 + 2u, 1u, FOOTER_BTN_H - 4u, COLOR_ACTIVE);
    for (uint8_t corner = 0u; corner < 4u; ++corner)
        MainGridStripFillRect(buffer, strip_y, strip_h, button_x + (corner & 1u ? 148u : 1u),
            FOOTER_BTN_Y0 + (corner & 2u ? FOOTER_BTN_H - 2u : 1u), 1u, 1u, COLOR_ACTIVE);
    MainGridStripDrawString(buffer, strip_y, strip_h, button_x + 18u, FOOTER_BTN_Y0 + 12u,
                           "M9 PLACE", &Font12x20, 1u, WHITE, COLOR_PANEL_ALT);
}

void UI_Display_ServiceDirectChordFrame(void)
{
    if (!s_direct_requested) return;
    if (!s_direct_job.active)
    {
        const uint8_t kind = DirectFindDirty();
        if (kind == DIRECT_REGION_COUNT) return;
        DirectBegin(kind);
    }
    uint8_t strip;
    if (!UiFrameJob_NextStrip(&s_direct_job, &strip)) return;
    uint8_t* buffer = UI_RenderScratch_Acquire(UI_RENDER_SCRATCH_OWNER_MENU);
    if (!buffer) return;
    TimeCommitProbe_DirectJobBegin();
    const uint16_t strip_y = (uint16_t)(s_direct_region.y + strip * DIRECT_STRIP_H);
    const uint16_t remaining = (uint16_t)(s_direct_region.y + s_direct_region.h - strip_y);
    const uint16_t strip_h = remaining < DIRECT_STRIP_H ? remaining : DIRECT_STRIP_H;
    s_compose_x0 = s_direct_region.x;
    s_compose_w = s_direct_region.w;
    DirectCompose(buffer, strip_y, strip_h);
    s_compose_x0 = 0u;
    s_compose_w = SCREEN_W;
    ST7789_DrawRGB565Buffer(s_direct_region.x, strip_y, s_direct_region.w, strip_h, buffer,
                            (uint32_t)s_direct_region.w * strip_h * 2u);
    UI_RenderScratch_Release(UI_RENDER_SCRATCH_OWNER_MENU);
    TimeCommitProbe_DirectJobEnd();
    if (!UiFrameJob_CompleteStrip(&s_direct_job)) return;
    if (s_direct_kind == DIRECT_FULL)
    {
        s_direct_drawn = s_direct_snapshot;
        for (uint8_t row = 0u; row < 2u; ++row)
            s_direct_value_selected[row] = s_direct_marker_selected[row] = s_direct_snapshot.cursor == row;
        s_direct_valid = 1u;
    }
    else if (s_direct_kind == DIRECT_ROOT || s_direct_kind == DIRECT_TYPE)
    {
        const uint8_t row = s_direct_kind == DIRECT_ROOT ? 0u : 1u;
        memcpy(s_direct_drawn.values[row], s_direct_snapshot.values[row], sizeof(s_direct_drawn.values[row]));
        s_direct_value_selected[row] = s_direct_snapshot.cursor == row;
    }
    else if (s_direct_kind == DIRECT_MARK_ROOT || s_direct_kind == DIRECT_MARK_TYPE)
        s_direct_marker_selected[s_direct_kind == DIRECT_MARK_ROOT ? 0u : 1u] =
            s_direct_snapshot.cursor == (s_direct_kind == DIRECT_MARK_ROOT ? 0u : 1u);
    else if (s_direct_kind == DIRECT_GRID)
        memcpy(s_direct_drawn.grid_text, s_direct_snapshot.grid_text, sizeof(s_direct_drawn.grid_text));
    else if (s_direct_kind == DIRECT_PREVIEW)
    {
        /* Space-pad the drawn preview cache so deletion has the same identity as an empty cell. */
        for (uint8_t index = (uint8_t)strlen(s_direct_drawn.preview); index < sizeof(s_direct_drawn.preview) - 1u; ++index)
            s_direct_drawn.preview[index] = ' ';
        s_direct_drawn.preview[s_direct_character] = DirectPreviewCharacter(&s_direct_snapshot, s_direct_character);
    }
    else memcpy(s_direct_drawn.subtitle, s_direct_snapshot.subtitle, sizeof(s_direct_drawn.subtitle));
    TimeCommitProbe_DirectComplete();
}

void UI_Display_DrawChordParams(uint8_t step, const ChordParams* chord, uint8_t param_cursor, uint8_t footer_action)
{
    MenuTemplate_Begin(MENU_FRAME_PARAMS);

    if (s_chord_params_direct_mode)
    {
        DirectCapture(chord, param_cursor);
        s_direct_requested = 1u;
        if (!s_direct_valid && !s_direct_job.active) DirectBegin(DIRECT_FULL);
        return;
    }
    else if (s_param_row_cursor == 0xFF)
    {
        /* First draw: clear entire list and draw all rows */
        FillRegion(UI_REGION_MENU_LIST, COLOR_BG);
        for (uint8_t i = 0; i < 4; i++)
        {
            DrawParamRow(i, chord, (i == param_cursor));
        }
    }
    else if (s_param_row_cursor != param_cursor)
    {
        /* Cursor moved: redraw only old/new value rows + marker */
        DrawParamRow(s_param_row_cursor, chord, 0);
        DrawParamRow(param_cursor, chord, 1);
    }
    else
    {
        /* Cursor same: update current value only */
        DrawParamRow(param_cursor, chord, 1);
    }

    if (!s_chord_params_direct_mode)
    {
        s_param_row_cursor = param_cursor;

        /* Title */
        char title[20];
        snprintf(title, sizeof(title), "Bar %u Parameters", step);
        MenuTemplate_DrawHeader(title, "Per-step Parameters", COLOR_TEXT_MUTED);
    }

    /* Footer action buttons */
    if (!s_param_footer_valid)
    {
        FillRegion(UI_REGION_MENU_FOOTER, COLOR_PANEL);
        UI_Display_DrawParamFooterActions(footer_action);
        s_param_footer_valid = 1;
    }
    else
    {
        UI_Display_DrawParamFooterActions(footer_action);
    }
}

void UI_Display_NavigateChordParams(int8_t delta, uint8_t step, ChordParams* chord, uint8_t param_cursor, uint8_t footer_action)
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
        default:
            break;
    }

    /* Redraw parameters */
    UI_Display_DrawChordParams(step, chord, param_cursor, footer_action);
}

void UI_Display_DrawParamFooterActions(uint8_t selected_action)
{
    if (s_chord_params_direct_mode)
    {
        (void)selected_action;
        FillRegion(UI_REGION_MENU_FOOTER, COLOR_PANEL);
        DrawCenteredFooterButton("M9 PLACE", 1u);
        return;
    }

    static const char* labels[PARAM_ACTION_COUNT] = {"BACK", "PREV", "NEXT", "SAVE"};
    FillRegion(UI_REGION_MENU_FOOTER, COLOR_PANEL);

    for (uint8_t i = 0; i < PARAM_ACTION_COUNT; i++)
    {
        DrawParamFooterButton(i, labels[i], i == selected_action);
    }
}

void UI_Display_NavigateParamFooterActions(int8_t delta, uint8_t step, const ChordParams* chord, uint8_t param_cursor, uint8_t* footer_action)
{
    if (s_chord_params_direct_mode)
    {
        (void)delta;
        (void)step;
        (void)chord;
        (void)param_cursor;
        if (footer_action) *footer_action = PARAM_ACTION_MAIN;
        s_param_footer_selection = PARAM_ACTION_MAIN;
        return;
    }

    uint8_t old = *footer_action;
    int16_t next = (int16_t)old + delta;
    while (next < 0) next += PARAM_ACTION_COUNT;
    while (next >= PARAM_ACTION_COUNT) next -= PARAM_ACTION_COUNT;

    *footer_action = (uint8_t)next;

    DrawParamFooterButton(old,
                          (old == PARAM_ACTION_MAIN) ? "BACK" :
                          (old == PARAM_ACTION_PREV) ? "PREV" :
                          (old == PARAM_ACTION_NEXT) ? "NEXT" : "SAVE",
                          0);
    DrawParamFooterButton(*footer_action,
                          (*footer_action == PARAM_ACTION_MAIN) ? "BACK" :
                          (*footer_action == PARAM_ACTION_PREV) ? "PREV" :
                          (*footer_action == PARAM_ACTION_NEXT) ? "NEXT" : "SAVE",
                          1);
}

void UI_Display_SetChordParamsDirectMode(uint8_t enabled)
{
    if (s_chord_params_direct_mode != (enabled ? 1u : 0u)) UI_Display_CancelDirectChordFrame();
    s_chord_params_direct_mode = enabled ? 1u : 0u;
    if (s_chord_params_direct_mode)
    {
        s_param_footer_selection = PARAM_ACTION_MAIN;
    }
}

void UI_Display_SetDirectChordInfo(uint8_t bar_number, uint8_t pos_number, uint8_t grid_division, const uint8_t notes[4])
{
    s_direct_chord_bar_number = bar_number;
    s_direct_chord_pos_number = pos_number;
    s_direct_chord_grid_division = grid_division;

    for (uint8_t i = 0u; i < 4u; ++i)
    {
        s_direct_chord_notes[i] = (notes != NULL) ? notes[i] : 0xFFu;
    }
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

static void DrawTimingRow(uint8_t index,
                          uint8_t step_count,
                          uint8_t step_division,
                          uint8_t ts_num,
                          uint8_t ts_den,
                          uint8_t swing,
                          uint8_t is_selected,
                          uint8_t draw_label)
{
    const uint16_t y_positions[] = {
        PARAM_ROW_Y0,
        PARAM_ROW_Y0 + PARAM_ROW_STEP,
        PARAM_ROW_Y0 + (PARAM_ROW_STEP * 2),
        PARAM_ROW_Y0 + (PARAM_ROW_STEP * 3),
        PARAM_ROW_Y0 + (PARAM_ROW_STEP * 4)
    };
    const uint16_t value_x = (uint16_t)(SCREEN_W - 80);
    uint16_t y = y_positions[index];
    uint16_t color = is_selected ? COLOR_ACTIVE : COLOR_TEXT_MAIN;

    ST7789_FillRect(0, y, 3, 22, is_selected ? COLOR_ACTIVE : COLOR_BG);
    ST7789_FillRect(value_x, y, (uint16_t)(SCREEN_W - value_x), 22, COLOR_BG);

    const uint16_t label_color = COLOR_TEXT_MAIN;

    const char* div_text = "1/16";
    switch (step_division)
    {
        case 1: div_text = "1/4"; break;
        case 2: div_text = "1/8"; break;
        case 4: div_text = "1/16"; break;
        default: break;
    }

    if (index == 0)
    {
        char b[12];
        snprintf(b, sizeof(b), "%u", step_count);
        if (draw_label) ST7789_DrawString(8, y, "Pattern Loop", &Font12x20, label_color, COLOR_BG);
        ST7789_DrawString(value_x, y, b,      &Font12x20, color, COLOR_BG);
    }
    else if (index == 1)
    {
        if (draw_label) ST7789_DrawString(8, y, "Grid",  &Font12x20, label_color, COLOR_BG);
        ST7789_DrawString(value_x, y, div_text, &Font12x20, color, COLOR_BG);
    }
    else if (index == 2)
    {
        char b[12];
        snprintf(b, sizeof(b), "%u", ts_num);
        if (draw_label) ST7789_DrawString(8, y, "Beats Per Bar", &Font12x20, label_color, COLOR_BG);
        ST7789_DrawString(value_x, y, b,      &Font12x20, color, COLOR_BG);
    }
    else if (index == 3)
    {
        char b[12];
        snprintf(b, sizeof(b), "%u", ts_den);
        if (draw_label) ST7789_DrawString(8, y, "Beat Unit",  &Font12x20, label_color, COLOR_BG);
        ST7789_DrawString(value_x, y, b,      &Font12x20, color, COLOR_BG);
    }
    else if (index == 4)
    {
        char b[12];
        snprintf(b, sizeof(b), "%u%%", swing);
        if (draw_label) ST7789_DrawString(8, y, "Swing",      &Font12x20, label_color, COLOR_BG);
        ST7789_DrawString(value_x, y, b,      &Font12x20, color, COLOR_BG);
    }
}

void UI_Display_DrawTimingMenu(uint8_t step_count,
                               uint8_t step_division,
                               uint8_t ts_num,
                               uint8_t ts_den,
                               uint8_t swing,
                               uint8_t cursor,
                               uint8_t has_unsaved_changes,
                               uint8_t save_rejected)
{
    MenuTemplate_Begin(MENU_FRAME_TIMING);
    const TimingFrame current = {step_count, step_division, ts_num, ts_den,
                                  swing, cursor, has_unsaved_changes, save_rejected};
    if (s_timing_frame_job.active)
    {
        TimeCommitProbe_TimingDeferred(memcmp(&s_timing_latest, &current, sizeof(current)) == 0 ? 1u : 0u);
        s_timing_latest = current;
        return;
    }
    s_timing_latest = current;

    uint8_t first_draw = (s_timing_row_cursor == 0xFF) ? 1u : 0u;
    uint8_t value_changed = 0u;

    if (s_timing_prev_step_count != step_count) value_changed = 1u;
    if (s_timing_prev_step_division != step_division) value_changed = 1u;
    if (s_timing_prev_ts_num != ts_num) value_changed = 1u;
    if (s_timing_prev_ts_den != ts_den) value_changed = 1u;
    if (s_timing_prev_swing != swing) value_changed = 1u;

    if (first_draw)
    {
        s_timing_frame = current;
        s_timing_followup = 0u;
        TimeCommitProbe_TimingFrameBegin(UiFrameJob_Begin(&s_timing_frame_job,
            (uint8_t)((SCREEN_H + TIMING_STRIP_H - 1u) / TIMING_STRIP_H)));
        return;
    }
    else if (s_timing_row_cursor != cursor)
    {
        DrawTimingRow(s_timing_row_cursor, step_count, step_division, ts_num, ts_den, swing, 0u, 0u);
        DrawTimingRow(cursor, step_count, step_division, ts_num, ts_den, swing, 1u, 0u);
    }
    else if (value_changed)
    {
        DrawTimingRow(cursor, step_count, step_division, ts_num, ts_den, swing, 1u, 0u);
    }

    s_timing_row_cursor = cursor;

    const char* indicator = save_rejected ? "INVALID" :
        (has_unsaved_changes ? "*UNSAVED" : "Global sequencer timing");
    if (!s_menu_header_valid || strcmp(s_menu_header_indicator, indicator) != 0)
        ST7789_FillRect(0u, MENU_TOP_Y, SCREEN_W, 2u, COLOR_BG);
    MenuTemplate_DrawHeader("Pattern Timing",
                            save_rejected ? "INVALID" :
                            has_unsaved_changes ? "*UNSAVED" : "Global sequencer timing",
                            save_rejected ? RED : YELLOW);

    if (!s_timing_footer_valid)
    {
        FillRegion(UI_REGION_MENU_FOOTER, COLOR_PANEL);
        s_timing_footer_valid = 1u;
    }

    s_timing_prev_step_count = step_count;
    s_timing_prev_step_division = step_division;
    s_timing_prev_ts_num = ts_num;
    s_timing_prev_ts_den = ts_den;
    s_timing_prev_swing = swing;
    s_timing_prev_unsaved = has_unsaved_changes;
}

uint8_t UI_Display_IsTimingFrameActive(void)
{
    return s_timing_frame_job.active || s_timing_followup;
}

void UI_Display_ResetTimingFrameCache(void)
{
    if (UI_Display_IsTimingFrameActive()) return;
    s_timing_row_cursor = 0xFFu;
    s_timing_footer_valid = 0u;
    s_menu_header_valid = 0u;
}

void UI_Display_CancelTimingFrame(void)
{
    if (s_timing_frame_job.active) TimeCommitProbe_TimingFrameCancel();
    UiFrameJob_Cancel(&s_timing_frame_job);
    s_timing_followup = 0u;
}

static const char* TimingIndicator(const TimingFrame* frame)
{
    return frame->rejected ? "INVALID" : (frame->unsaved ? "*UNSAVED" : "Global sequencer timing");
}

void UI_Display_ServiceTimingFrame(void)
{
    if (s_timing_followup)
    {
        s_timing_followup = 0u;
        const TimingFrame latest = s_timing_latest;
        const uint8_t changed[5] = {
            latest.step_count != s_timing_frame.step_count,
            latest.division != s_timing_frame.division,
            latest.numerator != s_timing_frame.numerator,
            latest.denominator != s_timing_frame.denominator,
            latest.swing != s_timing_frame.swing
        };
        /* Values edited on several rows during construction must all survive the handoff. */
        for (uint8_t row = 0u; row < 5u; ++row)
        {
            if (changed[row] && row != latest.cursor && row != s_timing_frame.cursor)
                DrawTimingRow(row, latest.step_count, latest.division, latest.numerator,
                              latest.denominator, latest.swing, 0u, 0u);
        }
        UI_Display_DrawTimingMenu(latest.step_count, latest.division, latest.numerator,
                                 latest.denominator, latest.swing, latest.cursor,
                                 latest.unsaved, latest.rejected);
        return;
    }
    uint8_t strip;
    if (!UiFrameJob_NextStrip(&s_timing_frame_job, &strip)) return;
    uint8_t* buffer = UI_RenderScratch_Acquire(UI_RENDER_SCRATCH_OWNER_MENU);
    if (!buffer) return;
    TimeCommitProbe_TimingJobBegin();
    const uint16_t strip_y = (uint16_t)(strip * TIMING_STRIP_H);
    const uint16_t strip_h = SCREEN_H - strip_y < TIMING_STRIP_H ?
        (uint16_t)(SCREEN_H - strip_y) : TIMING_STRIP_H;
    const TimingFrame* frame = &s_timing_frame;
    MainGridStripFillRect(buffer, strip_y, strip_h, 0u, strip_y, SCREEN_W, strip_h, COLOR_BG);
    MainGridStripFillRect(buffer, strip_y, strip_h, 0u, 0u, SCREEN_W, MENU_TOP_Y, COLOR_PANEL_ALT);
    const char* labels[5] = {"Pattern Loop", "Grid", "Beats Per Bar", "Beat Unit", "Swing"};
    char values[5][12];
    snprintf(values[0], sizeof(values[0]), "%u", frame->step_count);
    snprintf(values[1], sizeof(values[1]), "%s", frame->division == 1u ? "1/4" :
             (frame->division == 2u ? "1/8" : "1/16"));
    snprintf(values[2], sizeof(values[2]), "%u", frame->numerator);
    snprintf(values[3], sizeof(values[3]), "%u", frame->denominator);
    snprintf(values[4], sizeof(values[4]), "%u%%", frame->swing);
    for (uint8_t row = 0u; row < 5u; ++row)
    {
        const uint16_t y = (uint16_t)(PARAM_ROW_Y0 + row * PARAM_ROW_STEP);
        if (y >= strip_y + strip_h || y + 22u <= strip_y) continue;
        const uint16_t colour = row == frame->cursor ? COLOR_ACTIVE : COLOR_TEXT_MAIN;
        MainGridStripFillRect(buffer, strip_y, strip_h, 0u, y, 3u, 22u,
                             row == frame->cursor ? COLOR_ACTIVE : COLOR_BG);
        MainGridStripDrawString(buffer, strip_y, strip_h, 8u, y, labels[row],
                               &Font12x20, 1u, COLOR_TEXT_MAIN, COLOR_BG);
        MainGridStripDrawString(buffer, strip_y, strip_h, SCREEN_W - 80u, y, values[row],
                               &Font12x20, 1u, colour, COLOR_BG);
    }
    MainGridStripDrawString(buffer, strip_y, strip_h, 4u, 4u, "Pattern Timing",
                           &Font12x20, 1u, COLOR_TEXT_MAIN, COLOR_PANEL_ALT);
    MainGridStripDrawString(buffer, strip_y, strip_h, 4u, 22u, TimingIndicator(frame),
                           &Font12x20, 1u, frame->rejected ? RED : YELLOW, COLOR_PANEL_ALT);
    MainGridStripFillRect(buffer, strip_y, strip_h, 0u, MENU_FOOTER_Y, SCREEN_W,
                         UI_REGION_MENU_FOOTER_H, COLOR_PANEL);
    ST7789_DrawRGB565Buffer(0u, strip_y, SCREEN_W, strip_h, buffer,
                            (uint32_t)SCREEN_W * strip_h * 2u);
    UI_RenderScratch_Release(UI_RENDER_SCRATCH_OWNER_MENU);
    TimeCommitProbe_TimingJobEnd();
    if (!UiFrameJob_CompleteStrip(&s_timing_frame_job)) return;
    s_timing_row_cursor = frame->cursor;
    s_timing_prev_step_count = frame->step_count;
    s_timing_prev_step_division = frame->division;
    s_timing_prev_ts_num = frame->numerator;
    s_timing_prev_ts_den = frame->denominator;
    s_timing_prev_swing = frame->swing;
    s_timing_prev_unsaved = frame->unsaved;
    s_timing_footer_valid = 1u;
    StrCopy(s_menu_header_title, sizeof(s_menu_header_title), "Pattern Timing");
    StrCopy(s_menu_header_indicator, sizeof(s_menu_header_indicator), TimingIndicator(frame));
    s_menu_header_indicator_color = frame->rejected ? RED : YELLOW;
    s_menu_header_valid = 1u;
    s_timing_followup = memcmp(frame, &s_timing_latest, sizeof(*frame)) != 0 ? 1u : 0u;
    TimeCommitProbe_TimingFrameComplete();
}

static void DrawQuantiserTimingRow(uint8_t index,
                                   uint8_t enabled,
                                   uint8_t grid_division,
                                   uint8_t strength,
                                   uint8_t humanize_ms,
                                   uint8_t lag_ms,
                                   uint8_t is_selected,
                                   uint8_t draw_label)
{
    const uint16_t y_positions[] = {
        PARAM_ROW_Y0,
        PARAM_ROW_Y0 + PARAM_ROW_STEP,
        PARAM_ROW_Y0 + (PARAM_ROW_STEP * 2),
        PARAM_ROW_Y0 + (PARAM_ROW_STEP * 3),
        PARAM_ROW_Y0 + (PARAM_ROW_STEP * 4)
    };
    const uint16_t value_x = (uint16_t)(SCREEN_W - 80);
    const char* grid_text[] = {"1/4", "1/8", "1/16", "1/32"};
    uint16_t y = y_positions[index];
    uint16_t color = is_selected ? COLOR_ACTIVE : COLOR_TEXT_MAIN;

    ST7789_FillRect(0, y, 3, 22, is_selected ? COLOR_ACTIVE : COLOR_BG);
    ST7789_FillRect(value_x, y, (uint16_t)(SCREEN_W - value_x), 22, COLOR_BG);

    if (index == 0)
    {
        if (draw_label) ST7789_DrawString(8, y, "Quantize", &Font12x20, COLOR_TEXT_MAIN, COLOR_BG);
        ST7789_DrawString(value_x, y, enabled ? "ON" : "OFF", &Font12x20, color, COLOR_BG);
    }
    else if (index == 1)
    {
        uint8_t grid_idx = (grid_division > 3) ? 3 : grid_division;
        if (draw_label) ST7789_DrawString(8, y, "Grid", &Font12x20, COLOR_TEXT_MAIN, COLOR_BG);
        ST7789_DrawString(value_x, y, grid_text[grid_idx], &Font12x20, color, COLOR_BG);
    }
    else if (index == 2)
    {
        char b[12];
        snprintf(b, sizeof(b), "%u%%", strength);
        if (draw_label) ST7789_DrawString(8, y, "Amount", &Font12x20, COLOR_TEXT_MAIN, COLOR_BG);
        ST7789_DrawString(value_x, y, b, &Font12x20, color, COLOR_BG);
    }
    else if (index == 3)
    {
        char b[12];
        snprintf(b, sizeof(b), "%ums", humanize_ms);
        if (draw_label) ST7789_DrawString(8, y, "Humanize", &Font12x20, COLOR_TEXT_MAIN, COLOR_BG);
        ST7789_DrawString(value_x, y, b, &Font12x20, color, COLOR_BG);
    }
    else if (index == 4)
    {
        char b[12];
        snprintf(b, sizeof(b), "%ums", lag_ms);
        if (draw_label) ST7789_DrawString(8, y, "Lag", &Font12x20, COLOR_TEXT_MAIN, COLOR_BG);
        ST7789_DrawString(value_x, y, b, &Font12x20, color, COLOR_BG);
    }
}

void UI_Display_DrawQuantiserTimingMenu(uint8_t enabled,
                                        uint8_t grid_division,
                                        uint8_t strength,
                                        uint8_t humanize_ms,
                                        uint8_t lag_ms,
                                        uint8_t cursor,
                                        uint8_t has_unsaved_changes)
{
    MenuTemplate_Begin(MENU_FRAME_QUANT_TIMING);

    uint8_t first_draw = (s_timing_row_cursor == 0xFF) ? 1u : 0u;
    uint8_t value_changed = 0u;

    if (s_timing_prev_step_count != enabled) value_changed = 1u;
    if (s_timing_prev_step_division != grid_division) value_changed = 1u;
    if (s_timing_prev_ts_num != strength) value_changed = 1u;
    if (s_timing_prev_ts_den != humanize_ms) value_changed = 1u;
    if (s_timing_prev_swing != lag_ms) value_changed = 1u;

    if (first_draw)
    {
        FillRegion(UI_REGION_MENU_LIST, COLOR_BG);
        for (uint8_t i = 0; i < 5; i++)
        {
            DrawQuantiserTimingRow(i, enabled, grid_division, strength, humanize_ms, lag_ms, (i == cursor), 1u);
        }
    }
    else if (s_timing_row_cursor != cursor)
    {
        DrawQuantiserTimingRow(s_timing_row_cursor, enabled, grid_division, strength, humanize_ms, lag_ms, 0u, 0u);
        DrawQuantiserTimingRow(cursor, enabled, grid_division, strength, humanize_ms, lag_ms, 1u, 0u);
    }
    else if (value_changed)
    {
        DrawQuantiserTimingRow(cursor, enabled, grid_division, strength, humanize_ms, lag_ms, 1u, 0u);
    }

    s_timing_row_cursor = cursor;

    MenuTemplate_DrawHeader("Quantiser Timing", has_unsaved_changes ? "*UNSAVED" : "Timing quantise setup", YELLOW);

    if (!s_timing_footer_valid)
    {
        FillRegion(UI_REGION_MENU_FOOTER, COLOR_PANEL);
        s_timing_footer_valid = 1u;
    }

    s_timing_prev_step_count = enabled;
    s_timing_prev_step_division = grid_division;
    s_timing_prev_ts_num = strength;
    s_timing_prev_ts_den = humanize_ms;
    s_timing_prev_swing = lag_ms;
    s_timing_prev_unsaved = has_unsaved_changes;
}

void UI_Display_DrawQuanRouterMenu(const uint8_t* sources,
                                   const uint8_t* targets,
                                   uint8_t cursor,
                                   uint8_t edit_field,
                                   uint8_t has_unsaved_changes)
{
    static const char* src_names[] = {"OFF", "ADC-IN1", "ADC-IN2", "ADC-IN3", "ADC-IN4"};
    static const char* tgt_names[] = {"OFF", "CV1", "CV2", "CV3", "CV4", "GATE1", "GATE2", "GATE3", "GATE4"};
    const uint16_t col_slot_x = 10u;
    const uint16_t col_src_x = (uint16_t)(SCREEN_W / 3u);
    const uint16_t col_tgt_x = (uint16_t)((SCREEN_W * 2u) / 3u);
    const uint16_t row_y0 = UI_REGION_MENU_LIST.y + 1u;
    const uint16_t row_h = 17u;
    const uint16_t row_text_y_off = 1u;

    uint8_t first_draw;

    MenuTemplate_Begin(MENU_FRAME_QUAN_ROUTER);
    first_draw = (s_quan_router_prev_cursor == 0xFF) ? 1u : 0u;

    if (first_draw || (s_quan_router_prev_unsaved != has_unsaved_changes))
    {
        MenuTemplate_DrawHeader("Quantizer CV Router",
                                has_unsaved_changes ? "*UNSAVED step 3..10" : "step 3..10 assignment",
                                YELLOW);
    }

    if (first_draw)
    {
        FillRegion(UI_REGION_MENU_LIST, COLOR_BG);
        for (uint8_t i = 0; i < 8; i++)
        {
            uint16_t y = (uint16_t)(row_y0 + (i * row_h));
            uint8_t selected = (i == cursor) ? 1u : 0u;
            uint16_t text_col = selected ? COLOR_ACTIVE : COLOR_TEXT_MAIN;
            uint16_t bg_col = COLOR_BG;
            uint8_t src_idx = sources ? sources[i] : 0u;
            uint8_t tgt_idx = targets ? targets[i] : 0u;
            char slot_txt[8];
            char src_txt[10];
            char tgt_txt[12];

            if (src_idx >= 5) src_idx = 0;
            if (tgt_idx >= 9) tgt_idx = 0;

            ST7789_FillRect(0, y, SCREEN_W, row_h, bg_col);
            ST7789_FillRect(0, y, 4, row_h, selected ? COLOR_ACTIVE : COLOR_BG);

            snprintf(slot_txt, sizeof(slot_txt), "S%02u", (unsigned)(i + 3));
            snprintf(src_txt, sizeof(src_txt), "%s%s",
                     (selected && edit_field == 0) ? ">" : " ", src_names[src_idx]);
            snprintf(tgt_txt, sizeof(tgt_txt), "%s%s",
                     (selected && edit_field == 1) ? ">" : " ", tgt_names[tgt_idx]);

            ST7789_DrawString(col_slot_x, (uint16_t)(y + row_text_y_off), slot_txt, &Font12x20, text_col, bg_col);
            ST7789_DrawString(col_src_x, (uint16_t)(y + row_text_y_off), src_txt, &Font12x20, text_col, bg_col);
            ST7789_DrawString(col_tgt_x, (uint16_t)(y + row_text_y_off), tgt_txt, &Font12x20, text_col, bg_col);
        }
    }
    else
    {
        for (uint8_t i = 0; i < 8; i++)
        {
            uint8_t needs_row_redraw = 0u;
            uint8_t src_idx = sources ? sources[i] : 0u;
            uint8_t tgt_idx = targets ? targets[i] : 0u;

            if (src_idx != s_quan_router_prev_source[i]) needs_row_redraw = 1u;
            if (tgt_idx != s_quan_router_prev_target[i]) needs_row_redraw = 1u;
            if (i == cursor && s_quan_router_prev_edit_field != edit_field) needs_row_redraw = 1u;
            if (i == cursor && s_quan_router_prev_cursor != cursor) needs_row_redraw = 1u;
            if (i == s_quan_router_prev_cursor && s_quan_router_prev_cursor != cursor) needs_row_redraw = 1u;

            if (needs_row_redraw)
            {
                uint16_t y = (uint16_t)(row_y0 + (i * row_h));
                uint8_t selected = (i == cursor) ? 1u : 0u;
                uint16_t text_col = selected ? COLOR_ACTIVE : COLOR_TEXT_MAIN;
                uint16_t bg_col = COLOR_BG;
                char slot_txt[8];
                char src_txt[10];
                char tgt_txt[12];

                if (src_idx >= 5) src_idx = 0;
                if (tgt_idx >= 9) tgt_idx = 0;

                ST7789_FillRect(0, y, SCREEN_W, row_h, bg_col);
                ST7789_FillRect(0, y, 4, row_h, selected ? COLOR_ACTIVE : COLOR_BG);

                snprintf(slot_txt, sizeof(slot_txt), "S%02u", (unsigned)(i + 3));
                snprintf(src_txt, sizeof(src_txt), "%s%s",
                         (selected && edit_field == 0) ? ">" : " ", src_names[src_idx]);
                snprintf(tgt_txt, sizeof(tgt_txt), "%s%s",
                         (selected && edit_field == 1) ? ">" : " ", tgt_names[tgt_idx]);

                ST7789_DrawString(col_slot_x, (uint16_t)(y + row_text_y_off), slot_txt, &Font12x20, text_col, bg_col);
                ST7789_DrawString(col_src_x, (uint16_t)(y + row_text_y_off), src_txt, &Font12x20, text_col, bg_col);
                ST7789_DrawString(col_tgt_x, (uint16_t)(y + row_text_y_off), tgt_txt, &Font12x20, text_col, bg_col);
            }
        }
    }

    if (!s_timing_footer_valid)
    {
        FillRegion(UI_REGION_MENU_FOOTER, COLOR_PANEL);
        s_timing_footer_valid = 1u;
    }

    for (uint8_t i = 0; i < 8; i++)
    {
        s_quan_router_prev_source[i] = sources ? sources[i] : 0u;
        s_quan_router_prev_target[i] = targets ? targets[i] : 0u;
    }
    s_quan_router_prev_cursor = cursor;
    s_quan_router_prev_edit_field = edit_field;
    s_quan_router_prev_unsaved = has_unsaved_changes;
}

void UI_Display_DrawArpMenu(uint8_t arp_mode,
                            uint8_t arp_rate,
                            uint8_t cursor,
                            uint8_t has_unsaved_changes)
{
    static const char* mode_labels[] = {"Block", "Up", "Down", "UpDn", "Random"};
    static const char* rate_labels[] = {"1/4", "1/8", "1/16", "1/32"};
    const uint16_t value_x = (uint16_t)(SCREEN_W - 96);
    const uint16_t label_color = COLOR_TEXT_MAIN;
    char value[16];

    if (arp_mode > 4u) arp_mode = 0u;
    if (arp_rate > 3u) arp_rate = 2u;
    if (cursor > 1u) cursor = 0u;

    MenuTemplate_Begin(MENU_FRAME_TIMING);

    {
        uint8_t first_draw = (s_timing_row_cursor == 0xFF) ? 1u : 0u;
        uint8_t value_changed = (s_arp_prev_mode != arp_mode) || (s_arp_prev_rate != arp_rate);

        if (first_draw)
        {
            FillRegion(UI_REGION_MENU_LIST, COLOR_BG);

            ST7789_DrawString(8, PARAM_ROW_Y0, "Mode", &Font12x20, label_color, COLOR_BG);
            ST7789_DrawString(8, (uint16_t)(PARAM_ROW_Y0 + PARAM_ROW_STEP), "Rate", &Font12x20, label_color, COLOR_BG);
        }

        if (first_draw || s_timing_row_cursor != cursor || value_changed)
        {
            /* Row 0: mode */
            ST7789_FillRect(0, PARAM_ROW_Y0, 3, 22, (cursor == 0u) ? COLOR_ACTIVE : COLOR_BG);
            ST7789_FillRect(value_x, PARAM_ROW_Y0, (uint16_t)(SCREEN_W - value_x), 22, COLOR_BG);
            snprintf(value, sizeof(value), "%s", mode_labels[arp_mode]);
            ST7789_DrawString(value_x, PARAM_ROW_Y0, value, &Font12x20,
                              (cursor == 0u) ? COLOR_ACTIVE : COLOR_TEXT_MAIN,
                              COLOR_BG);

            /* Row 1: rate */
            ST7789_FillRect(0, (uint16_t)(PARAM_ROW_Y0 + PARAM_ROW_STEP), 3, 22,
                            (cursor == 1u) ? COLOR_ACTIVE : COLOR_BG);
            ST7789_FillRect(value_x, (uint16_t)(PARAM_ROW_Y0 + PARAM_ROW_STEP),
                            (uint16_t)(SCREEN_W - value_x), 22, COLOR_BG);
            snprintf(value, sizeof(value), "%s", rate_labels[arp_rate]);
            ST7789_DrawString(value_x, (uint16_t)(PARAM_ROW_Y0 + PARAM_ROW_STEP), value, &Font12x20,
                              (cursor == 1u) ? COLOR_ACTIVE : COLOR_TEXT_MAIN,
                              COLOR_BG);
        }

        s_timing_row_cursor = cursor;
        s_arp_prev_mode = arp_mode;
        s_arp_prev_rate = arp_rate;
    }

    MenuTemplate_DrawHeader("Pattern ARP",
                            has_unsaved_changes ? "*UNSAVED" : "Mode and rate",
                            YELLOW);

    if (!s_timing_footer_valid)
    {
        FillRegion(UI_REGION_MENU_FOOTER, COLOR_PANEL);
        s_timing_footer_valid = 1u;
    }
}

static void DrawSongChainRow(const uint8_t* chain,
                             uint8_t length,
                             uint8_t page_base,
                             uint8_t row,
                             uint8_t cursor,
                             uint8_t playing_slot,
                             uint8_t blink_on)
{
    uint8_t pos = (uint8_t)(page_base + row);
    uint16_t y = (uint16_t)(SONG_ROW_Y0 + (row * SONG_ROW_STEP));
    uint8_t selected = (pos == cursor) ? 1u : 0u;
    uint16_t color = selected ? COLOR_ACTIVE : COLOR_TEXT_MAIN;

    ST7789_FillRect(0, y, SCREEN_W, 22, COLOR_BG);

    if (pos < length)
    {
        char slot_label[20];
        char pat_label[16];
        snprintf(slot_label, sizeof(slot_label), "%u Slot %u", (unsigned)(row + 1), (unsigned)(pos + 1));
        snprintf(pat_label, sizeof(pat_label), "P%02u", (unsigned)(chain[pos] + 1));
        ST7789_DrawString(4, y, slot_label, &Font12x20, color, COLOR_BG);
        ST7789_DrawString((uint16_t)(SCREEN_W - 56), y, pat_label, &Font12x20, color, COLOR_BG);

        if (pos == playing_slot)
        {
            uint16_t outline = blink_on ? LIGHTBLUE : COLOR8(12, 22, 36);
            DrawRectBorder(0, y, SCREEN_W, 22, outline);
        }
    }
    else
    {
        char slot_label[20];
        snprintf(slot_label, sizeof(slot_label), "%u Slot --", (unsigned)(row + 1));
        ST7789_DrawString(4, y, slot_label, &Font12x20, COLOR_TEXT_MUTED, COLOR_BG);
    }
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

    if (length < 1) length = 1;
    if (length > 32) length = 32;
    if (cursor >= length) cursor = (uint8_t)(length - 1);

    uint8_t page_base = (uint8_t)((cursor / 4) * 4);
    uint8_t full_redraw = 0u;

    if (!s_song_page_valid) full_redraw = 1u;
    if (s_song_prev_page_base != page_base) full_redraw = 1u;
    if (s_song_prev_length != length) full_redraw = 1u;

    if (full_redraw)
    {
        FillRegion(UI_REGION_MENU_LIST, COLOR_BG);
        for (uint8_t i = 0; i < 4; i++)
        {
            DrawSongChainRow(chain, length, page_base, i, cursor, playing_slot, blink_on);
            {
                uint8_t pos = (uint8_t)(page_base + i);
                s_song_prev_chain_page[i] = (pos < length) ? chain[pos] : 0xFF;
            }
        }
        s_song_page_valid = 1;
    }
    else
    {
        if (s_song_prev_cursor != cursor)
        {
            uint8_t old_row = (uint8_t)(s_song_prev_cursor - page_base);
            uint8_t new_row = (uint8_t)(cursor - page_base);
            if (old_row < 4) DrawSongChainRow(chain, length, page_base, old_row, cursor, playing_slot, blink_on);
            if (new_row < 4 && new_row != old_row) DrawSongChainRow(chain, length, page_base, new_row, cursor, playing_slot, blink_on);
        }

        if (s_song_prev_playing_slot != playing_slot || s_song_prev_blink_on != blink_on)
        {
            if (s_song_prev_playing_slot >= page_base && s_song_prev_playing_slot < (uint8_t)(page_base + 4))
            {
                DrawSongChainRow(chain,
                                 length,
                                 page_base,
                                 (uint8_t)(s_song_prev_playing_slot - page_base),
                                 cursor,
                                 playing_slot,
                                 blink_on);
            }
            if (playing_slot >= page_base && playing_slot < (uint8_t)(page_base + 4) && playing_slot != s_song_prev_playing_slot)
            {
                DrawSongChainRow(chain,
                                 length,
                                 page_base,
                                 (uint8_t)(playing_slot - page_base),
                                 cursor,
                                 playing_slot,
                                 blink_on);
            }
        }

        for (uint8_t i = 0; i < 4; i++)
        {
            uint8_t pos = (uint8_t)(page_base + i);
            uint8_t now_val = (pos < length) ? chain[pos] : 0xFF;
            if (s_song_prev_chain_page[i] != now_val)
            {
                DrawSongChainRow(chain, length, page_base, i, cursor, playing_slot, blink_on);
                s_song_prev_chain_page[i] = now_val;
            }
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

    if (!s_song_footer_valid)
    {
        FillRegion(UI_REGION_MENU_FOOTER, COLOR_PANEL);
        ST7789_DrawString(6, FOOTER_TEXT_Y1, "9+ 10- Slot", &Font12x20, COLOR_TEXT_MAIN, COLOR_PANEL);
        ST7789_DrawString(6, FOOTER_TEXT_Y2, "11< 12> Slot", &Font12x20, COLOR_TEXT_MAIN, COLOR_PANEL);
        ST7789_DrawString(142, MENU_FOOTER_Y + 17, "REC MAIN", &Font12x20, COLOR_ACTIVE, COLOR_PANEL);
        s_song_footer_valid = 1;
    }

    s_song_prev_page_base = page_base;
    s_song_prev_cursor = cursor;
    s_song_prev_playing_slot = playing_slot;
    s_song_prev_blink_on = blink_on;
    s_song_prev_length = length;
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
    ST7789_DrawString((uint16_t)(x + (btn_w - 50) / 2), (uint16_t)(y + 18), labels[slot], &Font12x20, text_col, bg);
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
    if (!s_chord_footer_valid)
    {
        FillRegion(UI_REGION_MENU_FOOTER, COLOR_PANEL);
        DrawCenteredFooterButton(" MAIN BARS", 1);
        s_chord_footer_valid = 1;
    }
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
static uint8_t s_step_roll_layout_valid = 0;
static uint8_t s_step_roll_last_step = 0xFF;
static uint16_t s_step_roll_last_note_mask = 0;
static uint8_t s_step_roll_last_selected_key = 0xFF;

/* ── Step piano roll layout constants ─────────────────────────────────────
 *
 * Header  y = 0 .. ROLL_HEADER_H-1          (48 px — matches main screen
 *                                             header+status so no bleed)
 * Rows    y = ROLL_HEADER_H .. +12*ROW_H-1  (12 × 13 px)
 * Footer  y = SCREEN_H-ROLL_FOOTER_H ..end  (35 px, dark panel)
 *
 * Each draw function is fully self-contained: it fills every pixel it
 * owns before writing anything on top.  No function may write outside
 * its declared region.
 * ────────────────────────────────────────────────────────────────────── */
#define ROLL_HEADER_H   48u
#define ROLL_FOOTER_H   35u
#define ROLL_FOOTER_Y   ((uint16_t)(SCREEN_H - ROLL_FOOTER_H))
#define ROLL_GRID_Y     ROLL_HEADER_H
#define ROLL_ROW_H      13u
#define ROLL_LABEL_W    44u
#define ROLL_GRID_X     ROLL_LABEL_W
#define ROLL_GRID_W     ((uint16_t)(SCREEN_W - ROLL_LABEL_W))

/* Colour palette for the roll */
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

static uint8_t RollIsBlack(uint8_t note)
{
    return (note == 1u || note == 3u || note == 6u || note == 8u || note == 10u) ? 1u : 0u;
}

/*
 * RollDrawRow — paints one complete horizontal row (label + grid lane).
 *
 * Owns the rectangle:  x=0, y=row_y, w=SCREEN_W, h=ROLL_ROW_H
 * Every pixel in that rectangle is written exactly once, in order:
 *   1. Label background fill
 *   2. Key graphic on top of label
 *   3. Grid lane background fill
 *   4. Selection highlight (if selected)
 *   5. Note block (if note is on)
 *   6. Row separator line (bottom edge, always last)
 */
static void RollDrawRow(uint8_t note)
{
    uint8_t  row    = (uint8_t)(11u - note);
    uint16_t y      = (uint16_t)(ROLL_GRID_Y + (uint16_t)row * ROLL_ROW_H);
    uint8_t  on     = (s_piano_note_mask  & (1u << note)) ? 1u : 0u;
    uint8_t  sel    = (s_selected_piano_key == note)      ? 1u : 0u;
    uint8_t  black  = RollIsBlack(note);

    /* ── 1. Label area: full white background ── */
    ST7789_FillRect(0u, y, ROLL_LABEL_W, ROLL_ROW_H, ROLL_COL_WHITE_KEY_BG);

    /* ── 2. Key graphic ── */
    if (black)
    {
        /* Black-key inset: shorter and inset from left */
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

    /* Selection indicator on label: 2-px yellow left edge */
    if (sel)
    {
        ST7789_FillRect(0u, y, 2u, ROLL_ROW_H, ROLL_COL_SEL_BORDER);
    }

    /* Key right-edge divider */
    ST7789_FillRect((uint16_t)(ROLL_LABEL_W - 1u), y, 1u, ROLL_ROW_H, ROLL_COL_KEY_DIVIDER);

    /* ── 3. Grid lane background ── */
    uint16_t lane_bg = black ? ROLL_COL_LANE_BLACK : ROLL_COL_LANE_WHITE;
    ST7789_FillRect(ROLL_GRID_X, y, ROLL_GRID_W, ROLL_ROW_H, lane_bg);

    /* ── 4. Selection highlight: top and bottom border on lane ── */
    if (sel)
    {
        ST7789_FillRect(ROLL_GRID_X, y,
                        ROLL_GRID_W, 1u, ROLL_COL_SEL_BORDER);
        ST7789_FillRect(ROLL_GRID_X, (uint16_t)(y + ROLL_ROW_H - 2u),
                        ROLL_GRID_W, 1u, ROLL_COL_SEL_BORDER);
    }

    /* ── 5. Note block ── */
    if (on)
    {
        uint16_t bh = 5u;
        uint16_t by = (uint16_t)(y + (ROLL_ROW_H - bh) / 2u);
        ST7789_FillRect(ROLL_GRID_X, by, ROLL_GRID_W, bh, ROLL_COL_NOTE_BLOCK);
    }

    /* ── 6. Row separator — always last, never overwritten ── */
    ST7789_FillRect(0u, (uint16_t)(y + ROLL_ROW_H - 1u), SCREEN_W, 1u, ROLL_COL_ROW_SEP);
}

/*
 * RollDrawHeader — paints exactly y=0..ROLL_HEADER_H-1.
 * Never writes a single pixel below that line.
 */
static void RollDrawHeader(void)
{
    char step_label[12];
    char chord_name[20];

    snprintf(step_label, sizeof(step_label), "BAR %u", s_step_roll_step);
    Bridge_GetStepChordDisplayName((uint8_t)(s_step_roll_step - 1u), chord_name, sizeof(chord_name));

    /* Fill entire header band */
    ST7789_FillRect(0u, 0u, SCREEN_W, ROLL_HEADER_H, COLOR_PANEL_ALT);

    /* Bar number, top-left */
    ST7789_DrawString(8u, 8u,  step_label, &Font12x20, COLOR_TEXT_MAIN, COLOR_PANEL_ALT);
    /* Chord name, second line */
    ST7789_DrawString(8u, 28u, chord_name, &Font8x12,  COLOR_ACTIVE,    COLOR_PANEL_ALT);

    /* Single 1px divider — same weight as row separators */
    ST7789_FillRect(0u, (uint16_t)(ROLL_HEADER_H - 1u), SCREEN_W, 1u, ROLL_COL_ROW_SEP);
}

static void RollDrawFooter(void)
{
    ST7789_FillRect(0u, ROLL_FOOTER_Y, SCREEN_W, ROLL_FOOTER_H, COLOR_PANEL);
    ST7789_DrawString(6u,   (uint16_t)(ROLL_FOOTER_Y + 12u),
                      "PLAY SAVE", &Font12x20, COLOR_ACTIVE,    COLOR_PANEL);
    ST7789_DrawString(126u, (uint16_t)(ROLL_FOOTER_Y + 12u),
                      "REC BACK",  &Font12x20, COLOR_TEXT_MAIN, COLOR_PANEL);
}

static void RollDrawAllRows(void)
{
    for (uint8_t n = 0u; n < 12u; n++)
    {
        RollDrawRow(n);
    }
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
    uint16_t fill = on ? COLOR8(244, 244, 244) : WHITE;
    uint16_t border = selected ? YELLOW : COLOR8(96, 96, 96);
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
    uint16_t fill = on ? COLOR8(116, 32, 32) : BLACK;
    uint16_t border = selected ? YELLOW : COLOR8(112, 112, 112);
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

// static void DrawPianoKeyByNote(uint8_t note)
// {
//     int8_t white_slot = PianoWhiteSlotForNote(note);
//     if (white_slot >= 0)
//     {
//         DrawPianoWhiteKey((uint8_t)white_slot, note);
//         /* White keys sit below black keys, so redraw black overlay keys. */
//         for (uint8_t i = 0; i < PIANO_BLACK_COUNT; i++)
//         {
//             DrawPianoBlackKey(i, k_piano_black_keys[i]);
//         }
//         return;
//     }

//     int8_t black_slot = PianoBlackSlotForNote(note);
//     if (black_slot >= 0)
//     {
//         DrawPianoBlackKey((uint8_t)black_slot, note);
//     }
// }

// static void DrawPianoKeyboardScreen(const char* title, const char* footer_label, const uint16_t note_mask, uint8_t selected_key)
// {
//     MenuTemplate_Begin(MENU_FRAME_CHORD);
//     FillRegion(UI_REGION_MENU_LIST, COLOR_BG);

//     ST7789_DrawString(4, 4, title, &Font12x20, COLOR_TEXT_MAIN, COLOR_PANEL_ALT);
//     MenuTemplate_DrawHeader(title, k_note_names[(selected_key < 12) ? selected_key : 0], YELLOW);
//     StrCopy(s_piano_header_title, sizeof(s_piano_header_title), title);

//     s_piano_note_mask = note_mask;
//     s_selected_piano_key = (selected_key < 12) ? selected_key : 0;

//     for (uint8_t i = 0; i < PIANO_WHITE_COUNT; i++)
//     {
//         DrawPianoWhiteKey(i, k_piano_white_keys[i]);
//     }

//     for (uint8_t i = 0; i < PIANO_BLACK_COUNT; i++)
//     {
//         DrawPianoBlackKey(i, k_piano_black_keys[i]);
//     }

//     FillRegion(UI_REGION_MENU_FOOTER, COLOR_PANEL);
//     ST7789_DrawString(6, FOOTER_TEXT_Y1, "PLAY SAVE", &Font12x20, COLOR_ACTIVE, COLOR_PANEL);
//     ST7789_DrawString(126, FOOTER_TEXT_Y1, "REC BACK", &Font12x20, COLOR_TEXT_MAIN, COLOR_PANEL);
//     s_chord_footer_valid = 1;
// }

// static void DrawStepPianoRollScreen(uint8_t step, const uint16_t note_mask, uint8_t selected_key)
// {
//     uint8_t  need_layout  = (s_step_roll_layout_valid == 0u);
//     uint8_t  step_changed = (s_step_roll_last_step != step);
//     uint16_t prev_mask    = s_step_roll_last_note_mask;
//     uint8_t  prev_sel     = s_step_roll_last_selected_key;

//     MenuTemplate_Begin(MENU_FRAME_CHORD);

//     /* Update state before drawing so every paint function sees current values */
//     s_piano_note_mask    = note_mask;
//     s_selected_piano_key = (selected_key < 12u) ? selected_key : 0u;
//     s_step_roll_step     = step;

//     if (need_layout)
//     {
//         /* First entry: wipe below header, draw everything from scratch */
//         ST7789_FillRect(0u, ROLL_HEADER_H, SCREEN_W,
//                         (uint16_t)(SCREEN_H - ROLL_HEADER_H), COLOR_BG);
//         RollDrawHeader();
//         RollDrawAllRows();
//         RollDrawFooter();
//         s_chord_footer_valid     = 1;
//         s_step_roll_layout_valid = 1u;
//     }
//     else
//     {
//         if (step_changed)
//         {
//             RollDrawHeader();
//         }

//         if (prev_sel != s_selected_piano_key)
//         {
//             /* Repaint old and new selection rows — nothing else */
//             if (prev_sel < 12u) RollDrawRow(prev_sel);
//             RollDrawRow(s_selected_piano_key);
//         }

//         uint16_t changed = (uint16_t)(prev_mask ^ note_mask);
//         if (changed != 0u)
//         {
//             for (uint8_t n = 0u; n < 12u; n++)
//             {
//                 if (changed & (uint16_t)(1u << n)) RollDrawRow(n);
//             }
//         }
//     }

//     s_step_roll_last_step         = step;
//     s_step_roll_last_note_mask    = note_mask;
//     s_step_roll_last_selected_key = s_selected_piano_key;
// }

void UI_Display_ResetStepPianoRollCache(void)
{
    UI_PianoRollScreen_ResetCache();
}

void UI_Display_DrawPianoKeyboard(const uint16_t note_mask, uint8_t selected_key)
{
    PianoRollContext ctx = {
        .mode = PIANO_ROLL_MODE_KEYBOARD,
        .step = 1,
        .note_mask = note_mask,
        .selected_key = selected_key,
        .selected_midi_note = (uint8_t)(60u + (selected_key % 12u)),
        .top_visible_midi = 71u,
        .slot_index = 0,
        .slot_count = 1,
        .grid_division = 4u,
        .slot_notes = {0xFFu, 0xFFu, 0xFFu, 0xFFu},
        .cv_absolute_valid = 0,
        .arp_on = 0,
        .length_mode_active = 0,
        .pending_length = 1,
        .key_label = "C",
        .scale_label = "Chromatic",
        .clear_confirm_active = 0,
        .clear_confirm_yes_selected = 0,
        .bar_skipped = 0,
        .title = "Create Chord",
        .footer_label = "SAVE/DONE"
    };

    UI_PianoRollScreen_SetContext(&ctx);
    UI_PianoRollScreen_ForceRedraw();

    {
        UiScreen* screen = UI_PianoRollScreen_Get();
        if (screen && screen->on_draw)
        {
            screen->on_draw();
        }
    }
}

void UI_Display_DrawStepPianoRoll(uint8_t step, const uint16_t note_mask, uint8_t selected_key)
{
    /* Delegate to new piano roll module */
    PianoRollContext ctx = {
        .mode = PIANO_ROLL_MODE_STEP_ROLL,
        .step = step,
        .note_mask = note_mask,
        .selected_key = selected_key,
        .selected_midi_note = (uint8_t)(60u + (selected_key % 12u)),
        .top_visible_midi = 71u,
        .slot_index = 0,
        .slot_count = 1,
        .grid_division = 4u,
        .slot_notes = {0xFFu, 0xFFu, 0xFFu, 0xFFu},
        .cv_absolute_valid = 0,
        .arp_on = 0,
        .length_mode_active = 0,
        .pending_length = 1,
        .key_label = "C",
        .scale_label = "Chromatic",
        .clear_confirm_active = 0,
        .clear_confirm_yes_selected = 0,
        .bar_skipped = 0,
        .title = "Piano Roll",
        .footer_label = "SAVE/DONE"
    };
    UI_PianoRollScreen_SetContext(&ctx);
    
    /* Force redraw to keep piano roll visible */
    UI_PianoRollScreen_ForceRedraw();
    
    /* Draw the screen */
    UiScreen* screen = UI_PianoRollScreen_Get();
    if (screen && screen->on_draw)
    {
        screen->on_draw();
    }
}

void UI_Display_NavigatePianoKeyboard(int8_t delta)
{
    UI_PianoRollScreen_NavigateKey(delta);
}

uint8_t UI_Display_GetSelectedPianoKey(void)
{
    return UI_PianoRollScreen_GetSelectedKey();
}

void UI_Display_TogglePianoKey(uint8_t key)
{
    UI_PianoRollScreen_ToggleKey(key);
}

uint16_t UI_Display_GetCurrentNoteMask(void)
{
    return UI_PianoRollScreen_GetNoteMask();
}

void UI_Display_SetPianoNoteMask(uint16_t mask)
{
    UI_PianoRollScreen_SetNoteMask(mask);
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

    ST7789_DrawString(16, 108, name, &Font12x20, COLOR_TEXT_MAIN, COLOR_BOX_BG);

    if (cursor < 16)
    {
        uint16_t cx = 16 + (uint16_t)cursor * 10;
        ST7789_FillRect(cx, 126, 9, 2, YELLOW);
    }

    FillRegion(UI_REGION_MENU_FOOTER, COLOR_PANEL);
    ST7789_DrawString(6, FOOTER_TEXT_Y1, "PLAY SAVE", &Font12x20, COLOR_ACTIVE, COLOR_PANEL);
    ST7789_DrawString(126, FOOTER_TEXT_Y1, "REC BACK", &Font12x20, COLOR_TEXT_MAIN, COLOR_PANEL);
    s_chord_footer_valid = 1;
}

/* ── User Chord Load ─────────────────────────────────────────────────────── */

#include "stm32/user_chord_bridge.h"

static uint8_t s_selected_user_chord = 0;

static void DrawUserChordLoadRow(uint8_t row,
                                 uint8_t count,
                                 uint8_t selected_index)
{
    uint16_t y = (uint16_t)(PARAM_ROW_Y0 + 4 + (row * 20));
    ST7789_FillRect(0, y, SCREEN_W, 18, COLOR_BG);

    if (row < count)
    {
        const UserChordInfo* chord = Bridge_UserChord_Get(row);
        if (chord)
        {
            uint16_t text_col = (row == selected_index) ? COLOR_ACTIVE : COLOR_TEXT_MAIN;
            char line[24];
            snprintf(line, sizeof(line), "%2u: %s", (unsigned)(row + 1), chord->name);
            ST7789_DrawString(10, y, line, &Font12x20, text_col, COLOR_BG);
        }
    }
}

void UI_Display_DrawUserChordLoad(void)
{
    MenuTemplate_Begin(MENU_FRAME_CHORD);
    uint8_t entering = !StrEq(s_menu_header_title, "Load Chord");

    const char* title = "Load Chord";
    ST7789_DrawString(4, 4, title, &Font12x20, COLOR_TEXT_MAIN, COLOR_PANEL_ALT);
    MenuTemplate_DrawHeader(title, "SELECT CHORD", YELLOW);

    /* Draw user chord list */
    uint8_t count = Bridge_UserChord_GetCount();
    if (count > 7) count = 7;

    if (entering || !s_user_chord_load_valid || s_user_chord_load_prev_count != count)
    {
        FillRegion(UI_REGION_MENU_LIST, COLOR_BG);
        for (uint8_t i = 0; i < 7; i++)
        {
            DrawUserChordLoadRow(i, count, s_selected_user_chord);
        }
        s_user_chord_load_valid = 1;
    }
    else if (s_user_chord_load_prev_selection != s_selected_user_chord)
    {
        if (s_user_chord_load_prev_selection < 7)
        {
            DrawUserChordLoadRow(s_user_chord_load_prev_selection, count, s_selected_user_chord);
        }
        if (s_selected_user_chord < 7)
        {
            DrawUserChordLoadRow(s_selected_user_chord, count, s_selected_user_chord);
        }
    }

    if (count == 0)
    {
        ST7789_DrawString(10, 100, "No saved chords", &Font12x20, GRAY, COLOR_BG);
    }

    /* Footer */
    if (entering || !s_user_chord_load_footer_valid)
    {
        FillRegion(UI_REGION_MENU_FOOTER, COLOR_PANEL);
        ST7789_DrawString(6, FOOTER_TEXT_Y1, "PLAY LOAD", &Font12x20, COLOR_ACTIVE, COLOR_PANEL);
        ST7789_DrawString(126, FOOTER_TEXT_Y1, "REC BACK", &Font12x20, COLOR_TEXT_MAIN, COLOR_PANEL);
        ST7789_DrawString(6, FOOTER_TEXT_Y2, "SHIFT+REC DELETE", &Font12x20, ORANGE, COLOR_PANEL);
        s_chord_footer_valid = 1;
        s_user_chord_load_footer_valid = 1;
    }

    s_user_chord_load_prev_selection = s_selected_user_chord;
    s_user_chord_load_prev_count = count;
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

void UI_Display_DrawYesNoQuestionModal(const char* message, uint8_t yes_selected)
{
    const uint16_t box_w = 188u;
    const uint16_t box_h = 88u;
    const uint16_t box_x = (uint16_t)((SCREEN_W - box_w) / 2u);
    const uint16_t box_y = (uint16_t)((SCREEN_H - box_h) / 2u);
    const uint16_t btn_w = 68u;
    const uint16_t btn_h = 26u;
    const uint16_t btn_y = (uint16_t)(box_y + box_h - btn_h - 10u);
    const uint16_t no_x = (uint16_t)(box_x + 18u);
    const uint16_t yes_x = (uint16_t)(box_x + box_w - btn_w - 18u);
    const uint16_t panel = COLOR_PANEL_ALT;

    if (!message) message = "CONFIRM?";

    ST7789_FillRect(box_x, box_y, box_w, box_h, panel);
    DrawRectBorder(box_x, box_y, box_w, box_h, COLOR_BOX_BORDER);
    ST7789_DrawString((uint16_t)(box_x + 14u), (uint16_t)(box_y + 16u), message, &Font12x20, WHITE, panel);

    DrawRoundedButton(no_x,
                      btn_y,
                      btn_w,
                      btn_h,
                      yes_selected ? COLOR_BOX_BG : COLOR_PANEL,
                      yes_selected ? COLOR_BOX_BORDER : COLOR_ACTIVE);
    DrawRoundedButton(yes_x,
                      btn_y,
                      btn_w,
                      btn_h,
                      yes_selected ? COLOR_PANEL : COLOR_BOX_BG,
                      yes_selected ? COLOR_ACTIVE : COLOR_BOX_BORDER);

    ST7789_DrawString((uint16_t)(no_x + 20u), (uint16_t)(btn_y + 6u), "NO", &Font12x20,
                      yes_selected ? WHITE : BLACK,
                      yes_selected ? COLOR_BOX_BG : COLOR_PANEL);
    ST7789_DrawString((uint16_t)(yes_x + 14u), (uint16_t)(btn_y + 6u), "YES", &Font12x20,
                      yes_selected ? BLACK : WHITE,
                      yes_selected ? COLOR_PANEL : COLOR_BOX_BG);
}