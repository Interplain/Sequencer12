/* Host test: sliced Main Grid full-frame job (ui_display.c + ui_frame_job.c).
 * Build: see the command at the bottom of this file. */
#include <stddef.h>
#include "ui/ui_display.h"
#include "ui/ui_frame_job.h"
#include "ui/screens/ui_screen_piano_roll.h"
#include "stm32/user_chord_bridge.h"
#include "lib/ST7789/st7789.h"
#include <stdio.h>
#include <string.h>

static int s_failures = 0;
#define CHECK(cond, msg) do { if (cond) { printf("[PASS] %s\n", msg); } \
    else { printf("[FAIL] %s (line %d)\n", msg, __LINE__); ++s_failures; } } while (0)

/* ── ST7789 stubs ───────────────────────────────────────────────────────── */
SPI_HandleTypeDef hspi1;
static uint32_t s_rgb_calls = 0u;
static uint16_t s_rgb_x = 0u, s_rgb_y = 0u, s_rgb_w = 0u, s_rgb_h = 0u;
static uint32_t s_other_draw_calls = 0u;
static uint8_t s_panel[320u * 240u * 2u];
static uint8_t s_reference[sizeof(s_panel)];
static uint32_t s_rgb_bytes;

static void HostPixel(uint16_t x, uint16_t y, uint16_t colour)
{
    if (x >= 320u || y >= 240u) return;
    const uint32_t offset = ((uint32_t)y * 320u + x) * 2u;
    s_panel[offset] = (uint8_t)(colour >> 8);
    s_panel[offset + 1u] = (uint8_t)colour;
}

void ST7789_DrawRGB565Buffer(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                             const uint8_t* buffer, uint32_t buffer_size)
{
    s_rgb_bytes += buffer_size;
    for (uint16_t row = 0u; row < h; ++row)
        memcpy(s_panel + ((uint32_t)(y + row) * 320u + x) * 2u,
               buffer + (uint32_t)row * w * 2u, (uint32_t)w * 2u);
    ++s_rgb_calls;
    s_rgb_x = x; s_rgb_y = y; s_rgb_w = w; s_rgb_h = h;
}
void ST7789_DisplayOn(void) {}
void ST7789_Fill_Color(uint16_t c) { (void)c; ++s_other_draw_calls; }
void ST7789_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t c)
{
    ++s_other_draw_calls;
    for (uint16_t row = y; row < y + h; ++row)
        for (uint16_t col = x; col < x + w; ++col) HostPixel(col, row, c);
}
void ST7789_FillRectBordered(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                             uint16_t f, uint16_t b, uint8_t t)
{ (void)x; (void)y; (void)w; (void)h; (void)f; (void)b; (void)t; ++s_other_draw_calls; }
void ST7789_DrawPixel(uint16_t x, uint16_t y, uint16_t c) { HostPixel(x, y, c); ++s_other_draw_calls; }
void ST7789_DrawChar(uint16_t x, uint16_t y, char ch, const Font_t* f, uint16_t fg, uint16_t bg)
{
    ++s_other_draw_calls;
    const uint8_t character = (uint8_t)ch;
    if (character < f->first || character > f->last) return;
    const uint16_t stride = (uint16_t)((f->width + 7u) / 8u);
    const uint8_t* glyph = f->data + (character - f->first) * f->height * stride;
    for (uint16_t row = 0u; row < f->height; ++row)
        for (uint16_t col = 0u; col < f->width; ++col)
            HostPixel((uint16_t)(x + col), (uint16_t)(y + row),
                      glyph[row * stride + col / 8u] & (0x80u >> (col % 8u)) ? fg : bg);
}
void ST7789_DrawString(uint16_t x, uint16_t y, const char* s, const Font_t* f, uint16_t fg, uint16_t bg)
{
    for (; *s; ++s, x = (uint16_t)(x + f->width)) ST7789_DrawChar(x, y, *s, f, fg, bg);
}

