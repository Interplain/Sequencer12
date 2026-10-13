#pragma once
#include "core/device.h"
#include "devices/sequencer/sequencer_types.h"
#include "devices/sequencer/pattern_bank.h"
#include "devices/sequencer/arp_engine.h"
#include "devices/sequencer/chords/chord_library.h"
#include "devices/sequencer/chords/user_chords.h"
#include "platform/midi/midi_clock.h"
#include <cstdint>
#include <cstddef>

class SequencerDevice : public Device, public midi::MidiClockListener
{
public:
    void Init()     override;
    void Tick1ms()  override;
    void Tick5ms()  override;
    void Tick10ms() override;
    void Tick20ms() override;
    void Process()  override;
    uint8_t  GetCurrentPatternIndex() const;   /* edit/view target (sidebar, UI, persistence) */
    uint8_t  GetPlayPatternIndex() const;      /* chain-driven playback target */
    uint32_t GetRunTimeMs() const;
    uint32_t GetCompletedLoops() const;

    /* ── MidiClockListener ───────────────────────────────────────────── */
    void OnClockTick()     override;
    void OnClockStart()    override;
    void OnClockStop()     override;
    void OnClockContinue() override;

    /* ── Public transport ────────────────────────────────────────────── */
    void     Start();
    void     Stop();
    void     Reset();
    void     TickMusical();
    void     NotifyUClockMusicalCallback();
    void     ServicePendingMusicalTicks();
    bool     ServiceOnePendingStep();
    bool     ConsumeOutputSnapshotRequest(bool* forced_refresh = nullptr);
    void     CommitOutputSnapshot();
    void     DrainPendingStepEvents();
    bool     InitializePatternResidency();
    void     ServicePatternResidency();
    void     MarkEditPatternMutated();
    bool     GetEditStepSlot(uint8_t step_index, sequencer::StepSlot* out_step) const;
    bool     ReplaceEditStepSlot(uint8_t step_index, const sequencer::StepSlot& source);
    bool     IsPatternResidencyReady() const { return residency_ready_; }
    bool     IsPatternBoundaryHeld() const { return pattern_boundary_held_; }
    void     SetBpm(uint32_t bpm);
    uint32_t GetPendingStepEvents() const;
    uint32_t GetPendingMusicalTicks() const;
    uint32_t GetMaxPendingStepEvents() const;
    uint32_t GetPendingStepBacklogCount() const;
    uint32_t GetUClockMusicalCallbackCount() const;
    uint32_t GetTickMusicalCount() const;
    uint32_t GetPendingStepEnqueueCount() const;
    uint32_t GetServiceOneStepCount() const;
    void     SetPatternStepCount(uint8_t step_count);
    uint8_t  GetPatternStepCount() const;
    bool     SetPatternTiming(uint8_t step_division, uint8_t numerator, uint8_t denominator);
    /* Validates only the EDIT Pattern against the unchanged global time signature. */
    bool     SetPatternStepDivision(uint8_t step_division);
    uint8_t  GetPatternStepDivision() const;
    void     SetPatternArpMode(sequencer::ArpMode mode);
    void     SetPatternArpRate(sequencer::ArpRate rate);
    sequencer::ArpMode GetPatternArpMode() const;
    sequencer::ArpRate GetPatternArpRate() const;
    void     SetTimeSignature(uint8_t numerator, uint8_t denominator);
    uint8_t  GetTimeSigNumerator() const;
    uint8_t  GetTimeSigDenominator() const;
    void     SetSwing(uint8_t swing);
    uint8_t  GetSwing() const;
    void     SetStepChordParams(uint8_t step_index,
                                uint8_t root_key,
                                uint8_t chord_type,
                                uint8_t arp_pattern,
                                uint8_t duration,
                                uint8_t repeat_count);
    void     SetStepCustomNoteMask(uint8_t step_index, uint16_t note_mask);
    void     SetStepCustomUserChord(uint8_t step_index, uint16_t note_mask, const char* name);
    void     SetStepLedgerLength(uint8_t step_index, uint8_t length);
    void     SetStepLedgerSlot(uint8_t step_index, uint8_t slot_index, const sequencer::LedgerSlot& slot_notes);
    uint8_t  GetStepLedgerLength(uint8_t step_index) const;
    sequencer::LedgerSlot GetStepLedgerSlot(uint8_t step_index, uint8_t slot_index) const;
    uint8_t  IsStepEventStartCanonical(uint8_t step_index, uint8_t canonical_index) const;
    uint8_t  GetStepEventLengthCanonical(uint8_t step_index, uint8_t canonical_index) const;
    uint8_t  GetStepEventMaxLengthCanonical(uint8_t step_index, uint8_t canonical_index) const;
    void     SetStepEventLengthCanonical(uint8_t step_index, uint8_t canonical_index, uint8_t length_positions);
    uint8_t  IsStepEventStartGrid(uint8_t step_index, uint8_t slot_index) const;
    uint8_t  GetStepEventLengthGrid(uint8_t step_index, uint8_t slot_index) const;
    uint8_t  GetStepEventMaxLengthGrid(uint8_t step_index, uint8_t slot_index) const;
    void     SetStepEventLengthGrid(uint8_t step_index, uint8_t slot_index, uint8_t grid_length);
    void     ClearStepLedger(uint8_t step_index);
    void     SetStepBarState(uint8_t step_index, sequencer::BarState state);
    sequencer::BarState GetStepBarState(uint8_t step_index) const;
    bool     IsStepSkipped(uint8_t step_index) const;
    void     SetPatternRepeatCount(uint8_t repeat_count);
    uint8_t  GetPatternRepeatCount() const;
    void     SetCurrentPatternIndex(uint8_t pattern_index);
    void     SetPlaybackSource(sequencer::PlaybackSource source);
    sequencer::PlaybackSource GetPlaybackSource() const;
    void     SetChainLength(uint8_t length);
    uint8_t  GetChainLength() const;
    void     SetChainPatternAt(uint8_t pos, uint8_t pattern_index);
    uint8_t  GetChainPatternAt(uint8_t pos) const;
    uint8_t  GetChainCurrentPosition() const;
    uint8_t  GetCurrentPatternRepeatProgress() const;
    uint8_t  GetCurrentStepSubIndex() const;
    uint16_t GetStepNoteMask(uint8_t step_index) const;
    uint16_t GetStepNoteMaskForPlayback(uint8_t step_index) const;
    uint16_t GetCurrentStepNoteMaskForPlayback() const;
    uint8_t  GetStepNotesForPlayback(uint8_t step_index, uint8_t slot_index, uint8_t out_notes[4]) const;
    uint8_t  GetCurrentStepNotesForPlayback(uint8_t out_notes[4]) const;
    /* Lane-aware siblings used only by ARP Block/Off-mode CV/gate routing; ARP Up/Down/UpDn/
     * Random/AsPlayed continue to use the plain accessors above and are unaffected. out_lanes
     * values are 0=A,1=B,2=C,3=D, falling back to sorted array index when no explicit lane
     * assignment exists for that ledger position (today's behaviour). */
    uint8_t  GetStepNotesForPlaybackWithLanes(uint8_t step_index, uint8_t slot_index, uint8_t out_notes[4], uint8_t out_lanes[4]) const;
    uint8_t  GetCurrentStepNotesForPlaybackWithLanes(uint8_t out_notes[4], uint8_t out_lanes[4]) const;
    void     ExportSong(sequencer::Song* out_song) const;
    void     ImportSong(const sequencer::Song& song);
    const sequencer::Song& GetSongView() const;
    sequencer::Song* GetMutableSongForImport();
    void     FinalizeImportedSong();
    bool     GetStepChordUiParams(uint8_t step_index,
                                  uint8_t* root_key,
                                  uint8_t* chord_type,
                                  uint8_t* duration,
                                  uint8_t* repeat_count) const;
    bool     GetStepCustomChordName(uint8_t step_index, char* buf, size_t buf_len) const;
    uint32_t GetCurrentStep() const { return current_bar_; }
    uint32_t GetElapsedMs()   const { return elapsed_step_ms_; }
    bool     IsPlaying()      const { return playing_; }
    bool     IsGateActive()   const { return gate_active_; }
    bool     IsGateActivationPending() const { return gate_activation_pending_; }
    sequencer::ArpMode GetCurrentArpMode() const;
    /* Returns the absolute MIDI note (0-127) of the current arp note, or 0xFF if no note. */
    uint8_t  GetCurrentNote() const { return arp_.CurrentNote(); }
    /* Consume a pending CV/gate event. Returns true if note+gate state changed.
     * note is absolute MIDI when valid. gate is true when gate is active.
     * Clears the dirty flag atomically — call once per Process() cycle. */
    bool     ConsumeCvEvent(uint8_t* note, bool* gate);
private:
    void RecalculateStepIntervalMs();
    void RecalculateStepTicks();
    uint8_t ResolveEventNotesForPlayback(uint8_t step_index, uint8_t slot_index, uint8_t out_notes[4]) const;
    uint8_t ResolveEventNotesForPlaybackWithLanes(uint8_t step_index, uint8_t slot_index, uint8_t out_notes[4], uint8_t out_lanes[4]) const;
    void AdvanceStep();
    void GateOn();
    void GateOff();
    void RetriggerGate(uint32_t low_gap_ms = 1u);
    void ApplyCurrentStepBehavior();
    bool PrepareNextPattern();
    bool ResumeHeldPatternBoundary();
    bool PromotePreparedNext();
    bool ResolveNextPattern(uint8_t* out_pattern_index,
                            uint8_t* out_chain_position,
                            uint32_t* out_skipped_loops,
                            bool* out_stops_transport) const;
    bool EnsurePlayPattern(uint8_t pattern_index);

