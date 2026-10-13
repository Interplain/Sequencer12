#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

#define PATTERN_STORE_HOST_TEST 1
#include "platform/fram/persistence_store.h"
#include "platform/fram/persistence_retry.h"

namespace
{

constexpr uint32_t kSchemaMagic = 0x53313246u;
constexpr uint8_t kSchemaReady = 3u;
constexpr uint32_t kPatternStoreMagic = 0x53313250u;

std::array<uint8_t, 0x20000u> s_fram;
std::array<uint32_t, 0x20000u> s_write_counts;
bool s_ready = true;
bool s_block_io = false;
uint32_t s_fail_write_once_at = 0xFFFFFFFFu;
uint8_t s_ready_write_fault = 0u; /* 1=before write, 2=after physical write */
bool s_forbidden_write = false;
bool s_ready_before_required_data = false;
int s_failures = 0;

struct SchemaRecordView
{
    uint32_t magic;
    uint16_t version;
    uint16_t record_size;
    uint32_t generation;
    uint8_t state;
    uint8_t reserved[15];
    uint32_t crc32;
};

struct SongHeaderRecordView
{
    uint32_t magic;
    uint16_t version;
    uint16_t payload_size;
    uint32_t generation;
    uint32_t payload_crc32;
    uint32_t header_crc32;
    PersistentSongHeaderData payload;
};

uint32_t SlotAddress(uint8_t index)
{
    return FRAM_PATTERNSTORE_ADDR + (uint32_t)index * FRAM_PATTERNSTORE_SLOT_STRIDE;
}

bool IsSchemaAddress(uint32_t address)
{
    return address == FRAM_SCHEMA_RECORD_A_ADDR || address == FRAM_SCHEMA_RECORD_B_ADDR;
}

void Check(bool condition, const char* name)
{
    std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", name);
    if (!condition) ++s_failures;
}

void SimulateReset(void)
{
    s_block_io = false;
    s_fail_write_once_at = 0xFFFFFFFFu;
    s_ready_write_fault = 0u;
    s_forbidden_write = false;
    s_ready_before_required_data = false;
    PatternStore_ResetRuntimeStateForTest();
    PersistenceStore_ResetRuntimeStateForTest();
}

void ResetBlankFram(void)
{
    s_fram.fill(0xFFu);
    s_write_counts.fill(0u);
    s_ready = true;
    SimulateReset();
}

uint8_t LatestSchemaState(void)
{
    SchemaRecordView a{};
    SchemaRecordView b{};
    memcpy(&a, s_fram.data() + FRAM_SCHEMA_RECORD_A_ADDR, sizeof(a));
    memcpy(&b, s_fram.data() + FRAM_SCHEMA_RECORD_B_ADDR, sizeof(b));
    const bool a_magic = a.magic == kSchemaMagic;
    const bool b_magic = b.magic == kSchemaMagic;
    if (a_magic && b_magic) return (int32_t)(b.generation - a.generation) > 0 ? b.state : a.state;
    if (a_magic) return a.state;
    if (b_magic) return b.state;
    return 0u;
}

uint8_t CountReadySchemaRecords(void)
{
    SchemaRecordView a{};
    SchemaRecordView b{};
    memcpy(&a, s_fram.data() + FRAM_SCHEMA_RECORD_A_ADDR, sizeof(a));
    memcpy(&b, s_fram.data() + FRAM_SCHEMA_RECORD_B_ADDR, sizeof(b));
    return (uint8_t)((a.magic == kSchemaMagic && a.state == kSchemaReady ? 1u : 0u) +
                     (b.magic == kSchemaMagic && b.state == kSchemaReady ? 1u : 0u));
}

bool AllPatternsValid(void)
{
    for (uint8_t i = 0u; i < FRAM_PATTERNSTORE_SLOT_COUNT; ++i)
    {
        if (!PatternStore_IsValid(i)) return false;
    }
    return true;
}

uint8_t CountValidSongHeaders(void)
{
    SongHeaderRecordView a{};
    SongHeaderRecordView b{};
    memcpy(&a, s_fram.data() + FRAM_SONG_HEADER_A_ADDR, sizeof(a));
    memcpy(&b, s_fram.data() + FRAM_SONG_HEADER_B_ADDR, sizeof(b));
    uint8_t count = 0u;
    if (a.magic == 0x53313248u) ++count;
    if (b.magic == 0x53313248u) ++count;
    return count;
}

} // namespace

