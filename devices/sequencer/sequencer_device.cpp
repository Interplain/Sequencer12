#include "devices/sequencer/sequencer_device.h"
#include <iostream>
#include "devices/sequencer/arp_engine.h"

using sequencer::StepSlot;
using sequencer::StepType;
using sequencer::Pattern;
using namespace sequencer;

// ── Convenience accessors ─────────────────────────────────────────────────────

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

    // Positive = shift up, negative = shift down
    uint8_t semitones = static_cast<uint8_t>(
        ((transpose % 12) + 12) % 12);

    return sequencer::TransposeNoteMask(note_mask, semitones);
}


// ── Init ──────────────────────────────────────────────────────────────────────

void SequencerDevice::Init()
{
    bank_.Init();
    bank_.SetGlobalBpm(120);
    bank_.SetKey(KeyRoot::C, KeyScale::Major);
    bank_.GetSong().transpose = 2;

       // ── MIDI clock ────────────────────────────────────────────────────────────
    midi_clock_enabled_ = true;
    midi_tick_count_    = 0;
    midi_clock_.Init(midi::ClockMode::Master, this);
    midi_clock_.SetBpm(bank_.GetGlobalBpm());

    playing_               = false;
    gate_active_           = false;
    current_pattern_index_ = 0;
    current_step_          = 0;
    elapsed_step_ms_       = 0;
    gate_elapsed_ms_       = 0;
    gate_length_ms_        = 100;
    runtime_ms_            = 0;
    demo_phase_            = 0;

    // ── Build test pattern 0 ──────────────────────────────────────────────────
    Pattern& p0 = bank_.GetPattern(0);

    for (uint32_t i = 0; i < kStepCount; ++i)
    {
        p0.steps[i].type                = StepType::Empty;
        p0.steps[i].duration_multiplier = 1;
        p0.steps[i].note_mask           = 0;
    }

    p0.steps[0].type        = StepType::Chord;
    p0.steps[0].velocity    = 100;
    p0.steps[0].probability = 100;
    ToggleStepNote(0, 0);   // C
    ToggleStepNote(0, 4);   // E
    ToggleStepNote(0, 7);   // G

    p0.steps[1].type        = StepType::Chord;
    p0.steps[1].velocity    = 80;
    p0.steps[1].probability = 75;
    ToggleStepNote(1, 2);   // D
    ToggleStepNote(1, 5);   // F
    ToggleStepNote(1, 9);   // A

    p0.steps[2].type = StepType::Rest;

    p0.steps[3].type        = StepType::Chord;
    p0.steps[3].velocity    = 60;
    p0.steps[3].probability = 50;
    ToggleStepNote(3, 4);   // E
    ToggleStepNote(3, 7);   // G
    ToggleStepNote(3, 11);  // B

    p0.steps[4].type = StepType::Skip;

    p0.steps[5].type                = StepType::Chord;
    p0.steps[5].duration_multiplier = 2;
    p0.steps[5].velocity            = 100;
    p0.steps[5].probability         = 100;
    ToggleStepNote(5, 5);   // F
    ToggleStepNote(5, 9);   // A
    ToggleStepNote(5, 0);   // C

    p0.steps[6].type = StepType::Empty;

    p0.steps[7].type        = StepType::Chord;
    p0.steps[7].velocity    = 127;
    p0.steps[7].probability = 25;
    ToggleStepNote(7, 7);   // G
    ToggleStepNote(7, 11);  // B
    ToggleStepNote(7, 2);   // D

    p0.steps[8].type = StepType::Rest;

    p0.steps[9].type        = StepType::Chord;
    p0.steps[9].velocity    = 100;
    p0.steps[9].probability = 100;
    ToggleStepNote(9, 9);   // A
    ToggleStepNote(9, 0);   // C
    ToggleStepNote(9, 4);   // E

    p0.steps[10].type = StepType::Skip;

    p0.steps[11].type        = StepType::Chord;
    p0.steps[11].velocity    = 100;
    p0.steps[11].probability = 100;
    ToggleStepNote(11, 11); // B
    ToggleStepNote(11, 2);  // D
    ToggleStepNote(11, 6);  // F#

    // ── Build test pattern 1 ──────────────────────────────────────────────────
    Pattern& p1 = bank_.GetPattern(1);

    for (uint32_t i = 0; i < kStepCount; ++i)
    {
        p1.steps[i].type                = StepType::Empty;
        p1.steps[i].duration_multiplier = 1;
        p1.steps[i].note_mask           = 0;
    }

    p1.tempo_multiplier = 1.5f;
    p1.playback_mode    = PlaybackMode::PingPong;
    p1.repeat_count     = 2;
    p1.step_count       = 6;
    p1.arp_mode         = ArpMode::Up;
    p1.arp_rate         = ArpRate::Sixteenth;

    p1.steps[0].type = StepType::Chord;
    SetNote(p1.steps[0].note_mask, 0);   // C
    SetNote(p1.steps[0].note_mask, 3);   // Eb
    SetNote(p1.steps[0].note_mask, 7);   // G

    p1.steps[1].type = StepType::Rest;

    p1.steps[2].type = StepType::Chord;
    SetNote(p1.steps[2].note_mask, 5);   // F
    SetNote(p1.steps[2].note_mask, 8);   // Ab
    SetNote(p1.steps[2].note_mask, 0);   // C

    p1.steps[3].type = StepType::Rest;

    p1.steps[4].type = StepType::Chord;
    SetNote(p1.steps[4].note_mask, 7);   // G
    SetNote(p1.steps[4].note_mask, 11);  // B
    SetNote(p1.steps[4].note_mask, 2);   // D

    p1.steps[5].type = StepType::Rest;

    // ── Chain: pattern 0 → pattern 1 → loop ──────────────────────────────────
    bank_.ChainClear();
    bank_.ChainAppend(0);
    bank_.ChainAppend(1);

    // ── Initialise engine state ───────────────────────────────────────────────
    step_changed_   = true;
    status_changed_ = true;
    gate_changed_   = false;

    RecalculateStepIntervalMs();
    ApplyCurrentStepBehavior();

    std::cout << "SequencerDevice initialized" << std::endl;
    std::cout << "Global BPM = " << bank_.GetGlobalBpm()
              << ", key = " << static_cast<int>(bank_.GetKeyRoot())
              << ", base step interval = " << base_step_interval_ms_
              << " ms, gate length = " << gate_length_ms_
              << " ms" << std::endl;
}