    const char* StepTypeName(sequencer::StepType type) const;
    static sequencer::BarState LegacyStepTypeToBarState(sequencer::StepType type);
    void        PrintNoteMask(uint16_t note_mask) const;
    uint16_t    ApplyTranspose(uint16_t note_mask) const;

    /* ── Step helpers ────────────────────────────────────────────────── */
    void                       SetStepType(uint32_t step_index, sequencer::StepType type);
    void                       SetStepDurationMultiplier(uint32_t step_index, uint32_t multiplier);
    void                       ToggleStepNote(uint32_t step_index, uint8_t note);
    void                       ClearStepNotes(uint32_t step_index);
    const sequencer::StepSlot& GetStep(uint32_t step_index) const;

    /* ── Pattern accessors ───────────────────────────────────────────── */
    /* Edit target: what the UI views/edits. Never consulted by playback. */
    sequencer::Pattern&        EditPattern();
    const sequencer::Pattern&  EditPattern() const;
    /* Play target: driven by PatternChain. Read from the uClock ISR. */
    sequencer::Pattern&        PlayPattern();
    const sequencer::Pattern&  PlayPattern() const;
 
private:
    /* ── Core ────────────────────────────────────────────────────────── */
    sequencer::PatternBank  bank_{};
    sequencer::ArpEngine    arp_{};
    midi::MidiClock         midi_clock_{};

