#include "platform/fram/pattern_store.h"
#if defined(PATTERN_STORE_HOST_TEST)
extern "C" uint8_t PatternStoreBackend_Read(uint32_t address, uint8_t* dst, uint16_t len);
extern "C" uint8_t PatternStoreBackend_Write(uint32_t address, const uint8_t* src, uint16_t len);
#define MB85RC1MT_Read PatternStoreBackend_Read
#define MB85RC1MT_Write PatternStoreBackend_Write
#define MB85RC1MT_SIZE_BYTES 0x20000u
#else
#include "platform/fram/mb85rc1mt.h"
#endif
#include <cstddef>
#include <cstring>
#include <new>
#include "stm32/time_commit_probe.h"

namespace
{

struct PatternStoreHeader
{
    uint32_t magic;
    uint16_t version;
    uint16_t payload_size;
    uint32_t generation;
    uint32_t payload_crc32;
    uint32_t header_crc32;
};

struct PatternJournalHeader
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

constexpr uint32_t kPatternStoreMagic = 0x53313250u;
constexpr uint32_t kPatternJournalMagic = 0x5331324Au;
constexpr uint16_t kPatternStoreVersion = 2u;
constexpr uint32_t kInvalidMagic = 0u;

alignas(sequencer::Pattern) uint8_t s_pattern_store_scratch[sizeof(sequencer::Pattern)];
uint8_t s_verify_scratch[PATTERNSTORE_CHUNK_BYTES];

uint32_t Crc32Begin(void) { return 0xFFFFFFFFu; }

uint32_t Crc32Update(uint32_t crc, const uint8_t* data, uint32_t len)
{
    for (uint32_t i = 0u; i < len; ++i)
    {
        crc ^= data[i];
        for (uint8_t bit = 0u; bit < 8u; ++bit)
        {
            const uint32_t mask = (uint32_t)(-(int32_t)(crc & 1u));
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return crc;
}

uint32_t Crc32Finish(uint32_t crc) { return crc ^ 0xFFFFFFFFu; }

uint32_t Crc32Compute(const uint8_t* data, uint32_t len)
{
    return Crc32Finish(Crc32Update(Crc32Begin(), data, len));
}

uint32_t SlotAddress(uint8_t index)
{
    return FRAM_PATTERNSTORE_ADDR + (uint32_t)index * FRAM_PATTERNSTORE_SLOT_STRIDE;
}

uint32_t HeaderCrc(const PatternStoreHeader& header)
{
    return Crc32Compute(reinterpret_cast<const uint8_t*>(&header),
                        offsetof(PatternStoreHeader, header_crc32));
}

uint32_t HeaderCrc(const PatternJournalHeader& header)
{
    return Crc32Compute(reinterpret_cast<const uint8_t*>(&header),
                        offsetof(PatternJournalHeader, header_crc32));
}

bool HeaderValid(const PatternStoreHeader& header)
{
    return header.magic == kPatternStoreMagic &&
           header.version == kPatternStoreVersion &&
           header.payload_size == sizeof(sequencer::Pattern) &&
           header.generation != 0u &&
           header.header_crc32 == HeaderCrc(header);
}

bool HeaderValid(const PatternJournalHeader& header)
{
    return header.magic == kPatternJournalMagic &&
           header.version == kPatternStoreVersion &&
           header.payload_size == sizeof(sequencer::Pattern) &&
           header.pattern_index < FRAM_PATTERNSTORE_SLOT_COUNT &&
           header.generation != 0u &&
           header.header_crc32 == HeaderCrc(header);
}

PatternStoreHeader MakeStoreHeader(uint32_t generation, uint32_t payload_crc)
{
    PatternStoreHeader header{};
    header.magic = kPatternStoreMagic;
    header.version = kPatternStoreVersion;
    header.payload_size = (uint16_t)sizeof(sequencer::Pattern);
    header.generation = generation;
    header.payload_crc32 = payload_crc;
    header.header_crc32 = HeaderCrc(header);
    return header;
}

PatternJournalHeader MakeJournalHeader(uint8_t index, uint32_t generation, uint32_t payload_crc)
{
    PatternJournalHeader header{};
    header.magic = kPatternJournalMagic;
    header.version = kPatternStoreVersion;
    header.payload_size = (uint16_t)sizeof(sequencer::Pattern);
    header.pattern_index = index;
    header.generation = generation;
    header.payload_crc32 = payload_crc;
    header.header_crc32 = HeaderCrc(header);
    return header;
}

bool GenerationNewer(uint32_t candidate, uint32_t reference)
{
    return (int32_t)(candidate - reference) > 0;
}

bool ReadStoreHeader(uint8_t index, PatternStoreHeader* header)
{
    return index < FRAM_PATTERNSTORE_SLOT_COUNT && header &&
           MB85RC1MT_Read(SlotAddress(index), reinterpret_cast<uint8_t*>(header), sizeof(*header));
}

bool ReadJournalHeader(PatternJournalHeader* header)
{
    return header && MB85RC1MT_Read(FRAM_PATTERNSTORE_JOURNAL_ADDR,
                                    reinterpret_cast<uint8_t*>(header), sizeof(*header));
}

bool ValidatePayloadAt(uint32_t address, uint32_t expected_crc)
{
    uint32_t crc = Crc32Begin();
    uint32_t offset = 0u;
    while (offset < sizeof(sequencer::Pattern))
    {
        const uint16_t remaining = (uint16_t)(sizeof(sequencer::Pattern) - offset);
        const uint16_t chunk = remaining < sizeof(s_verify_scratch) ? remaining : sizeof(s_verify_scratch);
        if (!MB85RC1MT_Read(address + offset, s_verify_scratch, chunk)) return false;
        TimeCommitProbe_RecordCrcBytes(chunk);
        crc = Crc32Update(crc, s_verify_scratch, chunk);
        offset += chunk;
    }
    return Crc32Finish(crc) == expected_crc;
}

bool ValidateStore(uint8_t index, PatternStoreHeader* out_header)
{
    PatternStoreHeader header{};
    if (!ReadStoreHeader(index, &header) || !HeaderValid(header)) return false;
    if (!ValidatePayloadAt(SlotAddress(index) + sizeof(header), header.payload_crc32)) return false;
    if (out_header) *out_header = header;
    return true;
}

bool ReadJournalPayload(void)
{
    return MB85RC1MT_Read(FRAM_PATTERNSTORE_JOURNAL_ADDR + sizeof(PatternJournalHeader),
                          s_pattern_store_scratch,
                          (uint16_t)sizeof(sequencer::Pattern));
}

bool InvalidateAt(uint32_t address)
{
    const uint32_t invalid_magic = kInvalidMagic;
    return MB85RC1MT_Write(address,
                           reinterpret_cast<const uint8_t*>(&invalid_magic),
                           sizeof(invalid_magic));
}

bool JournalRetired(void)
{
    PatternJournalHeader header{};
    return ReadJournalHeader(&header) && !HeaderValid(header);
}

bool RetireJournal(void)
{
    return InvalidateAt(FRAM_PATTERNSTORE_JOURNAL_ADDR) && JournalRetired();
}

bool WriteDestinationFromScratch(uint8_t index, uint32_t generation, uint32_t payload_crc)
{
    const uint32_t base = SlotAddress(index);
    const PatternStoreHeader header = MakeStoreHeader(generation, payload_crc);
    if (!InvalidateAt(base)) return false;
    if (!MB85RC1MT_Write(base + sizeof(header), s_pattern_store_scratch,
                         (uint16_t)sizeof(sequencer::Pattern))) return false;
    if (!MB85RC1MT_Write(base, reinterpret_cast<const uint8_t*>(&header), sizeof(header))) return false;

    PatternStoreHeader validated{};
    return ValidateStore(index, &validated) &&
           validated.generation == generation &&
           validated.payload_crc32 == payload_crc;
}

uint32_t NextGeneration(uint8_t index)
{
    PatternStoreHeader header{};
    if (!ReadStoreHeader(index, &header) || !HeaderValid(header)) return 1u;
    const uint32_t next = header.generation + 1u;
    return next == 0u ? 1u : next;
}

bool MagicIsInvalid(uint32_t address)
{
    uint32_t magic = 0xFFFFFFFFu;
    return MB85RC1MT_Read(address, reinterpret_cast<uint8_t*>(&magic), sizeof(magic)) &&
           magic != kPatternStoreMagic && magic != kPatternJournalMagic;
}

enum class WritePhase : uint8_t
{
    Idle,
    VerifyExistingJournalPayload,
    VerifyExistingDestinationPayload,
    ReadExistingJournalPayload,
    InvalidateJournal,
    WriteJournalPayload,
    WriteJournalHeader,
    VerifyJournalHeader,
    VerifyJournalPayload,
    InvalidateDestination,
    WriteDestinationPayload,
    WriteDestinationHeader,
    VerifyDestinationHeader,
    VerifyDestinationPayload,
    RetireJournal,
    VerifyJournalRetired
};

WritePhase s_write_phase = WritePhase::Idle;
uint8_t s_write_index = 0xFFu;
uint16_t s_write_offset = 0u;
uint32_t s_verify_crc = 0u;
PatternStoreHeader s_store_header{};
PatternJournalHeader s_journal_header{};
PatternStoreHeader s_recovery_destination_header{};
PatternStoreRecoveryStatus s_runtime_recovery_status = PATTERNSTORE_RECOVERY_NONE;
PatternStoreRecoveryStatus s_runtime_recovery_action = PATTERNSTORE_RECOVERY_NONE;

bool WriteChunk(uint32_t payload_address)
{
    const uint16_t remaining = (uint16_t)(sizeof(sequencer::Pattern) - s_write_offset);
    const uint16_t chunk = remaining < PATTERNSTORE_CHUNK_BYTES ? remaining : PATTERNSTORE_CHUNK_BYTES;
    if (!MB85RC1MT_Write(payload_address + s_write_offset,
                         s_pattern_store_scratch + s_write_offset, chunk)) return false;
    s_write_offset = (uint16_t)(s_write_offset + chunk);
    return true;
}

enum class VerifyChunkStatus : uint8_t { Busy, Done, IoFailed, IntegrityFault };

VerifyChunkStatus VerifyChunk(uint32_t payload_address, uint32_t expected_crc)
{
    const uint16_t remaining = (uint16_t)(sizeof(sequencer::Pattern) - s_write_offset);
    const uint16_t chunk = remaining < PATTERNSTORE_CHUNK_BYTES ? remaining : PATTERNSTORE_CHUNK_BYTES;
    if (!MB85RC1MT_Read(payload_address + s_write_offset, s_verify_scratch, chunk))
        return VerifyChunkStatus::IoFailed;
    s_verify_crc = Crc32Update(s_verify_crc, s_verify_scratch, chunk);
    s_write_offset = (uint16_t)(s_write_offset + chunk);
    if (s_write_offset < sizeof(sequencer::Pattern)) return VerifyChunkStatus::Busy;
    return Crc32Finish(s_verify_crc) == expected_crc ?
           VerifyChunkStatus::Done : VerifyChunkStatus::IntegrityFault;
}

void ResetWriteState(void)
{
    s_write_phase = WritePhase::Idle;
    s_write_index = 0xFFu;
    s_write_offset = 0u;
    s_runtime_recovery_action = PATTERNSTORE_RECOVERY_NONE;
}

void BeginRuntimeRecovery(const PatternJournalHeader& journal)
{
    s_journal_header = journal;
    s_write_index = journal.pattern_index;
    s_write_offset = 0u;
    s_verify_crc = Crc32Begin();
    s_runtime_recovery_status = PATTERNSTORE_RECOVERY_NONE;
    s_runtime_recovery_action = PATTERNSTORE_RECOVERY_NONE;
    s_write_phase = WritePhase::VerifyExistingJournalPayload;
}

void BeginRuntimeReplay(void)
{
    s_write_offset = 0u;
    s_runtime_recovery_action = PATTERNSTORE_RECOVERY_REPLAYED;
    s_write_phase = WritePhase::ReadExistingJournalPayload;
}

enum class ReadPhase : uint8_t { Idle, ReadingPayload };
ReadPhase s_read_phase = ReadPhase::Idle;
uint8_t s_read_index = 0xFFu;
uint16_t s_read_offset = 0u;
uint32_t s_read_expected_crc = 0u;
uint32_t s_read_crc = 0u;
sequencer::Pattern* s_read_target = nullptr;

uint8_t BeginPreparedWrite(uint8_t index, uint32_t generation)
{
    if (index >= FRAM_PATTERNSTORE_SLOT_COUNT || generation == 0u ||
        s_write_phase != WritePhase::Idle || s_read_phase != ReadPhase::Idle) return 0u;

    PatternJournalHeader pending{};
    if (!ReadJournalHeader(&pending) || HeaderValid(pending)) return 0u;

    const uint32_t payload_crc = Crc32Compute(s_pattern_store_scratch, sizeof(sequencer::Pattern));
    s_store_header = MakeStoreHeader(generation, payload_crc);
    s_journal_header = MakeJournalHeader(index, generation, payload_crc);
    s_write_index = index;
    s_write_offset = 0u;
    s_write_phase = WritePhase::InvalidateJournal;
    return 1u;
}

} // namespace

static_assert(sizeof(sequencer::Pattern) == 2604u, "PatternStore V2 payload size changed");
static_assert(sizeof(PatternStoreHeader) == 20u, "PatternStore V2 header layout changed");
static_assert(sizeof(PatternJournalHeader) == 24u, "PatternStore journal header layout changed");
static_assert(sizeof(PatternStoreHeader) + sizeof(sequencer::Pattern) == FRAM_PATTERNSTORE_SLOT_STRIDE,
              "PatternStore V2 record must exactly fill its existing stride");
static_assert(sizeof(PatternJournalHeader) + sizeof(sequencer::Pattern) <= FRAM_PATTERNSTORE_JOURNAL_STRIDE,
              "PatternStore journal record exceeds its reserved stride");
static_assert(FRAM_PATTERNSTORE_JOURNAL_ADDR >= FRAM_PATTERNSTORE_END,
              "PatternStore journal overlaps PatternStore slots");
static_assert(FRAM_PATTERNSTORE_JOURNAL_END <= MB85RC1MT_SIZE_BYTES,
              "PatternStore journal exceeds physical FRAM");

uint8_t PatternStore_ReadWithGeneration(uint8_t index, sequencer::Pattern* out_pattern, uint32_t* out_generation)
{
    if (!out_pattern || index >= FRAM_PATTERNSTORE_SLOT_COUNT ||
        s_write_phase != WritePhase::Idle || s_read_phase != ReadPhase::Idle) return 0u;
    PatternStoreHeader header{};
    if (!ValidateStore(index, &header)) return 0u;
    if (!MB85RC1MT_Read(SlotAddress(index) + sizeof(header), s_pattern_store_scratch,
                        (uint16_t)sizeof(sequencer::Pattern))) return 0u;
    memcpy(out_pattern, s_pattern_store_scratch, sizeof(sequencer::Pattern));
    if (out_generation) *out_generation = header.generation;
    return 1u;
}

uint8_t PatternStore_Read(uint8_t index, sequencer::Pattern* out_pattern)
{
    return PatternStore_ReadWithGeneration(index, out_pattern, nullptr);
}

uint8_t PatternStore_GetGeneration(uint8_t index, uint32_t* out_generation)
{
    PatternStoreHeader header{};
    if (!out_generation || s_write_phase != WritePhase::Idle ||
        s_read_phase != ReadPhase::Idle || !ValidateStore(index, &header)) return 0u;
    *out_generation = header.generation;
    return 1u;
}

uint8_t PatternStore_IsValid(uint8_t index)
{
    if (s_write_phase != WritePhase::Idle || s_read_phase != ReadPhase::Idle) return 0u;
    return ValidateStore(index, nullptr) ? 1u : 0u;
}

uint8_t PatternStore_WriteAsyncBeginGeneration(uint8_t index,
                                               const sequencer::Pattern* pattern,
                                               uint32_t generation)
{
    if (index >= FRAM_PATTERNSTORE_SLOT_COUNT || !pattern || generation == 0u ||
        s_write_phase != WritePhase::Idle || s_read_phase != ReadPhase::Idle) return 0u;

    if (s_runtime_recovery_status == PATTERNSTORE_RECOVERY_INTEGRITY_FAULT) return 0u;
    PatternJournalHeader pending{};
    if (!ReadJournalHeader(&pending))
    {
        s_runtime_recovery_status = PATTERNSTORE_RECOVERY_FAILED;
        return 0u;
    }
    if (HeaderValid(pending))
    {
        BeginRuntimeRecovery(pending);
        return 0u;
    }

    s_runtime_recovery_status = PATTERNSTORE_RECOVERY_NONE;
    memcpy(s_pattern_store_scratch, pattern, sizeof(sequencer::Pattern));
    return BeginPreparedWrite(index, generation);
}

uint8_t PatternStore_WriteAsyncBegin(uint8_t index, const sequencer::Pattern* pattern)
{
    return PatternStore_WriteAsyncBeginGeneration(index, pattern, NextGeneration(index));
}

PatternStoreAsyncStatus PatternStore_WriteAsyncService(void)
{
    if (s_write_phase == WritePhase::Idle) return PATTERNSTORE_ASYNC_IDLE;

    switch (s_write_phase)
    {
        case WritePhase::VerifyExistingJournalPayload:
        {
            const VerifyChunkStatus verify = VerifyChunk(
                FRAM_PATTERNSTORE_JOURNAL_ADDR + sizeof(PatternJournalHeader),
                s_journal_header.payload_crc32);
            if (verify == VerifyChunkStatus::Busy) return PATTERNSTORE_ASYNC_BUSY;
            if (verify == VerifyChunkStatus::IoFailed)
            {
                ResetWriteState();
                s_runtime_recovery_status = PATTERNSTORE_RECOVERY_FAILED;
                return PATTERNSTORE_ASYNC_FAILED;
            }
            if (verify == VerifyChunkStatus::IntegrityFault)
            {
                ResetWriteState();
                s_runtime_recovery_status = PATTERNSTORE_RECOVERY_INTEGRITY_FAULT;
                return PATTERNSTORE_ASYNC_FAILED;
            }

            if (!ReadStoreHeader(s_journal_header.pattern_index, &s_recovery_destination_header))
            {
                ResetWriteState();
                s_runtime_recovery_status = PATTERNSTORE_RECOVERY_FAILED;
                return PATTERNSTORE_ASYNC_FAILED;
            }
            if (!HeaderValid(s_recovery_destination_header))
            {
                BeginRuntimeReplay();
                return PATTERNSTORE_ASYNC_BUSY;
            }
            s_write_offset = 0u;
            s_verify_crc = Crc32Begin();
            s_write_phase = WritePhase::VerifyExistingDestinationPayload;
            return PATTERNSTORE_ASYNC_BUSY;
        }

        case WritePhase::VerifyExistingDestinationPayload:
        {
            const VerifyChunkStatus verify = VerifyChunk(
                SlotAddress(s_journal_header.pattern_index) + sizeof(PatternStoreHeader),
                s_recovery_destination_header.payload_crc32);
            if (verify == VerifyChunkStatus::Busy) return PATTERNSTORE_ASYNC_BUSY;
            if (verify == VerifyChunkStatus::IoFailed)
            {
                ResetWriteState();
                s_runtime_recovery_status = PATTERNSTORE_RECOVERY_FAILED;
                return PATTERNSTORE_ASYNC_FAILED;
            }
            if (verify == VerifyChunkStatus::IntegrityFault)
            {
                BeginRuntimeReplay();
                return PATTERNSTORE_ASYNC_BUSY;
            }

            if (s_recovery_destination_header.generation == s_journal_header.generation)
            {
                if (s_recovery_destination_header.payload_crc32 != s_journal_header.payload_crc32)
                {
                    ResetWriteState();
                    s_runtime_recovery_status = PATTERNSTORE_RECOVERY_INTEGRITY_FAULT;
                    return PATTERNSTORE_ASYNC_FAILED;
                }
                s_runtime_recovery_action = PATTERNSTORE_RECOVERY_RETIRED;
                s_write_phase = WritePhase::RetireJournal;
                return PATTERNSTORE_ASYNC_BUSY;
            }
            if (GenerationNewer(s_recovery_destination_header.generation,
                                s_journal_header.generation))
            {
                s_runtime_recovery_action = PATTERNSTORE_RECOVERY_RETIRED;
                s_write_phase = WritePhase::RetireJournal;
                return PATTERNSTORE_ASYNC_BUSY;
            }
            BeginRuntimeReplay();
            return PATTERNSTORE_ASYNC_BUSY;
        }

        case WritePhase::ReadExistingJournalPayload:
        {
            const uint16_t remaining = (uint16_t)(sizeof(sequencer::Pattern) - s_write_offset);
            const uint16_t chunk = remaining < PATTERNSTORE_CHUNK_BYTES ?
                                   remaining : PATTERNSTORE_CHUNK_BYTES;
            if (!MB85RC1MT_Read(FRAM_PATTERNSTORE_JOURNAL_ADDR +
                                sizeof(PatternJournalHeader) + s_write_offset,
                                s_pattern_store_scratch + s_write_offset, chunk))
            {
                ResetWriteState();
                s_runtime_recovery_status = PATTERNSTORE_RECOVERY_FAILED;
                return PATTERNSTORE_ASYNC_FAILED;
            }
            s_write_offset = (uint16_t)(s_write_offset + chunk);
            if (s_write_offset < sizeof(sequencer::Pattern)) return PATTERNSTORE_ASYNC_BUSY;
            s_write_index = s_journal_header.pattern_index;
            s_store_header = MakeStoreHeader(s_journal_header.generation,
                                             s_journal_header.payload_crc32);
            s_write_phase = WritePhase::InvalidateDestination;
            return PATTERNSTORE_ASYNC_BUSY;
        }

        case WritePhase::InvalidateJournal:
            if (!InvalidateAt(FRAM_PATTERNSTORE_JOURNAL_ADDR)) break;
            s_write_offset = 0u;
            s_write_phase = WritePhase::WriteJournalPayload;
            return PATTERNSTORE_ASYNC_BUSY;

        case WritePhase::WriteJournalPayload:
            if (!WriteChunk(FRAM_PATTERNSTORE_JOURNAL_ADDR + sizeof(PatternJournalHeader))) break;
            if (s_write_offset >= sizeof(sequencer::Pattern)) s_write_phase = WritePhase::WriteJournalHeader;
            return PATTERNSTORE_ASYNC_BUSY;

        case WritePhase::WriteJournalHeader:
            if (!MB85RC1MT_Write(FRAM_PATTERNSTORE_JOURNAL_ADDR,
                                 reinterpret_cast<const uint8_t*>(&s_journal_header),
                                 sizeof(s_journal_header))) break;
            s_write_phase = WritePhase::VerifyJournalHeader;
            return PATTERNSTORE_ASYNC_BUSY;

        case WritePhase::VerifyJournalHeader:
        {
            PatternJournalHeader header{};
            if (!ReadJournalHeader(&header) || !HeaderValid(header) ||
                header.pattern_index != s_write_index ||
                header.generation != s_journal_header.generation ||
                header.payload_crc32 != s_journal_header.payload_crc32) break;
            s_write_offset = 0u;
            s_verify_crc = Crc32Begin();
            s_write_phase = WritePhase::VerifyJournalPayload;
            return PATTERNSTORE_ASYNC_BUSY;
        }

        case WritePhase::VerifyJournalPayload:
        {
            const VerifyChunkStatus verify = VerifyChunk(
                FRAM_PATTERNSTORE_JOURNAL_ADDR + sizeof(PatternJournalHeader),
                s_journal_header.payload_crc32);
            if (verify == VerifyChunkStatus::Busy) return PATTERNSTORE_ASYNC_BUSY;
            if (verify != VerifyChunkStatus::Done) break;
            s_write_phase = WritePhase::InvalidateDestination;
            return PATTERNSTORE_ASYNC_BUSY;
        }

        case WritePhase::InvalidateDestination:
            if (!InvalidateAt(SlotAddress(s_write_index))) break;
            s_write_offset = 0u;
            s_write_phase = WritePhase::WriteDestinationPayload;
            return PATTERNSTORE_ASYNC_BUSY;

        case WritePhase::WriteDestinationPayload:
            if (!WriteChunk(SlotAddress(s_write_index) + sizeof(PatternStoreHeader))) break;
            if (s_write_offset >= sizeof(sequencer::Pattern)) s_write_phase = WritePhase::WriteDestinationHeader;
            return PATTERNSTORE_ASYNC_BUSY;

        case WritePhase::WriteDestinationHeader:
            if (!MB85RC1MT_Write(SlotAddress(s_write_index),
                                 reinterpret_cast<const uint8_t*>(&s_store_header),
                                 sizeof(s_store_header))) break;
            s_write_phase = WritePhase::VerifyDestinationHeader;
            return PATTERNSTORE_ASYNC_BUSY;

        case WritePhase::VerifyDestinationHeader:
        {
            PatternStoreHeader header{};
            if (!ReadStoreHeader(s_write_index, &header) || !HeaderValid(header) ||
                header.generation != s_store_header.generation ||
                header.payload_crc32 != s_store_header.payload_crc32) break;
            s_write_offset = 0u;
            s_verify_crc = Crc32Begin();
            s_write_phase = WritePhase::VerifyDestinationPayload;
            return PATTERNSTORE_ASYNC_BUSY;
        }

        case WritePhase::VerifyDestinationPayload:
        {
            const VerifyChunkStatus verify = VerifyChunk(
                SlotAddress(s_write_index) + sizeof(PatternStoreHeader),
                s_store_header.payload_crc32);
            if (verify == VerifyChunkStatus::Busy) return PATTERNSTORE_ASYNC_BUSY;
            if (verify != VerifyChunkStatus::Done) break;
            s_write_phase = WritePhase::RetireJournal;
            return PATTERNSTORE_ASYNC_BUSY;
        }

        case WritePhase::RetireJournal:
            if (!InvalidateAt(FRAM_PATTERNSTORE_JOURNAL_ADDR))
            {
                ResetWriteState();
                s_runtime_recovery_status = PATTERNSTORE_RECOVERY_FAILED;
                return PATTERNSTORE_ASYNC_DONE;
            }
            s_write_phase = WritePhase::VerifyJournalRetired;
            return PATTERNSTORE_ASYNC_BUSY;

        case WritePhase::VerifyJournalRetired:
        {
            const PatternStoreRecoveryStatus completed_action = s_runtime_recovery_action;
            if (!JournalRetired())
            {
                ResetWriteState();
                s_runtime_recovery_status = PATTERNSTORE_RECOVERY_FAILED;
                return PATTERNSTORE_ASYNC_DONE;
            }
            ResetWriteState();
            s_runtime_recovery_status = completed_action;
            return PATTERNSTORE_ASYNC_DONE;
        }

        case WritePhase::Idle:
            return PATTERNSTORE_ASYNC_IDLE;
    }

    ResetWriteState();
    s_runtime_recovery_status = PATTERNSTORE_RECOVERY_FAILED;
    return PATTERNSTORE_ASYNC_FAILED;
}

uint8_t PatternStore_WriteAsyncBusy(void)
{
    return s_write_phase != WritePhase::Idle ? 1u : 0u;
}

PatternStoreRecoveryStatus PatternStore_GetRuntimeRecoveryStatus(void)
{
    return s_runtime_recovery_status;
}

uint8_t PatternStore_WriteGeneration(uint8_t index, const sequencer::Pattern* pattern, uint32_t generation)
{
    if (!PatternStore_WriteAsyncBeginGeneration(index, pattern, generation)) return 0u;
    PatternStoreAsyncStatus status = PATTERNSTORE_ASYNC_BUSY;
    while (status == PATTERNSTORE_ASYNC_BUSY) status = PatternStore_WriteAsyncService();
    return status == PATTERNSTORE_ASYNC_DONE ? 1u : 0u;
}

uint8_t PatternStore_Write(uint8_t index, const sequencer::Pattern* pattern)
{
    return PatternStore_WriteGeneration(index, pattern, NextGeneration(index));
}

uint8_t PatternStore_WriteDefaultGeneration(uint8_t index, uint32_t generation)
{
    if (index >= FRAM_PATTERNSTORE_SLOT_COUNT || generation == 0u ||
        s_write_phase != WritePhase::Idle || s_read_phase != ReadPhase::Idle) return 0u;

    sequencer::Pattern* pattern = new (s_pattern_store_scratch) sequencer::Pattern{};
    if (index == 0u) pattern->playback_mode = sequencer::PlaybackMode::Loop;
    if (!BeginPreparedWrite(index, generation)) return 0u;

    PatternStoreAsyncStatus status = PATTERNSTORE_ASYNC_BUSY;
    while (status == PATTERNSTORE_ASYNC_BUSY) status = PatternStore_WriteAsyncService();
    return status == PATTERNSTORE_ASYNC_DONE ? 1u : 0u;
}

uint8_t PatternStore_InvalidateSlot(uint8_t index)
{
    if (index >= FRAM_PATTERNSTORE_SLOT_COUNT ||
        s_write_phase != WritePhase::Idle || s_read_phase != ReadPhase::Idle) return 0u;
    const uint32_t address = SlotAddress(index);
    return InvalidateAt(address) && MagicIsInvalid(address) ? 1u : 0u;
}

uint8_t PatternStore_DiscardJournal(void)
{
    if (s_write_phase != WritePhase::Idle || s_read_phase != ReadPhase::Idle) return 0u;
    return InvalidateAt(FRAM_PATTERNSTORE_JOURNAL_ADDR) &&
           MagicIsInvalid(FRAM_PATTERNSTORE_JOURNAL_ADDR) ? 1u : 0u;
}

PatternStoreRecoveryStatus PatternStore_RecoverJournal(void)
{
    if (s_write_phase != WritePhase::Idle || s_read_phase != ReadPhase::Idle)
        return PATTERNSTORE_RECOVERY_FAILED;

    PatternJournalHeader journal{};
    if (!ReadJournalHeader(&journal) || !HeaderValid(journal)) return PATTERNSTORE_RECOVERY_NONE;
    if (!ValidatePayloadAt(FRAM_PATTERNSTORE_JOURNAL_ADDR + sizeof(journal), journal.payload_crc32))
        return PATTERNSTORE_RECOVERY_INTEGRITY_FAULT;
    if (!ReadJournalPayload()) return PATTERNSTORE_RECOVERY_FAILED;

    PatternStoreHeader destination{};
    const bool destination_valid = ValidateStore(journal.pattern_index, &destination);
    if (destination_valid)
    {
        if (destination.generation == journal.generation)
        {
            if (destination.payload_crc32 != journal.payload_crc32)
                return PATTERNSTORE_RECOVERY_INTEGRITY_FAULT;
            return RetireJournal() ? PATTERNSTORE_RECOVERY_RETIRED : PATTERNSTORE_RECOVERY_FAILED;
        }
        if (GenerationNewer(destination.generation, journal.generation))
            return RetireJournal() ? PATTERNSTORE_RECOVERY_RETIRED : PATTERNSTORE_RECOVERY_FAILED;
    }

    if (!WriteDestinationFromScratch(journal.pattern_index, journal.generation, journal.payload_crc32))
        return PATTERNSTORE_RECOVERY_FAILED;
    if (!RetireJournal()) return PATTERNSTORE_RECOVERY_FAILED;
    return PATTERNSTORE_RECOVERY_REPLAYED;
}

uint8_t PatternStore_ReadAsyncBeginGeneration(uint8_t index,
                                              sequencer::Pattern* out_pattern,
                                              uint32_t* out_generation)
{
    if (index >= FRAM_PATTERNSTORE_SLOT_COUNT || !out_pattern ||
        s_read_phase != ReadPhase::Idle || s_write_phase != WritePhase::Idle) return 0u;
    PatternStoreHeader header{};
    if (!ReadStoreHeader(index, &header) || !HeaderValid(header)) return 0u;
    s_read_index = index;
    s_read_offset = 0u;
    s_read_expected_crc = header.payload_crc32;
    s_read_crc = Crc32Begin();
    s_read_target = out_pattern;
    s_read_phase = ReadPhase::ReadingPayload;
    if (out_generation) *out_generation = header.generation;
    return 1u;
}

uint8_t PatternStore_ReadAsyncBegin(uint8_t index, sequencer::Pattern* out_pattern)
{
    return PatternStore_ReadAsyncBeginGeneration(index, out_pattern, nullptr);
}

PatternStoreAsyncStatus PatternStore_ReadAsyncService(void)
{
    if (s_read_phase == ReadPhase::Idle) return PATTERNSTORE_ASYNC_IDLE;
    const uint16_t remaining = (uint16_t)(sizeof(sequencer::Pattern) - s_read_offset);
    const uint16_t chunk = remaining < PATTERNSTORE_CHUNK_BYTES ? remaining : PATTERNSTORE_CHUNK_BYTES;
    uint8_t* dst = reinterpret_cast<uint8_t*>(s_read_target) + s_read_offset;
    if (!MB85RC1MT_Read(SlotAddress(s_read_index) + sizeof(PatternStoreHeader) + s_read_offset,
                        dst, chunk))
    {
        s_read_phase = ReadPhase::Idle;
        s_read_target = nullptr;
        return PATTERNSTORE_ASYNC_FAILED;
    }
    s_read_crc = Crc32Update(s_read_crc, dst, chunk);
    s_read_offset = (uint16_t)(s_read_offset + chunk);
    if (s_read_offset < sizeof(sequencer::Pattern)) return PATTERNSTORE_ASYNC_BUSY;

    const bool valid = Crc32Finish(s_read_crc) == s_read_expected_crc;
    s_read_phase = ReadPhase::Idle;
    s_read_target = nullptr;
    return valid ? PATTERNSTORE_ASYNC_DONE : PATTERNSTORE_ASYNC_FAILED;
}

uint8_t PatternStore_ReadAsyncBusy(void)
{
    return s_read_phase != ReadPhase::Idle ? 1u : 0u;
}

#if defined(PATTERN_STORE_HOST_TEST)
void PatternStore_ResetRuntimeStateForTest(void)
{
    ResetWriteState();
    s_runtime_recovery_status = PATTERNSTORE_RECOVERY_NONE;
    s_read_phase = ReadPhase::Idle;
    s_read_index = 0xFFu;
    s_read_offset = 0u;
    s_read_target = nullptr;
}
#endif