// ── Tick ──────────────────────────────────────────────────────────────────────

void SequencerDevice::Tick1ms()
{
    ++runtime_ms_;

    if (demo_phase_ == 0 && runtime_ms_ >= 1000)
    {
        Start();
        demo_phase_ = 1;
    }
    else if (demo_phase_ == 1 && runtime_ms_ >= 6000)
    {
        Stop();
        demo_phase_ = 2;
    }
    else if (demo_phase_ == 2 && runtime_ms_ >= 8000)
    {
        Reset();
        Start();
        demo_phase_ = 3;
    }
    else if (demo_phase_ == 3 && runtime_ms_ >= 13000)
    {
        Stop();
        demo_phase_ = 4;
    }
    else if (demo_phase_ == 4 && runtime_ms_ >= 15000)
    {
        Reset();
        Start();
        demo_phase_ = 5;
    }

    else if (demo_phase_ == 5 && runtime_ms_ >= 30000)
    {
        Stop();
        std::cout << "\n--- Demo complete ---" << std::endl;
        demo_phase_ = 6;
        exit(0);
    }

    if (gate_active_)
    {
        ++gate_elapsed_ms_;
        if (gate_elapsed_ms_ >= gate_length_ms_)
        {
            GateOff();
        }
    }

// ── MIDI clock tick ───────────────────────────────────────────────────────
    if (midi_clock_enabled_)
    {
        midi_clock_.Tick1ms();
    }    

// ── Arp tick ──────────────────────────────────────────────────────────────
    if (playing_ &&
        arp_.HasNotes() &&
        arp_interval_ms_ > 0 &&
        CurrentPattern().arp_mode != sequencer::ArpMode::Off)
    {
        ++arp_elapsed_ms_;

        if (arp_elapsed_ms_ >= arp_interval_ms_)
        {
            arp_elapsed_ms_ = 0;
            arp_.Advance();
            arp_note_changed_ = true;
            GateOn();   // retrigger gate on each arp note
        }
    }


    if (!playing_) return;

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

// ── Process ───────────────────────────────────────────────────────────────────

void SequencerDevice::Process()
{
    if (status_changed_)
    {
        status_changed_ = false;

        std::cout
            << "[TRANSPORT] "
            << (playing_ ? "PLAYING" : "STOPPED")
            << "   pattern=" << static_cast<int>(current_pattern_index_)
            << "   step="    << current_step_
            << "   bpm="     << bank_.GetEffectiveBpm(current_pattern_index_)
            << std::endl;
    }

    if (step_changed_)
    {
        step_changed_ = false;

        const StepSlot& slot = GetStep(current_step_);

        std::cout
            << "Step = "    << current_step_
            << " / "        << (kStepCount - 1)
            << "   pat = "  << static_cast<int>(current_pattern_index_)
            << "   type = " << StepTypeName(slot.type)
            << "   durx = " << slot.duration_multiplier
            << "   bpm = "  << bank_.GetEffectiveBpm(current_pattern_index_)
            << "   t = "    << runtime_ms_ << "ms"
            << "   state = "<< (playing_ ? "PLAYING" : "STOPPED");

        if (slot.type == StepType::Chord)
        {
            std::cout << "   notes = ";
            PrintNoteMask(ApplyTranspose(slot.note_mask));
        }

        std::cout << std::endl;
    }

    if (gate_changed_)
    {
        gate_changed_ = false;

        std::cout
            << "[GATE] "
            << (gate_active_ ? "HIGH" : "LOW")
            << "   pat="  << static_cast<int>(current_pattern_index_)
            << "   step=" << current_step_
            << std::endl;
    }

    if (arp_note_changed_)
    {
        arp_note_changed_ = false;

        if (arp_.HasNotes() &&
            CurrentPattern().arp_mode != sequencer::ArpMode::Off)
        {
            static const char* kNoteNames[12] =
            {
                "C", "C#", "D", "D#", "E", "F",
                "F#", "G", "G#", "A", "A#", "B"
            };

            uint8_t note = arp_.CurrentNote();

            if (note < 12)
            {
                std::cout
                    << "[ARP] note = " << kNoteNames[note]
                    << "   pat="       << static_cast<int>(current_pattern_index_)
                    << "   step="      << current_step_
                    << "   mode="      << static_cast<int>(
                                          CurrentPattern().arp_mode)
                    << "   interval="  << arp_interval_ms_ << "ms"
                    << std::endl;
            }
        }
    }
}


// ── Engine ────────────────────────────────────────────────────────────────────

void SequencerDevice::RecalculateStepIntervalMs()
{
    uint32_t bpm = bank_.GetEffectiveBpm(current_pattern_index_);

    if (bpm == 0) bpm = 1;

    base_step_interval_ms_ = 60000 / bpm;

    current_step_interval_ms_ =
        base_step_interval_ms_ *
        CurrentPattern().steps[current_step_].duration_multiplier;
}

void SequencerDevice::AdvanceStep()
{
    const Pattern& pat       = CurrentPattern();
    uint8_t        step_count = pat.step_count;

    // Clamp step_count to valid range
    if (step_count == 0 || step_count > kStepCount)
    {
        step_count = kStepCount;
    }

    // ── Calculate next step based on playback mode ────────────────────────────
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
                // Hit the top — reverse direction
                step_direction_ = -1;
                next            = static_cast<int32_t>(step_count) - 2;
                if (next < 0) next = 0;
            }
            else if (next < 0)
            {
                // Hit the bottom — reverse direction, pattern complete
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
            // Never advances chain — just wraps within step_count
            uint32_t next = current_step_ + 1;
            if (next >= step_count) next = 0;
            current_step_ = next;
            break;
        }
    }

    // ── Handle pattern completion ─────────────────────────────────────────────
    if (pattern_complete)
    {
        switch (pat.playback_mode)
        {
            case PlaybackMode::OneShot:
                // Play once then stop — never advance chain
                Stop();
                std::cout << "[ONESHOT] Pattern complete, stopping" << std::endl;
                return;

            case PlaybackMode::Loop:
                // Never advances chain
                break;

            case PlaybackMode::Forward:
            case PlaybackMode::PingPong:
            {
                ++repeat_current_;

                std::cout
                    << "[REPEAT] " << static_cast<int>(repeat_current_)
                    << " / "       << static_cast<int>(pat.repeat_count)
                    << std::endl;

                if (repeat_current_ >= pat.repeat_count)
                {
                    // Done repeating — advance chain
                    repeat_current_ = 0;
                    step_direction_ = 1;

                    bank_.ChainAdvance();
                    current_pattern_index_ = bank_.ChainCurrentPatternIndex();
                    status_changed_        = true;

                    std::cout
                        << "[CHAIN] Advanced to pattern "
                        << static_cast<int>(current_pattern_index_)
                        << "   chain pos = "
                        << static_cast<int>(bank_.ChainCurrentPosition())
                        << std::endl;
                }
                break;
            }
        }
    }

    // ── Skip handling ─────────────────────────────────────────────────────────
    for (uint32_t guard = 0; guard < kStepCount; ++guard)
    {
        if (CurrentPattern().steps[current_step_].type != StepType::Skip)
        {
            break;
        }

        std::cout << "[STEP] Skipping step " << current_step_ << std::endl;
        current_step_ = (current_step_ + 1) % step_count;
    }

    ApplyCurrentStepBehavior();
    step_changed_ = true;
}

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
    arp_elapsed_ms_        = 0;   // add this
    arp_interval_ms_       = 0;   // add this
    arp_.Init();                   // add this
    bank_.ChainReset();
    current_pattern_index_ = bank_.ChainCurrentPatternIndex();
    GateOff();
    ApplyCurrentStepBehavior();
    step_changed_   = true;
    status_changed_ = true;
}

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

