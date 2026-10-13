#ifndef SEQUENCER_CLIPBOARD_HPP
#define SEQUENCER_CLIPBOARD_HPP

#include "devices/sequencer/sequencer_device.h"

namespace sequencer_clipboard
{

struct BarClipboard
{
    sequencer::StepSlot snapshot{};
    uint8_t valid = 0u;
    uint8_t source_pattern = 0u;
    uint8_t source_bar = 0u;
};

struct PatternClipboard
{
    sequencer::Pattern snapshot{};
    uint8_t valid = 0u;
    uint8_t source_pattern = 0u;
};

void Init();
bool CopyEditBar(const SequencerDevice& device, uint8_t zero_based_bar);
bool CanPasteEditBar(const SequencerDevice& device, uint8_t zero_based_bar);
bool PasteEditBar(SequencerDevice& device, uint8_t zero_based_bar);
const BarClipboard& GetBarClipboard();
const PatternClipboard& GetPatternClipboard();

}

#endif