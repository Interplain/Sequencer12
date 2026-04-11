#pragma once
#include <cstdint>
#include <array>
#include "devices/sequencer/sequencer_types.h"

namespace sequencer
{

// ─────────────────────────────────────────────
// Chord types
// ─────────────────────────────────────────────

enum class ChordType : uint8_t
{
    // Triads
    Major = 0,
    Minor,
    Diminished,
    Augmented,
    Sus2,
    Sus4,

    // 7ths
    Major7,
    Minor7,
    Dom7,
    Dim7,
    HalfDim7,
    MinMaj7,

    // Extended
    Major9,
    Minor9,
    Dom9,
    Major11,
    Minor11,
    Dom11,
    Major13,
    Minor13,
    Add9,
    mAdd9,
    Major6,
    Minor6,

    Count
};

constexpr uint8_t kChordTypeCount = static_cast<uint8_t>(ChordType::Count);

// ─────────────────────────────────────────────
// Preset entry
// ─────────────────────────────────────────────

struct ChordPreset
{
    KeyRoot     root;
    ChordType   type;
    uint16_t    note_mask;
    const char* type_name;   // e.g. "maj", "min7", "dom9"
};

// ─────────────────────────────────────────────
// Total presets = 12 roots x 24 types = 288
// ─────────────────────────────────────────────

constexpr uint32_t kPresetCount = 12 * kChordTypeCount;

// ─────────────────────────────────────────────
// Chord library
// ─────────────────────────────────────────────

class ChordLibrary
{
public:

    // Returns the full preset table
    static const ChordPreset* GetPresets();
    static uint32_t           GetPresetCount();

    // Get a single preset by index
    static const ChordPreset& GetPreset(uint32_t index);

    // Get index of first preset for a given root
    // Presets are ordered root-first so all types for a root are contiguous
    static uint32_t GetRootOffset(KeyRoot root);

    // Get a preset by root + type
    static const ChordPreset& GetPreset(KeyRoot root, ChordType type);

    // Returns note_mask for a root + type
    static uint16_t GetNoteMask(KeyRoot root, ChordType type);

    // Returns true if all notes in a preset are diatonic in the given key
    static bool IsDiatonic(const ChordPreset& preset,
                           KeyRoot key_root,
                           KeyScale key_scale);

    // Returns the short type name string
    static const char* GetTypeName(ChordType type);

    // Returns the root name string
    static const char* GetRootName(KeyRoot root);
};

} // namespace sequencer