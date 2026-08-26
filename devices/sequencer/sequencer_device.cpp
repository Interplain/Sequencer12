#include "devices/sequencer/sequencer_device.h"
#include "devices/sequencer/arp_engine.h"
#include "core/pitch_mapping.h"
#include "stm32f4xx_hal.h"
#include <cstring>

using sequencer::StepSlot;
using sequencer::StepType;
using sequencer::Pattern;
using namespace sequencer;

static ChordType UiChordTypeToLibraryType(uint8_t chord_type)
{
    switch (chord_type)
    {
        case 1:  return ChordType::Major;
        case 2:  return ChordType::Minor;
        case 3:  return ChordType::Diminished;
        case 4:  return ChordType::Augmented;
        case 5:  return ChordType::Sus4;
        case 6:  return ChordType::Dom7;
        case 7:  return ChordType::Major7;
        case 8:  return ChordType::Minor7;
        case 9:  return ChordType::Dim7;
        case 10: return ChordType::Dom7;   /* 7sus4 fallback */
        case 11: return ChordType::Major6;
        case 12: return ChordType::Minor6;
        case 13: return ChordType::Dom9;
        case 14: return ChordType::Major9;
        case 15: return ChordType::Minor9;
        case 16: return ChordType::Dom11;
        default: return ChordType::Major;
    }
}

static uint32_t UiDurationToMultiplier(uint8_t duration)
{
    switch (duration)
    {
        case 0: return 1; /* 16th */
        case 1: return 2; /* 8th */
        case 2: return 4; /* quarter */
        case 3: return 3; /* dotted 8th */
        default: return 1;
    }
}

static uint8_t UiMultiplierToDuration(uint32_t multiplier)
{
    switch (multiplier)
    {
        case 1: return 0; /* 16th */
        case 2: return 1; /* 8th */
        case 4: return 2; /* quarter */
        case 3: return 3; /* dotted 8th */
        default: return 0;
    }
}

static uint8_t ClampLedgerLength(uint8_t length)
{
    if (length < 1u) return 1u;
    if (length > kStepLedgerMax) return kStepLedgerMax;
    return length;
}

static bool IsSupportedGridDivision(uint8_t grid_division)
{
    return grid_division == 1u || grid_division == 2u || grid_division == 4u || grid_division == 8u;
}

static uint8_t StepsPerBar(const TimeSig& time_sig, uint8_t grid_division)
{
    if (!IsSupportedGridDivision(grid_division)) return 0u;
    if (time_sig.numerator == 0u) return 0u;
    if (!(time_sig.denominator == 2u || time_sig.denominator == 4u || time_sig.denominator == 8u)) return 0u;

    const uint32_t storage_numerator = (uint32_t)time_sig.numerator * 32u;
    if ((storage_numerator % time_sig.denominator) != 0u) return 0u;
    if ((storage_numerator / time_sig.denominator) > kStepLedgerMax) return 0u;

    const uint32_t step_numerator =
        (uint32_t)time_sig.numerator * 4u * grid_division;
    if ((step_numerator % time_sig.denominator) != 0u) return 0u;

    const uint32_t steps = step_numerator / time_sig.denominator;
    if (steps == 0u || steps > kStepLedgerMax) return 0u;
    return (uint8_t)steps;
}

static uint8_t GridPositionToLedgerIndex(uint8_t grid_division, uint8_t grid_position)
{
    if (!IsSupportedGridDivision(grid_division)) return kStepLedgerMax;
    const uint8_t ledger_index = (uint8_t)(grid_position * (8u / grid_division));
    return (ledger_index < kStepLedgerMax) ? ledger_index : kStepLedgerMax;
}

static uint8_t CanonicalStrideForDivision(uint8_t grid_division)
{
    if (!IsSupportedGridDivision(grid_division)) return 1u;
    return (uint8_t)(8u / grid_division);
}

static uint8_t CanonicalPositionsPerBar(const TimeSig& time_sig)
{
    const uint8_t positions = StepsPerBar(time_sig, 8u);
    return (positions == 0u) ? kStepLedgerMax : positions;
}

static uint8_t PackedEventLengthRawGet(const StepSlot& slot, uint8_t canonical_index)
{
    if (canonical_index >= kStepLedgerMax) return 0u;

    const uint16_t bit_pos = (uint16_t)canonical_index * 5u;
    const uint8_t byte_index = (uint8_t)(bit_pos >> 3u);
    const uint8_t bit_offset = (uint8_t)(bit_pos & 0x07u);

    uint32_t chunk = slot.event_length_packed[byte_index];
    if ((uint8_t)(byte_index + 1u) < kStepEventLengthPackedBytes)
    {
        chunk |= (uint32_t)slot.event_length_packed[byte_index + 1u] << 8u;
    }
    if (bit_offset > 3u && (uint8_t)(byte_index + 2u) < kStepEventLengthPackedBytes)
    {
        chunk |= (uint32_t)slot.event_length_packed[byte_index + 2u] << 16u;
    }

    return (uint8_t)((chunk >> bit_offset) & 0x1Fu);
}

static void PackedEventLengthRawSet(StepSlot& slot, uint8_t canonical_index, uint8_t raw_value)
{
    if (canonical_index >= kStepLedgerMax) return;

    raw_value &= 0x1Fu;
    const uint16_t bit_pos = (uint16_t)canonical_index * 5u;
    const uint8_t byte_index = (uint8_t)(bit_pos >> 3u);
    const uint8_t bit_offset = (uint8_t)(bit_pos & 0x07u);

    uint32_t chunk = slot.event_length_packed[byte_index];
    if ((uint8_t)(byte_index + 1u) < kStepEventLengthPackedBytes)
    {
        chunk |= (uint32_t)slot.event_length_packed[byte_index + 1u] << 8u;
    }
    if ((uint8_t)(byte_index + 2u) < kStepEventLengthPackedBytes)
    {
        chunk |= (uint32_t)slot.event_length_packed[byte_index + 2u] << 16u;
    }

    const uint32_t mask = (uint32_t)0x1Fu << bit_offset;
    chunk = (chunk & ~mask) | ((uint32_t)raw_value << bit_offset);

    slot.event_length_packed[byte_index] = (uint8_t)(chunk & 0xFFu);
    if ((uint8_t)(byte_index + 1u) < kStepEventLengthPackedBytes)
    {
        slot.event_length_packed[byte_index + 1u] = (uint8_t)((chunk >> 8u) & 0xFFu);
    }
    if ((uint8_t)(byte_index + 2u) < kStepEventLengthPackedBytes)
    {
        slot.event_length_packed[byte_index + 2u] = (uint8_t)((chunk >> 16u) & 0xFFu);
    }
}

static uint8_t IsCanonicalEventStart(const StepSlot& slot, uint8_t canonical_index)
{
    if (canonical_index >= kStepLedgerMax) return 0u;
    return sequencer::LedgerSlotIsEmpty(slot.note_ledger[canonical_index]) ? 0u : 1u;
}

static uint8_t FindNextEventStartCanonical(const StepSlot& slot,
                                           uint8_t canonical_start,
                                           uint8_t canonical_positions)
{
    if (canonical_positions > kStepLedgerMax) canonical_positions = kStepLedgerMax;
    for (uint8_t idx = (uint8_t)(canonical_start + 1u); idx < canonical_positions; ++idx)
    {
        if (IsCanonicalEventStart(slot, idx))
        {
            return idx;
        }
    }
    return canonical_positions;
}

static uint8_t EventLengthCanonicalForStart(const StepSlot& slot,
                                            uint8_t canonical_start,
                                            uint8_t canonical_positions)
{
    if (!IsCanonicalEventStart(slot, canonical_start)) return 0u;

    uint8_t max_len = 1u;
    const uint8_t next_start = FindNextEventStartCanonical(slot, canonical_start, canonical_positions);
    if (next_start > canonical_start)
    {
        max_len = (uint8_t)(next_start - canonical_start);
    }

    uint8_t len = (uint8_t)(PackedEventLengthRawGet(slot, canonical_start) + 1u);
    if (len < 1u) len = 1u;
    if (len > max_len) len = max_len;
    return len;
}

static uint8_t IsCoarserDivisionChange(uint8_t current_division, uint8_t proposed_division)
{
    const uint8_t current_stride = CanonicalStrideForDivision(current_division);
    const uint8_t proposed_stride = CanonicalStrideForDivision(proposed_division);
    return (proposed_stride > current_stride) ? 1u : 0u;
}

static uint8_t StepSlotCompatibleWithCanonicalStride(const StepSlot& slot,
                                                     uint8_t canonical_positions,
                                                     uint8_t required_stride)
{
    if (canonical_positions > kStepLedgerMax) canonical_positions = kStepLedgerMax;
    if (required_stride == 0u) return 0u;

    for (uint8_t start = 0u; start < canonical_positions; ++start)
    {
        if (!IsCanonicalEventStart(slot, start)) continue;

        const uint8_t len = EventLengthCanonicalForStart(slot, start, canonical_positions);
        if (len == 0u) return 0u;
        if ((start % required_stride) != 0u) return 0u;
        if ((len % required_stride) != 0u) return 0u;
    }

    return 1u;
}

