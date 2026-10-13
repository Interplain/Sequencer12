#include "platform/fram/pattern_residency.h"
#include "platform/fram/pattern_store.h"
#include "platform/fram/storage_boot_probe.h"
#include "stm32/time_commit_probe.h"
#include <cstring>

namespace
{

constexpr uint8_t kConfigDivisionMask = 0x03u;
constexpr uint8_t kConfigModeMask = 0x0Cu;
constexpr uint8_t kConfigRepeatMask = 0xF0u;

PatternDirectoryEntry s_directory[sequencer::kPatternCount];
sequencer::Pattern s_directory_scratch;
PatternCacheEntry s_cache[PATTERN_RESIDENCY_CACHE_ENTRY_COUNT];
PatternRoleState s_roles;
uint8_t s_active_io_entry = PATTERN_RESIDENCY_INVALID_ENTRY;
/* The active read was started without a known directory generation. */
uint8_t s_active_io_discovery = 0u;
uint8_t s_pending_role_entry = PATTERN_RESIDENCY_INVALID_ENTRY;
uint8_t s_pending_role = PATTERN_RESIDENCY_INVALID_ENTRY;
uint8_t s_last_role_load_error[3] = {0u, 0u, 0u};
#if defined(PATTERN_STORE_HOST_TEST)
uint32_t s_directory_lookup_count_for_test = 0u;
#endif

uint8_t DivisionCode(uint8_t division)
{
    switch (division)
    {
        case 1u: return 0u;
        case 2u: return 1u;
        case 4u: return 2u;
        case 8u: return 3u;
        default: return 0xFFu;
    }
}

uint8_t DivisionFromCode(uint8_t code)
{
    return (uint8_t)(1u << (code & kConfigDivisionMask));
}

bool DeriveDirectoryEntry(const sequencer::Pattern& pattern,
                          uint32_t generation,
                          PatternDirectoryEntry* out_entry)
{
    if (!out_entry || generation == 0u) return false;
    const uint8_t division_code = DivisionCode(pattern.step_division);
    if (division_code == 0xFFu) return false;
    if ((uint8_t)pattern.playback_mode > (uint8_t)sequencer::PlaybackMode::OneShot) return false;

    uint8_t step_count = pattern.step_count;
    if (step_count == 0u || step_count > sequencer::kStepCount) step_count = sequencer::kStepCount;
    uint16_t playable_mask = 0u;
    for (uint8_t step = 0u; step < step_count; ++step)
    {
        if (pattern.steps[step].bar_state != sequencer::BarState::Skip)
            playable_mask |= (uint16_t)(1u << step);
    }

    uint8_t repeat = pattern.repeat_count;
    if (repeat < 1u) repeat = 1u;
    if (repeat > 16u) repeat = 16u;

    out_entry->generation = generation;
    out_entry->playable_step_mask = playable_mask;
    out_entry->packed_config = (uint8_t)(division_code |
        (((uint8_t)pattern.playback_mode & 0x03u) << 2u) |
        ((uint8_t)(repeat - 1u) << 4u));
    out_entry->status = PATTERN_DIRECTORY_VALID;
    return true;
}

uint8_t PopulateDirectoryEntry(uint8_t pattern_index)
{
    if (pattern_index >= sequencer::kPatternCount) return 0u;
    PatternDirectoryEntry& entry = s_directory[pattern_index];
    if (entry.status != PATTERN_DIRECTORY_UNKNOWN)
        return entry.status == PATTERN_DIRECTORY_VALID ? 1u : 0u;

    PROBE_READ_SOURCE(READ_SOURCE_DIRECTORY_POPULATE);
    uint32_t generation = 0u;
    if (PatternStore_ReadWithGeneration(pattern_index, &s_directory_scratch, &generation) &&
        DeriveDirectoryEntry(s_directory_scratch, generation, &entry)) return 1u;

    entry = PatternDirectoryEntry{};
    entry.status = PATTERN_DIRECTORY_INVALID;
    return 0u;
}

void MarkDirectoryInvalid(uint8_t pattern_index)
{
    if (pattern_index >= sequencer::kPatternCount) return;
    s_directory[pattern_index] = PatternDirectoryEntry{};
    s_directory[pattern_index].status = PATTERN_DIRECTORY_INVALID;
}

uint8_t* RolePointer(PatternCacheRole role)
{
    switch (role)
    {
        case PatternCacheRole::Play: return &s_roles.play_entry;
        case PatternCacheRole::Next: return &s_roles.next_entry;
        case PatternCacheRole::Edit: return &s_roles.edit_entry;
    }
    return nullptr;
}

bool EntryReferenced(uint8_t entry_index)
{
    return s_roles.play_entry == entry_index ||
           s_roles.next_entry == entry_index ||
           s_roles.edit_entry == entry_index;
}

void ResetEntryMetadata(PatternCacheEntry* entry)
{
    entry->current_generation = 0u;
    entry->persisted_generation = 0u;
    entry->io_generation = 0u;
    entry->pattern_index = PATTERN_RESIDENCY_INVALID_PATTERN;
    entry->requested_pattern_index = PATTERN_RESIDENCY_INVALID_PATTERN;
    entry->residency_state = PATTERN_CACHE_EMPTY;
    entry->io_state = PATTERN_CACHE_IO_IDLE;
    entry->error = PATTERN_CACHE_ERROR_NONE;
    entry->reserved = 0u;
}

int8_t FindResident(uint8_t pattern_index, uint32_t generation)
{
    for (uint8_t i = 0u; i < PATTERN_RESIDENCY_CACHE_ENTRY_COUNT; ++i)
    {
        const PatternCacheEntry& entry = s_cache[i];
        if (entry.residency_state == PATTERN_CACHE_READY &&
            entry.pattern_index == pattern_index &&
            entry.current_generation == generation) return (int8_t)i;
    }
    return -1;
}

uint8_t StepsPerBar(const sequencer::TimeSig& time_sig, uint8_t division)
{
    if (DivisionCode(division) == 0xFFu || time_sig.numerator == 0u ||
        !(time_sig.denominator == 2u || time_sig.denominator == 4u || time_sig.denominator == 8u)) return 0u;
    const uint32_t numerator = (uint32_t)time_sig.numerator * 4u * division;
    if ((numerator % time_sig.denominator) != 0u) return 0u;
    const uint32_t count = numerator / time_sig.denominator;
    return (count > 0u && count <= sequencer::kStepLedgerMax) ? (uint8_t)count : 0u;
}

void DeriveStepProjection(sequencer::StepSlot& step)
{
    uint16_t note_mask = 0u;
    for (uint8_t position = 0u; position < sequencer::kStepLedgerMax; ++position)
        note_mask |= sequencer::LedgerSlotToPitchClassMask(step.note_ledger[position]);
    step.note_mask = note_mask;
    step.type = step.bar_state == sequencer::BarState::Skip ? sequencer::StepType::Skip :
                (note_mask != 0u ? sequencer::StepType::Chord : sequencer::StepType::Empty);
}

} // namespace