/* ── Bridge / HAL / Piano Roll stubs ────────────────────────────────────── */
static uint32_t s_bridge_reads = 0u;
static uint32_t s_current_step = 0u;
uint32_t HAL_GetTick(void) { return 0u; }
uint8_t  Bridge_GetPatternStepCount(void) { ++s_bridge_reads; return 2u; }
uint8_t  Bridge_GetPatternStepDivision(void) { ++s_bridge_reads; return 4u; }
uint8_t  Bridge_GetSwing(void) { ++s_bridge_reads; return 50u; }
uint8_t  Bridge_GetStepLedgerLength(uint8_t i) { (void)i; ++s_bridge_reads; return 4u; }
uint8_t  Bridge_GetChainLength(void) { ++s_bridge_reads; return 1u; }
uint8_t  Bridge_GetCurrentPattern(void) { ++s_bridge_reads; return 0u; }
uint32_t Bridge_GetCurrentStep(void) { ++s_bridge_reads; return s_current_step; }
uint8_t  Bridge_GetCurrentStepSubIndex(void) { ++s_bridge_reads; return 0u; }
uint32_t Bridge_GetRunTimeMs(void) { ++s_bridge_reads; return 1234u; }
const char* Bridge_GetStepChordDisplayName(uint8_t i, char* b, uint8_t n) { (void)i; if (b && n) b[0] = 0; return b; }
uint8_t Bridge_UserChord_GetCount(void) { return 0u; }
const UserChordInfo* Bridge_UserChord_Get(uint8_t i) { (void)i; return 0; }
UiScreen* UI_PianoRollScreen_Get(void) { return 0; }
void UI_PianoRollScreen_SetContext(const PianoRollContext* c) { (void)c; }
uint8_t UI_PianoRollScreen_GetSelectedKey(void) { return 0u; }
uint16_t UI_PianoRollScreen_GetNoteMask(void) { return 0u; }
void UI_PianoRollScreen_SetNoteMask(uint16_t m) { (void)m; }
void UI_PianoRollScreen_NavigateKey(int8_t d) { (void)d; }
void UI_PianoRollScreen_ToggleKey(uint8_t k) { (void)k; }
void UI_PianoRollScreen_ForceRedraw(void) {}
void UI_PianoRollScreen_ResetCache(void) {}

/* ── Helpers ────────────────────────────────────────────────────────────── */
static const uint8_t k_no_notes[12] = {0};

static void BeginFrame(uint8_t active_step)
{
    UI_Display_FastReturnToGrid();
    UI_Display_BeginMainGridFrame(1u, active_step, k_no_notes, 120u, TRANSPORT_PLAYING, 0u,
                                  0u, (uint8_t)(active_step ? active_step - 1u : 0u), 0u, 1234u);
}

static uint32_t ServiceUntilDone(uint32_t limit)
{
    uint32_t calls = 0u;
    while (UI_Display_IsMainGridFrameActive() && calls < limit)
    {
        (void)UI_Display_ServiceMainGridFrame();
        ++calls;
    }
    return calls;
}

static void TestFrameJobUnit(void)
{
    UiFrameJob job;
    memset(&job, 0, sizeof(job));
    uint8_t strip = 0xFFu;
    CHECK(UiFrameJob_Begin(&job, 3u) == 0u, "job: fresh begin is not a restart");
    CHECK(UiFrameJob_NextStrip(&job, &strip) && strip == 0u, "job: first strip is 0");
    CHECK(UiFrameJob_CompleteStrip(&job) == 0u, "job: strip 0 does not finish frame");
    CHECK(UiFrameJob_Defer(&job, UI_FRAME_DEFER_BOXES) == 1u, "job: defer while active");
    CHECK(UiFrameJob_TakeDeferred(&job) == 0u, "job: deferred not released while active");
    CHECK(UiFrameJob_Begin(&job, 3u) == 1u, "job: begin while active reports restart");
    CHECK(UiFrameJob_NextStrip(&job, &strip) && strip == 0u, "job: restart begins at strip 0");
    (void)UiFrameJob_CompleteStrip(&job);
    (void)UiFrameJob_CompleteStrip(&job);
    CHECK(UiFrameJob_CompleteStrip(&job) == 1u, "job: last strip finishes frame");
    CHECK(!job.active && !UiFrameJob_NextStrip(&job, &strip), "job: inactive after finish");
    CHECK(UiFrameJob_TakeDeferred(&job) == UI_FRAME_DEFER_BOXES, "job: deferred survives restart");
    CHECK(UiFrameJob_Defer(&job, UI_FRAME_DEFER_HEADER) == 0u, "job: no deferral when idle");
    (void)UiFrameJob_Begin(&job, 3u);
    (void)UiFrameJob_Defer(&job, UI_FRAME_DEFER_STATUS);
    UiFrameJob_Cancel(&job);
    CHECK(!job.active && job.deferred == 0u, "job: cancel clears state");
}

