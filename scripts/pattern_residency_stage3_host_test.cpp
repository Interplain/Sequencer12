#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

#define PATTERN_STORE_HOST_TEST 1
#include "platform/fram/pattern_residency.h"
#include "platform/fram/pattern_store.h"

namespace
{

std::array<uint8_t, 0x20000u> s_fram;
uint32_t s_fail_write_once_at = 0xFFFFFFFFu;
uint32_t s_read_calls = 0u;
uint32_t s_read_bytes = 0u;
int s_failures = 0;

uint32_t SlotAddress(uint8_t index)
{
    return FRAM_PATTERNSTORE_ADDR + (uint32_t)index * FRAM_PATTERNSTORE_SLOT_STRIDE;
}

void Check(bool condition, const char* name)
{
    std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", name);
    if (!condition) ++s_failures;
}

sequencer::Pattern MakePattern(uint8_t index)
{
    sequencer::Pattern pattern{};
    pattern.step_count = 4u;
    pattern.step_division = index == 2u ? 8u : 4u;
    pattern.playback_mode = index == 2u ? sequencer::PlaybackMode::PingPong :
                                         sequencer::PlaybackMode::Loop;
    pattern.repeat_count = index == 2u ? 5u : 1u;
    for (uint8_t step = 0u; step < pattern.step_count; ++step)
        pattern.steps[step].bar_state = index == 1u ? sequencer::BarState::Skip :
                                                     sequencer::BarState::Active;
    pattern.steps[0].note_ledger[0].notes[0] = (uint8_t)(48u + index);
    return pattern;
}

void ResetFram(void)
{
    s_fram.fill(0xFFu);
    s_fail_write_once_at = 0xFFFFFFFFu;
    PatternStore_ResetRuntimeStateForTest();
    PatternResidency_ResetForTest();
}

bool PrepareStore(void)
{
    ResetFram();
    for (uint8_t index = 0u; index < sequencer::kPatternCount; ++index)
    {
        const sequencer::Pattern pattern = MakePattern(index);
        if (!PatternStore_WriteGeneration(index, &pattern, (uint32_t)index + 10u)) return false;
    }
    return true;
}

PatternCacheServiceStatus PumpIo(void)
{
    PatternCacheServiceStatus status = PATTERN_CACHE_SERVICE_BUSY;
    for (uint32_t guard = 0u; guard < 1000u && status == PATTERN_CACHE_SERVICE_BUSY; ++guard)
        status = PatternResidency_ServiceIo();
    return status;
}

bool LoadEntry(uint8_t entry, uint8_t pattern)
{
    const PatternDirectoryEntry* directory = PatternResidency_GetDirectoryEntry(pattern);
    return directory && PatternResidency_RequestLoad(entry, pattern, directory->generation) &&
           PumpIo() == PATTERN_CACHE_SERVICE_DONE;
}

void TestDirectory(void)
{
    Check(PrepareStore(), "32 authoritative Pattern records prepared");
    Check(PatternResidency_RebuildDirectory(), "directory reconstructs from 32 valid records");

    const PatternDirectoryEntry* entry0 = PatternResidency_GetDirectoryEntry(0u);
    const PatternDirectoryEntry* entry1 = PatternResidency_GetDirectoryEntry(1u);
    const PatternDirectoryEntry* entry2 = PatternResidency_GetDirectoryEntry(2u);
    Check(entry0 && entry0->status == PATTERN_DIRECTORY_VALID && entry0->generation == 10u,
          "directory captures committed generation");
    Check(entry1 && entry1->playable_step_mask == 0u && !PatternDirectory_IsUsable(entry1),
          "directory detects all-skipped Pattern");
    Check(entry2 && PatternDirectory_GetStepDivision(entry2) == 8u &&
          PatternDirectory_GetPlaybackMode(entry2) == sequencer::PlaybackMode::PingPong &&
          PatternDirectory_GetRepeatCount(entry2) == 5u,
          "directory extracts timing, playback mode, and repeat metadata");

    s_fram[SlotAddress(6u) + 20u] ^= 0x01u;
    Check(!PatternResidency_RebuildDirectory(), "directory rebuild reports invalid Pattern record");
    const PatternDirectoryEntry* invalid = PatternResidency_GetDirectoryEntry(6u);
    Check(invalid && invalid->status == PATTERN_DIRECTORY_INVALID,
          "invalid Pattern has explicit directory fault representation");
}

void TestNonBlockingDirectory(void)
{
    Check(PrepareStore(), "non-blocking directory store prepared");
    s_fram[SlotAddress(6u) + 20u] ^= 0x01u;
    PatternResidency_Init();

    uint32_t reads_before = s_read_calls;
    const PatternDirectoryEntry* first = PatternResidency_GetDirectoryEntry(0u);
    Check(first && first->status == PATTERN_DIRECTORY_UNKNOWN && s_read_calls == reads_before,
          "directory lookup of an unread Pattern returns UNKNOWN without FRAM I/O");

    reads_before = s_read_calls;
    uint32_t bytes_before = s_read_bytes;
    Check(PatternResidency_RequestEdit(0u, 0u) == PATTERN_CACHE_REQUEST_LOAD_STARTED,
          "unknown EDIT request starts an asynchronous discovery load");
    Check(s_read_calls - reads_before == 1u &&
          s_read_bytes - bytes_before < sizeof(sequencer::Pattern),
          "discovery request reads only the record header synchronously");
    bytes_before = s_read_bytes;
    Check(PumpIo() == PATTERN_CACHE_SERVICE_DONE, "discovery load completes");
    Check(s_read_bytes - bytes_before == sizeof(sequencer::Pattern),
          "discovery reads the payload exactly once while validating CRC");
    first = PatternResidency_GetDirectoryEntry(0u);
    Check(first->status == PATTERN_DIRECTORY_VALID && first->generation == 10u,
          "discovery populates directory metadata and stored generation");
    const int8_t edit = PatternResidency_GetRoleEntry(PatternCacheRole::Edit);
    Check(edit >= 0 && PatternResidency_GetCacheEntry((uint8_t)edit)->pattern_index == 0u &&
          PatternResidency_GetCacheEntry((uint8_t)edit)->current_generation == 10u,
          "discovered Pattern is resident and assigned to the requested role");
    Check((PatternResidency_GetDirectory() + 6u)->status == PATTERN_DIRECTORY_UNKNOWN,
          "discovery leaves unrelated Pattern entries unread");

    Check(PatternResidency_RequestDirectoryDiscovery(6u) == PATTERN_CACHE_REQUEST_LOAD_STARTED &&
          PumpIo() == PATTERN_CACHE_SERVICE_FAILED,
          "corrupt Pattern discovery fails CRC validation");
    Check(PatternResidency_GetDirectoryEntry(6u)->status == PATTERN_DIRECTORY_INVALID &&
          PatternResidency_RequestEdit(6u, 0u) == PATTERN_CACHE_REQUEST_FAILED &&
          PatternResidency_GetCacheEntry((uint8_t)edit)->pattern_index == 0u,
          "corrupt Pattern becomes INVALID without displacing the valid EDIT");

    memset(&s_fram[SlotAddress(7u)], 0xFF, 4u);
    Check(PatternResidency_RequestDirectoryDiscovery(7u) == PATTERN_CACHE_REQUEST_FAILED &&
          PatternResidency_GetDirectoryEntry(7u)->status == PATTERN_DIRECTORY_INVALID,
          "missing record header fails closed as INVALID");

    Check(PatternResidency_RequestEdit(3u, 0u) == PATTERN_CACHE_REQUEST_LOAD_STARTED &&
          PatternResidency_CancelPendingLoad(PatternCacheRole::Edit) &&
          PumpIo() == PATTERN_CACHE_SERVICE_DONE,
          "superseded discovery still completes its validated read");
    Check(PatternResidency_GetRoleEntry(PatternCacheRole::Edit) == edit &&
          PatternResidency_GetDirectoryEntry(3u)->status == PATTERN_DIRECTORY_VALID,
          "superseded discovery populates the directory but does not take the role");

    PatternResidency_MarkMutated((uint8_t)edit);
    Check(PatternResidency_ServiceDirectoryWarm() == 0u,
          "directory warming does not start while a Pattern is dirty");
}

void TestDirectoryWarming(void)
{
    Check(PrepareStore(), "warming store prepared");
    s_fram[SlotAddress(9u) + 20u] ^= 0x01u;
    PatternResidency_Init();
    uint32_t started = 0u;
    for (uint32_t guard = 0u; guard < 100u; ++guard)
    {
        if (!PatternResidency_ServiceDirectoryWarm()) break;
        ++started;
        (void)PumpIo();
    }
    uint8_t valid = 0u;
    for (uint8_t i = 0u; i < sequencer::kPatternCount; ++i)
        if (PatternResidency_GetDirectoryEntry(i)->status == PATTERN_DIRECTORY_VALID) ++valid;
    Check(started == sequencer::kPatternCount && valid == sequencer::kPatternCount - 1u &&
          PatternResidency_GetDirectoryEntry(9u)->status == PATTERN_DIRECTORY_INVALID,
          "background warming resolves every Pattern one async load at a time");
    Check(PatternResidency_GetRoleEntry(PatternCacheRole::Play) < 0 &&
          PatternResidency_GetRoleEntry(PatternCacheRole::Edit) < 0 &&
          PatternResidency_GetRoleEntry(PatternCacheRole::Next) < 0,
          "warming never assigns PLAY, NEXT, or EDIT");
}

void TestCacheRolesAndEviction(void)
{
    Check(PrepareStore() && PatternResidency_RebuildDirectory(), "cache test directory prepared");
    Check(PATTERN_RESIDENCY_CACHE_ENTRY_COUNT == 3u &&
          PatternResidency_GetCacheEntry(0u) && PatternResidency_GetCacheEntry(1u) &&
          PatternResidency_GetCacheEntry(2u) && !PatternResidency_GetCacheEntry(3u),
          "exactly three physical cache entries exist");
    Check(LoadEntry(0u, 0u) && LoadEntry(1u, 2u) && LoadEntry(2u, 3u),
          "three cache entries load validated Pattern records");

    const sequencer::Pattern before = *PatternResidency_GetMutablePattern(0u);
    Check(PatternResidency_AssignRole(PatternCacheRole::Play, 0u) &&
          PatternResidency_AssignRole(PatternCacheRole::Next, 0u),
          "PLAY equals NEXT alias is supported");
    Check(PatternResidency_AssignRole(PatternCacheRole::Edit, 0u),
          "PLAY equals NEXT equals EDIT alias is supported");
    Check(memcmp(&before, PatternResidency_GetMutablePattern(0u), sizeof(before)) == 0,
          "role alias changes do not copy or alter Pattern payload");

    Check(PatternResidency_AssignRole(PatternCacheRole::Play, 0u) &&
          PatternResidency_AssignRole(PatternCacheRole::Next, 1u) &&
          PatternResidency_AssignRole(PatternCacheRole::Edit, 0u),
          "PLAY equals EDIT with separate NEXT is supported");
    Check(PatternResidency_AssignRole(PatternCacheRole::Edit, 1u),
          "NEXT equals EDIT is supported");
    Check(PatternResidency_AssignRole(PatternCacheRole::Edit, 2u),
          "PLAY, NEXT, and EDIT may all differ");

    Check(PatternResidency_IsPinned(0u) && PatternResidency_IsPinned(1u) &&
          PatternResidency_IsPinned(2u), "PLAY, validated NEXT, and EDIT roles pin entries");

    PatternResidency_ClearRole(PatternCacheRole::Edit);
    Check(PatternResidency_FindEvictableEntry() == 2,
          "clean idle unreferenced entry is evictable");

    PatternResidency_AssignRole(PatternCacheRole::Edit, 2u);
    Check(PatternResidency_MarkMutated(2u) && PatternResidency_IsDirty(2u),
          "authoritative mutation advances current generation and marks dirty");
    PatternResidency_ClearRole(PatternCacheRole::Edit);
    Check(PatternResidency_IsPinned(2u) && PatternResidency_FindEvictableEntry() < 0,
          "dirty unreferenced entry remains pinned and non-evictable");
    Check(PatternResidency_RequestEdit(4u, 14u) == PATTERN_CACHE_REQUEST_WAIT,
          "PLAY/NEXT/dirty EDIT exhaustion reports WAIT without hidden entry");
}

void TestIoPinningAndGeneration(void)
{
    Check(PrepareStore() && PatternResidency_RebuildDirectory(), "generation test directory prepared");
    Check(PatternResidency_RequestLoad(0u, 0u, 10u), "cache load request starts");
    Check(PatternResidency_IsPinned(0u) && PatternResidency_FindEvictableEntry() == 1,
          "I/O entry is pinned and skipped by eviction");
    Check(PumpIo() == PATTERN_CACHE_SERVICE_DONE, "valid cache load completes");

    Check(PatternResidency_MarkMutated(0u), "first edit advances generation to G");
    const PatternCacheEntry* entry = PatternResidency_GetCacheEntry(0u);
    const uint32_t generation_g = entry->current_generation;
    Check(PatternResidency_BeginWriteback(0u), "writeback snapshots generation G");
    entry = PatternResidency_GetCacheEntry(0u);
    Check(entry->io_generation == generation_g && entry->io_state == PATTERN_CACHE_IO_WRITING,
          "writeback records io_generation G");
    Check(PatternResidency_MarkMutated(0u), "edit to G+1 is represented during G writeback");
    Check(PumpIo() == PATTERN_CACHE_SERVICE_DONE, "generation G writeback completes");
    entry = PatternResidency_GetCacheEntry(0u);
    Check(entry->persisted_generation == generation_g &&
          entry->current_generation == generation_g + 1u && PatternResidency_IsDirty(0u),
          "persisted generation advances only to G and G+1 remains dirty");

    Check(PatternResidency_BeginWriteback(0u), "G+1 writeback begins");
    Check(PumpIo() == PATTERN_CACHE_SERVICE_DONE, "G+1 writeback completes");
    entry = PatternResidency_GetCacheEntry(0u);
    Check(entry->persisted_generation == entry->current_generation &&
          !PatternResidency_IsDirty(0u), "validated writeback transitions entry to clean");

    Check(PatternResidency_MarkMutated(0u) && PatternResidency_BeginWriteback(0u),
          "failed-write scenario begins dirty writeback");
    s_fail_write_once_at = FRAM_PATTERNSTORE_JOURNAL_ADDR + 24u;
    Check(PumpIo() == PATTERN_CACHE_SERVICE_FAILED, "write failure is reported");
    entry = PatternResidency_GetCacheEntry(0u);
    Check(PatternResidency_IsDirty(0u) && entry->error == PATTERN_CACHE_ERROR_WRITE &&
          PatternResidency_IsPinned(0u), "failed write leaves dirty faulted entry pinned");
}

void TestLoadFailures(void)
{
    Check(PrepareStore() && PatternResidency_RebuildDirectory(), "load-failure directory prepared");
    Check(PatternResidency_RequestLoad(0u, 0u, 11u), "stale generation load starts");
    Check(PumpIo() == PATTERN_CACHE_SERVICE_STALE,
          "load completion with unexpected generation is rejected as stale");

    PatternResidency_ResetForTest();
    Check(PatternResidency_RebuildDirectory(), "directory rebuilt before corruption test");
    s_fram[SlotAddress(5u) + 20u] ^= 0x40u;
    const PatternDirectoryEntry* directory = PatternResidency_GetDirectoryEntry(5u);
    Check(directory && PatternResidency_RequestLoad(0u, 5u, directory->generation),
          "corrupt payload load begins from previously validated directory identity");
    Check(PumpIo() == PATTERN_CACHE_SERVICE_FAILED,
          "corrupt cache load fails payload CRC validation");
    const PatternCacheEntry* entry = PatternResidency_GetCacheEntry(0u);
    Check(entry && entry->residency_state == PATTERN_CACHE_FAULT &&
          entry->error == PATTERN_CACHE_ERROR_READ,
          "corrupt load leaves explicit cache fault state");

    Check(PrepareStore() && PatternResidency_RebuildDirectory(),
          "EDIT load retry directory prepared");
    Check(PatternResidency_RequestEdit(4u, 14u) == PATTERN_CACHE_REQUEST_LOAD_STARTED,
          "EDIT request starts asynchronous load");
    s_fram[SlotAddress(4u) + 20u] ^= 0x20u;
    Check(PumpIo() == PATTERN_CACHE_SERVICE_FAILED,
          "EDIT load failure is reported");
    const PatternDirectoryEntry* edit_directory = PatternResidency_GetDirectoryEntry(5u);
    Check(edit_directory && PatternResidency_RequestEdit(5u, edit_directory->generation) ==
          PATTERN_CACHE_REQUEST_LOAD_STARTED,
          "EDIT request can retry after prior load failure");
}

} // namespace

extern "C" uint8_t PatternStoreBackend_Read(uint32_t address, uint8_t* dst, uint16_t len)
{
    if (!dst || address + len > s_fram.size()) return 0u;
    ++s_read_calls;
    s_read_bytes += len;
    memcpy(dst, s_fram.data() + address, len);
    return 1u;
}

extern "C" uint8_t PatternStoreBackend_Write(uint32_t address, const uint8_t* src, uint16_t len)
{
    if (!src || address + len > s_fram.size()) return 0u;
    if (address == s_fail_write_once_at)
    {
        s_fail_write_once_at = 0xFFFFFFFFu;
        return 0u;
    }
    memcpy(s_fram.data() + address, src, len);
    return 1u;
}

int main()
{
    TestDirectory();
    TestNonBlockingDirectory();
    TestDirectoryWarming();
    TestCacheRolesAndEviction();
    TestIoPinningAndGeneration();
    TestLoadFailures();
    std::printf("%s: %d failure(s)\n", s_failures == 0 ? "PASS" : "FAIL", s_failures);
    return s_failures == 0 ? 0 : 1;
}