static_assert(sizeof(PatternDirectoryEntry) == 8u,
              "Pattern Directory entry exceeds the accepted 8-byte projection");
static_assert(sizeof(PatternCacheEntry) == 2624u,
              "Pattern cache entry metadata no longer fits the accepted compact layout");
static_assert(sizeof(PatternRoleState) == 3u,
              "Pattern role state must remain three entry references");

void PatternResidency_Init(void)
{
    memset(s_directory, 0, sizeof(s_directory));
    for (uint8_t i = 0u; i < PATTERN_RESIDENCY_CACHE_ENTRY_COUNT; ++i)
        ResetEntryMetadata(&s_cache[i]);
    s_roles.play_entry = PATTERN_RESIDENCY_INVALID_ENTRY;
    s_roles.next_entry = PATTERN_RESIDENCY_INVALID_ENTRY;
    s_roles.edit_entry = PATTERN_RESIDENCY_INVALID_ENTRY;
    s_active_io_entry = PATTERN_RESIDENCY_INVALID_ENTRY;
    s_active_io_discovery = 0u;
    s_pending_role_entry = PATTERN_RESIDENCY_INVALID_ENTRY;
    s_pending_role = PATTERN_RESIDENCY_INVALID_ENTRY;
    memset(s_last_role_load_error, 0, sizeof(s_last_role_load_error));
}