extern "C" uint8_t PatternStoreBackend_IsReady(void)
{
    return s_ready ? 1u : 0u;
}

extern "C" uint8_t PatternStoreBackend_Read(uint32_t address, uint8_t* dst, uint16_t len)
{
    if (s_block_io || !dst || address + len > s_fram.size()) return 0u;
    memcpy(dst, s_fram.data() + address, len);
    return 1u;
}

extern "C" uint8_t PatternStoreBackend_Write(uint32_t address, const uint8_t* src, uint16_t len)
{
    if (s_block_io || !src || address + len > s_fram.size()) return 0u;
    if (address < FRAM_PATTERNSTORE_ADDR ||
        address + len > FRAM_PERSISTENCE_METADATA_END)
    {
        s_forbidden_write = true;
        return 0u;
    }
    if (address == s_fail_write_once_at)
    {
        s_fail_write_once_at = 0xFFFFFFFFu;
        return 0u;
    }

    if (IsSchemaAddress(address) && len == FRAM_SCHEMA_RECORD_SIZE && src[12] == kSchemaReady)
    {
        bool patterns_present = true;
        for (uint8_t index = 0u; index < FRAM_PATTERNSTORE_SLOT_COUNT; ++index)
        {
            uint32_t magic = 0u;
            memcpy(&magic, s_fram.data() + SlotAddress(index), sizeof(magic));
            if (magic != kPatternStoreMagic) patterns_present = false;
        }
        SongHeaderRecordView song_a{};
        SongHeaderRecordView song_b{};
        memcpy(&song_a, s_fram.data() + FRAM_SONG_HEADER_A_ADDR, sizeof(song_a));
        memcpy(&song_b, s_fram.data() + FRAM_SONG_HEADER_B_ADDR, sizeof(song_b));
        if (!patterns_present || song_a.magic != 0x53313248u || song_b.magic != 0x53313248u)
            s_ready_before_required_data = true;

        if (s_ready_write_fault == 1u)
        {
            s_ready_write_fault = 0u;
            return 0u;
        }
        if (s_ready_write_fault == 2u)
        {
            memcpy(s_fram.data() + address, src, len);
            ++s_write_counts[address];
            s_ready_write_fault = 0u;
            s_block_io = true;
            return 1u;
        }
    }

    memcpy(s_fram.data() + address, src, len);
    ++s_write_counts[address];
    return 1u;
}

