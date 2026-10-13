#ifndef SEQUENCER_OUTPUT_SNAPSHOT_H
#define SEQUENCER_OUTPUT_SNAPSHOT_H

#include <cstdint>

class SequencerDevice;

struct SequencerOutputSnapshotState
{
    uint32_t last_step_index = 0xFFFFFFFFu;
    uint16_t last_step_mask = 0xFFFFu;
    uint8_t last_arp_note = 0xFFu;
    uint8_t last_substep_index = 0xFFu;
    volatile uint8_t gate_channel_mask = 0u;
};

struct SequencerOutputSnapshotOps
{
    void* context;
    uint16_t (*pitch_to_code)(void* context, uint8_t lane, float volts);
    void (*write_lanes)(void* context, const uint16_t values[4]);
};

bool SequencerOutput_WriteSnapshot(SequencerDevice& sequencer,
                                   SequencerOutputSnapshotState& state,
                                   const SequencerOutputSnapshotOps& ops,
                                   const uint16_t zero_codes[4],
                                   bool unison_mode,
                                   bool force_refresh);

#endif