static uint8_t PatternCompatibleWithCoarserDivision(const Pattern& pattern, uint8_t proposed_division)
{
    const uint8_t required_stride = CanonicalStrideForDivision(proposed_division);
    for (uint8_t bar = 0u; bar < kStepCount; ++bar)
    {
        if (!StepSlotCompatibleWithCanonicalStride(pattern.steps[bar], kStepLedgerMax, required_stride))
        {
            return 0u;
        }
    }
    return 1u;
}

static uint8_t ResolveEventStartCanonicalForPosition(const StepSlot& slot,
                                                     uint8_t canonical_pos,
                                                     uint8_t canonical_positions,
                                                     uint8_t* out_start)
{
    if (!out_start) return 0u;
    if (canonical_pos >= canonical_positions) return 0u;

    if (IsCanonicalEventStart(slot, canonical_pos))
    {
        *out_start = canonical_pos;
        return 1u;
    }

    for (int16_t s = (int16_t)canonical_pos - 1; s >= 0; --s)
    {
        const uint8_t start = (uint8_t)s;
        if (!IsCanonicalEventStart(slot, start)) continue;

        const uint8_t len = EventLengthCanonicalForStart(slot, start, canonical_positions);
        if (len == 0u) continue;
        if (canonical_pos < (uint8_t)(start + len))
        {
            *out_start = start;
            return 1u;
        }
    }

    return 0u;
}

static uint16_t PitchClassMaskFromLedger(const StepSlot& slot)
{
    uint16_t mask = 0u;
    for (uint8_t i = 0u; i < kStepLedgerMax; ++i)
    {
        mask |= LedgerSlotToPitchClassMask(slot.note_ledger[i]);
    }
    return mask;
}

static void RefreshSlotSummary(StepSlot& slot)
{
    const uint16_t summary = PitchClassMaskFromLedger(slot);
    slot.note_mask = summary;
}

static void UpdateLegacyStepTypeProjection(StepSlot& slot)
{
    if (slot.bar_state == BarState::Skip)
    {
        slot.type = StepType::Skip;
        return;
    }

    slot.type = (slot.note_mask != 0u) ? StepType::Chord : StepType::Empty;
}

static void InitLedgerFromMask(sequencer::StepSlot& slot, uint16_t note_mask, uint8_t length)
{
    constexpr uint8_t kDefaultMidiBase = (uint8_t)kLegacyPitchClassFallbackMidiBase; /* C4 */
    for (uint8_t i = 0u; i < kStepLedgerMax; ++i)
    {
        LedgerSlotClear(slot.note_ledger[i]);
    }

    LedgerSlot& first = slot.note_ledger[0];
    for (uint8_t note = 0u; note < 12u; ++note)
    {
        if ((note_mask & (uint16_t)(1u << note)) == 0u) continue;
        (void)LedgerSlotAdd(first, (uint8_t)(kDefaultMidiBase + note));
    }

    slot.repeat_count = ClampLedgerLength(length);
    slot.note_mask = (uint16_t)(note_mask & 0x0FFFu);

    /* Default event length is one visible 1/16 grid position (2 canonical ticks). */
    PackedEventLengthRawSet(slot, 0u, 1u);
}

static void NormalizePatternDivisionCadence(Pattern& pattern, const TimeSig& time_sig)
{
    if (StepsPerBar(time_sig, pattern.step_division) == 0u)
    {
        pattern.step_division = 4u;
    }

    const uint8_t fixed_len = StepsPerBar(time_sig, pattern.step_division);
    for (uint8_t i = 0u; i < kStepCount; ++i)
    {
        StepSlot& slot = pattern.steps[i];
        slot.repeat_count = ClampLedgerLength(fixed_len);
        RefreshSlotSummary(slot);
        UpdateLegacyStepTypeProjection(slot);
    }
}

static uint16_t LimitNoteMaskToMaxVoices(uint16_t note_mask, uint8_t max_voices)
{
    uint16_t limited = 0u;
    uint8_t count = 0u;

    for (uint8_t note = 0u; note < 12u; ++note)
    {
        const uint16_t bit = (uint16_t)(1u << note);
        if ((note_mask & bit) == 0u)
        {
            continue;
        }

        if (count < max_voices)
        {
            limited |= bit;
            ++count;
        }
    }

    return limited;
}

static uint8_t LedgerSlotToSortedNotes(const LedgerSlot& slot, uint8_t out_notes[4])
{
    uint8_t count = 0u;
    for (uint8_t i = 0u; i < slot.notes.size() && count < 4u; ++i)
    {
        const uint8_t note = slot.notes[i];
        if (note == kMidiNoteNone) continue;
        out_notes[count++] = note;
    }
    return count;
}

static bool ApplyTransposeToMidiNote(uint8_t note, int8_t transpose, uint8_t* out_note)
{
    if (!out_note) return false;
    if (!IsMidiNoteValid(note)) return false;

    const int16_t transposed = (int16_t)note + (int16_t)transpose;
    if (transposed < 0 || transposed > 127) return false;
    *out_note = (uint8_t)transposed;
    return true;
}

static LedgerSlot ApplyTransposeToLedgerSlot(const LedgerSlot& slot, int8_t transpose)
{
    LedgerSlot result{};
    LedgerSlotClear(result);

    for (uint8_t i = 0u; i < slot.notes.size(); ++i)
    {
        const uint8_t note = slot.notes[i];
        if (note == kMidiNoteNone) continue;

        uint8_t shifted = kMidiNoteNone;
        if (ApplyTransposeToMidiNote(note, transpose, &shifted))
        {
            (void)LedgerSlotAdd(result, shifted);
        }
    }

    return result;
}

/* ── Convenience accessors ──────────────────────────────────────────────── */

Pattern& SequencerDevice::CurrentPattern()
{
    return bank_.GetPattern(current_pattern_index_);
}

const Pattern& SequencerDevice::CurrentPattern() const
{
    return bank_.GetPattern(current_pattern_index_);
}

uint16_t SequencerDevice::ApplyTranspose(uint16_t note_mask) const
{
    return note_mask;
}

BarState SequencerDevice::LegacyStepTypeToBarState(StepType type)
{
    return (type == StepType::Skip) ? BarState::Skip : BarState::Active;
}

/* ── Init ────────────────────────────────────────────────────────────────── */
/*                                                                            */
/*  Sets up the pattern bank, MIDI clock, and builds two test patterns.      */
/*  Pattern 0 — 12 steps, forward playback, chord/rest/skip mix             */
/*  Pattern 1 — 6 steps, ping-pong, arp mode, chained after pattern 0       */
/*  Chain: pattern 0 → pattern 1 → loop                                     */
/*                                                                            */
void SequencerDevice::Init()
{
    bank_.Init();
    bank_.SetGlobalBpm(120);
    bank_.SetKey(KeyRoot::C, KeyScale::Major);
    bank_.GetSong().transpose = 0;

    /* MIDI clock — master mode */
    midi_clock_enabled_ = true;
    midi_tick_count_    = 0;
    midi_clock_.Init(midi::ClockMode::Master, this);
    midi_clock_.SetBpm(bank_.GetGlobalBpm());

    /* Transport state */
    playing_               = false;
    gate_active_           = false;
    current_pattern_index_ = 0;
    current_bar_           = 0;
    current_step_in_bar_   = 0;
    elapsed_step_ms_       = 0;
    gate_elapsed_ms_       = 0;
    gate_length_ms_        = 100;
    run_time_ms_           = 0;
    completed_loops_       = 0;

    /* ── Default pattern — 12 steps, all Chord, forward loop ───────── */
    Pattern& p0 = bank_.GetPattern(0);

    /* Playback mode — forward, loop forever, all 12 steps */
    p0.playback_mode    = PlaybackMode::Loop;
    p0.step_count       = 12;
    p0.repeat_count     = 1;
    p0.tempo_multiplier = 1.0f;
    p0.arp_mode         = ArpMode::Off;

    /* All steps are Rest (empty) — clean slate for user to build on */
    const uint8_t initial_subdivision_count =
        StepsPerBar(bank_.GetSong().time_sig, p0.step_division);
    for (uint32_t i = 0; i < kStepCount; ++i)
    {
        p0.steps[i].duration_multiplier = 1;
        p0.steps[i].repeat_count        = initial_subdivision_count;
        p0.steps[i].velocity            = 100;
        p0.steps[i].probability         = 100;
        p0.steps[i].bar_state           = BarState::Active;
        p0.steps[i].note_mask           = 0;
        UpdateLegacyStepTypeProjection(p0.steps[i]);
    }

    /* No chaining — single pattern loops forever */
    bank_.ChainClear();
    bank_.ChainAppend(0);

    /* Initialise engine ready state */
    step_changed_   = true;
    status_changed_ = true;
    gate_changed_   = false;

    RecalculateStepIntervalMs();
    RecalculateStepTicks();
    ApplyCurrentStepBehavior();
}