static void TestFifteenStripsOnePerCall(void)
{
    BeginFrame(1u);
    s_rgb_calls = 0u;
    s_bridge_reads = 0u;

    (void)UI_Display_ServiceMainGridFrame();
    CHECK(s_rgb_calls == 1u && s_rgb_y == 0u && s_rgb_w == 320u && s_rgb_h == 16u,
          "frame: one call sends exactly one 320x16 strip");
    CHECK(UI_Display_IsMainGridFrameActive(), "frame: one call cannot finish the frame");

    const uint32_t calls = 1u + ServiceUntilDone(100u);
    CHECK(calls == 15u && s_rgb_calls == 15u, "frame: exactly 15 strips complete one frame");
    CHECK(s_rgb_y == 224u && s_rgb_h == 16u, "frame: final strip covers rows 224-239");
    CHECK(!UI_Display_IsMainGridFrameActive(), "frame: job terminates after the last strip");
    CHECK(s_bridge_reads == 0u, "frame: strips render the Begin snapshot, not live engine state");

    s_rgb_calls = 0u;
    CHECK(UI_Display_ServiceMainGridFrame() == 0u && s_rgb_calls == 0u,
          "frame: idle service call draws nothing");
}

static void TestBarChangeDuringFrameNotLost(void)
{
    UI_Display_SetRepeatFlash(0u, 0u);
    BeginFrame(1u);
    (void)UI_Display_ServiceMainGridFrame();
    s_rgb_calls = 0u;
    s_other_draw_calls = 0u;

    UI_Display_DrawStepBox(1u, 1u, 0u, 0u);
    UI_Display_DrawStepBox(2u, 0u, 1u, 0u);
    UI_Display_DrawHeader(121u, TRANSPORT_PLAYING, 0u);
    UI_Display_DrawStatusRow(0u, 1u, 0u, 2000u);
    CHECK(s_rgb_calls == 0u && s_other_draw_calls == 0u,
          "defer: box/header/status updates during a frame do not touch the panel");

    (void)ServiceUntilDone(100u);
    const uint8_t deferred = UI_Display_TakeMainGridDeferred();
    CHECK((deferred & UI_FRAME_DEFER_BOXES) && (deferred & UI_FRAME_DEFER_HEADER) &&
          (deferred & UI_FRAME_DEFER_STATUS), "defer: all deferred kinds reported after frame");

    s_rgb_calls = 0u;
    UI_Display_DrawStepBox(1u, 1u, 0u, 0u);
    UI_Display_DrawStepBox(2u, 0u, 1u, 0u);
    CHECK(s_rgb_calls == 2u, "defer: Bar change during frame is drawn afterwards (old + new box)");
    CHECK(s_rgb_x == 63u && s_rgb_y == 56u && s_rgb_w == 55u && s_rgb_h == 56u,
          "incremental: Stage 1 box path still sends a single 55x56 box");

    s_rgb_calls = 0u;
    UI_Display_DrawStepBox(2u, 0u, 1u, 0u);
    CHECK(s_rgb_calls == 0u, "incremental: unchanged box is skipped by the cache");

    s_other_draw_calls = 0u;
    UI_Display_DrawHeader(121u, TRANSPORT_PLAYING, 0u);
    CHECK(s_other_draw_calls > 0u, "defer: header change during frame is drawn afterwards");
}

static void TestRepeatFlashDuringFrameNotLost(void)
{
    UI_Display_SetRepeatFlash(1u, 1u);
    BeginFrame(3u);
    (void)UI_Display_ServiceMainGridFrame();
    UI_Display_SetRepeatFlash(1u, 0u);
    s_rgb_calls = 0u;
    UI_Display_DrawStepBox(3u, 0u, 1u, 0u);
    CHECK(s_rgb_calls == 0u, "flash: toggle during frame is deferred");
    (void)ServiceUntilDone(100u);
    s_rgb_calls = 0u;
    UI_Display_DrawStepBox(3u, 0u, 1u, 0u);
    CHECK(s_rgb_calls == 1u, "flash: toggle during frame is drawn after the frame");
    UI_Display_SetRepeatFlash(0u, 0u);
}

