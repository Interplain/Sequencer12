#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#define PATTERN_STORE_HOST_TEST 1
#include "devices/sequencer/sequencer_device.h"
#include "platform/fram/pattern_residency.h"
#include "platform/fram/pattern_store.h"
#include "platform/fram/persistence_store.h"
#include "platform/fram/storage_boot_probe.h"
#include "stm32/storage_boot.h"

namespace
{

constexpr uint32_t kPatternStoreMagic = 0x53313250u;
constexpr uint32_t kJournalMagic = 0x5331324Au;
constexpr uint8_t kSlotReadEvent = 100u;

std::array<uint8_t, 0x20000u> s_fram;
bool s_ready = true;
uint32_t s_write_count = 0u;
uint32_t s_fail_write_once_at = 0xFFFFFFFFu;
std::vector<uint8_t> s_events;
int s_failures = 0;

void Check(bool condition, const char* name)
{
    std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", name);
    if (!condition) ++s_failures;
}

void RecordProbe(StorageBootProbeEvent event) { s_events.push_back((uint8_t)event); }

uint32_t SlotAddress(uint8_t index)
{
    return FRAM_PATTERNSTORE_ADDR + (uint32_t)index * FRAM_PATTERNSTORE_SLOT_STRIDE;
}

uint32_t ReadU32(uint32_t address)
{
    uint32_t value = 0u;
    memcpy(&value, s_fram.data() + address, sizeof(value));
    return value;
}

bool JournalValidMagic() { return ReadU32(FRAM_PATTERNSTORE_JOURNAL_ADDR) == kJournalMagic; }

void SimulatePowerCycle()
{
    s_fail_write_once_at = 0xFFFFFFFFu;
    PatternStore_ResetRuntimeStateForTest();
    PersistenceStore_ResetRuntimeStateForTest();
    PatternResidency_ResetForTest();
    s_events.clear();
    s_write_count = 0u;
}

sequencer::Pattern MakeJournalPattern()
{
    sequencer::Pattern pattern{};
    pattern.step_count = 5u;
    pattern.step_division = 1u;
    pattern.playback_mode = sequencer::PlaybackMode::Loop;
    pattern.repeat_count = 3u;
    pattern.steps[0].bar_state = sequencer::BarState::Active;
    pattern.steps[0].note_ledger[0].notes = {{60u, 64u, 67u, 0xFFu}};
    pattern.steps[2].note_ledger[4].notes = {{50u, 0xFFu, 0xFFu, 0xFFu}};
    return pattern;
}

bool FactoryStore()
{
    s_fram.fill(0xFFu);
    s_ready = true;
    SimulatePowerCycle();
    const bool ok = PersistenceStore_FactoryInitialize(nullptr) == PERSISTENCE_BOOT_READY;
    SimulatePowerCycle();
    return ok;
}

enum class JournalStop { InvalidDestination, CommittedDestination };

/* Runs the production async writer for Pattern 0 and "loses power" at the requested point. */
bool LeaveJournal(const sequencer::Pattern& pattern, uint32_t generation, JournalStop stop)
{
    if (!PatternStore_WriteAsyncBeginGeneration(0u, &pattern, generation)) return false;
    for (uint32_t guard = 0u; guard < 10000u; ++guard)
    {
        if (PatternStore_WriteAsyncService() != PATTERNSTORE_ASYNC_BUSY) return false;
        const uint32_t magic = ReadU32(SlotAddress(0u));
        const uint32_t slot_generation = ReadU32(SlotAddress(0u) + 8u);
        if (stop == JournalStop::InvalidDestination && magic == 0u) break;
        if (stop == JournalStop::CommittedDestination && magic == kPatternStoreMagic &&
            slot_generation == generation) break;
    }
    SimulatePowerCycle();
    return JournalValidMagic();
}

int FirstIndex(uint8_t event)
{
    for (size_t i = 0u; i < s_events.size(); ++i)
        if (s_events[i] == event) return (int)i;
    return -1;
}

bool Contains(uint8_t event) { return FirstIndex(event) >= 0; }

void CheckOrdering(const char* label)
{
    const int recovered = FirstIndex(STORAGE_BOOT_PROBE_RECOVERY_RESOLVED);
    const int lookup = FirstIndex(STORAGE_BOOT_PROBE_DIRECTORY_LOOKUP);
    const int load = FirstIndex(STORAGE_BOOT_PROBE_PATTERN_LOAD);
    bool slot_reads_ordered = true;
    for (size_t i = 0u; i < s_events.size(); ++i)
    {
        if (s_events[i] == kSlotReadEvent && (int)i > recovered && (int)i < lookup)
            slot_reads_ordered = false;
    }
    std::printf("  ordering (%s): recovery=%d directory=%d load=%d\n", label, recovered, lookup, load);
    Check(recovered >= 0 && lookup > recovered && load > lookup,
          "RecoverJournal completes BEFORE directory lookup BEFORE playback Pattern load");
    Check(slot_reads_ordered,
          "no PatternStore destination read between recovery completion and first directory lookup");
}

void CheckResidencyStarted(SequencerDevice& device, PersistenceBootStatus status)
{
    Check(status == PERSISTENCE_BOOT_READY && PersistenceStore_GetBootStatus() == PERSISTENCE_BOOT_READY,
          "boot reports READY");
    Check(PatternResidency_GetRoleEntry(PatternCacheRole::Play) >= 0 &&
          PatternResidency_GetRoleEntry(PatternCacheRole::Edit) >= 0 &&
          PatternResidency_GetRoleEntry(PatternCacheRole::Next) >= 0,
          "Pattern residency starts with PLAY/EDIT/NEXT roles");
    (void)device;
}

void CheckFailedClosed(PersistenceBootStatus status, PersistenceBootStatus expected, const char* label)
{
    Check(status == expected, label);
    Check(status != PERSISTENCE_BOOT_READY, "boot does not report READY");
    Check(PatternResidency_GetRoleEntry(PatternCacheRole::Play) < 0 &&
          PatternResidency_GetRoleEntry(PatternCacheRole::Edit) < 0 &&
          PatternResidency_GetRoleEntry(PatternCacheRole::Next) < 0,
          "Pattern residency does not start");
    Check(!Contains(STORAGE_BOOT_PROBE_DIRECTORY_LOOKUP) && !Contains(STORAGE_BOOT_PROBE_PATTERN_LOAD),
          "no Pattern directory lookup or playback Pattern load");
}

bool RecoveredPatternMatches(const sequencer::Pattern& expected, uint32_t generation)
{
    sequencer::Pattern stored{};
    uint32_t stored_generation = 0u;
    if (!PatternStore_ReadWithGeneration(0u, &stored, &stored_generation)) return false;
    const sequencer::Pattern* play = PatternResidency_GetRolePatternConst(PatternCacheRole::Play);
    return stored_generation == generation && memcmp(&stored, &expected, sizeof(stored)) == 0 &&
           play && play->step_count == expected.step_count &&
           memcmp(play->steps[0].note_ledger.data(), expected.steps[0].note_ledger.data(),
                  sizeof(expected.steps[0].note_ledger)) == 0 &&
           memcmp(play->steps[2].note_ledger.data(), expected.steps[2].note_ledger.data(),
                  sizeof(expected.steps[2].note_ledger)) == 0;
}

void TestCleanStorage()
{
    std::printf("-- clean current storage, no journal\n");
    Check(FactoryStore(), "clean store prepared");
    SequencerDevice device;
    uint32_t bpm = 0u;
    uint32_t generation = 0u;
    const PersistenceBootStatus status = StorageBoot_ActivateRuntime(device, &bpm, &generation);
    CheckResidencyStarted(device, status);
    CheckOrdering("clean");
    Check(bpm == 120u && generation != 0u, "Song Header imported");
    Check(s_write_count == 0u, "clean boot performs no FRAM writes");
}

void TestReplayInvalidDestination()
{
    std::printf("-- invalid destination + valid journal\n");
    Check(FactoryStore(), "replay store prepared");
    const sequencer::Pattern pattern = MakeJournalPattern();
    Check(LeaveJournal(pattern, 50u, JournalStop::InvalidDestination) &&
          ReadU32(SlotAddress(0u)) == 0u,
          "power loss leaves a valid journal and invalid destination");

    SequencerDevice device;
    const PersistenceBootStatus status = StorageBoot_ActivateRuntime(device, nullptr, nullptr);
    CheckResidencyStarted(device, status);
    CheckOrdering("replay");
    Check(!JournalValidMagic(), "journal retired after replay");
    Check(RecoveredPatternMatches(pattern, 50u),
          "destination validated with journal payload; residency plays the recovered Pattern");
}

void TestRetireCommittedDestination()
{
    std::printf("-- valid matching destination + valid journal\n");
    Check(FactoryStore(), "retire store prepared");
    const sequencer::Pattern pattern = MakeJournalPattern();
    Check(LeaveJournal(pattern, 60u, JournalStop::CommittedDestination) &&
          ReadU32(SlotAddress(0u)) == kPatternStoreMagic,
          "power loss leaves a valid journal and committed destination");

    std::array<uint8_t, FRAM_PATTERNSTORE_SLOT_STRIDE> slot_before{};
    memcpy(slot_before.data(), s_fram.data() + SlotAddress(0u), slot_before.size());
    SequencerDevice device;
    const PersistenceBootStatus status = StorageBoot_ActivateRuntime(device, nullptr, nullptr);
    CheckResidencyStarted(device, status);
    CheckOrdering("retire");
    Check(!JournalValidMagic(), "completed transaction recognised and journal retired");
    Check(memcmp(slot_before.data(), s_fram.data() + SlotAddress(0u), slot_before.size()) == 0,
          "committed destination is not rewritten");
    Check(RecoveredPatternMatches(pattern, 60u), "residency plays the committed Pattern");
}

void TestInterruptedRecoveryRetries()
{
    std::printf("-- interrupted recovery, then reboot\n");
    Check(FactoryStore(), "interrupted-recovery store prepared");
    const sequencer::Pattern pattern = MakeJournalPattern();
    Check(LeaveJournal(pattern, 70u, JournalStop::InvalidDestination),
          "valid journal awaiting replay");

    s_fail_write_once_at = SlotAddress(0u) + 20u;
    SequencerDevice first;
    const PersistenceBootStatus failed = StorageBoot_ActivateRuntime(first, nullptr, nullptr);
    CheckFailedClosed(failed, PERSISTENCE_BOOT_IO_FAULT, "recovery write failure fails closed as IO fault");
    Check(PersistenceStore_GetBootStatus() != PERSISTENCE_BOOT_READY,
          "persistence layer does not report READY after recovery failure");
    Check(JournalValidMagic(), "journal is retained after interrupted recovery");

    SimulatePowerCycle();
    SequencerDevice second;
    const PersistenceBootStatus status = StorageBoot_ActivateRuntime(second, nullptr, nullptr);
    CheckResidencyStarted(second, status);
    CheckOrdering("retry");
    Check(!JournalValidMagic() && RecoveredPatternMatches(pattern, 70u),
          "rebooted recovery safely replays and retires the journal");
}

void TestUnrecoverableJournal()
{
    std::printf("-- unrecoverable recovery condition\n");
    Check(FactoryStore(), "unrecoverable store prepared");
    Check(LeaveJournal(MakeJournalPattern(), 80u, JournalStop::InvalidDestination),
          "valid journal awaiting replay");
    s_fram[FRAM_PATTERNSTORE_JOURNAL_ADDR + 24u + 100u] ^= 0x01u;
    const auto image_before = s_fram;

    for (uint8_t boot = 0u; boot < 2u; ++boot)
    {
        SimulatePowerCycle();
        SequencerDevice device;
        const PersistenceBootStatus status = StorageBoot_ActivateRuntime(device, nullptr, nullptr);
        CheckFailedClosed(status, PERSISTENCE_BOOT_PATTERN_INTEGRITY_FAULT,
                          "corrupt journal payload fails closed as Pattern integrity fault");
        Check(PersistenceStore_GetBootStatus() != PERSISTENCE_BOOT_READY,
              "persistence layer does not report READY");
        Check(!Contains(STORAGE_BOOT_PROBE_RECOVERY_RESOLVED), "recovery is not reported resolved");
        Check(s_write_count == 0u && s_fram == image_before,
              "unrecoverable journal causes no erase, discard or other FRAM write");
    }
}

void TestSchemaFailures()
{
    std::printf("-- invalid / incompatible schema\n");

    s_fram.fill(0xFFu);
    SimulatePowerCycle();
    {
        const auto before = s_fram;
        SequencerDevice device;
        CheckFailedClosed(StorageBoot_ActivateRuntime(device, nullptr, nullptr),
                          PERSISTENCE_BOOT_FACTORY_REQUIRED, "blank storage requires user factory init");
        Check(s_write_count == 0u && s_fram == before, "blank storage is not automatically initialised");
    }

    Check(FactoryStore(), "unsupported-schema store prepared");
    {
        uint16_t version = 99u;
        memcpy(s_fram.data() + FRAM_SCHEMA_RECORD_A_ADDR + 4u, &version, sizeof(version));
        memcpy(s_fram.data() + FRAM_SCHEMA_RECORD_B_ADDR + 4u, &version, sizeof(version));
        const auto before = s_fram;
        SequencerDevice device;
        CheckFailedClosed(StorageBoot_ActivateRuntime(device, nullptr, nullptr),
                          PERSISTENCE_BOOT_UNSUPPORTED_SCHEMA, "unsupported schema fails closed");
        Check(s_write_count == 0u && s_fram == before, "unsupported schema causes no automatic erase");
    }

    Check(FactoryStore(), "corrupt-schema store prepared");
    {
        s_fram[FRAM_SCHEMA_RECORD_A_ADDR + 12u] ^= 0x5Au;
        s_fram[FRAM_SCHEMA_RECORD_B_ADDR + 12u] ^= 0x5Au;
        const auto before = s_fram;
        SequencerDevice device;
        CheckFailedClosed(StorageBoot_ActivateRuntime(device, nullptr, nullptr),
                          PERSISTENCE_BOOT_SCHEMA_INTEGRITY_FAULT, "corrupt schema fails closed");
        Check(s_write_count == 0u && s_fram == before, "corrupt schema causes no automatic erase");
    }

    Check(FactoryStore(), "corrupt-Song-Header store prepared");
    {
        s_fram[FRAM_SONG_HEADER_A_ADDR] ^= 0xFFu;
        s_fram[FRAM_SONG_HEADER_B_ADDR] ^= 0xFFu;
        const auto before = s_fram;
        SequencerDevice device;
        CheckFailedClosed(StorageBoot_ActivateRuntime(device, nullptr, nullptr),
                          PERSISTENCE_BOOT_SONG_HEADER_INTEGRITY_FAULT, "missing Song Header fails closed");
        Check(s_write_count == 0u && s_fram == before, "missing Song Header causes no automatic rewrite");
    }

    Check(FactoryStore(), "corrupt-Pattern-0 store prepared");
    {
        s_fram[SlotAddress(0u) + 20u + 7u] ^= 0x01u;
        const auto before = s_fram;
        SequencerDevice device;
        const PersistenceBootStatus status = StorageBoot_ActivateRuntime(device, nullptr, nullptr);
        Check(status == PERSISTENCE_BOOT_PATTERN_INTEGRITY_FAULT,
              "corrupt initial Pattern fails closed after recovery");
        Check(PatternResidency_GetRoleEntry(PatternCacheRole::Play) < 0,
              "residency does not run on corrupt initial Pattern");
        Check(s_write_count == 0u && s_fram == before, "corrupt Pattern causes no automatic regeneration");
    }

    s_fram.fill(0xFFu);
    SimulatePowerCycle();
    s_ready = false;
    {
        SequencerDevice device;
        CheckFailedClosed(StorageBoot_ActivateRuntime(device, nullptr, nullptr),
                          PERSISTENCE_BOOT_FRAM_NOT_READY, "unavailable FRAM fails closed");
    }
    s_ready = true;
}

} // namespace

