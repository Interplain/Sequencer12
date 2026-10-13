#ifndef PATTERN_RESIDENCY_H
#define PATTERN_RESIDENCY_H

#include "devices/sequencer/sequencer_types.h"
#include <cstdint>

#define PATTERN_RESIDENCY_CACHE_ENTRY_COUNT 3u
#define PATTERN_RESIDENCY_INVALID_ENTRY     0xFFu
#define PATTERN_RESIDENCY_INVALID_PATTERN   0xFFu

typedef enum
{
    PATTERN_DIRECTORY_UNKNOWN = 0,
    PATTERN_DIRECTORY_VALID,
    PATTERN_DIRECTORY_INVALID,
} PatternDirectoryStatus;

struct PatternDirectoryEntry
{
    uint32_t generation;
    uint16_t playable_step_mask;
    uint8_t packed_config;
    uint8_t status;
};

typedef enum
{
    PATTERN_CACHE_EMPTY = 0,
    PATTERN_CACHE_LOADING,
    PATTERN_CACHE_READY,
    PATTERN_CACHE_FAULT,
} PatternCacheResidencyState;

typedef enum
{
    PATTERN_CACHE_IO_IDLE = 0,
    PATTERN_CACHE_IO_READING,
    PATTERN_CACHE_IO_WRITING,
} PatternCacheIoState;

typedef enum
{
    PATTERN_CACHE_ERROR_NONE = 0,
    PATTERN_CACHE_ERROR_READ,
    PATTERN_CACHE_ERROR_WRITE,
    PATTERN_CACHE_ERROR_STALE_LOAD,
} PatternCacheError;

struct PatternCacheEntry
{
    sequencer::Pattern pattern;
    uint32_t current_generation;
    uint32_t persisted_generation;
    uint32_t io_generation;
    uint8_t pattern_index;
    uint8_t requested_pattern_index;
    uint8_t residency_state;
    uint8_t io_state;
    uint8_t error;
    uint8_t reserved;
};

enum class PatternCacheRole : uint8_t
{
    Play = 0,
    Next,
    Edit,
};

struct PatternRoleState
{
    uint8_t play_entry;
    uint8_t next_entry;
    uint8_t edit_entry;
};

typedef enum
{
    PATTERN_CACHE_REQUEST_READY = 0,
    PATTERN_CACHE_REQUEST_LOAD_STARTED,
    PATTERN_CACHE_REQUEST_WAIT,
    PATTERN_CACHE_REQUEST_FAILED,
} PatternCacheRequestStatus;

typedef enum
{
    PATTERN_CACHE_SERVICE_IDLE = 0,
    PATTERN_CACHE_SERVICE_BUSY,
    PATTERN_CACHE_SERVICE_DONE,
    PATTERN_CACHE_SERVICE_FAILED,
    PATTERN_CACHE_SERVICE_STALE,
} PatternCacheServiceStatus;

/* Boot/main-loop infrastructure only. No function in this API is ISR-safe. */
void PatternResidency_Init(void);
/* Synchronous full scan; tests/offline tools only, never interactive paths. */
uint8_t PatternResidency_RebuildDirectory(void);

const PatternDirectoryEntry* PatternResidency_GetDirectory(void);
/* Non-blocking: an entry not yet read since boot stays PATTERN_DIRECTORY_UNKNOWN. */
const PatternDirectoryEntry* PatternResidency_GetDirectoryEntry(uint8_t pattern_index);
uint8_t PatternDirectory_GetStepDivision(const PatternDirectoryEntry* entry);
sequencer::PlaybackMode PatternDirectory_GetPlaybackMode(const PatternDirectoryEntry* entry);
uint8_t PatternDirectory_GetRepeatCount(const PatternDirectoryEntry* entry);
uint8_t PatternDirectory_IsUsable(const PatternDirectoryEntry* entry);

const PatternCacheEntry* PatternResidency_GetCacheEntry(uint8_t entry_index);
sequencer::Pattern* PatternResidency_GetMutablePattern(uint8_t entry_index);
const PatternRoleState* PatternResidency_GetRoles(void);
int8_t PatternResidency_FindResidentEntry(uint8_t pattern_index, uint32_t generation);
int8_t PatternResidency_GetRoleEntry(PatternCacheRole role);
sequencer::Pattern* PatternResidency_GetRolePattern(PatternCacheRole role);
const sequencer::Pattern* PatternResidency_GetRolePatternConst(PatternCacheRole role);

uint8_t PatternResidency_AssignRole(PatternCacheRole role, uint8_t entry_index);
uint8_t PatternResidency_ClearRole(PatternCacheRole role);
uint8_t PatternResidency_CancelPendingRole(PatternCacheRole role);
uint8_t PatternResidency_CancelPendingLoad(PatternCacheRole role);
uint8_t PatternResidency_IsPinned(uint8_t entry_index);
int8_t PatternResidency_FindEvictableEntry(void);

PatternCacheRequestStatus PatternResidency_RequestRole(PatternCacheRole role,
                                                       uint8_t pattern_index,
                                                       uint32_t expected_generation);
PatternCacheRequestStatus PatternResidency_RequestEdit(uint8_t pattern_index,
                                                       uint32_t expected_generation);
/* expected_generation 0 = use the directory generation, or discover it by an
 * async CRC-checked load when the directory entry is still unknown. */
uint8_t PatternResidency_RequestLoad(uint8_t entry_index,
                                     uint8_t pattern_index,
                                     uint32_t expected_generation);
/* Role-less async load whose only purpose is to resolve an unknown directory entry. */
PatternCacheRequestStatus PatternResidency_RequestDirectoryDiscovery(uint8_t pattern_index);
/* Starts at most one background discovery when residency I/O is idle and nothing is dirty. */
uint8_t PatternResidency_ServiceDirectoryWarm(void);
uint8_t PatternResidency_BeginWriteback(uint8_t entry_index);
PatternCacheServiceStatus PatternResidency_ServiceIo(void);
uint8_t PatternResidency_BeginAnyDirtyWriteback(void);

uint8_t PatternResidency_MarkMutated(uint8_t entry_index);
uint8_t PatternResidency_IsDirty(uint8_t entry_index);
uint8_t PatternResidency_UpdateDirectoryFromCache(uint8_t entry_index);
uint8_t PatternResidency_ApplyLazyProjections(uint8_t entry_index,
                                              const sequencer::TimeSig* time_sig);
uint8_t PatternResidency_GetLastRoleLoadError(PatternCacheRole role);

#if defined(PATTERN_STORE_HOST_TEST)
void PatternResidency_ResetForTest(void);
uint32_t PatternResidency_GetDirectoryLookupCountForTest(void);
#endif

#endif