static void TestCancelAndRestart(void)
{
    BeginFrame(1u);
    (void)UI_Display_ServiceMainGridFrame();
    (void)UI_Display_ServiceMainGridFrame();
    UI_Display_DrawStepBox(5u, 0u, 1u, 0u);
    UI_Display_CancelMainGridFrame();
    s_rgb_calls = 0u;
    CHECK(!UI_Display_IsMainGridFrameActive() && UI_Display_ServiceMainGridFrame() == 0u &&
          s_rgb_calls == 0u, "cancel: screen transition leaves no running job");
    CHECK(UI_Display_TakeMainGridDeferred() == 0u, "cancel: no stale deferred work survives");

    BeginFrame(1u);
    for (int i = 0; i < 5; ++i) (void)UI_Display_ServiceMainGridFrame();
    BeginFrame(2u);
    s_rgb_calls = 0u;
    (void)UI_Display_ServiceMainGridFrame();
    CHECK(s_rgb_calls == 1u && s_rgb_y == 0u, "restart: a new transition restarts at strip 0");
    CHECK(1u + ServiceUntilDone(100u) == 15u, "restart: restarted frame still takes 15 strips");
}

static void TimingReference(uint8_t loop, uint8_t division, uint8_t numerator,
                    uint8_t denominator, uint8_t swing, uint8_t cursor,
                    uint8_t unsaved, uint8_t rejected)
{
    const uint16_t background = RGB565(2, 4, 8);
    const uint16_t header = RGB565(10, 16, 26);
    const uint16_t active = RGB565(16, 40, 63);
    const char* labels[5] = {"Pattern Loop", "Grid", "Beats Per Bar", "Beat Unit", "Swing"};
    char values[5][12];
    snprintf(values[0], sizeof(values[0]), "%u", loop);
    snprintf(values[1], sizeof(values[1]), "%s", division == 1u ? "1/4" :
         (division == 2u ? "1/8" : "1/16"));
    snprintf(values[2], sizeof(values[2]), "%u", numerator);
    snprintf(values[3], sizeof(values[3]), "%u", denominator);
    snprintf(values[4], sizeof(values[4]), "%u%%", swing);
    ST7789_FillRect(0u, 40u, 320u, 140u, background);
    for (uint8_t row = 0u; row < 5u; ++row)
    {
      const uint16_t y = (uint16_t)(51u + row * 24u);
      ST7789_FillRect(0u, y, 3u, 22u, row == cursor ? active : background);
      ST7789_DrawString(8u, y, labels[row], &Font12x20, WHITE, background);
      ST7789_DrawString(240u, y, values[row], &Font12x20,
                  row == cursor ? active : WHITE, background);
    }
    ST7789_FillRect(0u, 0u, 320u, 40u, header);
    ST7789_DrawString(4u, 4u, "Pattern Timing", &Font12x20, WHITE, header);
    ST7789_DrawString(4u, 22u, rejected ? "INVALID" :
                (unsaved ? "*UNSAVED" : "Global sequencer timing"),
                &Font12x20, rejected ? RED : YELLOW, header);
    ST7789_FillRect(0u, 180u, 320u, 60u, RGB565(6, 10, 16));
}

static unsigned TimingFinish(void)
{
    unsigned calls = 0u;
    while (UI_Display_IsTimingFrameActive() && calls < 40u)
    {
      UI_Display_ServiceTimingFrame();
      ++calls;
    }
    return calls;
}

