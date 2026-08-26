#ifndef S12_PITCH_MAPPING_H
#define S12_PITCH_MAPPING_H

/* Shared S12 pitch mapping (Option B): C0..C8 maps to -2..6V with C2 at 0V. */
enum
{
    kPitchMidiMin = 12,
    kPitchMidiMax = 108,
    kPitchZeroVoltMidi = 36,
    kLegacyPitchClassFallbackMidiBase = 60,
    kArpReconstructedMidiBase = 60
};

#define S12_PITCH_VOLTS_PER_SEMITONE (1.0f / 12.0f)

#endif /* S12_PITCH_MAPPING_H */