/* ── SetBpm — public BPM setter for UI layer ────────────────────────────── */
/*                                                                            */
/*  Updates the bank global BPM and recalculates the step interval.         */
/*  MIDI clock is updated if running in master mode.                         */
/*                                                                            */
void SequencerDevice::SetBpm(uint32_t bpm)
{
    if (bpm < 30)  bpm = 30;
    if (bpm > 300) bpm = 300;

    bank_.SetGlobalBpm(bpm);
    RecalculateStepIntervalMs();
    RecalculateStepTicks();

    if (midi_clock_enabled_ &&
        midi_clock_.GetMode() == midi::ClockMode::Master)
    {
        midi_clock_.SetBpm(bpm);
    }
}

void SequencerDevice::SetPatternStepCount(uint8_t step_count)
{
    if (step_count < 1) step_count = 1;
    if (step_count > kStepCount) step_count = kStepCount;
    CurrentPattern().step_count = step_count;
    if (current_bar_ >= step_count) current_bar_ = 0;
}

uint8_t SequencerDevice::GetPatternStepCount() const
{
    return CurrentPattern().step_count;
}

void SequencerDevice::SetPatternStepDivision(uint8_t step_division)
{
    const TimeSig& time_sig = bank_.GetSong().time_sig;
    (void)SetPatternTiming(step_division, time_sig.numerator, time_sig.denominator);
}

void SequencerDevice::SetPatternArpMode(sequencer::ArpMode mode)
{
    if (mode == sequencer::ArpMode::DownUp || mode == sequencer::ArpMode::AsPlayed)
    {
        mode = sequencer::ArpMode::Off;
    }

    CurrentPattern().arp_mode = mode;

    if (playing_ && current_bar_ < kStepCount)
    {
        ApplyCurrentStepBehavior();
        step_changed_ = true;
    }
}

void SequencerDevice::SetPatternArpRate(sequencer::ArpRate rate)
{
    if (rate > sequencer::ArpRate::ThirtySecond)
    {
        rate = sequencer::ArpRate::Sixteenth;
    }

    CurrentPattern().arp_rate = rate;
    arp_event_ticks_ = sequencer::ArpEngine::TicksPerEvent(rate);
    arp_ticks_accum_ = 0u;
}

bool SequencerDevice::SetPatternTiming(uint8_t step_division, uint8_t numerator, uint8_t denominator)
{
    const TimeSig proposed_time_sig{numerator, denominator};
    if (StepsPerBar(proposed_time_sig, step_division) == 0u) return false;

    if (IsCoarserDivisionChange(CurrentPattern().step_division, step_division))
    {
        if (!PatternCompatibleWithCoarserDivision(CurrentPattern(), step_division))
        {
            return false;
        }
    }

    Song& song = bank_.GetSong();
    for (uint8_t i = 0u; i < kPatternCount; ++i)
    {
        const uint8_t division = (i == current_pattern_index_)
            ? step_division
            : song.patterns[i].step_division;
        if (StepsPerBar(proposed_time_sig, division) == 0u) return false;
    }

    song.time_sig = proposed_time_sig;
    CurrentPattern().step_division = step_division;
    for (uint8_t i = 0u; i < kPatternCount; ++i)
    {
        NormalizePatternDivisionCadence(song.patterns[i], song.time_sig);
    }

    const uint8_t steps_in_bar = StepsPerBar(song.time_sig, CurrentPattern().step_division);
    if (current_step_in_bar_ >= steps_in_bar)
    {
        current_step_in_bar_ = (uint8_t)(steps_in_bar - 1u);
    }

    RecalculateStepIntervalMs();
    RecalculateStepTicks();
    return true;
}

uint8_t SequencerDevice::GetPatternStepDivision() const
{
    return CurrentPattern().step_division;
}

void SequencerDevice::SetTimeSignature(uint8_t numerator, uint8_t denominator)
{
    (void)SetPatternTiming(CurrentPattern().step_division, numerator, denominator);
}

uint8_t SequencerDevice::GetTimeSigNumerator() const
{
    return bank_.GetSong().time_sig.numerator;
}

uint8_t SequencerDevice::GetTimeSigDenominator() const
{
    return bank_.GetSong().time_sig.denominator;
}

void SequencerDevice::SetSwing(uint8_t swing)
{
    if (swing > 75) swing = 75;
    bank_.GetSong().swing = swing;
}

uint8_t SequencerDevice::GetSwing() const
{
    return bank_.GetSong().swing;
}

void SequencerDevice::SetStepChordParams(uint8_t step_index,
                                         uint8_t root_key,
                                         uint8_t chord_type,
                                         uint8_t arp_pattern,
                                         uint8_t duration,
                                         uint8_t repeat_count)
{
    if (step_index >= kStepCount) return;

    StepSlot& slot = CurrentPattern().steps[step_index];

    root_key %= 12;
    slot.duration_multiplier = UiDurationToMultiplier(duration);
    slot.repeat_count = StepsPerBar(bank_.GetSong().time_sig, CurrentPattern().step_division);
    (void)repeat_count;
    (void)arp_pattern;

    if (chord_type == 0)
    {
        for (uint8_t i = 0u; i < kStepLedgerMax; ++i)
        {
            LedgerSlotClear(slot.note_ledger[i]);
        }
        RefreshSlotSummary(slot);
        slot.custom_chord_name[0] = '\0';
    }
    else
    {
        const ChordType mapped = UiChordTypeToLibraryType(chord_type);
        slot.note_mask = LimitNoteMaskToMaxVoices(
            ChordLibrary::GetNoteMask(static_cast<KeyRoot>(root_key), mapped),
            4u);
        InitLedgerFromMask(slot, slot.note_mask, slot.repeat_count);
        RefreshSlotSummary(slot);
        slot.custom_chord_name[0] = '\0';
    }

    UpdateLegacyStepTypeProjection(slot);

    if (step_index == current_bar_)
    {
        ApplyCurrentStepBehavior();
        step_changed_ = true;
    }
}

void SequencerDevice::SetStepCustomNoteMask(uint8_t step_index, uint16_t note_mask)
{
    if (step_index >= kStepCount) return;

    StepSlot& slot = CurrentPattern().steps[step_index];
    const uint16_t clipped = LimitNoteMaskToMaxVoices((uint16_t)(note_mask & 0x0FFFu), 4u);
    slot.note_mask = clipped;
    slot.repeat_count = StepsPerBar(bank_.GetSong().time_sig, CurrentPattern().step_division);
    InitLedgerFromMask(slot, clipped, slot.repeat_count);
    RefreshSlotSummary(slot);
    UpdateLegacyStepTypeProjection(slot);
    slot.custom_chord_name[0] = '\0';
    if (step_index == current_bar_)
    {
        ApplyCurrentStepBehavior();
        step_changed_ = true;
    }
}

void SequencerDevice::SetStepCustomUserChord(uint8_t step_index, uint16_t note_mask, const char* name)
{
    if (step_index >= kStepCount) return;

    StepSlot& slot = CurrentPattern().steps[step_index];
    const uint16_t clipped = LimitNoteMaskToMaxVoices((uint16_t)(note_mask & 0x0FFFu), 4u);
    slot.note_mask = clipped;
    slot.repeat_count = StepsPerBar(bank_.GetSong().time_sig, CurrentPattern().step_division);
    InitLedgerFromMask(slot, clipped, slot.repeat_count);
    RefreshSlotSummary(slot);
    UpdateLegacyStepTypeProjection(slot);
    if (name && name[0] != '\0')
    {
        strncpy(slot.custom_chord_name, name, sizeof(slot.custom_chord_name));
        slot.custom_chord_name[sizeof(slot.custom_chord_name) - 1] = '\0';
    }
    else
    {
        slot.custom_chord_name[0] = '\0';
    }

    if (step_index == current_bar_)
    {
        ApplyCurrentStepBehavior();
        step_changed_ = true;
    }
}

void SequencerDevice::SetPatternRepeatCount(uint8_t repeat_count)
{
    if (repeat_count < 1) repeat_count = 1;
    if (repeat_count > 16) repeat_count = 16;
    CurrentPattern().repeat_count = repeat_count;
}

uint8_t SequencerDevice::GetPatternRepeatCount() const
{
    return CurrentPattern().repeat_count;
}

void SequencerDevice::SetCurrentPatternIndex(uint8_t pattern_index)
{
    if (pattern_index >= kPatternCount) return;

    /* Quick-load behavior: lock chain to selected pattern for now. */
    bank_.ChainClear();
    bank_.ChainAppend(pattern_index);
    bank_.ChainReset();

    current_pattern_index_ = pattern_index;
    NormalizePatternDivisionCadence(CurrentPattern(), bank_.GetSong().time_sig);
    current_bar_ = 0;
    current_step_in_bar_ = 0;
    repeat_current_ = 0;
    step_direction_ = 1;
    elapsed_step_ms_ = 0;

    ApplyCurrentStepBehavior();
    step_changed_ = true;
    status_changed_ = true;
}

