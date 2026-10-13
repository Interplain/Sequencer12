#include "ui/sequencer_clipboard.hpp"
#include "ui/ui_sequencer_clipboard.h"

namespace
{

sequencer_clipboard::BarClipboard s_bar_clipboard;
sequencer_clipboard::PatternClipboard s_pattern_clipboard;

}

namespace sequencer_clipboard
{

void Init()
{
    s_bar_clipboard = BarClipboard{};
    s_pattern_clipboard = PatternClipboard{};
}

bool CopyEditBar(const SequencerDevice& device, uint8_t zero_based_bar)
{
    sequencer::StepSlot snapshot{};
    if (!device.GetEditStepSlot(zero_based_bar, &snapshot)) return false;

    s_bar_clipboard.snapshot = snapshot;
    s_bar_clipboard.source_pattern = device.GetCurrentPatternIndex();
    s_bar_clipboard.source_bar = (uint8_t)(zero_based_bar + 1u);
    s_bar_clipboard.valid = 1u;
    return true;
}

bool CanPasteEditBar(const SequencerDevice& device, uint8_t zero_based_bar)
{
    if (!s_bar_clipboard.valid || device.IsPlaying()) return false;
    sequencer::StepSlot destination{};
    return device.GetEditStepSlot(zero_based_bar, &destination);
}

bool PasteEditBar(SequencerDevice& device, uint8_t zero_based_bar)
{
    if (!CanPasteEditBar(device, zero_based_bar)) return false;
    return device.ReplaceEditStepSlot(zero_based_bar, s_bar_clipboard.snapshot);
}

const BarClipboard& GetBarClipboard()
{
    return s_bar_clipboard;
}

const PatternClipboard& GetPatternClipboard()
{
    return s_pattern_clipboard;
}

}

extern "C" void UI_SequencerClipboard_Init(void)
{
    sequencer_clipboard::Init();
}

extern "C" uint8_t UI_SequencerClipboard_IsBarCopyPress(uint8_t in_bar_editor,
                                                          uint8_t shifted,
                                                          uint8_t matrix_step)
{
    return (in_bar_editor && !shifted && matrix_step == 6u) ? 1u : 0u;
}

extern "C" uint8_t UI_SequencerClipboard_IsBarPastePress(uint8_t in_bar_editor,
                                                           uint8_t shifted,
                                                           uint8_t matrix_step)
{
    return (in_bar_editor && !shifted && matrix_step == 7u) ? 1u : 0u;
}

extern "C" uint8_t UI_SequencerClipboard_IsBarValid(void)
{
    return s_bar_clipboard.valid;
}

extern "C" uint8_t UI_SequencerClipboard_GetBarSourcePattern(void)
{
    return s_bar_clipboard.source_pattern;
}

extern "C" uint8_t UI_SequencerClipboard_GetBarSourceBar(void)
{
    return s_bar_clipboard.source_bar;
}