static void TestTimingFrame(void)
{
    TimingReference(2u, 4u, 4u, 4u, 50u, 0u, 0u, 0u);
    memcpy(s_reference, s_panel, sizeof(s_panel));
    memset(s_panel, 0xCC, sizeof(s_panel));
    UI_Display_FastReturnToGrid();
    s_rgb_calls = s_rgb_bytes = s_other_draw_calls = s_bridge_reads = 0u;
    UI_Display_DrawTimingMenu(2u, 4u, 4u, 4u, 50u, 0u, 0u, 0u);
    CHECK(s_rgb_calls == 0u && s_other_draw_calls == 0u,
        "TIME entry queues a frame without synchronous drawing");
    UI_Display_ServiceTimingFrame();
    CHECK(s_rgb_calls == 1u && s_rgb_h == 8u && s_rgb_y == 0u && UI_Display_IsTimingFrameActive(),
        "TIME service sends one bounded 320x8 strip only");
    for (uint8_t request = 0u; request < 5u; ++request)
      UI_Display_DrawTimingMenu(2u, 4u, 4u, 4u, 50u, 0u, 0u, 0u);
    CHECK(TimingFinish() == 29u && s_rgb_calls == 30u && s_rgb_bytes == sizeof(s_panel),
        "TIME repeated requests do not restart or duplicate the 30-job frame");
    CHECK(memcmp(s_panel, s_reference, sizeof(s_panel)) == 0,
        "TIME full frame retains the previous layout and exact pixels");
    CHECK(s_bridge_reads == 0u, "TIME rendering performs no Bridge or storage reads");

    UI_Display_FastReturnToGrid();
    UI_Display_DrawTimingMenu(2u, 4u, 4u, 4u, 50u, 0u, 0u, 0u);
    UI_Display_ServiceTimingFrame();
    s_other_draw_calls = 0u;
    UI_Display_DrawTimingMenu(3u, 4u, 4u, 4u, 50u, 0u, 1u, 0u);
    UI_Display_DrawTimingMenu(3u, 4u, 5u, 4u, 50u, 2u, 1u, 0u);
    UI_Display_DrawTimingMenu(3u, 4u, 5u, 4u, 55u, 4u, 1u, 1u);
    CHECK(s_other_draw_calls == 0u, "TIME encoder edits during construction draw nothing immediately");
    for (uint8_t strip = 1u; strip < 30u; ++strip) UI_Display_ServiceTimingFrame();
    CHECK(memcmp(s_panel, s_reference, sizeof(s_panel)) == 0,
        "TIME strips remain a stable snapshot despite edits and cursor changes");
    UI_Display_ServiceTimingFrame();
    memcpy(s_reference, s_panel, sizeof(s_panel));
    TimingReference(3u, 4u, 5u, 4u, 55u, 4u, 1u, 1u);
    for (uint32_t offset = 0u; offset < sizeof(s_panel); offset += 2u)
    {
        if (memcmp(s_panel + offset, s_reference + offset, 2u) != 0)
        {
            fprintf(stderr, "TIME mismatch x=%lu y=%lu expected=%02x%02x actual=%02x%02x\n",
                    (unsigned long)((offset / 2u) % 320u), (unsigned long)((offset / 2u) / 320u),
                    s_panel[offset], s_panel[offset + 1u], s_reference[offset], s_reference[offset + 1u]);
            break;
        }
    }
    CHECK(memcmp(s_panel, s_reference, sizeof(s_panel)) == 0 && !UI_Display_IsTimingFrameActive(),
        "TIME deferred values on multiple rows, cursor and INVALID indicator are reconciled");
    const uint32_t before_incremental = s_rgb_calls;
    UI_Display_DrawTimingMenu(3u, 4u, 5u, 4u, 56u, 4u, 1u, 0u);
    CHECK(s_rgb_calls == before_incremental && !UI_Display_IsTimingFrameActive(),
        "TIME ordinary value editing remains incremental after full-frame completion");

    UI_Display_FastReturnToGrid();
    UI_Display_DrawTimingMenu(2u, 4u, 4u, 4u, 50u, 0u, 0u, 0u);
    UI_Display_ServiceTimingFrame();
    UI_Display_CancelTimingFrame();
    const uint32_t before_cancel = s_rgb_calls;
    UI_Display_ServiceTimingFrame();
    CHECK(s_rgb_calls == before_cancel && !UI_Display_IsTimingFrameActive(),
        "TIME cancellation leaves no stale work for the next screen");
    UI_Display_FastReturnToGrid();
    UI_Display_DrawTimingMenu(2u, 4u, 4u, 4u, 50u, 0u, 0u, 0u);
    CHECK(TimingFinish() == 30u, "TIME re-entry after cancellation starts exactly one new frame");
    UI_Display_ResetTimingFrameCache();
    UI_Display_DrawTimingMenu(2u, 4u, 4u, 4u, 50u, 0u, 0u, 0u);
    UI_Display_ServiceTimingFrame();
    UI_Display_ResetTimingFrameCache();
    UI_Display_DrawTimingMenu(2u, 4u, 4u, 4u, 50u, 0u, 0u, 0u);
    CHECK(TimingFinish() == 29u,
          "TIME activation starts a full frame even when its prior menu cache was valid; repeats do not restart");
}

