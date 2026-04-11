#pragma once
#include "core/device.h"
#include "devices/sequencer/sequencer_types.h"
#include "devices/sequencer/pattern_bank.h"
#include "devices/sequencer/arp_engine.h"
#include "devices/sequencer/chords/chord_library.h"
#include "devices/sequencer/chords/user_chords.h"
#include "platform/midi/midi_clock.h"
#include <cstdint>

class SequencerDevice : public Device, public midi::MidiClockListener
{
public:
    void Init()     override;
    void Tick1ms()  override;
    void Tick5ms()  override;
    void Tick10ms() override;
    void Tick20ms() override;
    void Process()  override;

    // ── MidiClockListener ─────────────────────────────────────────────────────
    void OnClockTick()     override;
    void OnClockStart()    override;
    void OnClockStop()     override;
    void OnClockContinue() override;

    // ── Public transport ──────────────────────────────────────────────────────
    void Start();
    void Stop();
    void Reset();

private:
    void RecalculateStepIntervalMs();
    void AdvanceStep();
    void GateOn();
    void GateOff();
    void ApplyCurrentStepBehavior();

    const char* StepTypeName(sequencer::StepType type) const;
    void        PrintNoteMask(uint16_t note_mask) const;
    uint16_t    ApplyTranspose(uint16_t note_mask) const;

    // Step helpers — operate on current pattern
    void                        SetStepType(uint32_t step_index, sequencer::StepType type);
    void                        SetStepDurationMultiplier(uint32_t step_index, uint32_t multiplier);
    void                        ToggleStepNote(uint32_t step_index, uint8_t note);
    void                        ClearStepNotes(uint32_t step_index);
    const sequencer::StepSlot&  GetStep(uint32_t step_index) const;

    // Convenience — returns current pattern's steps
    sequencer::Pattern&         CurrentPattern();
    const sequencer::Pattern&   CurrentPattern() const;

private:
    // ── Core ──────────────────────────────────────────────────────────────────
    sequencer::PatternBank  bank_{};
    sequencer::UserChords   user_chords_{};
    sequencer::ArpEngine    arp_{};
    midi::MidiClock         midi_clock_{};

    // ── Transport ─────────────────────────────────────────────────────────────
    bool        playing_                  = false;
    bool        gate_active_              = false;

    // ── Step engine ───────────────────────────────────────────────────────────
    uint8_t     current_pattern_index_    = 0;
    uint32_t    current_step_             = 0;
    int8_t      step_direction_           = 1;
    uint8_t     repeat_current_           = 0;

    uint32_t    elapsed_step_ms_          = 0;
    uint32_t    base_step_interval_ms_    = 500;
    uint32_t    current_step_interval_ms_ = 500;

    // ── Gate ──────────────────────────────────────────────────────────────────
    uint32_t    gate_elapsed_ms_          = 0;
    uint32_t    gate_length_ms_           = 100;

    // ── Arp ───────────────────────────────────────────────────────────────────
    uint32_t    arp_elapsed_ms_           = 0;
    uint32_t    arp_interval_ms_          = 0;

    // ── Dirty flags ───────────────────────────────────────────────────────────
    bool        step_changed_             = false;
    bool        status_changed_           = false;
    bool        gate_changed_             = false;
    bool        arp_note_changed_         = false;

    // ── Timing ────────────────────────────────────────────────────────────────
    uint32_t    runtime_ms_               = 0;
    uint8_t     demo_phase_               = 0;

    // ── MIDI clock ────────────────────────────────────────────────────────────
    uint8_t     midi_tick_count_          = 0;
    bool        midi_clock_enabled_       = false;
};