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

    // Load a new chord into the arp buffer
    // Call this when the sequencer advances to a new step
    void LoadChord(uint16_t note_mask, ArpMode mode);

    // Advance to the next arp note
    // Returns the next note (0-11) to play
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

    // Interval ms calculation helper
    // Returns the arp tick interval in ms for a given BPM and rate
    static uint32_t CalcIntervalMs(uint32_t bpm, ArpRate rate);

private:

    // Unpack note_mask into sorted note array
    void BuildNoteList(uint16_t note_mask);

    ArpMode  mode_          = ArpMode::Off;
    uint8_t  notes_[12]     = {};    // unpacked notes from mask
    uint8_t  note_count_    = 0;
    uint8_t  position_      = 0;
    int8_t   direction_     = 1;     // +1 or -1 for UpDown/DownUp
    bool     peak_          = false; // true when at top of UpDown cycle
    uint8_t  current_note_  = 0xFF;
};

} // namespace sequencer