void SequencerDevice::SetChainLength(uint8_t length)
{
    if (length < 1) length = 1;
    if (length > kChainLength) length = kChainLength;

    PatternChain& chain = bank_.GetSong().chain;
    uint8_t old_length = chain.length;
    bank_.ChainSetLength(length);

    if (old_length < length)
    {
        uint8_t fill = (old_length > 0) ? chain.pattern_indices[old_length - 1] : 0;
        if (fill >= kPatternCount) fill = 0;
        for (uint8_t i = old_length; i < length; ++i)
        {
            chain.pattern_indices[i] = fill;
        }
    }

    if (chain.position >= chain.length)
    {
        chain.position = 0;
    }
}

uint8_t SequencerDevice::GetChainLength() const
{
    uint8_t len = bank_.ChainGetLength();
    return (len == 0) ? 1 : len;
}

void SequencerDevice::SetChainPatternAt(uint8_t pos, uint8_t pattern_index)
{
    PatternChain& chain = bank_.GetSong().chain;
    if (pattern_index >= kPatternCount) return;
    if (pos >= chain.length) return;
    chain.pattern_indices[pos] = pattern_index;
}

uint8_t SequencerDevice::GetChainPatternAt(uint8_t pos) const
{
    const PatternChain& chain = bank_.GetSong().chain;
    if (chain.length == 0) return 0;
    if (pos >= chain.length) return 0;
    return chain.pattern_indices[pos];
}

uint8_t SequencerDevice::GetChainCurrentPosition() const
{
    return bank_.ChainCurrentPosition();
}

uint8_t SequencerDevice::GetCurrentPatternRepeatProgress() const
{
    return repeat_current_;
}

uint8_t SequencerDevice::GetCurrentStepSubIndex() const
{
    return current_step_in_bar_;
}

void SequencerDevice::SetStepLedgerLength(uint8_t step_index, uint8_t length)
{
    if (step_index >= kStepCount) return;

    StepSlot& slot = CurrentPattern().steps[step_index];
    const uint8_t fixed_len = StepsPerBar(bank_.GetSong().time_sig, CurrentPattern().step_division);

    /* The current grid division defines the runtime cadence for the whole step.
     * Do not let the active note count shorten or lengthen an otherwise identical step. */
    slot.repeat_count = fixed_len;
    RefreshSlotSummary(slot);
    UpdateLegacyStepTypeProjection(slot);
    (void)length;

    if (step_index == current_bar_)
    {
        ApplyCurrentStepBehavior();
        step_changed_ = true;
    }
}

void SequencerDevice::SetStepLedgerSlot(uint8_t step_index, uint8_t slot_index, const LedgerSlot& slot_notes)
{
    if (step_index >= kStepCount) return;

    StepSlot& slot = CurrentPattern().steps[step_index];
    const uint8_t fixed_len = StepsPerBar(bank_.GetSong().time_sig, CurrentPattern().step_division);
    if (slot_index >= fixed_len) return;
    const uint8_t ledger_index = GridPositionToLedgerIndex(CurrentPattern().step_division, slot_index);
    if (ledger_index >= kStepLedgerMax) return;
    const uint8_t canonical_positions = CanonicalPositionsPerBar(bank_.GetSong().time_sig);
    const uint8_t was_event_start = IsCanonicalEventStart(slot, ledger_index);

    LedgerSlot normalized{};
    LedgerSlotClear(normalized);
    for (uint8_t i = 0u; i < slot_notes.notes.size(); ++i)
    {
        const uint8_t note = slot_notes.notes[i];
        if (!IsMidiNoteValid(note)) continue;
        if (LedgerSlotCount(normalized) >= 4u) break;
        (void)LedgerSlotAdd(normalized, note);
    }

    slot.note_ledger[ledger_index] = normalized;
    const uint8_t is_event_start = IsCanonicalEventStart(slot, ledger_index);

    if (!was_event_start && is_event_start)
    {
        for (int16_t prev = (int16_t)ledger_index - 1; prev >= 0; --prev)
        {
            const uint8_t prev_start = (uint8_t)prev;
            if (!IsCanonicalEventStart(slot, prev_start)) continue;

            const uint8_t prev_len = EventLengthCanonicalForStart(slot, prev_start, canonical_positions);
            const uint8_t prev_end = (uint8_t)(prev_start + prev_len);
            if (prev_end > ledger_index)
            {
                PackedEventLengthRawSet(slot, prev_start, (uint8_t)((ledger_index - prev_start) - 1u));
            }
            break;
        }

        PackedEventLengthRawSet(slot,
                                ledger_index,
                                (uint8_t)(CanonicalStrideForDivision(CurrentPattern().step_division) - 1u));
    }

    slot.repeat_count = fixed_len;
    RefreshSlotSummary(slot);
    UpdateLegacyStepTypeProjection(slot);
    slot.custom_chord_name[0] = '\0';
    if (step_index == current_bar_)
    {
        ApplyCurrentStepBehavior();
        step_changed_ = true;
    }
}

uint8_t SequencerDevice::GetStepLedgerLength(uint8_t step_index) const
{
    if (step_index >= kStepCount) return 0u;
    return StepsPerBar(bank_.GetSong().time_sig, CurrentPattern().step_division);
}

LedgerSlot SequencerDevice::GetStepLedgerSlot(uint8_t step_index, uint8_t slot_index) const
{
    LedgerSlot empty{};
    LedgerSlotClear(empty);
    if (step_index >= kStepCount) return empty;
    const uint8_t steps_in_bar = StepsPerBar(bank_.GetSong().time_sig, CurrentPattern().step_division);
    if (slot_index >= steps_in_bar) return empty;
    const uint8_t ledger_index = GridPositionToLedgerIndex(CurrentPattern().step_division, slot_index);
    if (ledger_index >= kStepLedgerMax) return empty;
    return CurrentPattern().steps[step_index].note_ledger[ledger_index];
}

uint8_t SequencerDevice::IsStepEventStartCanonical(uint8_t step_index, uint8_t canonical_index) const
{
    if (step_index >= kStepCount) return 0u;
    const uint8_t canonical_positions = CanonicalPositionsPerBar(bank_.GetSong().time_sig);
    if (canonical_index >= canonical_positions) return 0u;
    return IsCanonicalEventStart(CurrentPattern().steps[step_index], canonical_index);
}

uint8_t SequencerDevice::GetStepEventMaxLengthCanonical(uint8_t step_index, uint8_t canonical_index) const
{
    if (step_index >= kStepCount) return 0u;
    const StepSlot& slot = CurrentPattern().steps[step_index];
    const uint8_t canonical_positions = CanonicalPositionsPerBar(bank_.GetSong().time_sig);
    if (canonical_index >= canonical_positions) return 0u;

    const uint8_t next_start = FindNextEventStartCanonical(slot, canonical_index, canonical_positions);
    if (next_start > canonical_index)
    {
        return (uint8_t)(next_start - canonical_index);
    }
    return 1u;
}

uint8_t SequencerDevice::GetStepEventLengthCanonical(uint8_t step_index, uint8_t canonical_index) const
{
    if (!IsStepEventStartCanonical(step_index, canonical_index)) return 0u;

    const StepSlot& slot = CurrentPattern().steps[step_index];
    const uint8_t canonical_positions = CanonicalPositionsPerBar(bank_.GetSong().time_sig);
    return EventLengthCanonicalForStart(slot, canonical_index, canonical_positions);
}

void SequencerDevice::SetStepEventLengthCanonical(uint8_t step_index,
                                                  uint8_t canonical_index,
                                                  uint8_t length_positions)
{
    if (step_index >= kStepCount) return;
    if (!IsStepEventStartCanonical(step_index, canonical_index)) return;

    uint8_t max_len = GetStepEventMaxLengthCanonical(step_index, canonical_index);
    if (max_len < 1u) max_len = 1u;
    if (length_positions < 1u) length_positions = 1u;
    if (length_positions > max_len) length_positions = max_len;

    StepSlot& slot = CurrentPattern().steps[step_index];
    PackedEventLengthRawSet(slot, canonical_index, (uint8_t)(length_positions - 1u));
}

uint8_t SequencerDevice::IsStepEventStartGrid(uint8_t step_index, uint8_t slot_index) const
{
    if (step_index >= kStepCount) return 0u;
    const uint8_t division = CurrentPattern().step_division;
    const uint8_t steps_in_bar = StepsPerBar(bank_.GetSong().time_sig, division);
    if (steps_in_bar == 0u || slot_index >= steps_in_bar) return 0u;

    const uint8_t canonical_index = GridPositionToLedgerIndex(division, slot_index);
    return IsStepEventStartCanonical(step_index, canonical_index);
}