uint8_t PatternResidency_RebuildDirectory(void)
{
    if (s_active_io_entry != PATTERN_RESIDENCY_INVALID_ENTRY ||
        s_roles.play_entry != PATTERN_RESIDENCY_INVALID_ENTRY ||
        s_roles.next_entry != PATTERN_RESIDENCY_INVALID_ENTRY ||
        s_roles.edit_entry != PATTERN_RESIDENCY_INVALID_ENTRY) return 0u;
    uint8_t all_valid = 1u;

    for (uint8_t index = 0u; index < sequencer::kPatternCount; ++index)
    {
        s_directory[index] = PatternDirectoryEntry{};
        if (!PopulateDirectoryEntry(index)) all_valid = 0u;
    }

    return all_valid;
}

const PatternDirectoryEntry* PatternResidency_GetDirectory(void) { return s_directory; }

const PatternDirectoryEntry* PatternResidency_GetDirectoryEntry(uint8_t pattern_index)
{
    if (pattern_index >= sequencer::kPatternCount) return nullptr;
#if defined(PATTERN_STORE_HOST_TEST)
    ++s_directory_lookup_count_for_test;
#endif
    STORAGE_BOOT_PROBE(STORAGE_BOOT_PROBE_DIRECTORY_LOOKUP);
    const uint8_t was_unknown = s_directory[pattern_index].status == PATTERN_DIRECTORY_UNKNOWN ? 1u : 0u;
    TimeCommitProbe_RecordDirectory(was_unknown,
        s_directory[pattern_index].status == PATTERN_DIRECTORY_VALID ? 1u : 0u);
    (void)was_unknown;
    return &s_directory[pattern_index];
}

uint8_t PatternDirectory_GetStepDivision(const PatternDirectoryEntry* entry)
{
    return entry ? DivisionFromCode(entry->packed_config) : 0u;
}

sequencer::PlaybackMode PatternDirectory_GetPlaybackMode(const PatternDirectoryEntry* entry)
{
    return entry ? (sequencer::PlaybackMode)((entry->packed_config & kConfigModeMask) >> 2u) :
                   sequencer::PlaybackMode::Forward;
}

uint8_t PatternDirectory_GetRepeatCount(const PatternDirectoryEntry* entry)
{
    return entry ? (uint8_t)(((entry->packed_config & kConfigRepeatMask) >> 4u) + 1u) : 0u;
}

uint8_t PatternDirectory_IsUsable(const PatternDirectoryEntry* entry)
{
    return entry && entry->status == PATTERN_DIRECTORY_VALID &&
           entry->playable_step_mask != 0u ? 1u : 0u;
}

const PatternCacheEntry* PatternResidency_GetCacheEntry(uint8_t entry_index)
{
    return entry_index < PATTERN_RESIDENCY_CACHE_ENTRY_COUNT ? &s_cache[entry_index] : nullptr;
}

sequencer::Pattern* PatternResidency_GetMutablePattern(uint8_t entry_index)
{
    return entry_index < PATTERN_RESIDENCY_CACHE_ENTRY_COUNT ? &s_cache[entry_index].pattern : nullptr;
}

const PatternRoleState* PatternResidency_GetRoles(void) { return &s_roles; }

int8_t PatternResidency_FindResidentEntry(uint8_t pattern_index, uint32_t generation)
{
    return FindResident(pattern_index, generation);
}

int8_t PatternResidency_GetRoleEntry(PatternCacheRole role)
{
    uint8_t* entry = RolePointer(role);
    return entry && *entry < PATTERN_RESIDENCY_CACHE_ENTRY_COUNT ? (int8_t)*entry : -1;
}

sequencer::Pattern* PatternResidency_GetRolePattern(PatternCacheRole role)
{
    const int8_t entry_index = PatternResidency_GetRoleEntry(role);
    if (entry_index < 0) return nullptr;
    PatternCacheEntry& entry = s_cache[(uint8_t)entry_index];
    return entry.residency_state == PATTERN_CACHE_READY ? &entry.pattern : nullptr;
}