extern "C" uint8_t PatternStoreBackend_IsReady(void) { return s_ready ? 1u : 0u; }

extern "C" uint8_t PatternStoreBackend_Read(uint32_t address, uint8_t* dst, uint16_t len)
{
    if (!s_ready || !dst || address + len > s_fram.size()) return 0u;
    if (address >= FRAM_PATTERNSTORE_ADDR && address < FRAM_PATTERNSTORE_END)
        s_events.push_back(kSlotReadEvent);
    memcpy(dst, s_fram.data() + address, len);
    return 1u;
}

extern "C" uint8_t PatternStoreBackend_Write(uint32_t address, const uint8_t* src, uint16_t len)
{
    if (!s_ready || !src || address + len > s_fram.size()) return 0u;
    if (address == s_fail_write_once_at)
    {
        s_fail_write_once_at = 0xFFFFFFFFu;
        return 0u;
    }
    ++s_write_count;
    memcpy(s_fram.data() + address, src, len);
    return 1u;
}

int main()
{
    g_storage_boot_probe = RecordProbe;
    TestCleanStorage();
    TestReplayInvalidDestination();
    TestRetireCommittedDestination();
    TestInterruptedRecoveryRetries();
    TestUnrecoverableJournal();
    TestSchemaFailures();
    std::printf("%s: %d failure(s)\n", s_failures == 0 ? "PASS" : "FAIL", s_failures);
    return s_failures == 0 ? 0 : 1;
}