uint8_t SequencerDevice::GetStepEventMaxLengthGrid(uint8_t step_index, uint8_t slot_index) const
{
    if (step_index >= kStepCount) return 0u;
    const uint8_t division = CurrentPattern().step_division;
    const uint8_t steps_in_bar = StepsPerBar(bank_.GetSong().time_sig, division);
    if (steps_in_bar == 0u || slot_index >= steps_in_bar) return 0u;

    const uint8_t stride = CanonicalStrideForDivision(division);
    const uint8_t canonical_index = GridPositionToLedgerIndex(division, slot_index);
    const uint8_t max_len_canonical = GetStepEventMaxLengthCanonical(step_index, canonical_index);
    uint8_t max_len_grid = (uint8_t)(max_len_canonical / stride);
    if (max_len_grid < 1u) max_len_grid = 1u;
    return max_len_grid;
}

uint8_t SequencerDevice::GetStepEventLengthGrid(uint8_t step_index, uint8_t slot_index) const
{
    if (step_index >= kStepCount) return 0u;
    const uint8_t division = CurrentPattern().step_division;
    const uint8_t steps_in_bar = StepsPerBar(bank_.GetSong().time_sig, division);
    if (steps_in_bar == 0u || slot_index >= steps_in_bar) return 0u;

    const uint8_t stride = CanonicalStrideForDivision(division);
    const uint8_t canonical_index = GridPositionToLedgerIndex(division, slot_index);
    const uint8_t len_canonical = GetStepEventLengthCanonical(step_index, canonical_index);
    if (len_canonical == 0u) return 0u;

    uint8_t len_grid = (uint8_t)(len_canonical / stride);
    if (len_grid < 1u) len_grid = 1u;
    return len_grid;
}

void SequencerDevice::SetStepEventLengthGrid(uint8_t step_index, uint8_t slot_index, uint8_t grid_length)
{
    if (step_index >= kStepCount) return;
    const uint8_t division = CurrentPattern().step_division;
    const uint8_t steps_in_bar = StepsPerBar(bank_.GetSong().time_sig, division);
    if (steps_in_bar == 0u || slot_index >= steps_in_bar) return;

    const uint8_t stride = CanonicalStrideForDivision(division);
    const uint8_t canonical_index = GridPositionToLedgerIndex(division, slot_index);
    if (!IsStepEventStartCanonical(step_index, canonical_index)) return;

    uint8_t max_len_grid = GetStepEventMaxLengthGrid(step_index, slot_index);
    if (max_len_grid < 1u) max_len_grid = 1u;
    if (grid_length < 1u) grid_length = 1u;
    if (grid_length > max_len_grid) grid_length = max_len_grid;

    const uint8_t len_canonical = (uint8_t)(grid_length * stride);
    SetStepEventLengthCanonical(step_index, canonical_index, len_canonical);
}

void SequencerDevice::ClearStepLedger(uint8_t step_index)
{
    if (step_index >= kStepCount) return;

    StepSlot& slot = CurrentPattern().steps[step_index];
    for (uint8_t i = 0u; i < kStepLedgerMax; ++i)
    {
        LedgerSlotClear(slot.note_ledger[i]);
    }
    slot.repeat_count = StepsPerBar(bank_.GetSong().time_sig, CurrentPattern().step_division);
    RefreshSlotSummary(slot);
    UpdateLegacyStepTypeProjection(slot);
    slot.custom_chord_name[0] = '\0';
    if (step_index == current_bar_)
    {
        ApplyCurrentStepBehavior();
        step_changed_ = true;
    }
}

uint16_t SequencerDevice::GetStepNoteMask(uint8_t step_index) const
{
    if (step_index >= kStepCount) return 0;
    return CurrentPattern().steps[step_index].note_mask;
}

static bool HasLedgerData(const StepSlot& slot)
{
    for (uint8_t i = 0u; i < kStepLedgerMax; ++i)
    {
        if (!LedgerSlotIsEmpty(slot.note_ledger[i])) return true;
    }
    return false;
}

static uint16_t ResolveLedgerMaskForIndex(const StepSlot& slot, uint8_t ledger_index)
{
    if (!HasLedgerData(slot)) return slot.note_mask;
    /* Ledger is populated: zero means intentional rest for that slot. */
    return LedgerSlotToPitchClassMask(slot.note_ledger[ledger_index]);
}

uint16_t SequencerDevice::GetStepNoteMaskForPlayback(uint8_t step_index) const
{
    if (step_index >= kStepCount) return 0;
    const StepSlot& slot = CurrentPattern().steps[step_index];
    const uint8_t ledger_index = GridPositionToLedgerIndex(CurrentPattern().step_division, 0u);
    if (!HasLedgerData(slot))
    {
        return ResolveLedgerMaskForIndex(slot, ledger_index);
    }

    const LedgerSlot transposed = ApplyTransposeToLedgerSlot(slot.note_ledger[ledger_index], bank_.GetSong().transpose);
    return LedgerSlotToPitchClassMask(transposed);
}

uint16_t SequencerDevice::GetCurrentStepNoteMaskForPlayback() const
{
    uint8_t notes[4] = {kMidiNoteNone, kMidiNoteNone, kMidiNoteNone, kMidiNoteNone};
    const uint8_t count = GetCurrentStepNotesForPlayback(notes);
    uint16_t mask = 0u;
    for (uint8_t i = 0u; i < count; ++i)
    {
        if (notes[i] == kMidiNoteNone) continue;
        mask |= (uint16_t)(1u << (notes[i] % 12u));
    }
    return mask;
}

uint8_t SequencerDevice::GetStepNotesForPlayback(uint8_t step_index, uint8_t slot_index, uint8_t out_notes[4]) const
{
    if (!out_notes) return 0u;
    for (uint8_t i = 0u; i < 4u; ++i) out_notes[i] = kMidiNoteNone;

    if (step_index >= kStepCount) return 0u;

    const StepSlot& slot = CurrentPattern().steps[step_index];
    const uint8_t steps_in_bar = StepsPerBar(bank_.GetSong().time_sig, CurrentPattern().step_division);
    if (steps_in_bar == 0u || slot_index >= steps_in_bar) return 0u;
    const uint8_t ledger_index = GridPositionToLedgerIndex(CurrentPattern().step_division, slot_index);
    if (ledger_index >= kStepLedgerMax) return 0u;

    LedgerSlot resolved{};
    LedgerSlotClear(resolved);

    if (HasLedgerData(slot))
    {
        resolved = slot.note_ledger[ledger_index];
    }
    else
    {
        constexpr uint8_t kDefaultMidiBase = (uint8_t)kLegacyPitchClassFallbackMidiBase;
        for (uint8_t note = 0u; note < 12u; ++note)
        {
            if ((slot.note_mask & (uint16_t)(1u << note)) != 0u)
            {
                (void)LedgerSlotAdd(resolved, (uint8_t)(kDefaultMidiBase + note));
            }
        }
    }

    const LedgerSlot transposed = ApplyTransposeToLedgerSlot(resolved, bank_.GetSong().transpose);
    return LedgerSlotToSortedNotes(transposed, out_notes);
}

uint8_t SequencerDevice::ResolveEventNotesForPlayback(uint8_t step_index,
                                                      uint8_t slot_index,
                                                      uint8_t out_notes[4]) const
{
    if (!out_notes) return 0u;
    for (uint8_t i = 0u; i < 4u; ++i) out_notes[i] = kMidiNoteNone;

    if (step_index >= kStepCount) return 0u;

    const StepSlot& slot = CurrentPattern().steps[step_index];
    const uint8_t division = CurrentPattern().step_division;
    const uint8_t steps_in_bar = StepsPerBar(bank_.GetSong().time_sig, division);
    if (steps_in_bar == 0u || slot_index >= steps_in_bar) return 0u;

    const uint8_t canonical_positions = CanonicalPositionsPerBar(bank_.GetSong().time_sig);
    const uint8_t canonical_pos = GridPositionToLedgerIndex(division, slot_index);
    if (canonical_pos >= canonical_positions) return 0u;

    uint8_t event_start = 0u;
    if (!ResolveEventStartCanonicalForPosition(slot, canonical_pos, canonical_positions, &event_start))
    {
        return 0u;
    }

    const uint8_t stride = CanonicalStrideForDivision(division);
    if ((event_start % stride) != 0u)
    {
        return 0u;
    }

    const uint8_t start_slot = (uint8_t)(event_start / stride);
    return GetStepNotesForPlayback(step_index, start_slot, out_notes);
}

uint8_t SequencerDevice::GetCurrentStepNotesForPlayback(uint8_t out_notes[4]) const
{
    if (current_bar_ >= kStepCount || !out_notes) return 0u;

    const uint8_t steps_in_bar = StepsPerBar(bank_.GetSong().time_sig, CurrentPattern().step_division);
    if (steps_in_bar == 0u) return 0u;

    const uint8_t grid_position = (current_step_in_bar_ < steps_in_bar) ?
                                  current_step_in_bar_ :
                                  (uint8_t)(steps_in_bar - 1u);
    return ResolveEventNotesForPlayback((uint8_t)current_bar_, grid_position, out_notes);
}

