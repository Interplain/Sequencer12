#pragma once
#include <cstdint>
#include <array>

namespace sequencer
{

// ─────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────

constexpr uint32_t kStepCount    = 12;
constexpr uint32_t kPatternCount = 32;
constexpr uint32_t kChainLength  = 32;
constexpr uint8_t  kStepLedgerMax = 32;
constexpr uint8_t  kStepEventLengthPackedBytes = 20;
constexpr uint8_t  kMidiNoteNone = 0xFFu;

struct LedgerSlot
{
    std::array<uint8_t, 4> notes{
        kMidiNoteNone,
        kMidiNoteNone,
        kMidiNoteNone,
        kMidiNoteNone
    };
};

inline bool IsMidiNoteValid(uint8_t note)
{
    return note <= 127u;
}

inline uint8_t LedgerSlotCount(const LedgerSlot& slot)
{
    uint8_t count = 0u;
    for (uint8_t i = 0u; i < slot.notes.size(); ++i)
    {
        if (slot.notes[i] != kMidiNoteNone) ++count;
    }
    return count;
}

inline bool LedgerSlotIsEmpty(const LedgerSlot& slot)
{
    return LedgerSlotCount(slot) == 0u;
}

inline void LedgerSlotSort(LedgerSlot& slot)
{
    for (uint8_t i = 0u; i < slot.notes.size(); ++i)
    {
        for (uint8_t j = (uint8_t)(i + 1u); j < slot.notes.size(); ++j)
        {
            const uint8_t a = slot.notes[i];
            const uint8_t b = slot.notes[j];
            const bool a_empty = (a == kMidiNoteNone);
            const bool b_empty = (b == kMidiNoteNone);

            if (a_empty && !b_empty)
            {
                slot.notes[i] = b;
                slot.notes[j] = a;
            }
            else if (!a_empty && !b_empty && b < a)
            {
                slot.notes[i] = b;
                slot.notes[j] = a;
            }
        }
    }
}

inline void LedgerSlotClear(LedgerSlot& slot)
{
    for (uint8_t i = 0u; i < slot.notes.size(); ++i)
    {
        slot.notes[i] = kMidiNoteNone;
    }
}

inline bool LedgerSlotContains(const LedgerSlot& slot, uint8_t note)
{
    if (!IsMidiNoteValid(note)) return false;
    for (uint8_t i = 0u; i < slot.notes.size(); ++i)
    {
        if (slot.notes[i] == note) return true;
    }
    return false;
}

inline bool LedgerSlotAdd(LedgerSlot& slot, uint8_t note)
{
    if (!IsMidiNoteValid(note)) return false;
    if (LedgerSlotContains(slot, note)) return false;

    for (uint8_t i = 0u; i < slot.notes.size(); ++i)
    {
        if (slot.notes[i] == kMidiNoteNone)
        {
            slot.notes[i] = note;
            LedgerSlotSort(slot);
            return true;
        }
    }
    return false;
}

inline bool LedgerSlotRemove(LedgerSlot& slot, uint8_t note)
{
    if (!IsMidiNoteValid(note)) return false;
    for (uint8_t i = 0u; i < slot.notes.size(); ++i)
    {
        if (slot.notes[i] == note)
        {
            slot.notes[i] = kMidiNoteNone;
            LedgerSlotSort(slot);
            return true;
        }
    }
    return false;
}

inline bool LedgerSlotToggle(LedgerSlot& slot, uint8_t note)
{
    if (!IsMidiNoteValid(note)) return false;
    if (LedgerSlotContains(slot, note)) return LedgerSlotRemove(slot, note);
    return LedgerSlotAdd(slot, note);
}

inline uint16_t LedgerSlotToPitchClassMask(const LedgerSlot& slot)
{
    uint16_t mask = 0u;
    for (uint8_t i = 0u; i < slot.notes.size(); ++i)
    {
        const uint8_t note = slot.notes[i];
        if (note == kMidiNoteNone) continue;
        mask |= (uint16_t)(1u << (note % 12u));
    }
    return mask;
}

// ─────────────────────────────────────────────
// Note mask helpers
// ─────────────────────────────────────────────

constexpr uint16_t NOTE_C  = (1u << 0);
constexpr uint16_t NOTE_CS = (1u << 1);
constexpr uint16_t NOTE_D  = (1u << 2);
constexpr uint16_t NOTE_DS = (1u << 3);
constexpr uint16_t NOTE_E  = (1u << 4);
constexpr uint16_t NOTE_F  = (1u << 5);
constexpr uint16_t NOTE_FS = (1u << 6);
constexpr uint16_t NOTE_G  = (1u << 7);
constexpr uint16_t NOTE_GS = (1u << 8);
constexpr uint16_t NOTE_A  = (1u << 9);
constexpr uint16_t NOTE_AS = (1u << 10);
constexpr uint16_t NOTE_B  = (1u << 11);

inline bool IsNoteActive(uint16_t note_mask, uint8_t note)
{
    if (note >= 12) return false;
    return (note_mask & (1u << note)) != 0;
}

inline void SetNote(uint16_t& note_mask, uint8_t note)
{
    if (note < 12) note_mask |= (1u << note);
}

inline void ClearNote(uint16_t& note_mask, uint8_t note)
{
    if (note < 12) note_mask &= ~(1u << note);
}

inline void ToggleNote(uint16_t& note_mask, uint8_t note)
{
    if (note < 12) note_mask ^= (1u << note);
}

inline void ClearAllNotes(uint16_t& note_mask)
{
    note_mask = 0;
}

// ─────────────────────────────────────────────
// Key signature
// ─────────────────────────────────────────────

enum class KeyRoot : uint8_t
{
    C = 0, Cs, D, Ds, E, F, Fs, G, Gs, A, As, B
};

enum class KeyScale : uint8_t
{
    Major = 0,
    NaturalMinor,
    Dorian,
    Phrygian,
    Lydian,
    Mixolydian,
    Locrian,
    HarmonicMinor,
    MelodicMinor
};

inline void GetScaleIntervals(KeyScale scale, uint8_t out_intervals[7])
{
    switch (scale)
    {
        case KeyScale::Major:
            out_intervals[0]=0; out_intervals[1]=2; out_intervals[2]=4;
            out_intervals[3]=5; out_intervals[4]=7; out_intervals[5]=9;
            out_intervals[6]=11; break;
        case KeyScale::NaturalMinor:
            out_intervals[0]=0; out_intervals[1]=2; out_intervals[2]=3;
            out_intervals[3]=5; out_intervals[4]=7; out_intervals[5]=8;
            out_intervals[6]=10; break;
        case KeyScale::Dorian:
            out_intervals[0]=0; out_intervals[1]=2; out_intervals[2]=3;
            out_intervals[3]=5; out_intervals[4]=7; out_intervals[5]=9;
            out_intervals[6]=10; break;
        case KeyScale::Phrygian:
            out_intervals[0]=0; out_intervals[1]=1; out_intervals[2]=3;
            out_intervals[3]=5; out_intervals[4]=7; out_intervals[5]=8;
            out_intervals[6]=10; break;
        case KeyScale::Lydian:
            out_intervals[0]=0; out_intervals[1]=2; out_intervals[2]=4;
            out_intervals[3]=6; out_intervals[4]=7; out_intervals[5]=9;
            out_intervals[6]=11; break;
        case KeyScale::Mixolydian:
            out_intervals[0]=0; out_intervals[1]=2; out_intervals[2]=4;
            out_intervals[3]=5; out_intervals[4]=7; out_intervals[5]=9;
            out_intervals[6]=10; break;
        case KeyScale::Locrian:
            out_intervals[0]=0; out_intervals[1]=1; out_intervals[2]=3;
            out_intervals[3]=5; out_intervals[4]=6; out_intervals[5]=8;
            out_intervals[6]=10; break;
        case KeyScale::HarmonicMinor:
            out_intervals[0]=0; out_intervals[1]=2; out_intervals[2]=3;
            out_intervals[3]=5; out_intervals[4]=7; out_intervals[5]=8;
            out_intervals[6]=11; break;
        case KeyScale::MelodicMinor:
            out_intervals[0]=0; out_intervals[1]=2; out_intervals[2]=3;
            out_intervals[3]=5; out_intervals[4]=7; out_intervals[5]=9;
            out_intervals[6]=11; break;
        default:
            out_intervals[0]=0; out_intervals[1]=2; out_intervals[2]=4;
            out_intervals[3]=5; out_intervals[4]=7; out_intervals[5]=9;
            out_intervals[6]=11; break;
    }
}

inline bool IsNoteDiatonic(uint8_t note, KeyRoot root, KeyScale scale)
{
    uint8_t intervals[7];
    GetScaleIntervals(scale, intervals);
    uint8_t root_val = static_cast<uint8_t>(root);
    for (int i = 0; i < 7; ++i)
    {
        if (((root_val + intervals[i]) % 12) == note)
            return true;
    }
    return false;
}

inline uint16_t TransposeNoteMask(uint16_t note_mask, uint8_t semitones)
{
    if (semitones == 0) return note_mask;
    uint16_t result = 0;
    for (int i = 0; i < 12; ++i)
    {
        if (note_mask & (1u << i))
            result |= (1u << ((i + semitones) % 12));
    }
    return result;
}

// ─────────────────────────────────────────────
// Step
// ─────────────────────────────────────────────

enum class StepType : uint8_t
{
    Chord,
    Rest,
    Skip,
    Empty
};

enum class BarState : uint8_t
{
    Active = 0,
    Skip
};

struct StepSlot
{
    StepType type                = StepType::Empty;
    BarState bar_state           = BarState::Active;
    uint32_t duration_multiplier = 1;
    uint8_t  repeat_count        = 1;     // ledger length / note count inside the step
    uint16_t note_mask           = 0;
    std::array<LedgerSlot, kStepLedgerMax> note_ledger{}; // one absolute-note cell per sub-note slot
    /* Canonical event lengths in 1/32 units packed as 32 x 5-bit length_minus_1 values.
     * Values at non-event-start positions are ignored. */
    std::array<uint8_t, kStepEventLengthPackedBytes> event_length_packed{};
    char     custom_chord_name[17] = {0};
    uint8_t  velocity            = 100;   // 0-127 MIDI standard
    uint8_t  probability         = 100;   // 0-100 percent chance of firing
};

// ─────────────────────────────────────────────
// Playback mode  — must be before Pattern
// ─────────────────────────────────────────────

enum class PlaybackMode : uint8_t
{
    Forward = 0,
    PingPong,
    Loop,
    OneShot
};

// ─────────────────────────────────────────────
// Arp
// ─────────────────────────────────────────────

enum class ArpMode : uint8_t
{
    Off = 0,
    Up,
    Down,
    UpDown,
    DownUp,
    Random,
    AsPlayed
};

enum class ArpRate : uint8_t
{
    Quarter = 0,   // 1/4
    Eighth,        // 1/8
    Sixteenth,     // 1/16
    ThirtySecond   // 1/32
};

// ─────────────────────────────────────────────
// Time signature
// ─────────────────────────────────────────────

struct TimeSig
{
    uint8_t numerator   = 4;   // beats per bar
    uint8_t denominator = 4;   // beat unit (4 = quarter note)
};

// ─────────────────────────────────────────────
// Pattern  (one complete 12-step block)
// ─────────────────────────────────────────────

struct Pattern
{
    std::array<StepSlot, kStepCount> steps{};

