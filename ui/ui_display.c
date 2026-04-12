#include "ui_display.h"
#include "lib/ST7789/st7789.h"
#include <stdio.h>

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
#define COL_PLAY     GREEN
#define COL_STOP     WHITE
#define COL_REC_ARM  RED
#define COL_REC_OFF  WHITE
#define COL_SHIFT    YELLOW
#define COL_ACTIVE   CYAN    /* active playing step */
#define COL_SELECTED GREEN   /* cursor selected step */
#define COL_NORMAL   WHITE   /* unselected step */

/* ── Forward declarations ────────────────────────────────────────────────── */
static void DrawRectBorder(uint16_t x, uint16_t y,
                           uint16_t w, uint16_t h, uint16_t color);

/* ── Init ────────────────────────────────────────────────────────────────── */
void UI_Display_Init(void)
{
    ST7789_Fill_Color(BLACK);
    UI_Display_DrawHeader(120, TRANSPORT_STOPPED, 0);
    UI_Display_DrawGrid();
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
        UI_Display_DrawStepBox(i, 0, 0);
}

void UI_Display_DrawStepBox(uint8_t step, uint8_t selected, uint8_t active)
{
    uint8_t  index = step - 1;
    uint8_t  col   = index % 4;
    uint8_t  row   = index / 4;

    uint16_t x = GRID_X + col * (BOX_W + GAP_X);
    uint16_t y = GRID_Y + row * (BOX_H + GAP_Y);

    /* Priority: active (playing) > selected (cursor) > normal */
    uint16_t colour;
    if      (active)   colour = COL_ACTIVE;
    else if (selected) colour = COL_SELECTED;
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

/* ── Debug time ────────────────────────────────────────────────────────────── */
void UI_Display_DrawDebugRow(uint16_t bpm, uint8_t step, uint32_t elapsed_ms)
{
    char buf[24];
    snprintf(buf, sizeof(buf), "%2u %4lu", step, elapsed_ms);
    ST7789_FillRect(0, 26, SCREEN_W, 24, BLACK);
    ST7789_DrawString(4, 26, buf, &Font16x24, CYAN, BLACK);
}