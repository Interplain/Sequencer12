#ifndef PERSISTENCE_STORE_H
#define PERSISTENCE_STORE_H

#include "devices/sequencer/sequencer_types.h"
#include "platform/fram/pattern_store.h"
#include <cstdint>

/* Stage 2 clean-schema metadata immediately follows the Stage 1 journal. */
#define FRAM_SCHEMA_RECORD_A_ADDR      FRAM_PATTERNSTORE_JOURNAL_END
#define FRAM_SCHEMA_RECORD_SIZE        0x20u
#define FRAM_SCHEMA_RECORD_B_ADDR      (FRAM_SCHEMA_RECORD_A_ADDR + FRAM_SCHEMA_RECORD_SIZE)
#define FRAM_SONG_HEADER_A_ADDR        (FRAM_SCHEMA_RECORD_B_ADDR + FRAM_SCHEMA_RECORD_SIZE)
#define FRAM_SONG_HEADER_RECORD_SIZE   0x40u
#define FRAM_SONG_HEADER_B_ADDR        (FRAM_SONG_HEADER_A_ADDR + FRAM_SONG_HEADER_RECORD_SIZE)
#define FRAM_PERSISTENCE_METADATA_END  (FRAM_SONG_HEADER_B_ADDR + FRAM_SONG_HEADER_RECORD_SIZE)

struct PersistentSongHeaderData
{
    uint32_t global_bpm;
    uint8_t key_root;
    uint8_t key_scale;
    uint8_t time_sig_numerator;
    uint8_t time_sig_denominator;
    uint8_t swing;
    int8_t transpose;
    uint8_t chain_length;
    uint8_t reserved;
    uint8_t pattern_indices[sequencer::kChainLength];
};

typedef enum
{
    PERSISTENCE_BOOT_NOT_RUN = 0,
    PERSISTENCE_BOOT_READY,
    PERSISTENCE_BOOT_INITIALIZATION_IN_PROGRESS,
    PERSISTENCE_BOOT_FRAM_NOT_READY,
    PERSISTENCE_BOOT_IO_FAULT,
    PERSISTENCE_BOOT_UNSUPPORTED_SCHEMA,
    PERSISTENCE_BOOT_SCHEMA_INTEGRITY_FAULT,
    PERSISTENCE_BOOT_PATTERN_INTEGRITY_FAULT,
    PERSISTENCE_BOOT_SONG_HEADER_INTEGRITY_FAULT,
    PERSISTENCE_BOOT_FACTORY_REQUIRED,
} PersistenceBootStatus;

typedef enum
{
    PERSISTENCE_FACTORY_IDLE = 0,
    PERSISTENCE_FACTORY_FORMATTING_MARKER,
    PERSISTENCE_FACTORY_INVALIDATING_JOURNAL,
    PERSISTENCE_FACTORY_INVALIDATING_PATTERNS,
    PERSISTENCE_FACTORY_INVALIDATING_SONG,
    PERSISTENCE_FACTORY_INITIALIZING_MARKER,
    PERSISTENCE_FACTORY_WRITING_PATTERNS,
    PERSISTENCE_FACTORY_WRITING_SONG,
    PERSISTENCE_FACTORY_VALIDATING,
    PERSISTENCE_FACTORY_WRITING_READY,
    PERSISTENCE_FACTORY_COMPLETE,
    PERSISTENCE_FACTORY_FAILED,
} PersistenceFactoryPhase;

typedef void (*PersistenceFactoryProgressCallback)(PersistenceFactoryPhase phase,
                                                   uint8_t index,
                                                   uint8_t total);

typedef enum
{
    SONG_HEADER_SELECT_NONE = 0,
    SONG_HEADER_SELECT_VALID,
    SONG_HEADER_SELECT_INTEGRITY_FAULT,
    SONG_HEADER_SELECT_IO_FAULT,
} SongHeaderSelectStatus;

/* Blocking boot/storage operation. It is called before sequencer and UI
 * initialization and never from interrupt or pattern-boundary context. */
PersistenceBootStatus PersistenceStore_Bootstrap(void);
PersistenceBootStatus PersistenceStore_GetBootStatus(void);
PersistenceBootStatus PersistenceStore_Inspect(void);
PersistenceBootStatus PersistenceStore_FactoryInitialize(
    PersistenceFactoryProgressCallback progress);

SongHeaderSelectStatus PersistenceStore_SelectSongHeader(PersistentSongHeaderData* out_data,
                                                         uint32_t* out_generation);
uint8_t PersistenceStore_WriteSongHeader(const PersistentSongHeaderData* data,
                                         uint32_t generation);
void PersistenceStore_DefaultSongHeader(PersistentSongHeaderData* out_data);

#if defined(PATTERN_STORE_HOST_TEST)
void PersistenceStore_ResetRuntimeStateForTest(void);
#endif

#endif