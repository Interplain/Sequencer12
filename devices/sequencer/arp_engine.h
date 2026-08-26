#pragma once
#include "devices/sequencer/sequencer_types.h"
#include <cstdint>

namespace sequencer
{

// ─────────────────────────────────────────────
// ArpEngine
//
// Takes a note_mask from a StepSlot and outputs
// one note at a time according to the arp mode
// and rate. Called from SequencerDevice on each
// arp tick.
// ─────────────────────────────────────────────

class ArpEngine
{
public:

    void Init();

    // Load a new absolute-MIDI source pool into the arp buffer.
    // count is clamped to 0..4 and notes are sorted ascending.
    // Call this when the sequencer advances to a new grid position.
    void LoadNotes(const uint8_t* notes, uint8_t count, ArpMode mode);

    // Advance to the next arp note
    // Returns the next absolute MIDI note (0-127) to play
    // Returns 0xFF if no notes loaded
    uint8_t Advance();

    // Returns the current note without advancing
    uint8_t CurrentNote() const;

    // Returns true if the arp has at least one note loaded
    bool HasNotes() const;

    // Returns how many notes are in the current chord
    uint8_t NoteCount() const;

    // Reset arp position to start
    void Reset();

    // Set mode — resets position
    void SetMode(ArpMode mode);
    ArpMode GetMode() const;

    // 96-PPQN tick cadence helper.
    // Returns ticks per ARP event for a given rate.
    static uint32_t TicksPerEvent(ArpRate rate);

private:
    // Build sorted note array from absolute MIDI notes.
    void BuildNoteList(const uint8_t* notes, uint8_t count);

    ArpMode  mode_          = ArpMode::Off;
    uint8_t  notes_[4]      = {};    // sorted absolute MIDI source pool
    uint8_t  note_count_    = 0;
    uint8_t  position_      = 0;
    int8_t   direction_     = 1;     // +1 or -1 for UpDown/DownUp
    bool     peak_          = false; // true when at top of UpDown cycle
    uint8_t  current_note_  = 0xFF;
};

} // namespace sequencer