    // Tempo
    float       tempo_multiplier  = 1.0f;  // 1.0 = follow global BPM

    // Step range
    uint8_t     step_count        = kStepCount;  // active steps 1-12
    uint8_t     step_division     = 4;           // steps per quarter: 1=1/4, 2=1/8, 4=1/16, 8=1/32

    // Playback
    PlaybackMode playback_mode    = PlaybackMode::Forward;
    uint8_t      repeat_count     = 1;     // times to play before chain advance

    // Arp
    ArpMode     arp_mode          = ArpMode::Off;
    ArpRate     arp_rate          = ArpRate::Sixteenth;
};

// ─────────────────────────────────────────────
// Pattern chain
// ─────────────────────────────────────────────

struct PatternChain
{
    std::array<uint8_t, kChainLength> pattern_indices{};
    uint8_t  length   = 0;
    uint8_t  position = 0;
};

// ─────────────────────────────────────────────
// Song  (top-level container)
// ─────────────────────────────────────────────

struct Song
{
    uint32_t     global_bpm     = 120;
    KeyRoot      key_root       = KeyRoot::C;
    KeyScale     key_scale      = KeyScale::Major;
    TimeSig      time_sig{};
    uint8_t      swing          = 0;
    int8_t       transpose      = 0;   // -24 to +24 semitones

    std::array<Pattern, kPatternCount> patterns{};
    PatternChain chain{};
};

} // namespace sequencer