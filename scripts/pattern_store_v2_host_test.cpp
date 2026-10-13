#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

#define PATTERN_STORE_HOST_TEST 1
#include "platform/fram/pattern_store.h"

static std::array<uint8_t, 0x20000u> s_fram;
static uint32_t s_corrupt_destination_after_header = 0xFFFFFFFFu;
static uint32_t s_fail_write_once_at = 0xFFFFFFFFu;

extern "C" uint8_t PatternStoreBackend_Read(uint32_t address, uint8_t* dst, uint16_t len)
{
    if (!dst || address + len > s_fram.size()) return 0u;
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
    if (address == s_corrupt_destination_after_header && len == 20u)
    {
        s_fram[address + 20u] ^= 0x80u;
        s_corrupt_destination_after_header = 0xFFFFFFFFu;
    }
    return 1u;
}

#include "platform/fram/pattern_store.cpp"

namespace
{

struct StoreHeader
{
    uint32_t magic;
    uint16_t version;
    uint16_t payload_size;
    uint32_t generation;
    uint32_t payload_crc32;
    uint32_t header_crc32;
};

struct JournalHeader
{
    uint32_t magic;
    uint16_t version;
    uint16_t payload_size;
    uint8_t pattern_index;
    uint8_t reserved[3];
    uint32_t generation;
    uint32_t payload_crc32;
    uint32_t header_crc32;
};

int s_failures = 0;

void Check(bool condition, const char* name)
{
    std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", name);
    if (!condition) ++s_failures;
}

void ResetFram(void)
{
    s_fram.fill(0xFFu);
    PatternStore_ResetRuntimeStateForTest();
    s_corrupt_destination_after_header = 0xFFFFFFFFu;
    s_fail_write_once_at = 0xFFFFFFFFu;
}

sequencer::Pattern MakePattern(uint8_t seed)
{
    sequencer::Pattern pattern{};
    pattern.step_count = (uint8_t)(1u + seed % 12u);
    pattern.step_division = 4u;
    pattern.repeat_count = (uint8_t)(1u + seed % 16u);
    pattern.steps[0].velocity = (uint8_t)(80u + seed);
    pattern.steps[0].note_ledger[0].notes[0] = (uint8_t)(48u + seed);
    pattern.steps[0].note_mask = (uint16_t)(1u << ((48u + seed) % 12u));
    pattern.steps[0].type = sequencer::StepType::Chord;
    return pattern;
}

bool Equal(const sequencer::Pattern& a, const sequencer::Pattern& b)
{
    return memcmp(&a, &b, sizeof(a)) == 0;
}

PatternStoreAsyncStatus PumpWrite(void)
{
    PatternStoreAsyncStatus status = PATTERNSTORE_ASYNC_BUSY;
    for (uint32_t guard = 0u; guard < 1000u && status == PATTERNSTORE_ASYNC_BUSY; ++guard)
        status = PatternStore_WriteAsyncService();
    return status;
}

bool PumpUntilJournalCommitted(void)
{
    for (uint32_t guard = 0u; guard < 200u; ++guard)
    {
        const PatternStoreAsyncStatus status = PatternStore_WriteAsyncService();
        JournalHeader header{};
        memcpy(&header, s_fram.data() + FRAM_PATTERNSTORE_JOURNAL_ADDR, sizeof(header));
        if (header.magic == 0x5331324Au) return true;
        if (status == PATTERNSTORE_ASYNC_FAILED) return false;
    }
    return false;
}

void TestRecordValidation(void)
{
    ResetFram();
    const auto pattern = MakePattern(1u);
    sequencer::Pattern out{};
    uint32_t generation = 0u;
    Check(PatternStore_WriteGeneration(3u, &pattern, 7u), "V2 transaction write succeeds");
    Check(PatternStore_ReadWithGeneration(3u, &out, &generation) && Equal(pattern, out),
          "V2 record reads back");
    Check(generation == 7u, "generation is preserved");

    const uint32_t base = FRAM_PATTERNSTORE_ADDR + 3u * FRAM_PATTERNSTORE_SLOT_STRIDE;
    s_fram[base + 12u] ^= 0x01u;
    Check(!PatternStore_IsValid(3u), "header CRC rejects changed generation bytes");

    Check(PatternStore_WriteGeneration(3u, &pattern, 8u), "record can be rewritten after header corruption");
    s_fram[base + sizeof(StoreHeader) + 10u] ^= 0x01u;
    Check(!PatternStore_IsValid(3u), "payload CRC rejects changed payload");
}

void TestDestinationValidation(void)
{
    ResetFram();
    const auto pattern = MakePattern(2u);
    const uint32_t base = FRAM_PATTERNSTORE_ADDR + 4u * FRAM_PATTERNSTORE_SLOT_STRIDE;
    s_corrupt_destination_after_header = base;
    Check(PatternStore_WriteAsyncBeginGeneration(4u, &pattern, 9u), "async transaction begins");
    Check(PumpWrite() == PATTERNSTORE_ASYNC_FAILED,
          "destination corruption prevents durable success");
    JournalHeader journal{};
    memcpy(&journal, s_fram.data() + FRAM_PATTERNSTORE_JOURNAL_ADDR, sizeof(journal));
    Check(journal.magic == 0x5331324Au, "validated journal remains after destination failure");
    Check(!PatternStore_WriteAsyncBeginGeneration(4u, &pattern, 10u),
          "new transaction cannot overwrite committed recovery journal");
}

void TestRecovery(void)
{
    ResetFram();
    const auto old_pattern = MakePattern(3u);
    const auto new_pattern = MakePattern(4u);
    Check(PatternStore_WriteGeneration(5u, &old_pattern, 10u), "old destination prepared");

    Check(PatternStore_WriteAsyncBeginGeneration(5u, &new_pattern, 11u), "new transaction begins");
    Check(PumpUntilJournalCommitted(), "journal commit is observable");
    PatternStore_ResetRuntimeStateForTest();
    Check(PatternStore_RecoverJournal() == PATTERNSTORE_RECOVERY_REPLAYED,
          "valid newer journal replays over destination");
    sequencer::Pattern out{};
    uint32_t generation = 0u;
    Check(PatternStore_ReadWithGeneration(5u, &out, &generation) && Equal(new_pattern, out) && generation == 11u,
          "recovered destination has journal payload and generation");

    Check(PatternStore_WriteGeneration(5u, &old_pattern, 12u), "newer destination prepared");
    Check(PatternStore_WriteAsyncBeginGeneration(5u, &new_pattern, 11u), "stale journal transaction begins");
    Check(PumpUntilJournalCommitted(), "stale journal commits");
    PatternStore_ResetRuntimeStateForTest();
    Check(PatternStore_RecoverJournal() == PATTERNSTORE_RECOVERY_RETIRED,
          "stale journal retires when destination generation is newer");

    Check(PatternStore_WriteAsyncBeginGeneration(5u, &old_pattern, 12u), "equal journal transaction begins");
    Check(PumpUntilJournalCommitted(), "equal journal commits");
    PatternStore_ResetRuntimeStateForTest();
    Check(PatternStore_RecoverJournal() == PATTERNSTORE_RECOVERY_RETIRED,
          "equal generation and CRC retires journal");

    Check(PatternStore_WriteAsyncBeginGeneration(5u, &new_pattern, 12u), "conflicting equal journal begins");
    Check(PumpUntilJournalCommitted(), "conflicting equal journal commits");
    PatternStore_ResetRuntimeStateForTest();
    Check(PatternStore_RecoverJournal() == PATTERNSTORE_RECOVERY_INTEGRITY_FAULT,
          "equal generation and different CRC reports integrity fault");
}

void TestInterruptedJournalIgnored(void)
{
    ResetFram();
    const auto pattern = MakePattern(5u);
    Check(PatternStore_WriteGeneration(6u, &pattern, 20u), "valid destination prepared");
    s_fram[FRAM_PATTERNSTORE_JOURNAL_ADDR + sizeof(JournalHeader)] = 0x22u;
    Check(PatternStore_RecoverJournal() == PATTERNSTORE_RECOVERY_NONE,
          "uncommitted journal payload is ignored");
    sequencer::Pattern out{};
    Check(PatternStore_Read(6u, &out) && Equal(pattern, out),
          "old destination remains valid after interrupted journal");
}

void TestRetirementFailure(void)
{
    ResetFram();
    const auto pattern = MakePattern(8u);
    const auto next_pattern = MakePattern(9u);
    Check(PatternStore_WriteAsyncBeginGeneration(8u, &pattern, 40u),
          "retirement-failure transaction begins");

    PatternStoreAsyncStatus status = PATTERNSTORE_ASYNC_BUSY;
    bool destination_valid = false;
    const uint32_t destination = FRAM_PATTERNSTORE_ADDR + 8u * FRAM_PATTERNSTORE_SLOT_STRIDE;
    for (uint32_t guard = 0u; guard < 1000u && status == PATTERNSTORE_ASYNC_BUSY; ++guard)
    {
        StoreHeader header{};
        memcpy(&header, s_fram.data() + destination, sizeof(header));
        if (header.magic == 0x53313250u)
        {
            destination_valid = true;
            s_fail_write_once_at = FRAM_PATTERNSTORE_JOURNAL_ADDR;
        }
        status = PatternStore_WriteAsyncService();
    }

    Check(destination_valid && status == PATTERNSTORE_ASYNC_DONE,
          "validated destination reports durable success when retirement is interrupted");
        Check(PatternStore_GetRuntimeRecoveryStatus() == PATTERNSTORE_RECOVERY_FAILED,
            "retirement failure remains known after durable destination success");

        Check(!PatternStore_WriteAsyncBeginGeneration(9u, &next_pattern, 41u) &&
            PatternStore_WriteAsyncBusy(),
            "next write starts runtime reconciliation before journal reuse");
        Check(PumpWrite() == PATTERNSTORE_ASYNC_DONE &&
            PatternStore_GetRuntimeRecoveryStatus() == PATTERNSTORE_RECOVERY_RETIRED,
            "runtime reconciliation validates destination and retires equal journal");
        Check(PatternStore_WriteAsyncBeginGeneration(9u, &next_pattern, 41u) &&
            PumpWrite() == PATTERNSTORE_ASYNC_DONE,
            "another Pattern transaction completes without reboot");

        sequencer::Pattern out{};
        uint32_t generation = 0u;
        Check(PatternStore_ReadWithGeneration(8u, &out, &generation) &&
            generation == 40u && Equal(out, pattern),
            "generation G remains durably committed after cleanup recovery");
        Check(PatternStore_ReadWithGeneration(9u, &out, &generation) &&
            generation == 41u && Equal(out, next_pattern),
            "post-reconciliation Pattern transaction validates");
}