void SequencerDevice::ExportSong(sequencer::Song* out_song) const
{
    if (!out_song) return;
    *out_song = bank_.GetSong();
}

const sequencer::Song& SequencerDevice::GetSongView() const
{
    return bank_.GetSong();
}

sequencer::Song* SequencerDevice::GetMutableSongForImport()
{
    return &bank_.GetSong();
}

void SequencerDevice::FinalizeImportedSong()
{
    if (bank_.GetSong().chain.length == 0)
    {
        bank_.ChainClear();
        bank_.ChainAppend(0);
    }

    bank_.ChainReset();
    current_pattern_index_ = bank_.ChainCurrentPatternIndex();
    if (current_pattern_index_ >= kPatternCount)
    {
        current_pattern_index_ = 0;
    }

    Song& loaded_song = bank_.GetSong();
    if (StepsPerBar(loaded_song.time_sig, 4u) == 0u)
    {
        loaded_song.time_sig = TimeSig{};
    }
    for (uint8_t i = 0u; i < kPatternCount; ++i)
    {
        NormalizePatternDivisionCadence(loaded_song.patterns[i], loaded_song.time_sig);
    }
    current_bar_ = 0;
    current_step_in_bar_ = 0;
    repeat_current_ = 0;
    step_direction_ = 1;
    elapsed_step_ms_ = 0;

    RecalculateStepIntervalMs();
    ApplyCurrentStepBehavior();
    step_changed_ = true;
    status_changed_ = true;
}

void SequencerDevice::ImportSong(const sequencer::Song& song)
{
    bank_.LoadSong(song);
    FinalizeImportedSong();
}

bool SequencerDevice::GetStepChordUiParams(uint8_t step_index,
                                           uint8_t* root_key,
                                           uint8_t* chord_type,
                                           uint8_t* duration,
                                           uint8_t* repeat_count) const
{
    if (step_index >= kStepCount || !root_key || !chord_type || !duration || !repeat_count)
    {
        return false;
    }

    const StepSlot& slot = CurrentPattern().steps[step_index];

    *duration = UiMultiplierToDuration(slot.duration_multiplier);
    *repeat_count = slot.repeat_count;
    if (*repeat_count < 1) *repeat_count = 1;
    if (*repeat_count > 16) *repeat_count = 16;

    if (slot.note_mask == 0 || slot.type != StepType::Chord)
    {
        *root_key = 0;
        *chord_type = 0;
        return true;
    }

    for (uint8_t r = 0; r < 12; ++r)
    {
        for (uint8_t ui_type = 1; ui_type <= 16; ++ui_type)
        {
            const ChordType mapped = UiChordTypeToLibraryType(ui_type);
            const uint16_t mask = ChordLibrary::GetNoteMask(static_cast<KeyRoot>(r), mapped);
            if (mask == slot.note_mask)
            {
                *root_key = r;
                *chord_type = ui_type;
                return true;
            }
        }
    }

    *root_key = 0;
    *chord_type = 1;
    return true;
}

bool SequencerDevice::GetStepCustomChordName(uint8_t step_index, char* buf, size_t buf_len) const
{
    if (step_index >= kStepCount || !buf || buf_len == 0)
    {
        return false;
    }

    const StepSlot& slot = CurrentPattern().steps[step_index];
    if (slot.custom_chord_name[0] == '\0')
    {
        return false;
    }

    strncpy(buf, slot.custom_chord_name, buf_len);
    buf[buf_len - 1] = '\0';
    return true;
}

ArpMode SequencerDevice::GetCurrentArpMode() const
{
    return CurrentPattern().arp_mode;
}

sequencer::ArpRate SequencerDevice::GetCurrentArpRate() const
{
    return CurrentPattern().arp_rate;
}

/* ── Tick1ms — called from SysTick ISR every 1ms ───────────────────────── */
/*                                                                            */
/*  Drives gate timing, MIDI clock, and legacy ms step advancement.          */
/*  Keep this lean — it runs inside an interrupt context on STM32.          */
/*                                                                            */
void SequencerDevice::Tick1ms()
{
    if (playing_)
    {
        ++run_time_ms_;
    }

    /* Gate timing — turn gate off after gate_length_ms_ */
    if (gate_active_)
    {
        ++gate_elapsed_ms_;
        if (gate_elapsed_ms_ >= gate_length_ms_)
            GateOff();
    }

    if (gate_retrigger_pending_)
    {
        if (gate_retrigger_delay_ms_ > 0u)
            --gate_retrigger_delay_ms_;

        if (gate_retrigger_delay_ms_ == 0u)
        {
            gate_retrigger_pending_ = false;
            GateOn();
        }
    }

    /* MIDI clock tick */
    if (midi_clock_enabled_)
        midi_clock_.Tick1ms();


    if (!playing_) return;

#if defined(S12_USE_UCLOCK_MUSICAL_STEP_CLOCK) && S12_USE_UCLOCK_MUSICAL_STEP_CLOCK
    /* uClock musical step clock takes over step advancement.
     * Legacy millisecond step advancement remains available when disabled. */
#else
    /* Step advancement */
    ++elapsed_step_ms_;

    if (elapsed_step_ms_ >= current_step_interval_ms_)
    {
        elapsed_step_ms_ = 0;
        AdvanceStep();
    }
#endif
}

void SequencerDevice::TickMusical()
{
    ++tick_musical_count_;

    if (!playing_)
    {
        return;
    }

    if (arp_.HasNotes() && CurrentPattern().arp_mode != sequencer::ArpMode::Off && arp_event_ticks_ > 0u)
    {
        ++arp_ticks_accum_;

        while (arp_ticks_accum_ >= arp_event_ticks_)
        {
            arp_ticks_accum_ -= arp_event_ticks_;
            arp_.Advance();
            arp_note_changed_ = true;
            RetriggerGate(1u);   /* force a real gate edge on each arp event */
        }
    }

    ++musical_ticks_accum_;

    while (musical_ticks_accum_ >= musical_step_ticks_)
    {
        musical_ticks_accum_ -= musical_step_ticks_;
        ++pending_step_events_;
        ++pending_step_enqueue_count_;
    }
}

void SequencerDevice::NotifyUClockMusicalCallback()
{
    ++uclock_musical_callback_count_;
    TickMusical();
}

bool SequencerDevice::ServiceOnePendingStep()
{
    uint32_t count = 0u;

    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    count = pending_step_events_;
    if (count == 0u)
    {
        __set_PRIMASK(primask);
        return false;
    }

    if (count > max_pending_step_events_)
    {
        max_pending_step_events_ = count;
    }

    if (count > 1u)
    {
        ++pending_step_backlog_count_;
    }

    pending_step_events_ = count - 1u;
    __set_PRIMASK(primask);

    ++service_one_step_count_;
    AdvanceStep();
    return true;
}

void SequencerDevice::DrainPendingStepEvents()
{
    (void)ServiceOnePendingStep();
}

void SequencerDevice::Tick5ms()  {}
void SequencerDevice::Tick10ms() {}
void SequencerDevice::Tick20ms() {}

/* ── Process — called from main loop ───────────────────────────────────── */
/*                                                                            */
/*  Handles dirty flags set by Tick1ms(). On STM32 this will drive MIDI     */
/*  output and UI updates. Currently a stub — expand as needed.             */
/*                                                                            */
void SequencerDevice::Process()
{
    /* Musical step events are serviced at the front of the main loop.
     * This function now handles only the dirty-flag work for the currently
     * resolved step snapshot and does not drain a backlog of pending events. */

    /* step_changed_ — fire MIDI notes, update UI step highlight */
    if (step_changed_)
    {
        step_changed_ = false;
        /* TODO: send MIDI note on/off for current step */
        /* TODO: notify UI layer of new step position   */
    }

    /* status_changed_ — transport state changed (play/stop) */
    if (status_changed_)
    {
        status_changed_ = false;
        /* TODO: update UI transport display */
    }

    /* gate_changed_ — gate went high or low */
    if (gate_changed_)
    {
        gate_changed_ = false;
        /* TODO: drive hardware gate output pin */
    }

    /* arp_note_changed_ — arp advanced to next note */
    if (arp_note_changed_)
    {
        arp_note_changed_ = false;
        /* TODO: send MIDI note for current arp note */
    }
}

/* ── Transport ───────────────────────────────────────────────────────────── */

void SequencerDevice::Start()
{
    if (playing_) return;
    playing_         = true;
    elapsed_step_ms_ = 0;
    musical_ticks_accum_ = 0u;
    pending_step_events_ = 0u;
    max_pending_step_events_ = 0u;
    pending_step_backlog_count_ = 0u;
    current_step_in_bar_ = 0;
    gate_retrigger_pending_  = false;
    gate_retrigger_delay_ms_ = 0u;
    ApplyCurrentStepBehavior();
    step_changed_   = true;
    status_changed_  = true;

    if (midi_clock_enabled_ &&
        midi_clock_.GetMode() == midi::ClockMode::Master)
    {
        midi_clock_.Start();
    }
}

