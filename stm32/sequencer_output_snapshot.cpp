#include "stm32/sequencer_output_snapshot.h"
#include "devices/sequencer/sequencer_device.h"
#include "core/pitch_mapping.h"

namespace
{

float NoteToPitchVolts(uint8_t note)
{
    return (static_cast<int32_t>(note) - kPitchZeroVoltMidi) * S12_PITCH_VOLTS_PER_SEMITONE;
}

} // namespace

bool SequencerOutput_WriteSnapshot(SequencerDevice& sequencer,
                                   SequencerOutputSnapshotState& state,
                                   const SequencerOutputSnapshotOps& ops,
                                   const uint16_t zero_codes[4],
                                   bool unison_mode,
                                   bool force_refresh)
{
    if (!sequencer.IsPlaying() || !ops.pitch_to_code || !ops.write_lanes || !zero_codes)
    {
        state.gate_channel_mask = 0u;
        return false;
    }

    const uint32_t step_index = sequencer.GetCurrentStep();
    const uint8_t substep = sequencer.GetCurrentStepSubIndex();
    uint8_t step_notes[4] = {sequencer::kMidiNoteNone,
                             sequencer::kMidiNoteNone,
                             sequencer::kMidiNoteNone,
                             sequencer::kMidiNoteNone};
    const uint8_t step_note_count = sequencer.GetCurrentStepNotesForPlayback(step_notes);
    uint16_t note_mask = 0u;
    for (uint8_t i = 0u; i < step_note_count; ++i)
    {
        const uint8_t note = step_notes[i];
        if (note != sequencer::kMidiNoteNone)
            note_mask |= (uint16_t)(1u << (note % 12u));
    }

    if (sequencer.GetCurrentArpMode() == sequencer::ArpMode::Off)
    {
        if (!force_refresh && step_index == state.last_step_index &&
            substep == state.last_substep_index && note_mask == state.last_step_mask)
        {
            sequencer.CommitOutputSnapshot();
            return false;
        }

        uint8_t notes[4] = {sequencer::kMidiNoteNone,
                            sequencer::kMidiNoteNone,
                            sequencer::kMidiNoteNone,
                            sequencer::kMidiNoteNone};
        uint8_t lanes[4] = {0u, 1u, 2u, 3u};
        const uint8_t note_count = sequencer.GetCurrentStepNotesForPlaybackWithLanes(notes, lanes);
        uint16_t out[4] = {zero_codes[0], zero_codes[1], zero_codes[2], zero_codes[3]};

        if (unison_mode)
        {
            if (note_count > 0u)
            {
                const float volts = NoteToPitchVolts(notes[0]);
                for (uint8_t lane = 0u; lane < 4u; ++lane)
                    out[lane] = ops.pitch_to_code(ops.context, lane, volts);
                ops.write_lanes(ops.context, out);
            }
            state.gate_channel_mask = note_count > 0u ? 0x0Fu : 0u;
        }
        else
        {
            uint8_t active_lane_mask = 0u;
            for (uint8_t i = 0u; i < note_count; ++i)
            {
                if (!sequencer::IsMidiNoteValid(notes[i])) continue;
                const uint8_t lane = lanes[i] < 4u ? lanes[i] : i;
                out[lane] = ops.pitch_to_code(ops.context, lane, NoteToPitchVolts(notes[i]));
                active_lane_mask = (uint8_t)(active_lane_mask | (uint8_t)(1u << lane));
            }
            if (active_lane_mask != 0u) ops.write_lanes(ops.context, out);
            state.gate_channel_mask = active_lane_mask;
        }

        state.last_step_index = step_index;
        state.last_substep_index = substep;
        state.last_step_mask = note_mask;
        state.last_arp_note = 0xFFu;
    }
    else
    {
        const uint8_t note = sequencer.GetCurrentNote();
        if (!force_refresh && step_index == state.last_step_index && note == state.last_arp_note)
        {
            sequencer.CommitOutputSnapshot();
            return false;
        }

        uint16_t out[4] = {zero_codes[0], zero_codes[1], zero_codes[2], zero_codes[3]};
        if (sequencer::IsMidiNoteValid(note))
        {
            out[0] = ops.pitch_to_code(ops.context, 0u, NoteToPitchVolts(note));
            state.gate_channel_mask = 0x01u;
        }
        else
        {
            state.gate_channel_mask = 0u;
        }
        ops.write_lanes(ops.context, out);
        state.last_step_index = step_index;
        state.last_step_mask = note_mask;
        state.last_arp_note = note;
    }

    sequencer.CommitOutputSnapshot();
    return true;
}