const sequencer::Pattern* PatternResidency_GetRolePatternConst(PatternCacheRole role)
{
    return PatternResidency_GetRolePattern(role);
}

uint8_t PatternResidency_AssignRole(PatternCacheRole role, uint8_t entry_index)
{
    uint8_t* target = RolePointer(role);
    if (!target || entry_index >= PATTERN_RESIDENCY_CACHE_ENTRY_COUNT ||
        s_cache[entry_index].residency_state != PATTERN_CACHE_READY) return 0u;
    *target = entry_index;
    return 1u;
}

uint8_t PatternResidency_ClearRole(PatternCacheRole role)
{
    uint8_t* target = RolePointer(role);
    if (!target) return 0u;
    *target = PATTERN_RESIDENCY_INVALID_ENTRY;
    return 1u;
}

uint8_t PatternResidency_CancelPendingRole(PatternCacheRole role)
{
    uint8_t* target = RolePointer(role);
    if (!target) return 0u;
    *target = PATTERN_RESIDENCY_INVALID_ENTRY;
    if (s_pending_role == (uint8_t)role)
    {
        s_pending_role = PATTERN_RESIDENCY_INVALID_ENTRY;
        s_pending_role_entry = PATTERN_RESIDENCY_INVALID_ENTRY;
    }
    return 1u;
}

uint8_t PatternResidency_CancelPendingLoad(PatternCacheRole role)
{
    if ((uint8_t)role > (uint8_t)PatternCacheRole::Edit) return 0u;
    if (s_pending_role == (uint8_t)role)
    {
        s_pending_role = PATTERN_RESIDENCY_INVALID_ENTRY;
        s_pending_role_entry = PATTERN_RESIDENCY_INVALID_ENTRY;
    }
    return 1u;
}

uint8_t PatternResidency_GetLastRoleLoadError(PatternCacheRole role)
{
    return (uint8_t)role <= (uint8_t)PatternCacheRole::Edit ?
           s_last_role_load_error[(uint8_t)role] : PATTERN_CACHE_ERROR_READ;
}

uint8_t PatternResidency_IsDirty(uint8_t entry_index)
{
    if (entry_index >= PATTERN_RESIDENCY_CACHE_ENTRY_COUNT) return 0u;
    const PatternCacheEntry& entry = s_cache[entry_index];
    return entry.current_generation != entry.persisted_generation ? 1u : 0u;
}

uint8_t PatternResidency_IsPinned(uint8_t entry_index)
{
    if (entry_index >= PATTERN_RESIDENCY_CACHE_ENTRY_COUNT) return 0u;
    const PatternCacheEntry& entry = s_cache[entry_index];
    return EntryReferenced(entry_index) || PatternResidency_IsDirty(entry_index) ||
           entry.io_state != PATTERN_CACHE_IO_IDLE ? 1u : 0u;
}

int8_t PatternResidency_FindEvictableEntry(void)
{
    for (uint8_t i = 0u; i < PATTERN_RESIDENCY_CACHE_ENTRY_COUNT; ++i)
    {
        if ((s_cache[i].residency_state == PATTERN_CACHE_EMPTY ||
             s_cache[i].residency_state == PATTERN_CACHE_FAULT) &&
            !PatternResidency_IsPinned(i))
            return (int8_t)i;
    }
    for (uint8_t i = 0u; i < PATTERN_RESIDENCY_CACHE_ENTRY_COUNT; ++i)
    {
        const PatternCacheEntry& entry = s_cache[i];
        if (entry.residency_state == PATTERN_CACHE_READY &&
            entry.io_state == PATTERN_CACHE_IO_IDLE &&
            entry.error == PATTERN_CACHE_ERROR_NONE &&
            !PatternResidency_IsDirty(i) && !EntryReferenced(i)) return (int8_t)i;
    }
    return -1;
}

