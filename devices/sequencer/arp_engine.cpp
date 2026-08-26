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

    for (int i = 0; i < 4; ++i) notes_[i] = 0;
}

void ArpEngine::LoadNotes(const uint8_t* notes, uint8_t count, ArpMode mode)
{
    mode_ = mode;
    BuildNoteList(notes, count);
    Reset();
}

void ArpEngine::Reset()
{
    position_  = 0;
    direction_ = 1;
    peak_      = false;

    if (note_count_ > 0)
    {
        switch (mode_)
        {
            case ArpMode::Down:
                position_ = (uint8_t)(note_count_ - 1u);
                current_note_ = notes_[position_];
                break;

            case ArpMode::DownUp:
                position_ = 0u;
                current_note_ = notes_[(uint8_t)(note_count_ - 1u)];
                break;

            default:
                position_ = 0u;
                current_note_ = notes_[0];
                break;
        }
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
            // AsPlayed currently follows sorted UP order.
            position_     = (position_ + 1) % note_count_;
            current_note_ = notes_[position_];
            break;
    }

    return current_note_;
}

// ─────────────────────────────────────────────
// 96-PPQN cadence helper
// ─────────────────────────────────────────────

uint32_t ArpEngine::TicksPerEvent(ArpRate rate)
{
    switch (rate)
    {
        case ArpRate::Quarter:      return 96u;
        case ArpRate::Eighth:       return 48u;
        case ArpRate::Sixteenth:    return 24u;
        case ArpRate::ThirtySecond: return 12u;
        default:                    return 24u;
    }
}

// ─────────────────────────────────────────────
// Build sorted absolute-MIDI note list
// ─────────────────────────────────────────────

void ArpEngine::BuildNoteList(const uint8_t* notes, uint8_t count)
{
    note_count_ = 0;

    if (!notes || count == 0u)
    {
        return;
    }

    if (count > 4u)
    {
        count = 4u;
    }

    for (uint8_t i = 0u; i < count; ++i)
    {
        const uint8_t note = notes[i];
        if (note > 127u)
        {
            continue;
        }

        uint8_t duplicate = 0u;
        for (uint8_t j = 0u; j < note_count_; ++j)
        {
            if (notes_[j] == note)
            {
                duplicate = 1u;
                break;
            }
        }
        if (duplicate)
        {
            continue;
        }

        notes_[note_count_++] = note;
    }

    for (uint8_t i = 0u; i < note_count_; ++i)
    {
        for (uint8_t j = (uint8_t)(i + 1u); j < note_count_; ++j)
        {
            if (notes_[j] < notes_[i])
            {
                const uint8_t t = notes_[i];
                notes_[i] = notes_[j];
                notes_[j] = t;
            }
        }
    }
}

} // namespace sequencer