namespace
{

void TestBlankInitialization(void)
{
    ResetBlankFram();
    const auto writes_before = s_write_counts;
    Check(PersistenceStore_Inspect() == PERSISTENCE_BOOT_FACTORY_REQUIRED,
        "blank FRAM requires explicit factory confirmation");
    Check(s_write_counts == writes_before, "storage inspection performs no writes");
    Check(PersistenceStore_FactoryInitialize(nullptr) == PERSISTENCE_BOOT_READY,
        "explicit factory initialization reaches READY");
    Check(AllPatternsValid(), "all 32 default Pattern records validate");

    PersistentSongHeaderData song{};
    uint32_t generation = 0u;
    Check(PersistenceStore_SelectSongHeader(&song, &generation) == SONG_HEADER_SELECT_VALID,
          "default Song Header validates");
    Check(song.global_bpm == 120u && song.time_sig_numerator == 4u &&
          song.time_sig_denominator == 4u && song.chain_length == 1u &&
          song.pattern_indices[0] == 0u,
          "default Song Header values are correct");
    Check(CountValidSongHeaders() == 2u, "first initialization creates both Song Header A/B records");
    Check(LatestSchemaState() == kSchemaReady, "schema is READY after required records validate");
}

void TestPatternInitializationResume(void)
{
    ResetBlankFram();
    s_fail_write_once_at = SlotAddress(7u) + 20u;
    Check(PersistenceStore_FactoryInitialize(nullptr) == PERSISTENCE_BOOT_IO_FAULT,
          "Pattern initialization interruption reports fault");
    Check(LatestSchemaState() != kSchemaReady, "schema is not READY after partial Pattern initialization");
    const uint32_t slot0_writes = s_write_counts[SlotAddress(0u)];

    SimulateReset();
    Check(PersistenceStore_Bootstrap() == PERSISTENCE_BOOT_READY,
          "Pattern initialization resumes after reset");
    Check(AllPatternsValid(), "resumed initialization validates all Patterns");
    Check(s_write_counts[SlotAddress(0u)] == slot0_writes,
          "resume does not rewrite already valid Pattern records");
}

void TestSongHeaderInitializationResume(void)
{
    ResetBlankFram();
    s_fail_write_once_at = FRAM_SONG_HEADER_A_ADDR + 20u;
    Check(PersistenceStore_FactoryInitialize(nullptr) == PERSISTENCE_BOOT_IO_FAULT,
          "Song Header creation interruption reports fault");
    Check(AllPatternsValid(), "Patterns remain valid after Song Header interruption");
    Check(LatestSchemaState() != kSchemaReady, "schema is not READY before Song Header exists");

    SimulateReset();
    Check(PersistenceStore_Bootstrap() == PERSISTENCE_BOOT_READY,
          "Song Header creation resumes after reset");
    Check(CountValidSongHeaders() == 2u, "resumed initialization restores Song Header A/B pair");
}

void TestReadyMarkerInterruptions(void)
{
    ResetBlankFram();
    s_ready_write_fault = 1u;
    Check(PersistenceStore_FactoryInitialize(nullptr) == PERSISTENCE_BOOT_IO_FAULT,
          "reset immediately before READY marker leaves initialization incomplete");
    Check(LatestSchemaState() != kSchemaReady, "READY is absent before marker commit");
    SimulateReset();
    Check(PersistenceStore_Bootstrap() == PERSISTENCE_BOOT_READY,
          "initialization resumes after pre-READY reset");

    ResetBlankFram();
    s_ready_write_fault = 2u;
    Check(PersistenceStore_FactoryInitialize(nullptr) == PERSISTENCE_BOOT_IO_FAULT,
          "reset immediately after physical READY marker is simulated");
    SimulateReset();
    Check(PersistenceStore_Bootstrap() == PERSISTENCE_BOOT_READY,
          "valid READY marker is selected after reset");
        Check(CountReadySchemaRecords() == 2u,
            "bootstrap restores redundant READY schema record");
    Check(AllPatternsValid(), "post-READY reset preserves initialized Patterns");
}

void TestSongHeaderAB(void)
{
    ResetBlankFram();
    Check(PersistenceStore_FactoryInitialize(nullptr) == PERSISTENCE_BOOT_READY,
          "Song A/B test store initializes");

    PersistentSongHeaderData current{};
    uint32_t generation = 0u;
    Check(PersistenceStore_SelectSongHeader(&current, &generation) == SONG_HEADER_SELECT_VALID,
          "newest default Song Header selects");
    current.global_bpm = 137u;
    Check(PersistenceStore_WriteSongHeader(&current, generation + 1u), "Song Header inactive record update succeeds");

    PersistentSongHeaderData selected{};
    uint32_t selected_generation = 0u;
    Check(PersistenceStore_SelectSongHeader(&selected, &selected_generation) == SONG_HEADER_SELECT_VALID &&
          selected.global_bpm == 137u && selected_generation == generation + 1u,
          "newest valid Song Header generation is selected");

    SongHeaderRecordView a{};
    SongHeaderRecordView b{};
    memcpy(&a, s_fram.data() + FRAM_SONG_HEADER_A_ADDR, sizeof(a));
    memcpy(&b, s_fram.data() + FRAM_SONG_HEADER_B_ADDR, sizeof(b));
    const uint32_t newest_address = (int32_t)(b.generation - a.generation) > 0 ?
                                    FRAM_SONG_HEADER_B_ADDR : FRAM_SONG_HEADER_A_ADDR;
    s_fram[newest_address] = 0u;
    Check(PersistenceStore_SelectSongHeader(&selected, &selected_generation) == SONG_HEADER_SELECT_VALID,
          "one valid and one invalid Song Header selects the valid record");

    ResetBlankFram();
        Check(PersistenceStore_FactoryInitialize(nullptr) == PERSISTENCE_BOOT_READY,
            "equal Song test store initializes");
    memcpy(s_fram.data() + FRAM_SONG_HEADER_A_ADDR,
           s_fram.data() + FRAM_SONG_HEADER_B_ADDR,
           FRAM_SONG_HEADER_RECORD_SIZE);
    Check(PersistenceStore_SelectSongHeader(&selected, &selected_generation) == SONG_HEADER_SELECT_VALID,
          "equal generation and equal Song payload is accepted");

    Check(PersistenceStore_SelectSongHeader(&current, &generation) == SONG_HEADER_SELECT_VALID,
          "equal conflict baseline selects");
    current.global_bpm++;
    Check(PersistenceStore_WriteSongHeader(&current, generation),
          "equal-generation conflicting Song record is physically valid");
    Check(PersistenceStore_SelectSongHeader(&selected, &selected_generation) ==
          SONG_HEADER_SELECT_INTEGRITY_FAULT,
          "equal generation and differing Song payload reports integrity fault");
}

void TestUnsupportedAndReadyCorruption(void)
{
    ResetBlankFram();
    SchemaRecordView unsupported{};
    unsupported.magic = kSchemaMagic;
    unsupported.version = 99u;
    unsupported.record_size = sizeof(unsupported);
    memcpy(s_fram.data() + FRAM_SCHEMA_RECORD_A_ADDR, &unsupported, sizeof(unsupported));
    Check(PersistenceStore_Bootstrap() == PERSISTENCE_BOOT_UNSUPPORTED_SCHEMA,
          "unsupported schema is not treated as blank");

    ResetBlankFram();
        Check(PersistenceStore_FactoryInitialize(nullptr) == PERSISTENCE_BOOT_READY,
            "corruption test store initializes");
    const uint32_t corrupt_address = SlotAddress(9u) + 20u;
    s_fram[corrupt_address] ^= 0x01u;
    const uint32_t writes_before = s_write_counts[SlotAddress(9u)];
    SimulateReset();
        Check(PersistenceStore_Bootstrap() == PERSISTENCE_BOOT_READY,
            "READY boot skips redundant full-pattern validation");
        Check(!PatternStore_IsValid(9u),
            "corrupted Pattern remains detectable when its slot is checked");
    Check(s_write_counts[SlotAddress(9u)] == writes_before,
            "READY-store corruption does not regenerate defaults");
}

void TestFramNotReady(void)
{
    ResetBlankFram();
    s_ready = false;
    Check(PersistenceStore_Bootstrap() == PERSISTENCE_BOOT_FRAM_NOT_READY,
          "FRAM readiness failure is explicit");
    s_ready = true;
}

