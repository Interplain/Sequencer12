#include "stm32/storage_boot.h"
#include "platform/fram/pattern_residency.h"
#include <cstring>

PersistenceBootStatus StorageBoot_ActivateRuntime(SequencerDevice& device,
                                                  uint32_t* out_bpm,
                                                  uint32_t* out_song_header_generation)
{
    PatternResidency_Init();
    device.Init();

    const PersistenceBootStatus storage = PersistenceStore_Bootstrap();
    if (storage != PERSISTENCE_BOOT_READY) return storage;

    PersistentSongHeaderData header{};
    uint32_t generation = 0u;
    if (PersistenceStore_SelectSongHeader(&header, &generation) != SONG_HEADER_SELECT_VALID)
        return PERSISTENCE_BOOT_SONG_HEADER_INTEGRITY_FAULT;

    sequencer::Song* song = device.GetMutableSongForImport();
    song->global_bpm = header.global_bpm;
    song->key_root = (sequencer::KeyRoot)header.key_root;
    song->key_scale = (sequencer::KeyScale)header.key_scale;
    song->time_sig.numerator = header.time_sig_numerator;
    song->time_sig.denominator = header.time_sig_denominator;
    song->swing = header.swing;
    song->transpose = header.transpose;
    song->chain.length = header.chain_length;
    song->chain.position = 0u;
    memcpy(song->chain.pattern_indices.data(), header.pattern_indices,
           sizeof(header.pattern_indices));

    if (!device.InitializePatternResidency()) return PERSISTENCE_BOOT_PATTERN_INTEGRITY_FAULT;
    /* SetBpm() derives step timing from the PLAY Pattern, so it needs residency first. */
    device.SetBpm(header.global_bpm);

    if (out_bpm) *out_bpm = header.global_bpm;
    if (out_song_header_generation) *out_song_header_generation = generation;
    return PERSISTENCE_BOOT_READY;
}
