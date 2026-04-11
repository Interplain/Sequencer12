#include "devices/sequencer/chords/chord_library.h"

namespace sequencer
{

// ─────────────────────────────────────────────
// Build note_mask from intervals
// ─────────────────────────────────────────────

static constexpr uint16_t MakeMask(uint8_t root,
                                   const uint8_t* intervals,
                                   uint8_t count)
{
    uint16_t mask = 0;
    for (uint8_t i = 0; i < count; ++i)
    {
        mask |= (1u << ((root + intervals[i]) % 12));
    }
    return mask;
}

// ─────────────────────────────────────────────
// Interval patterns (root-relative semitones)
// ─────────────────────────────────────────────

// Triads
static constexpr uint8_t kMaj[]     = { 0, 4, 7 };
static constexpr uint8_t kMin[]     = { 0, 3, 7 };
static constexpr uint8_t kDim[]     = { 0, 3, 6 };
static constexpr uint8_t kAug[]     = { 0, 4, 8 };
static constexpr uint8_t kSus2[]    = { 0, 2, 7 };
static constexpr uint8_t kSus4[]    = { 0, 5, 7 };

// 7ths
static constexpr uint8_t kMaj7[]    = { 0, 4, 7, 11 };
static constexpr uint8_t kMin7[]    = { 0, 3, 7, 10 };
static constexpr uint8_t kDom7[]    = { 0, 4, 7, 10 };
static constexpr uint8_t kDim7[]    = { 0, 3, 6, 9  };
static constexpr uint8_t kHDim7[]   = { 0, 3, 6, 10 };
static constexpr uint8_t kMinMaj7[] = { 0, 3, 7, 11 };

// Extended (note_mask wraps within octave — all notes mod 12)
static constexpr uint8_t kMaj9[]    = { 0, 4, 7, 11, 2  };
static constexpr uint8_t kMin9[]    = { 0, 3, 7, 10, 2  };
static constexpr uint8_t kDom9[]    = { 0, 4, 7, 10, 2  };
static constexpr uint8_t kMaj11[]   = { 0, 4, 7, 11, 2, 5  };
static constexpr uint8_t kMin11[]   = { 0, 3, 7, 10, 2, 5  };
static constexpr uint8_t kDom11[]   = { 0, 4, 7, 10, 2, 5  };
static constexpr uint8_t kMaj13[]   = { 0, 4, 7, 11, 2, 5, 9 };
static constexpr uint8_t kMin13[]   = { 0, 3, 7, 10, 2, 5, 9 };
static constexpr uint8_t kAdd9[]    = { 0, 4, 7, 2  };
static constexpr uint8_t kmAdd9[]   = { 0, 3, 7, 2  };
static constexpr uint8_t kMaj6[]    = { 0, 4, 7, 9  };
static constexpr uint8_t kMin6[]    = { 0, 3, 7, 9  };

// ─────────────────────────────────────────────
// Type name strings
// ─────────────────────────────────────────────

static const char* kTypeNames[kChordTypeCount] =
{
    "maj",   "min",   "dim",    "aug",    "sus2",   "sus4",
    "maj7",  "min7",  "dom7",   "dim7",   "hdim7",  "minmaj7",
    "maj9",  "min9",  "dom9",   "maj11",  "min11",  "dom11",
    "maj13", "min13", "add9",   "madd9",  "maj6",   "min6"
};

static const char* kRootNames[12] =
{
    "C", "C#", "D", "D#", "E", "F",
    "F#", "G", "G#", "A", "A#", "B"
};

// ─────────────────────────────────────────────
// Helper macro to build one preset row
// ─────────────────────────────────────────────

#define PRESET(root_val, type_enum, intervals) \
    { static_cast<KeyRoot>(root_val),          \
      ChordType::type_enum,                    \
      MakeMask(root_val, intervals,            \
               sizeof(intervals)/sizeof(intervals[0])), \
      kTypeNames[static_cast<uint8_t>(ChordType::type_enum)] }

// ─────────────────────────────────────────────
// Preset table — 12 roots × 24 types = 288
// Ordered: for each root, all types in ChordType order
// ─────────────────────────────────────────────

static const ChordPreset kPresets[kPresetCount] =
{
    #define ROOT(r) \
        PRESET(r, Major,      kMaj),    \
        PRESET(r, Minor,      kMin),    \
        PRESET(r, Diminished, kDim),    \
        PRESET(r, Augmented,  kAug),    \
        PRESET(r, Sus2,       kSus2),   \
        PRESET(r, Sus4,       kSus4),   \
        PRESET(r, Major7,     kMaj7),   \
        PRESET(r, Minor7,     kMin7),   \
        PRESET(r, Dom7,       kDom7),   \
        PRESET(r, Dim7,       kDim7),   \
        PRESET(r, HalfDim7,   kHDim7),  \
        PRESET(r, MinMaj7,    kMinMaj7),\
        PRESET(r, Major9,     kMaj9),   \
        PRESET(r, Minor9,     kMin9),   \
        PRESET(r, Dom9,       kDom9),   \
        PRESET(r, Major11,    kMaj11),  \
        PRESET(r, Minor11,    kMin11),  \
        PRESET(r, Dom11,      kDom11),  \
        PRESET(r, Major13,    kMaj13),  \
        PRESET(r, Minor13,    kMin13),  \
        PRESET(r, Add9,       kAdd9),   \
        PRESET(r, mAdd9,      kmAdd9),  \
        PRESET(r, Major6,     kMaj6),   \
        PRESET(r, Minor6,     kMin6)

    ROOT(0),   // C
    ROOT(1),   // C#
    ROOT(2),   // D
    ROOT(3),   // D#
    ROOT(4),   // E
    ROOT(5),   // F
    ROOT(6),   // F#
    ROOT(7),   // G
    ROOT(8),   // G#
    ROOT(9),   // A
    ROOT(10),  // A#
    ROOT(11),  // B

    #undef ROOT
};

#undef PRESET

// ─────────────────────────────────────────────
// ChordLibrary implementation
// ─────────────────────────────────────────────

const ChordPreset* ChordLibrary::GetPresets()
{
    return kPresets;
}

uint32_t ChordLibrary::GetPresetCount()
{
    return kPresetCount;
}

const ChordPreset& ChordLibrary::GetPreset(uint32_t index)
{
    if (index >= kPresetCount) index = 0;
    return kPresets[index];
}

uint32_t ChordLibrary::GetRootOffset(KeyRoot root)
{
    return static_cast<uint32_t>(root) * kChordTypeCount;
}

const ChordPreset& ChordLibrary::GetPreset(KeyRoot root, ChordType type)
{
    uint32_t index = GetRootOffset(root) +
                     static_cast<uint32_t>(type);
    return GetPreset(index);
}

uint16_t ChordLibrary::GetNoteMask(KeyRoot root, ChordType type)
{
    return GetPreset(root, type).note_mask;
}

bool ChordLibrary::IsDiatonic(const ChordPreset& preset,
                               KeyRoot key_root,
                               KeyScale key_scale)
{
    for (int i = 0; i < 12; ++i)
    {
        if (preset.note_mask & (1u << i))
        {
            if (!sequencer::IsNoteDiatonic(i, key_root, key_scale))
            {
                return false;
            }
        }
    }
    return true;
}

const char* ChordLibrary::GetTypeName(ChordType type)
{
    uint8_t idx = static_cast<uint8_t>(type);
    if (idx >= kChordTypeCount) return "???";
    return kTypeNames[idx];
}

const char* ChordLibrary::GetRootName(KeyRoot root)
{
    uint8_t idx = static_cast<uint8_t>(root);
    if (idx >= 12) return "?";
    return kRootNames[idx];
}

} // namespace sequencer