    void TestFactoryWriteContainment(void)
    {
        ResetBlankFram();
        for (uint32_t address = 0u; address < FRAM_PATTERNSTORE_ADDR; ++address)
          s_fram[address] = (uint8_t)((address * 37u + 11u) & 0xFFu);
        for (uint32_t address = FRAM_SCHEMA_RECORD_A_ADDR;
             address < FRAM_SONG_HEADER_A_ADDR;
             ++address)
            s_fram[address] = (uint8_t)((address * 19u + 7u) & 0xFFu);
        std::array<uint8_t, FRAM_PATTERNSTORE_ADDR> lower_before{};
        memcpy(lower_before.data(), s_fram.data(), lower_before.size());

        Check(PersistenceStore_Inspect() == PERSISTENCE_BOOT_SCHEMA_INTEGRITY_FAULT,
            "arbitrary nonblank schema is not silently initialized");
        Check(PersistenceStore_FactoryInitialize(nullptr) == PERSISTENCE_BOOT_READY,
            "explicit factory initialization accepts unknown prior musical storage");
        Check(!s_forbidden_write, "factory write backend observed no out-of-range write");
            Check(!s_ready_before_required_data,
                "factory writes READY only after Pattern and Song records exist");
        Check(memcmp(lower_before.data(), s_fram.data(), lower_before.size()) == 0,
            "factory initialization preserves complete lower 32 KiB byte-for-byte");
        Check(PersistenceStore_Bootstrap() == PERSISTENCE_BOOT_READY,
            "factory-created store boots as current READY schema");
    }