uint8_t PatternResidency_UpdateDirectoryFromCache(uint8_t entry_index)
{
    if (entry_index >= PATTERN_RESIDENCY_CACHE_ENTRY_COUNT) return 0u;
    const PatternCacheEntry& entry = s_cache[entry_index];
    if (entry.residency_state != PATTERN_CACHE_READY ||
        entry.pattern_index >= sequencer::kPatternCount) return 0u;
    PatternDirectoryEntry derived{};
    if (!DeriveDirectoryEntry(entry.pattern, entry.current_generation, &derived)) return 0u;
    s_directory[entry.pattern_index] = derived;
    return 1u;
}

uint8_t PatternResidency_RequestLoad(uint8_t entry_index,
                                     uint8_t pattern_index,
                                     uint32_t expected_generation)
{
    if (entry_index >= PATTERN_RESIDENCY_CACHE_ENTRY_COUNT ||
        pattern_index >= sequencer::kPatternCount ||
        s_active_io_entry != PATTERN_RESIDENCY_INVALID_ENTRY ||
        PatternResidency_IsPinned(entry_index)) return 0u;
    /* Generation 0 = discovery: only for a Pattern whose directory entry is still unknown. */
    const uint8_t discovery = expected_generation == 0u ? 1u : 0u;
    if (discovery && s_directory[pattern_index].status != PATTERN_DIRECTORY_UNKNOWN) return 0u;

    PatternCacheEntry& entry = s_cache[entry_index];
    uint32_t actual_generation = 0u;
    STORAGE_BOOT_PROBE(STORAGE_BOOT_PROBE_PATTERN_LOAD);
    TimeCommitProbe_RecordResidencyLoad();
    PROBE_READ_SOURCE(READ_SOURCE_RESIDENCY_IO);
    if (!PatternStore_ReadAsyncBeginGeneration(pattern_index, &entry.pattern, &actual_generation) ||
        (discovery && actual_generation == 0u))
    {
        ResetEntryMetadata(&entry);
        entry.residency_state = PATTERN_CACHE_FAULT;
        entry.error = PATTERN_CACHE_ERROR_READ;
        if (discovery) MarkDirectoryInvalid(pattern_index);
        return 0u;
    }

    entry.current_generation = discovery ? actual_generation : expected_generation;
    entry.persisted_generation = 0u;
    entry.io_generation = actual_generation;
    entry.pattern_index = PATTERN_RESIDENCY_INVALID_PATTERN;
    entry.requested_pattern_index = pattern_index;
    entry.residency_state = PATTERN_CACHE_LOADING;
    entry.io_state = PATTERN_CACHE_IO_READING;
    entry.error = PATTERN_CACHE_ERROR_NONE;
    s_active_io_entry = entry_index;
    s_active_io_discovery = discovery;
    return 1u;
}

PatternCacheRequestStatus PatternResidency_RequestRole(PatternCacheRole role,
                                                       uint8_t pattern_index,
                                                       uint32_t expected_generation)
{
    if (pattern_index >= sequencer::kPatternCount) return PATTERN_CACHE_REQUEST_FAILED;
    if (expected_generation == 0u)
    {
        const PatternDirectoryEntry& directory = s_directory[pattern_index];
        if (directory.status == PATTERN_DIRECTORY_INVALID) return PATTERN_CACHE_REQUEST_FAILED;
        if (directory.status == PATTERN_DIRECTORY_VALID) expected_generation = directory.generation;
    }
    if (s_pending_role_entry != PATTERN_RESIDENCY_INVALID_ENTRY)
        return PATTERN_CACHE_REQUEST_WAIT;
    const int8_t resident = expected_generation != 0u ? FindResident(pattern_index, expected_generation) : -1;
    if (resident >= 0)
    {
        s_last_role_load_error[(uint8_t)role] = PATTERN_CACHE_ERROR_NONE;
        return PatternResidency_AssignRole(role, (uint8_t)resident) ?
               PATTERN_CACHE_REQUEST_READY : PATTERN_CACHE_REQUEST_FAILED;
    }

    if (s_active_io_entry != PATTERN_RESIDENCY_INVALID_ENTRY)
        return PATTERN_CACHE_REQUEST_WAIT;
    const int8_t candidate = PatternResidency_FindEvictableEntry();
    if (candidate < 0) return PATTERN_CACHE_REQUEST_WAIT;
    if (!PatternResidency_RequestLoad((uint8_t)candidate, pattern_index, expected_generation))
        return PATTERN_CACHE_REQUEST_FAILED;
    s_pending_role_entry = (uint8_t)candidate;
    s_pending_role = (uint8_t)role;
    s_last_role_load_error[(uint8_t)role] = PATTERN_CACHE_ERROR_NONE;
    return PATTERN_CACHE_REQUEST_LOAD_STARTED;
}

