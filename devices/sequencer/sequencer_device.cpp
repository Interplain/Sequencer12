#include "devices/sequencer/sequencer_device.h"
#include "devices/sequencer/arp_engine.h"
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

static uint8_t StepDivisionToLedgerSlots(uint8_t step_division)
{
    switch (step_division)
    {
        case 1u: return 4u;   /* 1/4 = 4 columns per step */
        case 2u: return 8u;   /* 1/8 = 8 columns per step */
        case 4u: return 16u;  /* 1/16 = 16 columns per step */
        case 8u: return 32u;  /* 1/32 = 32 columns per step */
        default: return 4u;
    }
}

static void InitLedgerFromMask(sequencer::StepSlot& slot, uint16_t note_mask, uint8_t length)
{
    slot.note_ledger.fill(0u);
    slot.note_ledger[0] = note_mask;
    slot.repeat_count = ClampLedgerLength(length);
}

static void NormalizePatternDivisionCadence(Pattern& pattern)
{
    const uint8_t fixed_len = StepDivisionToLedgerSlots(pattern.step_division);
    for (uint8_t i = 0u; i < kStepCount; ++i)
    {
        StepSlot& slot = pattern.steps[i];
        slot.repeat_count = ClampLedgerLength(fixed_len);
        for (uint8_t j = fixed_len; j < kStepLedgerMax; ++j)
        {
            slot.note_ledger[j] = 0u;
        }
        if (slot.note_mask == 0u)
        {
            slot.type = StepType::Empty;
        }
        else
        {
            slot.type = StepType::Chord;
        }
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
    int8_t transpose = bank_.GetSong().transpose;
    if (transpose == 0) return note_mask;

    uint8_t semitones = static_cast<uint8_t>(
        ((transpose % 12) + 12) % 12);

    return sequencer::TransposeNoteMask(note_mask, semitones);
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
    current_step_          = 0;
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
    for (uint32_t i = 0; i < kStepCount; ++i)
    {
        p0.steps[i].duration_multiplier = 1;
        p0.steps[i].repeat_count        = 1;
        p0.steps[i].velocity            = 100;
        p0.steps[i].probability         = 100;
        p0.steps[i].type                = StepType::Rest;
        p0.steps[i].note_mask           = 0;
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
    if (current_step_ >= step_count) current_step_ = 0;
}

uint8_t SequencerDevice::GetPatternStepCount() const
{
    return CurrentPattern().step_count;
}

void SequencerDevice::SetPatternStepDivision(uint8_t step_division)
{
    if (step_division < 1) step_division = 1;
    if (step_division > 8) step_division = 8;

    Pattern& pattern = CurrentPattern();
    pattern.step_division = step_division;
    NormalizePatternDivisionCadence(pattern);

    RecalculateStepIntervalMs();
    RecalculateStepTicks();
}

uint8_t SequencerDevice::GetPatternStepDivision() const
{
    return CurrentPattern().step_division;
}

void SequencerDevice::SetTimeSignature(uint8_t numerator, uint8_t denominator)
{
    if (numerator < 1) numerator = 1;
    if (numerator > 12) numerator = 12;
    if (!(denominator == 2 || denominator == 4 || denominator == 8)) denominator = 4;
    bank_.GetSong().time_sig.numerator = numerator;
    bank_.GetSong().time_sig.denominator = denominator;
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
    slot.repeat_count = StepDivisionToLedgerSlots(CurrentPattern().step_division);
    (void)repeat_count;

    if (chord_type == 0)
    {
        slot.type = StepType::Empty;
        slot.note_mask = 0;
        slot.note_ledger.fill(0u);
        slot.custom_chord_name[0] = '\0';
    }
    else
    {
        const ChordType mapped = UiChordTypeToLibraryType(chord_type);
        slot.type = StepType::Chord;
        slot.note_mask = LimitNoteMaskToMaxVoices(
            ChordLibrary::GetNoteMask(static_cast<KeyRoot>(root_key), mapped),
            4u);
        InitLedgerFromMask(slot, slot.note_mask, slot.repeat_count);
        slot.custom_chord_name[0] = '\0';
    }

    (void)arp_pattern;
    CurrentPattern().arp_mode = ArpMode::Off;

    if (step_index == current_step_)
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
    slot.type = (clipped != 0u) ? StepType::Chord : StepType::Empty;
    slot.note_mask = clipped;
    slot.repeat_count = StepDivisionToLedgerSlots(CurrentPattern().step_division);
    InitLedgerFromMask(slot, clipped, slot.repeat_count);
    slot.custom_chord_name[0] = '\0';
    CurrentPattern().arp_mode = ArpMode::Off;

    if (step_index == current_step_)
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
    slot.type = (clipped != 0u) ? StepType::Chord : StepType::Empty;
    slot.note_mask = clipped;
    slot.repeat_count = StepDivisionToLedgerSlots(CurrentPattern().step_division);
    InitLedgerFromMask(slot, clipped, slot.repeat_count);
    CurrentPattern().arp_mode = ArpMode::Off;

    if (name && name[0] != '\0')
    {
        strncpy(slot.custom_chord_name, name, sizeof(slot.custom_chord_name));
        slot.custom_chord_name[sizeof(slot.custom_chord_name) - 1] = '\0';
    }
    else
    {
        slot.custom_chord_name[0] = '\0';
    }

    if (step_index == current_step_)
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
    NormalizePatternDivisionCadence(CurrentPattern());
    current_step_ = 0;
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
    return step_repeat_current_;
}

void SequencerDevice::SetStepLedgerLength(uint8_t step_index, uint8_t length)
{
    if (step_index >= kStepCount) return;

    StepSlot& slot = CurrentPattern().steps[step_index];
    const uint8_t fixed_len = StepDivisionToLedgerSlots(CurrentPattern().step_division);
    const uint8_t clamped = ClampLedgerLength(length);

    /* The current grid division defines the runtime cadence for the whole step.
     * Do not let the active note count shorten or lengthen an otherwise identical step. */
    slot.repeat_count = fixed_len;

    for (uint8_t i = fixed_len; i < kStepLedgerMax; ++i)
    {
        slot.note_ledger[i] = 0u;
    }

    uint16_t first_nonzero = 0u;
    for (uint8_t i = 0u; i < fixed_len; ++i)
    {
        if (slot.note_ledger[i] != 0u)
        {
            first_nonzero = slot.note_ledger[i];
            break;
        }
    }

    slot.note_mask = first_nonzero;
    slot.type = (first_nonzero != 0u) ? StepType::Chord : StepType::Empty;
    (void)clamped;

    if (step_index == current_step_)
    {
        ApplyCurrentStepBehavior();
        step_changed_ = true;
    }
}

void SequencerDevice::SetStepLedgerSlot(uint8_t step_index, uint8_t slot_index, uint16_t note_mask)
{
    if (step_index >= kStepCount || slot_index >= kStepLedgerMax) return;

    StepSlot& slot = CurrentPattern().steps[step_index];
    const uint8_t fixed_len = StepDivisionToLedgerSlots(CurrentPattern().step_division);
    if (slot_index >= fixed_len)
    {
        return;
    }

    const uint16_t clipped = LimitNoteMaskToMaxVoices((uint16_t)(note_mask & 0x0FFFu), 4u);
    slot.note_ledger[slot_index] = clipped;
    slot.repeat_count = fixed_len;

    uint16_t first_nonzero = 0u;
    for (uint8_t i = 0u; i < fixed_len; ++i)
    {
        if (slot.note_ledger[i] != 0u)
        {
            first_nonzero = slot.note_ledger[i];
            break;
        }
    }

    slot.note_mask = first_nonzero;
    slot.type = (first_nonzero != 0u) ? StepType::Chord : StepType::Empty;
    slot.custom_chord_name[0] = '\0';
    CurrentPattern().arp_mode = ArpMode::Off;

    if (step_index == current_step_)
    {
        ApplyCurrentStepBehavior();
        step_changed_ = true;
    }
}

uint8_t SequencerDevice::GetStepLedgerLength(uint8_t step_index) const
{
    if (step_index >= kStepCount) return 0u;
    return ClampLedgerLength(StepDivisionToLedgerSlots(CurrentPattern().step_division));
}

uint16_t SequencerDevice::GetStepLedgerSlot(uint8_t step_index, uint8_t slot_index) const
{
    if (step_index >= kStepCount || slot_index >= kStepLedgerMax) return 0u;
    return CurrentPattern().steps[step_index].note_ledger[slot_index];
}

uint16_t SequencerDevice::GetStepNoteMask(uint8_t step_index) const
{
    if (step_index >= kStepCount) return 0;
    return CurrentPattern().steps[step_index].note_mask;
}

uint16_t SequencerDevice::GetStepNoteMaskForPlayback(uint8_t step_index) const
{
    if (step_index >= kStepCount) return 0;
    const StepSlot& slot = CurrentPattern().steps[step_index];
    const uint16_t ledger_mask = slot.note_ledger[0];
    const uint16_t source_mask = (ledger_mask != 0u) ? ledger_mask : slot.note_mask;
    return ApplyTranspose(source_mask);
}

static uint16_t ResolveLedgerMaskForIndex(const StepSlot& slot, uint8_t ledger_index)
{
    const uint16_t first = slot.note_ledger[0];
    if (first == 0u)
    {
        /* Legacy path: no explicit ledger data, keep single-mask behavior. */
        return slot.note_mask;
    }

    /* Ledger is populated: zero means intentional rest for that slot. */
    return slot.note_ledger[ledger_index];
}

uint16_t SequencerDevice::GetCurrentStepNoteMaskForPlayback() const
{
    if (current_step_ >= kStepCount) return 0;

    const StepSlot& slot = CurrentPattern().steps[current_step_];
    const uint8_t ledger_len = ClampLedgerLength(slot.repeat_count);
    const uint8_t ledger_index = (step_repeat_current_ < ledger_len) ? step_repeat_current_ : (uint8_t)(ledger_len - 1u);
    const uint16_t source_mask = ResolveLedgerMaskForIndex(slot, ledger_index);
    return ApplyTranspose(source_mask);
}

void SequencerDevice::ExportSong(sequencer::Song* out_song) const
{
    if (!out_song) return;
    *out_song = bank_.GetSong();
}

void SequencerDevice::ImportSong(const sequencer::Song& song)
{
    bank_.LoadSong(song);

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

    NormalizePatternDivisionCadence(CurrentPattern());
    current_step_ = 0;
    repeat_current_ = 0;
    step_direction_ = 1;
    elapsed_step_ms_ = 0;

    RecalculateStepIntervalMs();
    ApplyCurrentStepBehavior();
    step_changed_ = true;
    status_changed_ = true;
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

/* ── Tick1ms — called from SysTick ISR every 1ms ───────────────────────── */
/*                                                                            */
/*  Drives gate timing, MIDI clock, arp engine, and step advancement.       */
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

    /* Arp tick — advance arp note at arp_interval_ms_ rate */
    if (playing_ &&
        arp_.HasNotes() &&
        arp_interval_ms_ > 0 &&
        CurrentPattern().arp_mode != sequencer::ArpMode::Off)
    {
        ++arp_elapsed_ms_;

        if (arp_elapsed_ms_ >= arp_interval_ms_)
        {
            arp_elapsed_ms_   = 0;
            arp_.Advance();
            arp_note_changed_ = true;
            RetriggerGate(1u);   /* force a real gate edge on each arp note */
        }
    }

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
    step_repeat_current_ = 0;
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
    current_step_          = 0;
    elapsed_step_ms_       = 0;
    musical_ticks_accum_   = 0u;
    pending_step_events_   = 0u;
    max_pending_step_events_ = 0u;
    pending_step_backlog_count_ = 0u;
    repeat_current_        = 0;
    step_repeat_current_   = 0;
    step_direction_        = 1;
    arp_elapsed_ms_        = 0;
    arp_interval_ms_       = 0;
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

    current_step_interval_ms_ =
        base_step_interval_ms_ *
        CurrentPattern().steps[current_step_].duration_multiplier;

    /* Ledger slots remain internal note selections within the same step.
     * They do not reduce the full step interval. */

    /* Gate length = 25% of step interval, minimum 5ms. Keep the gate
     * well inside the step so the next step does not inherit the attack. */
    gate_length_ms_ = current_step_interval_ms_ / 4u;
    if (gate_length_ms_ < 5u) gate_length_ms_ = 5u;
}

void SequencerDevice::RecalculateStepTicks()
{
    uint8_t division = CurrentPattern().step_division;
    if (division == 0u) division = 4u;

    uint32_t base_ticks = 96u / division;
    uint32_t duration_mult = CurrentPattern().steps[current_step_].duration_multiplier;
    if (duration_mult == 0u) duration_mult = 1u;

    musical_step_ticks_ = base_ticks * duration_mult;
    if (musical_step_ticks_ == 0u) musical_step_ticks_ = 1u;

    musical_ticks_accum_ = 0u;
    pending_step_events_ = 0u;
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
    const Pattern& pat        = CurrentPattern();
    uint8_t        step_count = pat.step_count;

    if (step_count == 0 || step_count > kStepCount)
        step_count = kStepCount;

    {
        const StepSlot& slot = GetStep(current_step_);
        const uint8_t repeats = ClampLedgerLength(slot.repeat_count);

        if ((step_repeat_current_ + 1u) < repeats)
        {
            ++step_repeat_current_;
            ApplyCurrentStepBehavior();
            step_changed_ = true;
            return;
        }
        step_repeat_current_ = 0;
    }

    bool pattern_complete = false;

    switch (pat.playback_mode)
    {
        case PlaybackMode::Forward:
        case PlaybackMode::OneShot:
        {
            uint32_t next = current_step_ + 1;
            if (next >= step_count)
            {
                next             = 0;
                pattern_complete = true;
            }
            current_step_ = next;
            break;
        }

        case PlaybackMode::PingPong:
        {
            int32_t next = static_cast<int32_t>(current_step_) + step_direction_;

            if (next >= static_cast<int32_t>(step_count))
            {
                step_direction_ = -1;
                next            = static_cast<int32_t>(step_count) - 2;
                if (next < 0) next = 0;
            }
            else if (next < 0)
            {
                step_direction_  = 1;
                next             = 1;
                pattern_complete = true;
                if (step_count <= 1) next = 0;
            }

            current_step_ = static_cast<uint32_t>(next);
            break;
        }

        case PlaybackMode::Loop:
        {
            uint32_t next = current_step_ + 1;
            if (next >= step_count)
            {
                next             = 0;
                pattern_complete = true;
            }
            current_step_ = next;
            break;
        }
    }

    /* Pattern completion — handle chaining and repeat */
    if (pattern_complete)
    {
        ++completed_loops_;

        switch (pat.playback_mode)
        {
            case PlaybackMode::OneShot:
                Stop();
                return;

            case PlaybackMode::Loop:
            {
                ++repeat_current_;

                if (repeat_current_ >= pat.repeat_count)
                {
                    repeat_current_ = 0;
                    bank_.ChainAdvance();
                    current_pattern_index_ = bank_.ChainCurrentPatternIndex();
                    status_changed_        = true;
                }
                break;
            }

            case PlaybackMode::Forward:
            case PlaybackMode::PingPong:
            {
                ++repeat_current_;

                if (repeat_current_ >= pat.repeat_count)
                {
                    repeat_current_ = 0;
                    step_direction_ = 1;
                    bank_.ChainAdvance();
                    current_pattern_index_ = bank_.ChainCurrentPatternIndex();
                    status_changed_        = true;
                }
                break;
            }
        }
    }

    /* Skip handling — advance past any Skip steps */
    for (uint32_t guard = 0; guard < kStepCount; ++guard)
    {
        if (CurrentPattern().steps[current_step_].type != StepType::Skip)
            break;
        current_step_ = (current_step_ + 1) % step_count;
    }

    ApplyCurrentStepBehavior();
    step_changed_ = true;
    RecalculateStepTicks();
}

/*                                                                            */
/*  ApplyCurrentStepBehavior — fires gate and loads arp for current step.   */
/*  Chord steps: probability roll, gate on, arp loaded.                     */
/*  Rest/Empty/Skip: gate off, arp cleared.                                 */
/*                                                                            */
void SequencerDevice::ApplyCurrentStepBehavior()
{
    RecalculateStepIntervalMs();
    RecalculateStepTicks();

    const StepSlot& slot = GetStep(current_step_);
    const Pattern&  pat  = CurrentPattern();
    const uint8_t ledger_len = ClampLedgerLength(slot.repeat_count);
    const uint8_t ledger_index = (step_repeat_current_ < ledger_len) ? step_repeat_current_ : (uint8_t)(ledger_len - 1u);

    switch (slot.type)
    {
        case StepType::Chord:
        {
            const uint16_t source_mask = ResolveLedgerMaskForIndex(slot, ledger_index);
            const uint16_t play_mask = ApplyTranspose(source_mask);
            if (play_mask == 0u)
            {
                GateOff();
                gate_retrigger_pending_  = false;
                gate_retrigger_delay_ms_ = 0u;
                arp_.Init();
                arp_elapsed_ms_   = 0;
                arp_interval_ms_  = 0;
                arp_note_changed_ = false;
                break;
            }

            uint8_t roll = static_cast<uint8_t>(rand() % 100);

            if (roll < slot.probability)
            {
                RetriggerGate(1u);
                arp_.LoadChord(play_mask, pat.arp_mode);
                if (pat.arp_mode == ArpMode::Off)
                {
                    arp_interval_ms_ = 0;
                }
                else
                {
                    uint32_t split_count = arp_.NoteCount();
                    if (split_count == 0u) split_count = 1u;
                    arp_interval_ms_ = current_step_interval_ms_ / split_count;
                    if (arp_interval_ms_ < 5u) arp_interval_ms_ = 5u;
                }
                arp_elapsed_ms_   = 0;
                arp_note_changed_ = true;
            }
            else
            {
                GateOff();
                gate_retrigger_pending_  = false;
                gate_retrigger_delay_ms_ = 0u;
                arp_.Init();
                arp_elapsed_ms_  = 0;
                arp_interval_ms_ = 0;
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
            arp_elapsed_ms_   = 0;
            arp_interval_ms_  = 0;
            break;
    }
}

/* ── Step helpers ────────────────────────────────────────────────────────── */

void SequencerDevice::SetStepType(uint32_t step_index, StepType type)
{
    if (step_index >= kStepCount) return;
    CurrentPattern().steps[step_index].type = type;
    if (step_index == current_step_)
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
    if (step_index == current_step_)
    {
        RecalculateStepIntervalMs();
        RecalculateStepTicks();
        step_changed_ = true;
    }
}

void SequencerDevice::ToggleStepNote(uint32_t step_index, uint8_t note)
{
    if (step_index >= kStepCount) return;
    ToggleNote(CurrentPattern().steps[step_index].note_mask, note);
    CurrentPattern().steps[step_index].note_mask =
        LimitNoteMaskToMaxVoices(CurrentPattern().steps[step_index].note_mask, 4u);
    CurrentPattern().steps[step_index].type =
        (CurrentPattern().steps[step_index].note_mask != 0u) ? StepType::Chord : StepType::Empty;
    if (step_index == current_step_) step_changed_ = true;
}

void SequencerDevice::ClearStepNotes(uint32_t step_index)
{
    if (step_index >= kStepCount) return;
    ClearAllNotes(CurrentPattern().steps[step_index].note_mask);
    if (step_index == current_step_) step_changed_ = true;
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