#include "devices/sequencer/sequencer_device.h"
#include "devices/sequencer/arp_engine.h"

using sequencer::StepSlot;
using sequencer::StepType;
using sequencer::Pattern;
using namespace sequencer;

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
    runtime_ms_            = 0;

    /* ── Default pattern — 12 steps, all Chord, forward loop ───────── */
    Pattern& p0 = bank_.GetPattern(0);

    /* Playback mode — forward, loop forever, all 12 steps */
    p0.playback_mode    = PlaybackMode::Loop;
    p0.step_count       = 12;
    p0.repeat_count     = 1;
    p0.tempo_multiplier = 1.0f;
    p0.arp_mode         = ArpMode::Off;

    /* All steps — Chord type, full probability, duration 1 */
    for (uint32_t i = 0; i < kStepCount; ++i)
    {
        p0.steps[i].type                = StepType::Chord;
        p0.steps[i].duration_multiplier = 1;
        p0.steps[i].velocity            = 100;
        p0.steps[i].probability         = 100;
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

    if (midi_clock_enabled_ &&
        midi_clock_.GetMode() == midi::ClockMode::Master)
    {
        midi_clock_.SetBpm(bpm);
    }
}

/* ── Tick1ms — called from SysTick ISR every 1ms ───────────────────────── */
/*                                                                            */
/*  Drives gate timing, MIDI clock, arp engine, and step advancement.       */
/*  Keep this lean — it runs inside an interrupt context on STM32.          */
/*                                                                            */
void SequencerDevice::Tick1ms()
{
    ++runtime_ms_;

    /* Gate timing — turn gate off after gate_length_ms_ */
    if (gate_active_)
    {
        ++gate_elapsed_ms_;
        if (gate_elapsed_ms_ >= gate_length_ms_)
            GateOff();
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
            GateOn();   /* retrigger gate on each arp note */
        }
    }

    if (!playing_) return;

    /* Step advancement */
    ++elapsed_step_ms_;

    if (elapsed_step_ms_ >= current_step_interval_ms_)
    {
        elapsed_step_ms_ = 0;
        AdvanceStep();
    }
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
    repeat_current_        = 0;
    step_direction_        = 1;
    arp_elapsed_ms_        = 0;
    arp_interval_ms_       = 0;
    arp_.Init();
    bank_.ChainReset();
    current_pattern_index_ = bank_.ChainCurrentPatternIndex();
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

/* ── Step engine ─────────────────────────────────────────────────────────── */
/*                                                                            */
/*  RecalculateStepIntervalMs — converts BPM to ms per step, applying       */
/*  the current pattern's tempo multiplier and step duration multiplier.    */
/*                                                                            */
void SequencerDevice::RecalculateStepIntervalMs()
{
    uint32_t bpm = bank_.GetEffectiveBpm(current_pattern_index_);
    if (bpm == 0) bpm = 1;

    base_step_interval_ms_ = 60000 / bpm;

    current_step_interval_ms_ =
        base_step_interval_ms_ *
        CurrentPattern().steps[current_step_].duration_multiplier;
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
            if (next >= step_count) next = 0;
            current_step_ = next;
            break;
        }
    }

    /* Pattern completion — handle chaining and repeat */
    if (pattern_complete)
    {
        switch (pat.playback_mode)
        {
            case PlaybackMode::OneShot:
                Stop();
                return;

            case PlaybackMode::Loop:
                break;

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
}

/*                                                                            */
/*  ApplyCurrentStepBehavior — fires gate and loads arp for current step.   */
/*  Chord steps: probability roll, gate on, arp loaded.                     */
/*  Rest/Empty/Skip: gate off, arp cleared.                                 */
/*                                                                            */
void SequencerDevice::ApplyCurrentStepBehavior()
{
    RecalculateStepIntervalMs();

    const StepSlot& slot = GetStep(current_step_);
    const Pattern&  pat  = CurrentPattern();

    switch (slot.type)
    {
        case StepType::Chord:
        {
            uint8_t roll = static_cast<uint8_t>(rand() % 100);

            if (roll < slot.probability)
            {
                GateOn();
                arp_.LoadChord(ApplyTranspose(slot.note_mask), pat.arp_mode);
                uint32_t bpm     = bank_.GetEffectiveBpm(current_pattern_index_);
                arp_interval_ms_ = sequencer::ArpEngine::CalcIntervalMs(
                    bpm, pat.arp_rate);
                arp_elapsed_ms_   = 0;
                arp_note_changed_ = true;
            }
            else
            {
                GateOff();
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
        step_changed_ = true;
    }
}

void SequencerDevice::ToggleStepNote(uint32_t step_index, uint8_t note)
{
    if (step_index >= kStepCount) return;
    ToggleNote(CurrentPattern().steps[step_index].note_mask, note);
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