    /* ── Transport ───────────────────────────────────────────────────── */
    bool        playing_                  = false;
    bool        gate_active_              = false;
    sequencer::PlaybackSource playback_source_ = sequencer::PlaybackSource::Pattern;

    /* ── Step engine ─────────────────────────────────────────────────── */
    uint8_t     edit_pattern_index_       = 0;  /* UI edit/view target only */
    uint8_t     play_pattern_index_       = 0;  /* chain-driven playback target */
    uint32_t    current_bar_              = 0;
    uint8_t     current_step_in_bar_      = 0;
    int8_t      step_direction_           = 1;
    uint8_t     repeat_current_           = 0;

    uint32_t    elapsed_step_ms_          = 0;
    uint32_t    base_step_interval_ms_    = 500;
    uint32_t    current_step_interval_ms_ = 500;

    /* ── uClock musical step clock diagnostics ───────────────────────── */
    uint32_t    uclock_musical_callback_count_ = 0;
    uint32_t    tick_musical_count_            = 0;
    uint32_t    pending_step_enqueue_count_    = 0;
    uint32_t    service_one_step_count_        = 0;
    uint32_t    musical_ticks_accum_           = 0;
    uint32_t    musical_step_ticks_            = 96;
    uint32_t    pending_step_events_           = 0;
    uint32_t    pending_musical_ticks_          = 0;
    uint32_t    max_pending_step_events_       = 0;
    uint32_t    pending_step_backlog_count_    = 0;

    /* ── Gate ────────────────────────────────────────────────────────── */
    uint32_t    gate_elapsed_ms_          = 0;
    uint32_t    gate_length_ms_           = 100;
    bool        gate_retrigger_pending_   = false;
    uint32_t    gate_retrigger_delay_ms_  = 0;
    bool        gate_activation_pending_  = false;

    /* ── Arp ─────────────────────────────────────────────────────────── */
    uint32_t    arp_ticks_accum_          = 0;
    uint32_t    arp_event_ticks_          = 0;

    /* ── Dirty flags ─────────────────────────────────────────────────── */
    bool        step_changed_             = false;
    bool        status_changed_           = false;
    bool        gate_changed_             = false;
    bool        arp_note_changed_         = false;
    bool        output_snapshot_pending_  = false;
    bool        output_snapshot_forced_   = false;
    bool        residency_ready_          = false;
    bool        pattern_boundary_held_    = false;
    bool        next_prepare_needed_      = false;
    uint8_t     requested_next_pattern_   = 0xFFu;
    uint32_t    requested_next_generation_ = 0u;
    uint8_t     requested_next_chain_position_ = 0u;
    uint32_t    requested_next_skipped_loops_ = 0u;
    bool        requested_next_stops_transport_ = false;
    uint8_t     pending_edit_pattern_index_ = 0xFFu;

    /* ── Timing ──────────────────────────────────────────────────────── */
    uint32_t run_time_ms_    = 0;
    uint32_t completed_loops_ = 0; 
    
    /* ── MIDI clock ──────────────────────────────────────────────────── */
    uint8_t     midi_tick_count_          = 0;
    bool        midi_clock_enabled_       = false;
};