        void TestRuntimeReconciliationCases(void)
        {
            const auto destination_pattern = MakePattern(10u);
            const auto journal_pattern = MakePattern(11u);
            const auto later_pattern = MakePattern(12u);

            ResetFram();
            Check(PatternStore_WriteGeneration(10u, &destination_pattern, 60u),
                "runtime stale-journal destination prepared");
            Check(PatternStore_WriteAsyncBeginGeneration(10u, &journal_pattern, 59u) &&
                PumpUntilJournalCommitted(), "runtime stale journal committed");
            PatternStore_ResetRuntimeStateForTest();
            Check(!PatternStore_WriteAsyncBeginGeneration(11u, &later_pattern, 61u) &&
                PatternStore_WriteAsyncBusy() && PumpWrite() == PATTERNSTORE_ASYNC_DONE &&
                PatternStore_GetRuntimeRecoveryStatus() == PATTERNSTORE_RECOVERY_RETIRED,
                "runtime reconciliation retires journal older than validated destination");
            sequencer::Pattern out{};
            uint32_t generation = 0u;
            Check(PatternStore_ReadWithGeneration(10u, &out, &generation) &&
                generation == 60u && Equal(out, destination_pattern),
                "stale-journal reconciliation preserves newer destination");

            ResetFram();
            Check(PatternStore_WriteGeneration(10u, &destination_pattern, 70u),
                "runtime replay destination prepared");
            Check(PatternStore_WriteAsyncBeginGeneration(10u, &journal_pattern, 71u) &&
                PumpUntilJournalCommitted(), "runtime newer journal committed");
            s_fram[FRAM_PATTERNSTORE_ADDR + 10u * FRAM_PATTERNSTORE_SLOT_STRIDE +
                 sizeof(StoreHeader) + 5u] ^= 0x01u;
            PatternStore_ResetRuntimeStateForTest();
            Check(!PatternStore_WriteAsyncBeginGeneration(11u, &later_pattern, 72u) &&
                PatternStore_WriteAsyncBusy() && PumpWrite() == PATTERNSTORE_ASYNC_DONE &&
                PatternStore_GetRuntimeRecoveryStatus() == PATTERNSTORE_RECOVERY_REPLAYED,
                "runtime reconciliation replays newer journal over incomplete destination");
            Check(PatternStore_ReadWithGeneration(10u, &out, &generation) &&
                generation == 71u && Equal(out, journal_pattern),
                "runtime journal replay validates recovered destination");
            Check(PatternStore_WriteAsyncBeginGeneration(11u, &later_pattern, 72u) &&
                PumpWrite() == PATTERNSTORE_ASYNC_DONE,
                "write proceeds after runtime journal replay");

            ResetFram();
            Check(PatternStore_WriteGeneration(10u, &destination_pattern, 80u),
                "runtime conflict destination prepared");
            Check(PatternStore_WriteAsyncBeginGeneration(10u, &journal_pattern, 80u) &&
                PumpUntilJournalCommitted(), "runtime conflicting journal committed");
            PatternStore_ResetRuntimeStateForTest();
            Check(!PatternStore_WriteAsyncBeginGeneration(11u, &later_pattern, 81u) &&
                PatternStore_WriteAsyncBusy() && PumpWrite() == PATTERNSTORE_ASYNC_FAILED &&
                PatternStore_GetRuntimeRecoveryStatus() == PATTERNSTORE_RECOVERY_INTEGRITY_FAULT,
                "runtime equal-generation different-CRC conflict is explicit");
            Check(!PatternStore_WriteAsyncBeginGeneration(11u, &later_pattern, 81u) &&
                !PatternStore_WriteAsyncBusy(),
                "runtime integrity fault blocks unchecked journal replacement");

            ResetFram();
            s_fram[FRAM_PATTERNSTORE_JOURNAL_ADDR] = 0x4Au;
            Check(PatternStore_WriteAsyncBeginGeneration(11u, &later_pattern, 90u) &&
                PumpWrite() == PATTERNSTORE_ASYNC_DONE,
                "invalid uncommitted journal is safely replaced by a new transaction");
        }

void TestEveryServiceInterruption(void)
{
    const auto old_pattern = MakePattern(6u);
    const auto new_pattern = MakePattern(7u);

    ResetFram();
    Check(PatternStore_WriteGeneration(7u, &old_pattern, 30u), "fault sweep baseline prepared");
    Check(PatternStore_WriteAsyncBeginGeneration(7u, &new_pattern, 31u), "fault sweep transaction begins");
    uint32_t service_calls = 0u;
    PatternStoreAsyncStatus status = PATTERNSTORE_ASYNC_BUSY;
    while (status == PATTERNSTORE_ASYNC_BUSY && service_calls < 1000u)
    {
        status = PatternStore_WriteAsyncService();
        ++service_calls;
    }
    Check(status == PATTERNSTORE_ASYNC_DONE, "fault sweep control transaction completes");

    bool all_ok = true;
    for (uint32_t cutoff = 0u; cutoff <= service_calls; ++cutoff)
    {
        ResetFram();
        if (!PatternStore_WriteGeneration(7u, &old_pattern, 30u) ||
            !PatternStore_WriteAsyncBeginGeneration(7u, &new_pattern, 31u))
        {
            all_ok = false;
            break;
        }

        bool committed_journal = false;
        for (uint32_t call = 0u; call < cutoff; ++call)
        {
            status = PatternStore_WriteAsyncService();
            JournalHeader journal{};
            memcpy(&journal, s_fram.data() + FRAM_PATTERNSTORE_JOURNAL_ADDR, sizeof(journal));
            if (journal.magic == 0x5331324Au) committed_journal = true;
            if (status != PATTERNSTORE_ASYNC_BUSY) break;
        }

        PatternStore_ResetRuntimeStateForTest();
        const PatternStoreRecoveryStatus recovery = PatternStore_RecoverJournal();
        sequencer::Pattern out{};
        uint32_t generation = 0u;
        if (!PatternStore_ReadWithGeneration(7u, &out, &generation))
        {
            all_ok = false;
            break;
        }

        const bool is_old = generation == 30u && Equal(out, old_pattern);
        const bool is_new = generation == 31u && Equal(out, new_pattern);
        if ((!is_old && !is_new) || (committed_journal && !is_new) ||
            recovery == PATTERNSTORE_RECOVERY_FAILED ||
            recovery == PATTERNSTORE_RECOVERY_INTEGRITY_FAULT)
        {
            all_ok = false;
            break;
        }
    }
    Check(all_ok, "reset after every async service step preserves old or commits new generation");
}

} // namespace

int main()
{
    TestRecordValidation();
    TestDestinationValidation();
    TestRecovery();
    TestInterruptedJournalIgnored();
    TestRetirementFailure();
    TestRuntimeReconciliationCases();
    TestEveryServiceInterruption();
    std::printf("%s: %d failure(s)\n", s_failures == 0 ? "PASS" : "FAIL", s_failures);
    return s_failures == 0 ? 0 : 1;
}