PatternCacheRequestStatus PatternResidency_RequestEdit(uint8_t pattern_index,
                                                       uint32_t expected_generation)
{
    return PatternResidency_RequestRole(PatternCacheRole::Edit, pattern_index, expected_generation);
}

PatternCacheRequestStatus PatternResidency_RequestDirectoryDiscovery(uint8_t pattern_index)
{
    if (pattern_index >= sequencer::kPatternCount) return PATTERN_CACHE_REQUEST_FAILED;
    if (s_directory[pattern_index].status != PATTERN_DIRECTORY_UNKNOWN)
        return PATTERN_CACHE_REQUEST_READY;
    if (s_pending_role_entry != PATTERN_RESIDENCY_INVALID_ENTRY ||
        s_active_io_entry != PATTERN_RESIDENCY_INVALID_ENTRY) return PATTERN_CACHE_REQUEST_WAIT;
    const int8_t candidate = PatternResidency_FindEvictableEntry();
    if (candidate < 0) return PATTERN_CACHE_REQUEST_WAIT;
    return PatternResidency_RequestLoad((uint8_t)candidate, pattern_index, 0u) ?
           PATTERN_CACHE_REQUEST_LOAD_STARTED : PATTERN_CACHE_REQUEST_FAILED;
}

uint8_t PatternResidency_ServiceDirectoryWarm(void)
{
    if (s_active_io_entry != PATTERN_RESIDENCY_INVALID_ENTRY ||
        s_pending_role_entry != PATTERN_RESIDENCY_INVALID_ENTRY ||
        PatternStore_WriteAsyncBusy() || PatternStore_ReadAsyncBusy()) return 0u;
    for (uint8_t i = 0u; i < PATTERN_RESIDENCY_CACHE_ENTRY_COUNT; ++i)
    {
        if (PatternResidency_IsDirty(i)) return 0u;
    }
    for (uint8_t index = 0u; index < sequencer::kPatternCount; ++index)
    {
        if (s_directory[index].status != PATTERN_DIRECTORY_UNKNOWN) continue;
        return PatternResidency_RequestDirectoryDiscovery(index) == PATTERN_CACHE_REQUEST_LOAD_STARTED
            ? 1u : 0u;
    }
    return 0u;
}

uint8_t PatternResidency_BeginWriteback(uint8_t entry_index)
{
    if (entry_index >= PATTERN_RESIDENCY_CACHE_ENTRY_COUNT ||
        s_active_io_entry != PATTERN_RESIDENCY_INVALID_ENTRY) return 0u;
    PatternCacheEntry& entry = s_cache[entry_index];
    if (entry.residency_state != PATTERN_CACHE_READY ||
        entry.io_state != PATTERN_CACHE_IO_IDLE || !PatternResidency_IsDirty(entry_index) ||
        entry.pattern_index >= sequencer::kPatternCount) return 0u;

    if (!PatternStore_WriteAsyncBeginGeneration(entry.pattern_index,
                                                &entry.pattern,
                                                entry.current_generation)) return 0u;
    entry.io_generation = entry.current_generation;
    entry.io_state = PATTERN_CACHE_IO_WRITING;
    entry.error = PATTERN_CACHE_ERROR_NONE;
    s_active_io_entry = entry_index;
    return 1u;
}