        void TestFactoryEarlyFailureResume(void)
        {
            ResetBlankFram();
            s_fail_write_once_at = FRAM_SCHEMA_RECORD_A_ADDR;
            Check(PersistenceStore_FactoryInitialize(nullptr) == PERSISTENCE_BOOT_IO_FAULT,
                "failure before first formatting marker is reported");
            Check(PersistenceStore_Inspect() == PERSISTENCE_BOOT_FACTORY_REQUIRED,
                "failed first formatting marker does not authorize automatic initialization");

            ResetBlankFram();
            s_fail_write_once_at = FRAM_PATTERNSTORE_JOURNAL_ADDR;
            Check(PersistenceStore_FactoryInitialize(nullptr) == PERSISTENCE_BOOT_IO_FAULT,
                "journal invalidation failure is reported");
            SimulateReset();
            Check(PersistenceStore_Bootstrap() == PERSISTENCE_BOOT_READY,
                "validated formatting marker resumes after journal invalidation failure");

            ResetBlankFram();
            s_fail_write_once_at = SlotAddress(5u);
            Check(PersistenceStore_FactoryInitialize(nullptr) == PERSISTENCE_BOOT_IO_FAULT,
                "Pattern invalidation failure is reported");
            SimulateReset();
            Check(PersistenceStore_Bootstrap() == PERSISTENCE_BOOT_READY,
                "formatting resumes after Pattern invalidation failure");
        }

    void TestSongHeaderRetryState(void)
    {
        PersistenceRetryState retry;
        retry.MarkPending(100u, 1500u);
        Check(retry.IsPending() && !retry.IsDue(1599u),
            "Song Header mutation remains pending until debounce expires");
        Check(retry.IsDue(1600u), "Song Header mutation becomes due for persistence");

            Check(!retry.CompleteAttempt(false, 1600u, 1500u) &&
                retry.IsPending() && !retry.IsDue(3099u),
            "failed Song Header save remains pending with bounded retry delay");
        Check(retry.IsDue(3100u), "failed Song Header save becomes retryable later");

            Check(!retry.CompleteAttempt(false, 3100u, 1500u) &&
                retry.IsPending() && !retry.IsDue(4599u) && retry.IsDue(4600u),
            "repeated Song Header failures remain pending without a tight loop");

            Check(!retry.CompleteAttempt(false, 3200u, 1500u) && retry.IsPending(),
                "forced-flush reports failure and preserves Song Header pending state");
            Check(retry.CompleteAttempt(true, 4700u, 1500u) &&
                !retry.IsPending() && !retry.IsDue(10000u),
            "successful durable Song Header save clears pending state");
    }

} // namespace

int main()
{
    TestBlankInitialization();
    TestPatternInitializationResume();
    TestSongHeaderInitializationResume();
    TestReadyMarkerInterruptions();
    TestSongHeaderAB();
    TestUnsupportedAndReadyCorruption();
    TestFramNotReady();
    TestFactoryWriteContainment();
    TestFactoryEarlyFailureResume();
    TestSongHeaderRetryState();
    std::printf("%s: %d failure(s)\n", s_failures == 0 ? "PASS" : "FAIL", s_failures);
    return s_failures == 0 ? 0 : 1;
}