void SequencerDevice::ApplyCurrentStepBehavior()
{
    RecalculateStepIntervalMs();

    const StepSlot& slot = GetStep(current_step_);
    const Pattern&  pat  = CurrentPattern();

    switch (slot.type)
    {
        case StepType::Chord:
        {
            // Probability check — roll 0-99, fire if less than probability
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

                // Log velocity
                std::cout
                    << "[VEL] step=" << current_step_
                    << "   velocity=" << static_cast<int>(slot.velocity)
                    << "   probability=" << static_cast<int>(slot.probability)
                    << "   roll=" << static_cast<int>(roll)
                    << "   FIRED"
                    << std::endl;
            }
            else
            {
                // Probability check failed — treat as rest
                GateOff();
                arp_.Init();
                arp_elapsed_ms_  = 0;
                arp_interval_ms_ = 0;

                std::cout
                    << "[VEL] step=" << current_step_
                    << "   probability=" << static_cast<int>(slot.probability)
                    << "   roll=" << static_cast<int>(roll)
                    << "   SKIPPED"
                    << std::endl;
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

// ── Step helpers ──────────────────────────────────────────────────────────────

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

void SequencerDevice::SetStepDurationMultiplier(uint32_t step_index, uint32_t multiplier)
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

// ── Display helpers ───────────────────────────────────────────────────────────

const char* SequencerDevice::StepTypeName(StepType type) const
{
    switch (type)
    {
        case StepType::Chord: return "Chord";
        case StepType::Rest:  return "Rest";
        case StepType::Skip:  return "Skip";
        case StepType::Empty: return "Empty";
        default:              return "Unknown";
    }
}

void SequencerDevice::PrintNoteMask(uint16_t note_mask) const
{
    static const char* kNoteNames[12] =
    {
        "C", "C#", "D", "D#", "E", "F",
        "F#", "G", "G#", "A", "A#", "B"
    };

    bool first = true;

    for (int i = 0; i < 12; ++i)
    {
        if ((note_mask & (1u << i)) != 0)
        {
            if (!first) std::cout << ",";
            std::cout << kNoteNames[i];
            first = false;
        }
    }

    if (first) std::cout << "(none)";

    
}

// ── MidiClockListener ─────────────────────────────────────────────────────────

void SequencerDevice::OnClockTick()
{
    ++midi_tick_count_;

    if (midi_tick_count_ >= midi::kPPQN)
    {
        midi_tick_count_ = 0;

        if (midi_clock_.GetMode() == midi::ClockMode::Slave && playing_)
        {
            elapsed_step_ms_ = current_step_interval_ms_;
        }
    }
}

void SequencerDevice::OnClockStart()
{
    if (midi_clock_.GetMode() == midi::ClockMode::Slave)
    {
        midi_tick_count_ = 0;
        Reset();
        Start();
        std::cout << "[SEQ] MIDI Start received" << std::endl;
    }
}

void SequencerDevice::OnClockStop()
{
    if (midi_clock_.GetMode() == midi::ClockMode::Slave)
    {
        Stop();
        std::cout << "[SEQ] MIDI Stop received" << std::endl;
    }
}

void SequencerDevice::OnClockContinue()
{
    if (midi_clock_.GetMode() == midi::ClockMode::Slave)
    {
        Start();
        std::cout << "[SEQ] MIDI Continue received" << std::endl;
    }
}