PatternCacheServiceStatus PatternResidency_ServiceIo(void)
{
    PROBE_READ_SOURCE(READ_SOURCE_RESIDENCY_IO);
    if (s_active_io_entry >= PATTERN_RESIDENCY_CACHE_ENTRY_COUNT)
    {
        if (PatternStore_WriteAsyncBusy())
        {
            const PatternStoreAsyncStatus status = PatternStore_WriteAsyncService();
            if (status == PATTERNSTORE_ASYNC_BUSY) return PATTERN_CACHE_SERVICE_BUSY;
            if (status == PATTERNSTORE_ASYNC_DONE) return PATTERN_CACHE_SERVICE_DONE;
            return PATTERN_CACHE_SERVICE_FAILED;
        }
        return PATTERN_CACHE_SERVICE_IDLE;
    }
    PatternCacheEntry& entry = s_cache[s_active_io_entry];

    if (entry.io_state == PATTERN_CACHE_IO_READING)
    {
        const PatternStoreAsyncStatus status = PatternStore_ReadAsyncService();
        if (status == PATTERNSTORE_ASYNC_BUSY) return PATTERN_CACHE_SERVICE_BUSY;
        const uint8_t completed_entry = s_active_io_entry;
        const uint8_t requested_pattern = entry.requested_pattern_index;
        const uint8_t discovery = s_active_io_discovery;
        s_active_io_entry = PATTERN_RESIDENCY_INVALID_ENTRY;
        s_active_io_discovery = 0u;
        entry.io_state = PATTERN_CACHE_IO_IDLE;
        if (status != PATTERNSTORE_ASYNC_DONE)
        {
            ResetEntryMetadata(&entry);
            entry.residency_state = PATTERN_CACHE_FAULT;
            entry.error = PATTERN_CACHE_ERROR_READ;
            if (discovery) MarkDirectoryInvalid(requested_pattern);
            if (s_pending_role_entry == completed_entry)
            {
                if (s_pending_role <= (uint8_t)PatternCacheRole::Edit)
                    s_last_role_load_error[s_pending_role] = PATTERN_CACHE_ERROR_READ;
                s_pending_role_entry = PATTERN_RESIDENCY_INVALID_ENTRY;
                s_pending_role = PATTERN_RESIDENCY_INVALID_ENTRY;
            }
            return PATTERN_CACHE_SERVICE_FAILED;
        }

        const PatternDirectoryEntry& directory = s_directory[requested_pattern];
        const bool discovered = discovery && directory.status == PATTERN_DIRECTORY_UNKNOWN;
        if ((!discovered && (directory.status != PATTERN_DIRECTORY_VALID ||
                             directory.generation != entry.current_generation)) ||
            entry.io_generation != entry.current_generation)
        {
            ResetEntryMetadata(&entry);
            if (s_pending_role_entry == completed_entry)
            {
                if (s_pending_role <= (uint8_t)PatternCacheRole::Edit)
                    s_last_role_load_error[s_pending_role] = PATTERN_CACHE_ERROR_STALE_LOAD;
                s_pending_role_entry = PATTERN_RESIDENCY_INVALID_ENTRY;
                s_pending_role = PATTERN_RESIDENCY_INVALID_ENTRY;
            }
            return PATTERN_CACHE_SERVICE_STALE;
        }

        entry.pattern_index = requested_pattern;
        entry.persisted_generation = entry.io_generation;
        entry.residency_state = PATTERN_CACHE_READY;
        entry.error = PATTERN_CACHE_ERROR_NONE;
        if (!PatternResidency_UpdateDirectoryFromCache(completed_entry) && discovered)
        {
            /* CRC-valid but semantically unusable record: fail closed. */
            ResetEntryMetadata(&entry);
            entry.residency_state = PATTERN_CACHE_FAULT;
            entry.error = PATTERN_CACHE_ERROR_READ;
            MarkDirectoryInvalid(requested_pattern);
            if (s_pending_role_entry == completed_entry)
            {
                if (s_pending_role <= (uint8_t)PatternCacheRole::Edit)
                    s_last_role_load_error[s_pending_role] = PATTERN_CACHE_ERROR_READ;
                s_pending_role_entry = PATTERN_RESIDENCY_INVALID_ENTRY;
                s_pending_role = PATTERN_RESIDENCY_INVALID_ENTRY;
            }
            return PATTERN_CACHE_SERVICE_FAILED;
        }
        if (s_pending_role_entry == completed_entry)
        {
            if (s_pending_role <= (uint8_t)PatternCacheRole::Edit)
            {
                (void)PatternResidency_AssignRole((PatternCacheRole)s_pending_role, completed_entry);
                s_last_role_load_error[s_pending_role] = PATTERN_CACHE_ERROR_NONE;
            }
            s_pending_role_entry = PATTERN_RESIDENCY_INVALID_ENTRY;
            s_pending_role = PATTERN_RESIDENCY_INVALID_ENTRY;
        }
        return PATTERN_CACHE_SERVICE_DONE;
    }

    if (entry.io_state == PATTERN_CACHE_IO_WRITING)
    {
        const PatternStoreAsyncStatus status = PatternStore_WriteAsyncService();
        if (status == PATTERNSTORE_ASYNC_BUSY) return PATTERN_CACHE_SERVICE_BUSY;
        s_active_io_entry = PATTERN_RESIDENCY_INVALID_ENTRY;
        entry.io_state = PATTERN_CACHE_IO_IDLE;
        if (status != PATTERNSTORE_ASYNC_DONE)
        {
            entry.error = PATTERN_CACHE_ERROR_WRITE;
            return PATTERN_CACHE_SERVICE_FAILED;
        }
        entry.persisted_generation = entry.io_generation;
        entry.error = PATTERN_CACHE_ERROR_NONE;
        (void)PatternResidency_UpdateDirectoryFromCache((uint8_t)(&entry - s_cache));
        return PATTERN_CACHE_SERVICE_DONE;
    }

    s_active_io_entry = PATTERN_RESIDENCY_INVALID_ENTRY;
    return PATTERN_CACHE_SERVICE_FAILED;
}

