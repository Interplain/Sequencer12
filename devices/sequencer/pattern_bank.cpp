#include "devices/sequencer/pattern_bank.h"
#include <algorithm>
#include <cstring>

namespace sequencer
{

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void PatternBank::Init()
{
    song_ = Song{};

    // Default chain: pattern 0 only
    song_.chain.length           = 1;
    song_.chain.pattern_indices[0] = 0;
    song_.chain.position         = 0;
}

// ── Song-level ────────────────────────────────────────────────────────────────

void PatternBank::SetGlobalBpm(uint32_t bpm)
{
    if (bpm == 0) bpm = 1;
    song_.global_bpm = bpm;
}

uint32_t PatternBank::GetGlobalBpm() const
{
    return song_.global_bpm;
}

void PatternBank::SetKey(KeyRoot root, KeyScale scale)
{
    song_.key_root  = root;
    song_.key_scale = scale;
}

KeyRoot PatternBank::GetKeyRoot() const
{
    return song_.key_root;
}

KeyScale PatternBank::GetKeyScale() const
{
    return song_.key_scale;
}

uint32_t PatternBank::GetEffectiveBpm(uint8_t pattern_index) const
{
    const Pattern& p = PatternAt(pattern_index);

    if (p.tempo_multiplier == 1.0f)
    {
        return song_.global_bpm;
    }

    return static_cast<uint32_t>(song_.global_bpm * p.tempo_multiplier);
}

bool PatternBank::IsNoteDiatonic(uint8_t note) const
{
    return sequencer::IsNoteDiatonic(note, song_.key_root, song_.key_scale);
}

uint16_t PatternBank::TransposeToKey(uint16_t note_mask) const
{
    uint8_t semitones = static_cast<uint8_t>(song_.key_root);
    return TransposeNoteMask(note_mask, semitones);
}

// ── Pattern access ─────────────────────────────────────────────────────────────

Pattern& PatternBank::GetPattern(uint8_t index)
{
    return PatternAt(index);
}

const Pattern& PatternBank::GetPattern(uint8_t index) const
{
    return PatternAt(index);
}

void PatternBank::ClearPattern(uint8_t index)
{
    PatternAt(index) = Pattern{};
}

void PatternBank::CopyPattern(uint8_t src, uint8_t dst)
{
    if (src == dst) return;
    PatternAt(dst) = PatternAt(src);
}

// ── Chain management ───────────────────────────────────────────────────────────

void PatternBank::ChainClear()
{
    song_.chain = PatternChain{};
}

bool PatternBank::ChainAppend(uint8_t pattern_index)
{
    if (song_.chain.length >= kChainLength) return false;
    if (pattern_index >= kPatternCount)     return false;

    song_.chain.pattern_indices[song_.chain.length] = pattern_index;
    ++song_.chain.length;
    return true;
}

bool PatternBank::ChainInsert(uint8_t pos, uint8_t pattern_index)
{
    if (song_.chain.length >= kChainLength) return false;
    if (pattern_index >= kPatternCount)     return false;
    if (pos > song_.chain.length)           return false;

    // Shift everything from pos onwards up by one
    for (uint8_t i = song_.chain.length; i > pos; --i)
    {
        song_.chain.pattern_indices[i] =
            song_.chain.pattern_indices[i - 1];
    }

    song_.chain.pattern_indices[pos] = pattern_index;
    ++song_.chain.length;
    return true;
}

void PatternBank::ChainRemove(uint8_t pos)
{
    if (pos >= song_.chain.length) return;

    for (uint8_t i = pos; i < song_.chain.length - 1; ++i)
    {
        song_.chain.pattern_indices[i] =
            song_.chain.pattern_indices[i + 1];
    }

    --song_.chain.length;

    // Keep playback position in bounds
    if (song_.chain.position >= song_.chain.length && song_.chain.length > 0)
    {
        song_.chain.position = song_.chain.length - 1;
    }
}

void PatternBank::ChainSetLength(uint8_t length)
{
    if (length > kChainLength) length = kChainLength;
    song_.chain.length = length;
}

uint8_t PatternBank::ChainGetLength() const
{
    return song_.chain.length;
}

uint8_t PatternBank::ChainGetPatternAt(uint8_t pos) const
{
    if (pos >= song_.chain.length) return 0;
    return song_.chain.pattern_indices[pos];
}

// ── Chain playback ─────────────────────────────────────────────────────────────

void PatternBank::ChainReset()
{
    song_.chain.position = 0;
}

uint8_t PatternBank::ChainCurrentPosition() const
{
    return song_.chain.position;
}

uint8_t PatternBank::ChainCurrentPatternIndex() const
{
    if (song_.chain.length == 0) return 0;
    return song_.chain.pattern_indices[song_.chain.position];
}

const Pattern& PatternBank::ChainCurrentPattern() const
{
    return PatternAt(ChainCurrentPatternIndex());
}

void PatternBank::ChainAdvance()
{
    if (song_.chain.length == 0) return;

    song_.chain.position =
        (song_.chain.position + 1) % song_.chain.length;
}

// ── Whole song ─────────────────────────────────────────────────────────────────

const Song& PatternBank::GetSong() const
{
    return song_;
}

Song& PatternBank::GetSong()
{
    return song_;
}

void PatternBank::LoadSong(const Song& song)
{
    song_ = song;
}

// ── Private ────────────────────────────────────────────────────────────────────

Pattern& PatternBank::PatternAt(uint8_t index)
{
    if (index >= kPatternCount) index = 0;
    return song_.patterns[index];
}

const Pattern& PatternBank::PatternAt(uint8_t index) const
{
    if (index >= kPatternCount) index = 0;
    return song_.patterns[index];
}

} // namespace sequencer