static void DirectReference(const ChordParams* chord, uint8_t cursor, const char* preview)
{
    const uint16_t bg = RGB565(2, 4, 8), header = RGB565(10, 16, 26), active = RGB565(16, 40, 63);
    const char* roots[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const char* types[] = {"Clear", "Major", "Minor", "Dim", "Aug", "Sus4", "7th", "Maj7",
        "Min7", "Dim7", "7sus4", "6th", "Min6", "9th", "Maj9", "Min9", "11th"};
    ST7789_FillRect(0u, 40u, 320u, 140u, bg);
    for (uint8_t row = 0u; row < 2u; ++row)
    {
        const uint16_t y = (uint16_t)(51u + row * 24u);
        ST7789_FillRect(0u, y, 3u, 22u, row == cursor ? active : bg);
        ST7789_DrawString(8u, y, row == 0u ? "Root Key:" : "Type:", &Font12x20, WHITE, bg);
        ST7789_DrawString(224u, y, row == 0u ? roots[chord->root_key] : types[chord->chord_type],
                          &Font12x20, row == cursor ? active : WHITE, bg);
    }
    ST7789_FillRect(0u, 0u, 320u, 40u, header);
    ST7789_DrawString(4u, 4u, "DIRECT CHORD", &Font12x20, WHITE, header);
    ST7789_DrawString(4u, 22u, "BAR01 POS01", &Font12x20, YELLOW, header);
    ST7789_DrawString(8u, 104u, "GRID 1/16", &Font12x20, active, bg);
    ST7789_DrawString(8u, 128u, preview, &Font12x20, LIGHTBLUE, bg);
    ST7789_FillRect(0u, 180u, 320u, 60u, RGB565(6, 10, 16));
    /* The existing rounded footer button, reconstructed through the public renderer. */
    UI_Display_DrawParamFooterActions(PARAM_ACTION_MAIN);
}

static unsigned DirectFinish(void)
{
    unsigned calls = 0u;
    while (UI_Display_IsDirectChordFrameActive() && calls < 300u)
    {
        const uint32_t before = s_rgb_calls;
        UI_Display_ServiceDirectChordFrame();
        CHECK(s_rgb_calls <= before + 1u, "direct job sends at most one region strip");
        ++calls;
    }
    return calls;
}

static void DirectCheckPixels(const ChordParams* chord, uint8_t cursor, const char* preview)
{
    memcpy(s_reference, s_panel, sizeof(s_panel));
    DirectReference(chord, cursor, preview);
    CHECK(memcmp(s_panel, s_reference, sizeof(s_panel)) == 0,
          "direct incremental pixels equal a correctly constructed full frame");
}

static void TestDirectChordFrame(void)
{
    ChordParams chord = {0};
    chord.chord_type = 1u;
    uint8_t notes[4] = {60u, 64u, 67u, 0xFFu};
    UI_Display_FastReturnToGrid();
    UI_Display_SetChordParamsDirectMode(1u);
    UI_Display_SetDirectChordInfo(1u, 1u, 4u, notes);
    s_rgb_calls = s_rgb_bytes = s_other_draw_calls = s_bridge_reads = 0u;
    UI_Display_DrawChordParams(1u, &chord, 0u, PARAM_ACTION_MAIN);
    CHECK(s_rgb_calls == 0u && s_other_draw_calls == 0u,
          "M1 entry requests a frame without synchronous body or footer drawing");
    UI_Display_ServiceDirectChordFrame();
    for (uint8_t duplicate = 0u; duplicate < 4u; ++duplicate)
        UI_Display_DrawChordParams(1u, &chord, 0u, PARAM_ACTION_MAIN);
    CHECK(DirectFinish() == 29u && s_rgb_calls == 30u && s_rgb_bytes == sizeof(s_panel),
          "direct entry completes one 30-strip frame despite repeated requests");
    CHECK(s_bridge_reads == 0u, "direct display jobs perform no Bridge/storage reads");
    DirectCheckPixels(&chord, 0u, "C4  E4  G4");
    UI_Display_DrawChordParams(1u, &chord, 0u, PARAM_ACTION_MAIN);
    CHECK(DirectFinish() == 0u, "unchanged direct values and preview cause no display transfers");

    uint32_t bytes_before = s_rgb_bytes;
    UI_Display_DrawChordParams(1u, &chord, 1u, PARAM_ACTION_MAIN);
    CHECK(DirectFinish() == 12u && s_rgb_bytes - bytes_before == 2u * (96u + 3u) * 22u * 2u,
          "cursor move touches only old/new selection value areas and markers");
    DirectCheckPixels(&chord, 1u, "C4  E4  G4");

    chord.root_key = 1u;
    notes[0] = 61u; notes[1] = 65u; notes[2] = 68u;
    UI_Display_SetDirectChordInfo(1u, 1u, 4u, notes);
    bytes_before = s_rgb_bytes;
    UI_Display_DrawChordParams(1u, &chord, 1u, PARAM_ACTION_MAIN);
    (void)DirectFinish();
    CHECK(s_rgb_bytes - bytes_before < 20000u, "Root edit sends only its field and changed preview characters");
    DirectCheckPixels(&chord, 1u, "C#4  F4  G#4");

    chord.chord_type = 9u;
    notes[1] = 64u; notes[2] = 67u; notes[3] = 70u;
    UI_Display_SetDirectChordInfo(1u, 1u, 4u, notes);
    UI_Display_DrawChordParams(1u, &chord, 1u, PARAM_ACTION_MAIN);
    (void)DirectFinish();
    DirectCheckPixels(&chord, 1u, "C#4  E4  G4  A#4");
    chord.chord_type = 0u;
    memset(notes, 0xFF, sizeof(notes));
    UI_Display_SetDirectChordInfo(1u, 1u, 4u, notes);
    UI_Display_DrawChordParams(1u, &chord, 1u, PARAM_ACTION_MAIN);
    (void)DirectFinish();
    DirectCheckPixels(&chord, 1u, "");

    UI_Display_ResetDirectChordFrame();
    UI_Display_DrawChordParams(1u, &chord, 0u, PARAM_ACTION_MAIN);
    UI_Display_ServiceDirectChordFrame();
    chord.root_key = 0u; chord.chord_type = 1u;
    notes[0] = 60u; notes[1] = 64u; notes[2] = 67u;
    UI_Display_SetDirectChordInfo(1u, 1u, 4u, notes);
    UI_Display_DrawChordParams(1u, &chord, 1u, PARAM_ACTION_MAIN);
    CHECK(DirectFinish() > 29u, "input during construction is reconciled after the snapshot without restart");
    DirectCheckPixels(&chord, 1u, "C4  E4  G4");
    UI_Display_ResetDirectChordFrame();
    UI_Display_DrawChordParams(1u, &chord, 0u, PARAM_ACTION_MAIN);
    UI_Display_ServiceDirectChordFrame();
    UI_Display_CancelDirectChordFrame();
    CHECK(!UI_Display_IsDirectChordFrameActive(), "cancelled direct editor cannot monopolize other renderers");
    UI_Display_DrawChordParams(1u, &chord, 0u, PARAM_ACTION_MAIN);
    CHECK(DirectFinish() == 30u, "return after confirmation/modal cancellation reconstructs one frame");
    UI_Display_CancelDirectChordFrame();
    CHECK(!UI_Display_IsDirectChordFrameActive(), "clean editor cache is invalidated under a modal without scheduling stale work");
    UI_Display_DrawChordParams(1u, &chord, 0u, PARAM_ACTION_MAIN);
    CHECK(DirectFinish() == 30u, "dismissal restores a clean editor from beneath the confirmation overlay");
    UI_Display_SetChordParamsDirectMode(0u);
}

int main(void)
{
    UI_Display_Init();
    TestFrameJobUnit();
    TestFifteenStripsOnePerCall();
    TestBarChangeDuringFrameNotLost();
    TestRepeatFlashDuringFrameNotLost();
    TestCancelAndRestart();
    TestTimingFrame();
    TestDirectChordFrame();
    printf("%s: %d failure(s)\n", s_failures ? "FAIL" : "PASS", s_failures);
    return s_failures ? 1 : 0;
}

/* gcc -std=gnu11 -DST7789_ROTATION=3 -Iscripts/host_stubs/c -I. -Iui -Istm32 -Ilib/ST7789 \
 *     scripts/main_grid_frame_host_test.c ui/ui_display.c ui/ui_frame_job.c \
 *     ui/ui_render_scratch.c lib/ST7789/s12_fonts.c -o /tmp/main_grid_frame_test */
