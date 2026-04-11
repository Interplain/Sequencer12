#include "devices/sequencer/arp_engine.h"
#include <cstdlib>   // for rand()

namespace sequencer
{

void ArpEngine::Init()
{
    mode_         = ArpMode::Off;
    note_count_   = 0;
    position_     = 0;
    direction_    = 1;
    peak_         = false;
    current_note_ = 0xFF;

    for (int i = 0; i < 12; ++i) notes_[i] = 0;
}

void ArpEngine::LoadChord(uint16_t note_mask, ArpMode mode)
{
    mode_ = mode;
    BuildNoteList(note_mask);
    Reset();
}

void ArpEngine::Reset()
{
    position_  = 0;
    direction_ = 1;
    peak_      = false;

    if (note_count_ > 0)
    {
        current_note_ = notes_[0];
    }
    else
    {
        current_note_ = 0xFF;
    }
}

void ArpEngine::SetMode(ArpMode mode)
{
    mode_ = mode;
    Reset();
}

ArpMode ArpEngine::GetMode() const
{
    return mode_;
}

bool ArpEngine::HasNotes() const
{
    return note_count_ > 0;
}

uint8_t ArpEngine::NoteCount() const
{
    return note_count_;
}

uint8_t ArpEngine::CurrentNote() const
{
    return current_note_;
}

// ─────────────────────────────────────────────
// Advance — returns next note to play
// ─────────────────────────────────────────────

uint8_t ArpEngine::Advance()
{
    if (note_count_ == 0) return 0xFF;
    if (note_count_ == 1)
    {
        current_note_ = notes_[0];
        return current_note_;
    }

    switch (mode_)
    {
        case ArpMode::Off:
            current_note_ = notes_[0];
            break;

        case ArpMode::Up:
            position_ = (position_ + 1) % note_count_;
            current_note_ = notes_[position_];
            break;

        case ArpMode::Down:
            if (position_ == 0)
                position_ = note_count_ - 1;
            else
                --position_;
            current_note_ = notes_[position_];
            break;

        case ArpMode::UpDown:
        {
            position_ += direction_;

            if (position_ >= note_count_)
            {
                direction_ = -1;
                position_  = note_count_ - 2;
                if (position_ >= note_count_) position_ = 0;
            }
            else if (position_ == 0 && direction_ == -1)
            {
                direction_ = 1;
            }

            current_note_ = notes_[position_];
            break;
        }

        case ArpMode::DownUp:
        {
            position_ += direction_;

            if (position_ >= note_count_)
            {
                direction_ = -1;
                position_  = note_count_ - 2;
                if (position_ >= note_count_) position_ = 0;
            }
            else if (position_ == 0 && direction_ == -1)
            {
                direction_ = 1;
            }

            // DownUp plays the reversed list
            uint8_t reversed = (note_count_ - 1) - position_;
            current_note_ = notes_[reversed];
            break;
        }

        case ArpMode::Random:
            position_     = static_cast<uint8_t>(rand() % note_count_);
            current_note_ = notes_[position_];
            break;

        case ArpMode::AsPlayed:
            // AsPlayed uses insertion order — same as Up on the
            // as-entered list (note_mask bit order = entry order)
            position_     = (position_ + 1) % note_count_;
            current_note_ = notes_[position_];
            break;
    }

    return current_note_;
}

// ─────────────────────────────────────────────
// Interval calculation
// ─────────────────────────────────────────────

uint32_t ArpEngine::CalcIntervalMs(uint32_t bpm, ArpRate rate)
{
    if (bpm == 0) bpm = 1;

    // Quarter note interval in ms
    uint32_t quarter_ms = 60000 / bpm;

    switch (rate)
    {
        case ArpRate::Quarter:      return quarter_ms;
        case ArpRate::Eighth:       return quarter_ms / 2;
        case ArpRate::Sixteenth:    return quarter_ms / 4;
        case ArpRate::ThirtySecond: return quarter_ms / 8;
        default:                    return quarter_ms / 4;
    }
}

// ─────────────────────────────────────────────
// Build sorted note list from mask
// ─────────────────────────────────────────────

void ArpEngine::BuildNoteList(uint16_t note_mask)
{
    note_count_ = 0;

    for (uint8_t i = 0; i < 12; ++i)
    {
        if (note_mask & (1u << i))
        {
            notes_[note_count_++] = i;
        }
    }
    // notes_ is already in ascending order (C=0 ... B=11)
    // Up mode plays ascending, Down plays descending
    // DownUp reverses the array index at play time
}

} // namespace sequencer