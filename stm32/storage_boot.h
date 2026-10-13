#ifndef STORAGE_BOOT_H
#define STORAGE_BOOT_H

#include "devices/sequencer/sequencer_device.h"
#include "platform/fram/persistence_store.h"

/* Production storage startup. Pattern residency is initialised only after
 * PersistenceStore_Bootstrap() has validated the schema and resolved the
 * PatternStore journal. Never factory-initialises blank or invalid storage.
 * Returns PERSISTENCE_BOOT_READY only when residency is running. */
PersistenceBootStatus StorageBoot_ActivateRuntime(SequencerDevice& device,
                                                  uint32_t* out_bpm,
                                                  uint32_t* out_song_header_generation);

#endif