void SequencerDevice::Stop()
{
    if (!playing_) return;
    playing_         = false;
    elapsed_step_ms_ = 0;
    gate_retrigger_pending_  = false;
    gate_retrigger_delay_ms_ = 0u;
    GateOff();
    status_changed_  = true;

    if (midi_clock_enabled_ &&
        midi_clock_.GetMode() == midi::ClockMode::Master)
    {
        midi_clock_.Stop();
    }
}

void SequencerDevice::Reset()
{
    current_bar_           = 0;
    current_step_in_bar_   = 0;
    elapsed_step_ms_       = 0;
    musical_ticks_accum_   = 0u;
    pending_step_events_   = 0u;
    max_pending_step_events_ = 0u;
    pending_step_backlog_count_ = 0u;
    repeat_current_        = 0;
    step_direction_        = 1;
    arp_ticks_accum_       = 0;
    arp_event_ticks_       = 0;
    run_time_ms_           = 0;
    completed_loops_       = 0;
    arp_.Init();
    bank_.ChainReset();
    current_pattern_index_ = bank_.ChainCurrentPatternIndex();
    gate_retrigger_pending_  = false;
    gate_retrigger_delay_ms_ = 0u;
    GateOff();
    ApplyCurrentStepBehavior();
    step_changed_   = true;
    status_changed_ = true;
}

/* ── Gate ────────────────────────────────────────────────────────────────── */

void SequencerDevice::GateOn()
{
    gate_active_     = true;
    gate_elapsed_ms_ = 0;
    gate_changed_    = true;
}

void SequencerDevice::GateOff()
{
    if (!gate_active_) return;
    gate_active_     = false;
    gate_elapsed_ms_ = 0;
    gate_changed_    = true;
}

void SequencerDevice::RetriggerGate(uint32_t low_gap_ms)
{
    if (low_gap_ms == 0u)
    {
        gate_retrigger_pending_  = false;
        gate_retrigger_delay_ms_ = 0u;
        GateOn();
        return;
    }

    if (gate_active_)
    {
        GateOff();
        gate_retrigger_pending_  = true;
        gate_retrigger_delay_ms_ = low_gap_ms;
    }
    else
    {
        GateOn();
    }
}

bool SequencerDevice::ConsumeCvEvent(uint8_t* note, bool* gate)
{
    bool dirty = step_changed_ || arp_note_changed_ || gate_changed_;
    if (!dirty) return false;
    *note = arp_.HasNotes() ? arp_.CurrentNote() : 0u;
    *gate = gate_active_;
    return true;
}

/* ── Step engine ─────────────────────────────────────────────────────────── */
/*                                                                            */
/*  RecalculateStepIntervalMs — converts BPM to ms per step, applying       */
/*  the current pattern's tempo multiplier and step duration multiplier.    */
/*  A sequencer step is treated as a sixteenth note by default.             */
/*                                                                            */
void SequencerDevice::RecalculateStepIntervalMs()
{
    uint32_t bpm = bank_.GetEffectiveBpm(current_pattern_index_);
    if (bpm == 0) bpm = 1;

    uint8_t division = CurrentPattern().step_division;
    if (division == 0) division = 4;

    /* Quarter-note ms divided by pattern step division */
    base_step_interval_ms_ = (60000 / bpm) / division;

    current_step_interval_ms_ = base_step_interval_ms_;

    /* Ledger slots remain internal note selections within the same step.
     * They do not reduce the full step interval. */

    /* Preserve the existing absolute gate lengths while transport cadence
     * remains fixed to the grid subdivision. */
    gate_length_ms_ =
        (base_step_interval_ms_ *
         CurrentPattern().steps[current_bar_].duration_multiplier) / 4u;
    if (gate_length_ms_ < 5u) gate_length_ms_ = 5u;
}

void SequencerDevice::RecalculateStepTicks()
{
    uint8_t division = CurrentPattern().step_division;
    if (division == 0u) division = 4u;

    uint32_t base_ticks = 96u / division;
    musical_step_ticks_ = base_ticks;
    if (musical_step_ticks_ == 0u) musical_step_ticks_ = 1u;
}

/*.......................................................... */
/* Time/Pattern Step/Position Display                      */

uint8_t SequencerDevice::GetCurrentPatternIndex() const
{
    return current_pattern_index_;
}

uint32_t SequencerDevice::GetPendingStepEvents() const
{
    return pending_step_events_;
}

uint32_t SequencerDevice::GetMaxPendingStepEvents() const
{
    return max_pending_step_events_;
}

uint32_t SequencerDevice::GetPendingStepBacklogCount() const
{
    return pending_step_backlog_count_;
}

uint32_t SequencerDevice::GetUClockMusicalCallbackCount() const
{
    return uclock_musical_callback_count_;
}

uint32_t SequencerDevice::GetTickMusicalCount() const
{
    return tick_musical_count_;
}

uint32_t SequencerDevice::GetPendingStepEnqueueCount() const
{
    return pending_step_enqueue_count_;
}

uint32_t SequencerDevice::GetServiceOneStepCount() const
{
    return service_one_step_count_;
}

uint32_t SequencerDevice::GetRunTimeMs() const
{
    return run_time_ms_;
}

uint32_t SequencerDevice::GetCompletedLoops() const
{
    return completed_loops_;
}

/*                                                                            */
/*  AdvanceStep — moves to the next step based on playback mode.            */
/*  Handles Forward, PingPong, OneShot, Loop modes.                         */
/*  On pattern completion, advances the chain to the next pattern.          */
/*  Skips any steps marked StepType::Skip.                                  */
/*                                                                            */
void SequencerDevice::AdvanceStep()
{
    auto clamp_step_count = [](const Pattern& pattern) -> uint8_t
    {
        uint8_t count = pattern.step_count;
        if (count == 0u || count > kStepCount) count = kStepCount;
        return count;
    };

    auto apply_pattern_completion = [&](const Pattern& completed_pattern) -> bool
    {
        ++completed_loops_;

        switch (completed_pattern.playback_mode)
        {
            case PlaybackMode::OneShot:
                Stop();
                return false;

            case PlaybackMode::Loop:
            {
                ++repeat_current_;
                if (repeat_current_ >= completed_pattern.repeat_count)
                {
                    repeat_current_ = 0;
                    bank_.ChainAdvance();
                    current_pattern_index_ = bank_.ChainCurrentPatternIndex();
                    status_changed_ = true;
                }
                return true;
            }

            case PlaybackMode::Forward:
            case PlaybackMode::PingPong:
            {
                ++repeat_current_;
                if (repeat_current_ >= completed_pattern.repeat_count)
                {
                    repeat_current_ = 0;
                    step_direction_ = 1;
                    bank_.ChainAdvance();
                    current_pattern_index_ = bank_.ChainCurrentPatternIndex();
                    status_changed_ = true;
                }
                return true;
            }
        }

        return true;
    };

    auto stop_for_all_skip_chain = [&]()
    {
        Stop();
        gate_retrigger_pending_  = false;
        gate_retrigger_delay_ms_ = 0u;
        arp_.Init();
        arp_ticks_accum_  = 0;
        arp_event_ticks_  = 0;
        arp_note_changed_ = false;
    };

    const uint8_t chain_len = (bank_.ChainGetLength() == 0u) ? 1u : bank_.ChainGetLength();
    const uint32_t max_pattern_exhaustions = (uint32_t)chain_len * 32u;
    uint32_t pattern_exhaustions = 0u;

    while (true)
    {
        const Pattern& pat = CurrentPattern();
        const uint8_t step_count = clamp_step_count(pat);
        if (current_bar_ >= step_count) current_bar_ = 0u;

        const uint8_t steps_in_bar = StepsPerBar(bank_.GetSong().time_sig, pat.step_division);
        const bool current_bar_skipped = (pat.steps[current_bar_].bar_state == BarState::Skip);

        if (!current_bar_skipped && (current_step_in_bar_ + 1u) < steps_in_bar)
        {
            ++current_step_in_bar_;
            ApplyCurrentStepBehavior();
            step_changed_ = true;
            return;
        }

        current_step_in_bar_ = 0;

        bool pattern_complete = false;

        switch (pat.playback_mode)
        {
            case PlaybackMode::Forward:
            case PlaybackMode::OneShot:
            {
                uint32_t next = current_bar_ + 1u;
                if (next >= step_count)
                {
                    next = 0u;
                    pattern_complete = true;
                }
                current_bar_ = next;
                break;
            }

            case PlaybackMode::PingPong:
            {
                int32_t next = static_cast<int32_t>(current_bar_) + step_direction_;

                if (next >= static_cast<int32_t>(step_count))
                {
                    step_direction_ = -1;
                    next = static_cast<int32_t>(step_count) - 2;
                    if (next < 0) next = 0;
                }
                else if (next < 0)
                {
                    step_direction_ = 1;
                    next = 1;
                    pattern_complete = true;
                    if (step_count <= 1u) next = 0;
                }

                current_bar_ = static_cast<uint32_t>(next);
                break;
            }

            case PlaybackMode::Loop:
            {
                uint32_t next = current_bar_ + 1u;
                if (next >= step_count)
                {
                    next = 0u;
                    pattern_complete = true;
                }
                current_bar_ = next;
                break;
            }
        }

        if (pattern_complete)
        {
            if (!apply_pattern_completion(pat))
            {
                return;
            }
        }

        const Pattern& scan_pattern = CurrentPattern();
        const uint8_t scan_count = clamp_step_count(scan_pattern);
        if (current_bar_ >= scan_count) current_bar_ = 0u;

        bool found_active = false;
        for (uint8_t guard = 0u; guard < scan_count; ++guard)
        {
            if (scan_pattern.steps[current_bar_].bar_state != BarState::Skip)
            {
                found_active = true;
                break;
            }
            current_bar_ = (current_bar_ + 1u) % scan_count;
        }

        if (found_active)
        {
            ApplyCurrentStepBehavior();
            step_changed_ = true;
            return;
        }

        if (++pattern_exhaustions > max_pattern_exhaustions)
        {
            stop_for_all_skip_chain();
            return;
        }

        if (!apply_pattern_completion(scan_pattern))
        {
            return;
        }

        current_bar_ = 0u;
        current_step_in_bar_ = 0u;
    }
}