uint8_t PatternResidency_BeginAnyDirtyWriteback(void)
{
    if (s_active_io_entry != PATTERN_RESIDENCY_INVALID_ENTRY) return 0u;
    for (uint8_t i = 0u; i < PATTERN_RESIDENCY_CACHE_ENTRY_COUNT; ++i)
    {
        if (PatternResidency_IsDirty(i) && s_cache[i].io_state == PATTERN_CACHE_IO_IDLE)
            return PatternResidency_BeginWriteback(i);
    }
    return 0u;
}

uint8_t PatternResidency_MarkMutated(uint8_t entry_index)
{
    if (entry_index >= PATTERN_RESIDENCY_CACHE_ENTRY_COUNT) return 0u;
    PatternCacheEntry& entry = s_cache[entry_index];
    if (entry.residency_state != PATTERN_CACHE_READY) return 0u;
    ++entry.current_generation;
    if (entry.current_generation == 0u) entry.current_generation = 1u;
    return PatternResidency_UpdateDirectoryFromCache(entry_index);
}

uint8_t PatternResidency_ApplyLazyProjections(uint8_t entry_index,
                                              const sequencer::TimeSig* time_sig)
{
    if (entry_index >= PATTERN_RESIDENCY_CACHE_ENTRY_COUNT || !time_sig) return 0u;
    PatternCacheEntry& entry = s_cache[entry_index];
    if (entry.residency_state != PATTERN_CACHE_READY) return 0u;
    const uint8_t count = StepsPerBar(*time_sig, entry.pattern.step_division);
    if (count == 0u) return 0u;
    for (uint8_t step = 0u; step < sequencer::kStepCount; ++step)
    {
        entry.pattern.steps[step].repeat_count = count;
        DeriveStepProjection(entry.pattern.steps[step]);
    }
    return PatternResidency_UpdateDirectoryFromCache(entry_index);
}

#if defined(PATTERN_STORE_HOST_TEST)
void PatternResidency_ResetForTest(void)
{
    PatternResidency_Init();
    s_directory_lookup_count_for_test = 0u;
}

uint32_t PatternResidency_GetDirectoryLookupCountForTest(void)
{
    return s_directory_lookup_count_for_test;
}
#endif