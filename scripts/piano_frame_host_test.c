#include <stddef.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ui/screens/ui_screen_piano_roll.c"

static uint8_t panel[SCREEN_W * SCREEN_H * 2u];
static uint8_t reference[sizeof(panel)];
static uint32_t transfers;
static uint32_t bytes_sent;
static uint32_t bridge_reads;
static uint8_t ledger_notes[32][4];
static uint32_t child_draws;
static void DrawChild(void) { ++child_draws; }

static void Pixel(uint16_t x, uint16_t y, uint16_t colour)
{
    if (x >= SCREEN_W || y >= SCREEN_H) return;
    const uint32_t offset = ((uint32_t)y * SCREEN_W + x) * 2u;
    panel[offset] = (uint8_t)(colour >> 8);
    panel[offset + 1u] = (uint8_t)colour;
}

void ST7789_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t colour)
{
    ++transfers;
    bytes_sent += (uint32_t)w * h * 2u;
    for (uint16_t row = y; row < y + h; ++row)
        for (uint16_t col = x; col < x + w; ++col) Pixel(col, row, colour);
}

void ST7789_DrawRGB565Buffer(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                            const uint8_t* buffer, uint32_t size)
{
    assert(size == (uint32_t)w * h * 2u);
    assert(x + w <= SCREEN_W && y + h <= SCREEN_H);
    ++transfers;
    bytes_sent += size;
    for (uint16_t row = 0u; row < h; ++row)
        memcpy(panel + ((uint32_t)(y + row) * SCREEN_W + x) * 2u,
               buffer + (uint32_t)row * w * 2u, (uint32_t)w * 2u);
}

void ST7789_DrawString(uint16_t x, uint16_t y, const char* text, const Font_t* font,
                       uint16_t fg, uint16_t bg)
{
    for (; *text; ++text, x = (uint16_t)(x + font->width))
    {
        const uint8_t character = (uint8_t)*text;
        if (character < font->first || character > font->last) continue;
        const uint16_t stride = (uint16_t)((font->width + 7u) / 8u);
        const uint8_t* glyph = font->data + (character - font->first) * font->height * stride;
        for (uint16_t row = 0; row < font->height; ++row)
            for (uint16_t col = 0; col < font->width; ++col)
                Pixel((uint16_t)(x + col), (uint16_t)(y + row),
                      glyph[row * stride + col / 8u] & (0x80u >> (col % 8u)) ? fg : bg);
    }
}

void ST7789_DrawPixel(uint16_t x, uint16_t y, uint16_t colour) { Pixel(x, y, colour); }
void ST7789_DebugBeginPianoRenderFrame(const char* tag) { (void)tag; }
void ST7789_DebugEndPianoRenderFrame(void) {}
void MenuTemplate_Begin(uint8_t frame) { (void)frame; }
void MenuTemplate_DrawHeader(const char* title, const char* indicator, uint16_t colour)
{ (void)title; (void)indicator; (void)colour; }
void FillRegion(UiRect region, uint16_t colour)
{ ST7789_FillRect(region.x, region.y, region.w, region.h, colour); }
BridgeLedgerSlot Bridge_GetStepLedgerSlotMidi(uint8_t bar, uint8_t slot)
{
    (void)bar;
    ++bridge_reads;
    BridgeLedgerSlot value;
    memcpy(value.notes, ledger_notes[slot], sizeof(value.notes));
    return value;
}
uint8_t Bridge_GetStepEventLength(uint8_t bar, uint8_t slot)
{ (void)bar; ++bridge_reads; return ledger_notes[slot][0] == 0xFFu ? 0u : 1u; }
uint8_t Bridge_GetStepEventMaxLength(uint8_t bar, uint8_t slot)
{ (void)bar; (void)slot; ++bridge_reads; return 4u; }

static PianoRollContext Context(void)
{
    PianoRollContext context = {0};
    context.mode = PIANO_ROLL_MODE_STEP_ROLL;
    context.step = 1u;
    context.selected_midi_note = 60u;
    context.top_visible_midi = 69u;
    context.slot_count = 4u;
    context.grid_division = 4u;
    context.key_label = "C";
    context.scale_label = "MAJ";
    context.confirm_message = "CLEAR BAR?";
    context.title = "S1/4";
    context.footer_label = "BAR EDITOR";
    memset(context.slot_notes, 0xFF, sizeof(context.slot_notes));
    return context;
}

static unsigned FinishFrame(void)
{
    unsigned calls = 0u;
    while (UI_PianoRollScreen_IsFrameActive())
    {
        const uint32_t before = transfers;
        UI_PianoRollScreen_ServiceFrame();
        assert(transfers == before + 1u);
        assert(++calls <= 30u);
    }
    return calls;
}