/*                                                                            */
/*  ApplyCurrentStepBehavior — fires gate and loads arp for current step.   */
/*  Chord steps: probability roll, gate on, arp loaded.                     */
/*  Rest/Empty/Skip: gate off, arp cleared.                                 */
/*                                                                            */
void SequencerDevice::ApplyCurrentStepBehavior()
{
    RecalculateStepIntervalMs();

    const StepSlot& slot = GetStep(current_bar_);
    if (slot.bar_state == BarState::Skip)
    {
        GateOff();
        gate_retrigger_pending_  = false;
        gate_retrigger_delay_ms_ = 0u;
        arp_.Init();
        arp_ticks_accum_  = 0;
        arp_event_ticks_  = 0;
        return;
    }

    const Pattern&  pat  = CurrentPattern();
    const uint8_t steps_in_bar = StepsPerBar(bank_.GetSong().time_sig, pat.step_division);
    const uint8_t grid_position = (current_step_in_bar_ < steps_in_bar) ? current_step_in_bar_ : (uint8_t)(steps_in_bar - 1u);
    const uint8_t canonical_positions = CanonicalPositionsPerBar(bank_.GetSong().time_sig);
    const uint8_t canonical_index = GridPositionToLedgerIndex(pat.step_division, grid_position);
    const uint8_t is_event_start = (canonical_index < canonical_positions) ?
        IsCanonicalEventStart(slot, canonical_index) : 0u;

    switch (slot.type)
    {
        case StepType::Chord:
        {
            uint8_t notes[4] = {kMidiNoteNone, kMidiNoteNone, kMidiNoteNone, kMidiNoteNone};
            const uint8_t note_count = ResolveEventNotesForPlayback((uint8_t)current_bar_, grid_position, notes);
            if (note_count == 0u)
            {
                GateOff();
                gate_retrigger_pending_  = false;
                gate_retrigger_delay_ms_ = 0u;
                arp_.Init();
                arp_ticks_accum_  = 0;
                arp_event_ticks_  = 0;
                arp_note_changed_ = false;
                break;
            }

            if (is_event_start == 0u)
            {
                /* Inside event body: sustain previous gate/arp state without retrigger. */
                break;
            }

            uint8_t roll = static_cast<uint8_t>(rand() % 100);

            if (roll < slot.probability)
            {
                RetriggerGate(1u);
                arp_.LoadNotes(notes, note_count, pat.arp_mode);
                if (pat.arp_mode == ArpMode::Off)
                {
                    arp_event_ticks_ = 0;
                }
                else
                {
                    arp_event_ticks_ = sequencer::ArpEngine::TicksPerEvent(pat.arp_rate);
                    if (arp_event_ticks_ == 0u) arp_event_ticks_ = 1u;
                }
                arp_ticks_accum_  = 0;
                arp_note_changed_ = true;
            }
            else
            {
                GateOff();
                gate_retrigger_pending_  = false;
                gate_retrigger_delay_ms_ = 0u;
                arp_.Init();
                arp_ticks_accum_ = 0;
                arp_event_ticks_ = 0;
            }
            break;
        }

        case StepType::Rest:
        case StepType::Empty:
        case StepType::Skip:
            GateOff();
            gate_retrigger_pending_  = false;
            gate_retrigger_delay_ms_ = 0u;
            arp_.Init();
            arp_ticks_accum_  = 0;
            arp_event_ticks_  = 0;
            break;
    }
}

/* ── Step helpers ────────────────────────────────────────────────────────── */

void SequencerDevice::SetStepType(uint32_t step_index, StepType type)
{
    if (step_index >= kStepCount) return;
    StepSlot& slot = CurrentPattern().steps[step_index];
    slot.bar_state = LegacyStepTypeToBarState(type);
    RefreshSlotSummary(slot);
    UpdateLegacyStepTypeProjection(slot);
    if (step_index == current_bar_)
    {
        ApplyCurrentStepBehavior();
        step_changed_ = true;
    }
}

void SequencerDevice::SetStepDurationMultiplier(uint32_t step_index,
                                                uint32_t multiplier)
{
    if (step_index >= kStepCount) return;
    if (multiplier == 0) multiplier = 1;
    CurrentPattern().steps[step_index].duration_multiplier = multiplier;
    if (step_index == current_bar_)
    {
        RecalculateStepIntervalMs();
        RecalculateStepTicks();
        step_changed_ = true;
    }
}

void SequencerDevice::ToggleStepNote(uint32_t step_index, uint8_t note)
{
    if (step_index >= kStepCount) return;
    StepSlot& slot = CurrentPattern().steps[step_index];
    ToggleNote(slot.note_mask, note);
    slot.note_mask = LimitNoteMaskToMaxVoices(slot.note_mask, 4u);
    UpdateLegacyStepTypeProjection(slot);
    if (step_index == current_bar_) step_changed_ = true;
}

void SequencerDevice::ClearStepNotes(uint32_t step_index)
{
    if (step_index >= kStepCount) return;
    StepSlot& slot = CurrentPattern().steps[step_index];
    ClearAllNotes(slot.note_mask);
    UpdateLegacyStepTypeProjection(slot);
    if (step_index == current_bar_) step_changed_ = true;
}

void SequencerDevice::SetStepBarState(uint8_t step_index, BarState state)
{
    if (step_index >= kStepCount) return;
    StepSlot& slot = CurrentPattern().steps[step_index];
    slot.bar_state = state;
    RefreshSlotSummary(slot);
    UpdateLegacyStepTypeProjection(slot);
    if (step_index == current_bar_)
    {
        ApplyCurrentStepBehavior();
        step_changed_ = true;
    }
}

BarState SequencerDevice::GetStepBarState(uint8_t step_index) const
{
    if (step_index >= kStepCount) return BarState::Active;
    return CurrentPattern().steps[step_index].bar_state;
}

bool SequencerDevice::IsStepSkipped(uint8_t step_index) const
{
    return GetStepBarState(step_index) == BarState::Skip;
}

const StepSlot& SequencerDevice::GetStep(uint32_t step_index) const
{
    static const StepSlot empty_slot{};
    if (step_index >= kStepCount) return empty_slot;
    return CurrentPattern().steps[step_index];
}

/* ── MIDI clock listener ─────────────────────────────────────────────────── */
/*                                                                            */
/*  OnClockTick — fires 24 times per quarter note (PPQN=24).               */
/*  In slave mode, advances the step when a full beat has elapsed.          */
/*                                                                            */
void SequencerDevice::OnClockTick()
{
    ++midi_tick_count_;

    if (midi_tick_count_ >= midi::kPPQN)
    {
        midi_tick_count_ = 0;

        if (midi_clock_.GetMode() == midi::ClockMode::Slave && playing_)
            elapsed_step_ms_ = current_step_interval_ms_;
    }
}

void SequencerDevice::OnClockStart()
{
    if (midi_clock_.GetMode() == midi::ClockMode::Slave)
    {
        midi_tick_count_ = 0;
        Reset();
        Start();
    }
}

void SequencerDevice::OnClockStop()
{
    if (midi_clock_.GetMode() == midi::ClockMode::Slave)
        Stop();
}

void SequencerDevice::OnClockContinue()
{
    if (midi_clock_.GetMode() == midi::ClockMode::Slave)
        Start();
}