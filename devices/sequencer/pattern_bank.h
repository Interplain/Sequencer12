#pragma once
#include "devices/sequencer/sequencer_types.h"

namespace sequencer
{

class PatternBank
{
public:

    // ── Lifecycle ─────────────────────────────────────────────
    void Init();

    // ── Song-level ────────────────────────────────────────────
    void        SetGlobalBpm(uint32_t bpm);
    uint32_t    GetGlobalBpm() const;

    void        SetKey(KeyRoot root, KeyScale scale);
    KeyRoot     GetKeyRoot() const;
    KeyScale    GetKeyScale() const;

    // Returns the effective BPM for a given pattern
    // (uses pattern override if set, otherwise global)
    uint32_t    GetEffectiveBpm(uint8_t pattern_index) const;

    // Returns true if a note (0-11) is diatonic in the current key
    bool        IsNoteDiatonic(uint8_t note) const;

    // Returns a transposed copy of a note_mask shifted to current key root
    uint16_t    TransposeToKey(uint16_t note_mask) const;

    // ── Pattern access ────────────────────────────────────────
    Pattern&        GetPattern(uint8_t index);
    const Pattern&  GetPattern(uint8_t index) const;

    void        ClearPattern(uint8_t index);
    void        CopyPattern(uint8_t src, uint8_t dst);

    // ── Chain management ──────────────────────────────────────
    void        ChainClear();
    bool        ChainAppend(uint8_t pattern_index);   // false if chain full
    bool        ChainInsert(uint8_t pos, uint8_t pattern_index);
    void        ChainRemove(uint8_t pos);
    void        ChainSetLength(uint8_t length);

    uint8_t     ChainGetLength() const;
    uint8_t     ChainGetPatternAt(uint8_t pos) const;

    // ── Chain playback ────────────────────────────────────────
    void        ChainReset();
    uint8_t     ChainCurrentPosition() const;
    uint8_t     ChainCurrentPatternIndex() const;
    const Pattern& ChainCurrentPattern() const;
    void        ChainAdvance();   // move to next position, wraps to 0

    // ── Whole song ────────────────────────────────────────────
  // ── Whole song ────────────────────────────────────────────────────────────
    const Song& GetSong() const;
    Song&       GetSong();        // add this line
    void        LoadSong(const Song& song);

private:
    Song    song_{};

    // Safe bounds-checked pattern access
    Pattern&        PatternAt(uint8_t index);
    const Pattern&  PatternAt(uint8_t index) const;
};

} // namespace sequencer