int main(void)
{
    memset(ledger_notes, 0xFF, sizeof(ledger_notes));
    ledger_notes[0][0] = 60u;
    PianoRollContext context = Context();
    UI_ScreenRouter_Init();
    UI_PianoRollScreen_SetContext(&context);
    OnEnter();
    DrawStepPianoRollScreen();
    memcpy(reference, panel, sizeof(panel));

    memset(panel, 0xCC, sizeof(panel));
    UI_PianoRollScreen_ResetCache();
    UI_PianoRollScreen_SetContext(&context);
    transfers = bytes_sent = 0u;
    OnDraw();
    assert(transfers == 0u && UI_PianoRollScreen_IsFrameActive());
    OnDraw();
    assert(transfers == 0u);
    bridge_reads = 0u;
    assert(FinishFrame() == 30u);
    assert(bridge_reads == 0u);
    assert(bytes_sent == sizeof(panel));
    for (uint32_t offset = 0; offset < sizeof(panel); offset += 2u)
    {
        if (memcmp(panel + offset, reference + offset, 2u) != 0)
        {
            fprintf(stderr, "first pixel mismatch x=%lu y=%lu actual=%02x%02x expected=%02x%02x\n",
                    (unsigned long)((offset / 2u) % SCREEN_W),
                    (unsigned long)((offset / 2u) / SCREEN_W),
                    panel[offset], panel[offset + 1u], reference[offset], reference[offset + 1u]);
            break;
        }
    }
    assert(memcmp(panel, reference, sizeof(panel)) == 0);
    puts("PASS: coherent 30-job frame, zero synchronous draw, one screen of bytes, identical pixels");

    UI_PianoRollScreen_ResetCache();
    UI_PianoRollScreen_SetContext(&context);
    OnDraw();
    UI_PianoRollScreen_ServiceFrame();
    ledger_notes[0][0] = 62u;
    UI_PianoRollScreen_SetContext(&context);
    OnDraw();
    assert(FinishFrame() == 29u);
    assert(memcmp(panel, reference, sizeof(panel)) == 0);
    const uint32_t before_edit = bytes_sent;
    OnDraw();
    assert(!UI_PianoRollScreen_IsFrameActive());
    assert(bytes_sent > before_edit && bytes_sent - before_edit < sizeof(panel));
    assert(memcmp(panel, reference, sizeof(panel)) != 0);
    puts("PASS: edit during construction deferred then applied incrementally");

    UI_PianoRollScreen_ResetCache();
    OnDraw();
    UI_PianoRollScreen_ServiceFrame();
    context.step = 2u;
    UI_PianoRollScreen_SetContext(&context);
    assert(FinishFrame() == 29u);
    OnDraw();
    assert(UI_PianoRollScreen_IsFrameActive());
    assert(FinishFrame() == 30u);
    puts("PASS: Bar selection waits for snapshot completion before its new frame");

    UI_PianoRollScreen_ResetCache();
    OnDraw();
    UI_PianoRollScreen_ServiceFrame();
    OnExit(SCREEN_EXIT_BACK);
    assert(!UI_PianoRollScreen_IsFrameActive());
    const uint32_t before_cancel = transfers;
    UI_PianoRollScreen_ServiceFrame();
    assert(transfers == before_cancel);
    UI_PianoRollScreen_ResetCache();
    UI_PianoRollScreen_SetContext(&context);
    OnDraw();
    OnDraw();
    assert(FinishFrame() == 30u);
    puts("PASS: cancellation and menu re-entry have no abandoned display work");

    UI_ScreenRouter_SwitchTo(UI_PianoRollScreen_Get());
    assert(FinishFrame() == 30u);
    UiScreen child = {0};
    child.name = "Timing";
    assert(UI_ScreenRouter_Push(&child));
    UI_PianoRollScreen_ResetCache();
    UI_PianoRollScreen_SetContext(&context);
    UI_PianoRollScreen_ForceRedraw();
    const uint32_t before_return = transfers;
    assert(UI_ScreenRouter_Pop(SCREEN_EXIT_SAVE));
    assert(transfers == before_return && UI_PianoRollScreen_IsFrameActive());
    OnDraw();
    assert(FinishFrame() == 30u);
    assert(transfers == before_return + 30u);
    puts("PASS: router child return requests one frame without synchronous or duplicate draw");
    UiScreen grandchild = {0};
    grandchild.name = "UserChord";
    child.on_draw = DrawChild;
    assert(UI_ScreenRouter_Push(&child));
    assert(UI_ScreenRouter_Push(&grandchild));
    child_draws = 0u;
    UI_PianoRollScreen_ResetCache();
    UI_PianoRollScreen_SetContext(&context);
    assert(UI_ScreenRouter_ReturnTo(UI_PianoRollScreen_Get(), SCREEN_EXIT_BACK));
    assert(child_draws == 0u && UI_ScreenRouter_GetActive() == UI_PianoRollScreen_Get());
    assert(FinishFrame() == 30u);
    puts("PASS: nested menu return draws no intermediate menu and requests one Piano frame");

    UI_PianoRollScreen_ResetCache();
    UI_PianoRollScreen_SetContext(&context);
    OnDraw();
    UI_PianoRollScreen_ServiceFrame();
    context.selected_midi_note = 61u;
    UI_PianoRollScreen_SetContext(&context);
    context.selected_midi_note = 62u;
    UI_PianoRollScreen_SetContext(&context);
    assert(FinishFrame() == 29u);
    OnDraw();
    memcpy(reference, panel, sizeof(panel));
    UI_PianoRollScreen_ResetCache();
    UI_PianoRollScreen_SetContext(&context);
    DrawStepPianoRollScreen();
    assert(memcmp(panel, reference, sizeof(panel)) == 0);
    puts("PASS: multiple cursor changes clear the snapshot highlight and retain the latest cursor");

    context.slot_count = 32u;
    for (uint8_t slot = 0u; slot < 32u; ++slot)
        ledger_notes[slot][0] = (uint8_t)(54u + slot % 12u);
    UI_PianoRollScreen_ResetCache();
    UI_PianoRollScreen_SetContext(&context);
    DrawStepPianoRollScreen();
    memcpy(reference, panel, sizeof(panel));
    UI_PianoRollScreen_ResetCache();
    UI_PianoRollScreen_SetContext(&context);
    OnDraw();
    assert(FinishFrame() == 30u);
    assert(memcmp(panel, reference, sizeof(panel)) == 0);
    puts("PASS: dense 32-slot frame remains coherent and visually equivalent");
    return 0;
}