#include "platform/fram/persistence_store.h"
#include "platform/fram/storage_boot_probe.h"
#if defined(PATTERN_STORE_HOST_TEST)
extern "C" uint8_t PatternStoreBackend_IsReady(void);
extern "C" uint8_t PatternStoreBackend_Read(uint32_t address, uint8_t* dst, uint16_t len);
extern "C" uint8_t PatternStoreBackend_Write(uint32_t address, const uint8_t* src, uint16_t len);
#define MB85RC1MT_IsReady PatternStoreBackend_IsReady
#define MB85RC1MT_Read PatternStoreBackend_Read
#define MB85RC1MT_Write PatternStoreBackend_Write
#define MB85RC1MT_SIZE_BYTES 0x20000u
#else
#include "platform/fram/mb85rc1mt.h"
#endif
#include <cstddef>
#include <cstring>

namespace
{

enum class SchemaState : uint8_t
{
    Formatting = 1u,
    Initializing = 2u,
    Ready = 3u,
};

struct SchemaRecord
{
    uint32_t magic;
    uint16_t version;
    uint16_t record_size;
    uint32_t generation;
    uint8_t state;
    uint8_t reserved[15];
    uint32_t header_crc32;
};

struct SongHeaderRecord
{
    uint32_t magic;
    uint16_t version;
    uint16_t payload_size;
    uint32_t generation;
    uint32_t payload_crc32;
    uint32_t header_crc32;
    PersistentSongHeaderData payload;
};

constexpr uint32_t kSchemaMagic = 0x53313246u; /* "S12F" */
constexpr uint32_t kSongHeaderMagic = 0x53313248u; /* "S12H" */
constexpr uint16_t kSchemaVersion = 1u;
constexpr uint16_t kSongHeaderVersion = 1u;
constexpr uint32_t kInvalidMagic = 0u;

PersistenceBootStatus s_boot_status = PERSISTENCE_BOOT_NOT_RUN;

uint32_t Crc32Compute(const uint8_t* data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0u; i < len; ++i)
    {
        crc ^= data[i];
        for (uint8_t bit = 0u; bit < 8u; ++bit)
        {
            const uint32_t mask = (uint32_t)(-(int32_t)(crc & 1u));
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

uint32_t SchemaCrc(const SchemaRecord& record)
{
    return Crc32Compute(reinterpret_cast<const uint8_t*>(&record),
                        offsetof(SchemaRecord, header_crc32));
}

uint32_t SongHeaderCrc(const SongHeaderRecord& record)
{
    return Crc32Compute(reinterpret_cast<const uint8_t*>(&record),
                        offsetof(SongHeaderRecord, header_crc32));
}

bool GenerationNewer(uint32_t candidate, uint32_t reference)
{
    return (int32_t)(candidate - reference) > 0;
}

bool BytesAllFF(const uint8_t* data, uint32_t len)
{
    for (uint32_t i = 0u; i < len; ++i)
    {
        if (data[i] != 0xFFu) return false;
    }
    return true;
}

bool SchemaValid(const SchemaRecord& record)
{
    return record.magic == kSchemaMagic &&
           record.version == kSchemaVersion &&
           record.record_size == sizeof(SchemaRecord) &&
           record.generation != 0u &&
           (record.state == (uint8_t)SchemaState::Formatting ||
            record.state == (uint8_t)SchemaState::Initializing ||
            record.state == (uint8_t)SchemaState::Ready) &&
           record.header_crc32 == SchemaCrc(record);
}

bool SongHeaderValid(const SongHeaderRecord& record)
{
    if (record.magic != kSongHeaderMagic ||
        record.version != kSongHeaderVersion ||
        record.payload_size != sizeof(PersistentSongHeaderData) ||
        record.generation == 0u ||
        record.header_crc32 != SongHeaderCrc(record) ||
        record.payload_crc32 != Crc32Compute(reinterpret_cast<const uint8_t*>(&record.payload),
                                             sizeof(record.payload))) return false;

    if (record.payload.global_bpm == 0u || record.payload.chain_length == 0u ||
        record.payload.chain_length > sequencer::kChainLength ||
        record.payload.key_root >= 12u || record.payload.key_scale >= 9u) return false;
    for (uint8_t i = 0u; i < record.payload.chain_length; ++i)
    {
        if (record.payload.pattern_indices[i] >= sequencer::kPatternCount) return false;
    }
    return true;
}

bool ReadSchemaPair(SchemaRecord* a, SchemaRecord* b)
{
    return a && b &&
           MB85RC1MT_Read(FRAM_SCHEMA_RECORD_A_ADDR, reinterpret_cast<uint8_t*>(a), sizeof(*a)) &&
           MB85RC1MT_Read(FRAM_SCHEMA_RECORD_B_ADDR, reinterpret_cast<uint8_t*>(b), sizeof(*b));
}

bool ReadSongHeaderPair(SongHeaderRecord* a, SongHeaderRecord* b)
{
    return a && b &&
           MB85RC1MT_Read(FRAM_SONG_HEADER_A_ADDR, reinterpret_cast<uint8_t*>(a), sizeof(*a)) &&
           MB85RC1MT_Read(FRAM_SONG_HEADER_B_ADDR, reinterpret_cast<uint8_t*>(b), sizeof(*b));
}

bool InvalidateAt(uint32_t address)
{
    const uint32_t invalid = kInvalidMagic;
    if (!MB85RC1MT_Write(address, reinterpret_cast<const uint8_t*>(&invalid), sizeof(invalid))) return false;
    uint32_t readback = 0xFFFFFFFFu;
    return MB85RC1MT_Read(address, reinterpret_cast<uint8_t*>(&readback), sizeof(readback)) &&
           readback == kInvalidMagic;
}

enum class SchemaInspectStatus : uint8_t
{
    Blank,
    Valid,
    Unsupported,
    IntegrityFault,
    IoFault,
};

SchemaInspectStatus InspectSchema(SchemaRecord* out_record, uint8_t* out_slot)
{
    SchemaRecord a{};
    SchemaRecord b{};
    if (!ReadSchemaPair(&a, &b)) return SchemaInspectStatus::IoFault;

    const bool valid_a = SchemaValid(a);
    const bool valid_b = SchemaValid(b);
    const bool blank_a = BytesAllFF(reinterpret_cast<const uint8_t*>(&a), sizeof(a));
    const bool blank_b = BytesAllFF(reinterpret_cast<const uint8_t*>(&b), sizeof(b));

    if ((a.magic == kSchemaMagic && a.version != kSchemaVersion) ||
        (b.magic == kSchemaMagic && b.version != kSchemaVersion))
        return SchemaInspectStatus::Unsupported;

    if (!valid_a && !valid_b)
    {
        if (blank_a || blank_b) return SchemaInspectStatus::Blank;
        return SchemaInspectStatus::IntegrityFault;
    }

    const SchemaRecord* selected = nullptr;
    uint8_t slot = 0u;
    if (valid_a && valid_b)
    {
        if (a.generation == b.generation &&
            memcmp(&a, &b, sizeof(a)) != 0) return SchemaInspectStatus::IntegrityFault;
        if (GenerationNewer(b.generation, a.generation)) { selected = &b; slot = 1u; }
        else { selected = &a; slot = 0u; }
    }
    else if (valid_a) { selected = &a; slot = 0u; }
    else { selected = &b; slot = 1u; }

    if (out_record) *out_record = *selected;
    if (out_slot) *out_slot = slot;
    return SchemaInspectStatus::Valid;
}

bool WriteSchemaState(SchemaState state)
{
    SchemaRecord current{};
    uint8_t active_slot = 1u;
    const SchemaInspectStatus inspect = InspectSchema(&current, &active_slot);
    if (inspect != SchemaInspectStatus::Valid && inspect != SchemaInspectStatus::Blank) return false;

    SchemaRecord next{};
    next.magic = kSchemaMagic;
    next.version = kSchemaVersion;
    next.record_size = sizeof(SchemaRecord);
    next.generation = (inspect == SchemaInspectStatus::Valid) ? current.generation + 1u : 1u;
    if (next.generation == 0u) next.generation = 1u;
    next.state = (uint8_t)state;
    next.header_crc32 = SchemaCrc(next);

    const uint32_t target = active_slot == 0u ? FRAM_SCHEMA_RECORD_B_ADDR : FRAM_SCHEMA_RECORD_A_ADDR;
    if (!InvalidateAt(target)) return false;
    if (!MB85RC1MT_Write(target, reinterpret_cast<const uint8_t*>(&next), sizeof(next))) return false;
    SchemaRecord readback{};
    return MB85RC1MT_Read(target, reinterpret_cast<uint8_t*>(&readback), sizeof(readback)) &&
           SchemaValid(readback) && readback.generation == next.generation &&
           readback.state == next.state;
}

SongHeaderSelectStatus SelectSongHeaderRecord(SongHeaderRecord* out_record, uint8_t* out_slot)
{
    SongHeaderRecord a{};
    SongHeaderRecord b{};
    if (!ReadSongHeaderPair(&a, &b)) return SONG_HEADER_SELECT_IO_FAULT;
    const bool valid_a = SongHeaderValid(a);
    const bool valid_b = SongHeaderValid(b);
    if (!valid_a && !valid_b) return SONG_HEADER_SELECT_NONE;

    const SongHeaderRecord* selected = nullptr;
    uint8_t slot = 0u;
    if (valid_a && valid_b)
    {
        if (a.generation == b.generation)
        {
            if (a.payload_crc32 != b.payload_crc32 ||
                memcmp(&a.payload, &b.payload, sizeof(a.payload)) != 0)
                return SONG_HEADER_SELECT_INTEGRITY_FAULT;
            selected = &a;
        }
        else if (GenerationNewer(b.generation, a.generation)) { selected = &b; slot = 1u; }
        else { selected = &a; }
    }
    else if (valid_a) selected = &a;
    else { selected = &b; slot = 1u; }

    if (out_record) *out_record = *selected;
    if (out_slot) *out_slot = slot;
    return SONG_HEADER_SELECT_VALID;
}

bool InvalidateSongHeaders(void)
{
    return InvalidateAt(FRAM_SONG_HEADER_A_ADDR) && InvalidateAt(FRAM_SONG_HEADER_B_ADDR);
}

bool EnsureSongHeaderPair(void)
{
    SongHeaderRecord a{};
    SongHeaderRecord b{};
    if (!ReadSongHeaderPair(&a, &b)) return false;
    const bool valid_a = SongHeaderValid(a);
    const bool valid_b = SongHeaderValid(b);
    if (valid_a && valid_b) return true;
    if (!valid_a && !valid_b) return false;

    const SongHeaderRecord& valid = valid_a ? a : b;
    uint32_t generation = valid.generation + 1u;
    if (generation == 0u) generation = 1u;
    return PersistenceStore_WriteSongHeader(&valid.payload, generation) != 0u;
}

bool ValidateAllPatterns(void)
{
    for (uint8_t index = 0u; index < FRAM_PATTERNSTORE_SLOT_COUNT; ++index)
    {
        if (!PatternStore_IsValid(index)) return false;
    }
    return true;
}

bool SchemaReadyPairValid(void)
{
    SchemaRecord a{};
    SchemaRecord b{};
    return ReadSchemaPair(&a, &b) && SchemaValid(a) && SchemaValid(b) &&
           a.state == (uint8_t)SchemaState::Ready &&
           b.state == (uint8_t)SchemaState::Ready;
}

PersistenceBootStatus Fail(PersistenceBootStatus status)
{
    s_boot_status = status;
    return status;
}

} // namespace

static_assert(sizeof(SchemaRecord) == FRAM_SCHEMA_RECORD_SIZE,
              "clean-schema record size changed");
static_assert(sizeof(PersistentSongHeaderData) == 44u,
              "persistent Song Header payload ABI changed");
static_assert(sizeof(SongHeaderRecord) == FRAM_SONG_HEADER_RECORD_SIZE,
              "Song Header A/B record size changed");
static_assert(FRAM_SCHEMA_RECORD_A_ADDR >= FRAM_PATTERNSTORE_JOURNAL_END,
              "schema records overlap PatternStore journal");
static_assert(FRAM_PERSISTENCE_METADATA_END <= MB85RC1MT_SIZE_BYTES,
              "Stage 2 metadata exceeds physical FRAM");
static_assert(FRAM_PATTERNSTORE_ADDR >= 0x08000u,
              "factory Pattern writes must not enter legacy/calibration FRAM");
static_assert(FRAM_PATTERNSTORE_JOURNAL_ADDR >= 0x08000u,
              "factory journal writes must not enter legacy/calibration FRAM");
static_assert(FRAM_SCHEMA_RECORD_A_ADDR >= 0x08000u &&
              FRAM_SONG_HEADER_B_ADDR >= 0x08000u,
              "factory metadata writes must not enter legacy/calibration FRAM");

namespace
{

bool WriteSchemaRecordAt(uint32_t address, SchemaState state, uint32_t generation)
{
    if (address < 0x08000u || generation == 0u) return false;
    SchemaRecord record{};
    record.magic = kSchemaMagic;
    record.version = kSchemaVersion;
    record.record_size = sizeof(record);
    record.generation = generation;
    record.state = (uint8_t)state;
    record.header_crc32 = SchemaCrc(record);
    if (!InvalidateAt(address) ||
        !MB85RC1MT_Write(address, reinterpret_cast<const uint8_t*>(&record), sizeof(record)))
        return false;
    SchemaRecord readback{};
    return MB85RC1MT_Read(address, reinterpret_cast<uint8_t*>(&readback), sizeof(readback)) &&
           SchemaValid(readback) && readback.generation == generation &&
           readback.state == (uint8_t)state;
}

bool WriteSongHeaderRecordAt(uint32_t address,
                             const PersistentSongHeaderData& data,
                             uint32_t generation)
{
    if (address < 0x08000u || generation == 0u) return false;
    SongHeaderRecord record{};
    record.magic = kSongHeaderMagic;
    record.version = kSongHeaderVersion;
    record.payload_size = sizeof(PersistentSongHeaderData);
    record.generation = generation;
    record.payload = data;
    record.payload_crc32 = Crc32Compute(
        reinterpret_cast<const uint8_t*>(&record.payload), sizeof(record.payload));
    record.header_crc32 = SongHeaderCrc(record);
    if (!InvalidateAt(address) ||
        !MB85RC1MT_Write(address + offsetof(SongHeaderRecord, payload),
                         reinterpret_cast<const uint8_t*>(&record.payload),
                         sizeof(record.payload)) ||
        !MB85RC1MT_Write(address, reinterpret_cast<const uint8_t*>(&record),
                         offsetof(SongHeaderRecord, payload))) return false;
    SongHeaderRecord readback{};
    return MB85RC1MT_Read(address, reinterpret_cast<uint8_t*>(&readback), sizeof(readback)) &&
           SongHeaderValid(readback) && readback.generation == generation &&
           memcmp(&readback.payload, &data, sizeof(data)) == 0;
}

void ReportFactory(PersistenceFactoryProgressCallback progress,
                   PersistenceFactoryPhase phase,
                   uint8_t index,
                   uint8_t total)
{
    if (progress) progress(phase, index, total);
}

} // namespace

void PersistenceStore_DefaultSongHeader(PersistentSongHeaderData* out_data)
{
    if (!out_data) return;
    memset(out_data, 0, sizeof(*out_data));
    out_data->global_bpm = 120u;
    out_data->key_root = (uint8_t)sequencer::KeyRoot::C;
    out_data->key_scale = (uint8_t)sequencer::KeyScale::Major;
    out_data->time_sig_numerator = 4u;
    out_data->time_sig_denominator = 4u;
    out_data->chain_length = 1u;
    out_data->pattern_indices[0] = 0u;
}

SongHeaderSelectStatus PersistenceStore_SelectSongHeader(PersistentSongHeaderData* out_data,
                                                         uint32_t* out_generation)
{
    SongHeaderRecord selected{};
    const SongHeaderSelectStatus status = SelectSongHeaderRecord(&selected, nullptr);
    if (status != SONG_HEADER_SELECT_VALID) return status;
    if (out_data) *out_data = selected.payload;
    if (out_generation) *out_generation = selected.generation;
    return status;
}

uint8_t PersistenceStore_WriteSongHeader(const PersistentSongHeaderData* data,
                                         uint32_t generation)
{
    if (!data || generation == 0u) return 0u;

    SongHeaderRecord current{};
    uint8_t active_slot = 1u;
    const SongHeaderSelectStatus selected = SelectSongHeaderRecord(&current, &active_slot);
    if (selected == SONG_HEADER_SELECT_IO_FAULT ||
        selected == SONG_HEADER_SELECT_INTEGRITY_FAULT) return 0u;

    SongHeaderRecord next{};
    next.magic = kSongHeaderMagic;
    next.version = kSongHeaderVersion;
    next.payload_size = sizeof(PersistentSongHeaderData);
    next.generation = generation;
    next.payload = *data;
    next.payload_crc32 = Crc32Compute(reinterpret_cast<const uint8_t*>(&next.payload), sizeof(next.payload));
    next.header_crc32 = SongHeaderCrc(next);

    const uint32_t target = active_slot == 0u ? FRAM_SONG_HEADER_B_ADDR : FRAM_SONG_HEADER_A_ADDR;
    if (!InvalidateAt(target)) return 0u;
    if (!MB85RC1MT_Write(target + offsetof(SongHeaderRecord, payload),
                         reinterpret_cast<const uint8_t*>(&next.payload), sizeof(next.payload))) return 0u;
    if (!MB85RC1MT_Write(target, reinterpret_cast<const uint8_t*>(&next),
                         offsetof(SongHeaderRecord, payload))) return 0u;

    SongHeaderRecord readback{};
    if (!MB85RC1MT_Read(target, reinterpret_cast<uint8_t*>(&readback), sizeof(readback)) ||
        !SongHeaderValid(readback)) return 0u;
    return readback.generation == generation &&
           readback.payload_crc32 == next.payload_crc32 &&
           memcmp(&readback.payload, data, sizeof(*data)) == 0 ? 1u : 0u;
}

PersistenceBootStatus PersistenceStore_Bootstrap(void)
{
    s_boot_status = PERSISTENCE_BOOT_INITIALIZATION_IN_PROGRESS;
    if (!MB85RC1MT_IsReady()) return Fail(PERSISTENCE_BOOT_FRAM_NOT_READY);

    SchemaRecord schema{};
    uint8_t schema_slot = 0u;
    SchemaInspectStatus inspect = InspectSchema(&schema, &schema_slot);
    (void)schema_slot;
    if (inspect == SchemaInspectStatus::IoFault) return Fail(PERSISTENCE_BOOT_IO_FAULT);
    if (inspect == SchemaInspectStatus::Unsupported) return Fail(PERSISTENCE_BOOT_UNSUPPORTED_SCHEMA);
    if (inspect == SchemaInspectStatus::IntegrityFault) return Fail(PERSISTENCE_BOOT_SCHEMA_INTEGRITY_FAULT);

    if (inspect == SchemaInspectStatus::Blank)
        return Fail(PERSISTENCE_BOOT_FACTORY_REQUIRED);

    if (inspect != SchemaInspectStatus::Valid) return Fail(PERSISTENCE_BOOT_SCHEMA_INTEGRITY_FAULT);

    SchemaState state = (SchemaState)schema.state;
    if (state == SchemaState::Formatting)
    {
        if (!PatternStore_DiscardJournal()) return Fail(PERSISTENCE_BOOT_IO_FAULT);
        for (uint8_t index = 0u; index < FRAM_PATTERNSTORE_SLOT_COUNT; ++index)
        {
            if (!PatternStore_InvalidateSlot(index)) return Fail(PERSISTENCE_BOOT_IO_FAULT);
        }
        if (!InvalidateSongHeaders()) return Fail(PERSISTENCE_BOOT_IO_FAULT);
        if (!WriteSchemaState(SchemaState::Initializing)) return Fail(PERSISTENCE_BOOT_IO_FAULT);
        state = SchemaState::Initializing;
    }

    if (state == SchemaState::Initializing)
    {
        const PatternStoreRecoveryStatus recovery = PatternStore_RecoverJournal();
        if (recovery == PATTERNSTORE_RECOVERY_FAILED) return Fail(PERSISTENCE_BOOT_IO_FAULT);
        if (recovery == PATTERNSTORE_RECOVERY_INTEGRITY_FAULT)
        {
            if (!PatternStore_DiscardJournal()) return Fail(PERSISTENCE_BOOT_IO_FAULT);
        }

        for (uint8_t index = 0u; index < FRAM_PATTERNSTORE_SLOT_COUNT; ++index)
        {
            if (!PatternStore_IsValid(index) &&
                !PatternStore_WriteDefaultGeneration(index, 1u))
                return Fail(PERSISTENCE_BOOT_IO_FAULT);
        }

        SongHeaderRecord selected{};
        const SongHeaderSelectStatus song_status = SelectSongHeaderRecord(&selected, nullptr);
        if (song_status == SONG_HEADER_SELECT_IO_FAULT) return Fail(PERSISTENCE_BOOT_IO_FAULT);
        if (song_status != SONG_HEADER_SELECT_VALID)
        {
            if (!InvalidateSongHeaders()) return Fail(PERSISTENCE_BOOT_IO_FAULT);
            PersistentSongHeaderData defaults{};
            PersistenceStore_DefaultSongHeader(&defaults);
            if (!PersistenceStore_WriteSongHeader(&defaults, 1u)) return Fail(PERSISTENCE_BOOT_IO_FAULT);
        }

        if (!EnsureSongHeaderPair()) return Fail(PERSISTENCE_BOOT_IO_FAULT);

        if (!ValidateAllPatterns()) return Fail(PERSISTENCE_BOOT_PATTERN_INTEGRITY_FAULT);
        if (PersistenceStore_SelectSongHeader(nullptr, nullptr) != SONG_HEADER_SELECT_VALID)
            return Fail(PERSISTENCE_BOOT_SONG_HEADER_INTEGRITY_FAULT);

        if (!WriteSchemaState(SchemaState::Ready)) return Fail(PERSISTENCE_BOOT_IO_FAULT);
        if (!WriteSchemaState(SchemaState::Ready)) return Fail(PERSISTENCE_BOOT_IO_FAULT);
        state = SchemaState::Ready;
    }

    if (state != SchemaState::Ready) return Fail(PERSISTENCE_BOOT_SCHEMA_INTEGRITY_FAULT);

    const PatternStoreRecoveryStatus recovery = PatternStore_RecoverJournal();
    if (recovery == PATTERNSTORE_RECOVERY_FAILED) return Fail(PERSISTENCE_BOOT_IO_FAULT);
    if (recovery == PATTERNSTORE_RECOVERY_INTEGRITY_FAULT)
        return Fail(PERSISTENCE_BOOT_PATTERN_INTEGRITY_FAULT);
    STORAGE_BOOT_PROBE(STORAGE_BOOT_PROBE_RECOVERY_RESOLVED);

    const SongHeaderSelectStatus song_status = PersistenceStore_SelectSongHeader(nullptr, nullptr);
    if (song_status == SONG_HEADER_SELECT_IO_FAULT) return Fail(PERSISTENCE_BOOT_IO_FAULT);
    if (song_status != SONG_HEADER_SELECT_VALID)
        return Fail(PERSISTENCE_BOOT_SONG_HEADER_INTEGRITY_FAULT);

    if (!SchemaReadyPairValid())
    {
        if (!WriteSchemaState(SchemaState::Ready) || !SchemaReadyPairValid())
            return Fail(PERSISTENCE_BOOT_IO_FAULT);
    }

    s_boot_status = PERSISTENCE_BOOT_READY;
    return s_boot_status;
}

PersistenceBootStatus PersistenceStore_Inspect(void)
{
    if (!MB85RC1MT_IsReady()) return PERSISTENCE_BOOT_FRAM_NOT_READY;
    SchemaRecord schema{};
    const SchemaInspectStatus inspect = InspectSchema(&schema, nullptr);
    if (inspect == SchemaInspectStatus::IoFault) return PERSISTENCE_BOOT_IO_FAULT;
    if (inspect == SchemaInspectStatus::Unsupported) return PERSISTENCE_BOOT_UNSUPPORTED_SCHEMA;
    if (inspect == SchemaInspectStatus::IntegrityFault)
        return PERSISTENCE_BOOT_SCHEMA_INTEGRITY_FAULT;
    if (inspect == SchemaInspectStatus::Blank) return PERSISTENCE_BOOT_FACTORY_REQUIRED;
    const SchemaState state = (SchemaState)schema.state;
    if (state == SchemaState::Formatting || state == SchemaState::Initializing)
        return PERSISTENCE_BOOT_INITIALIZATION_IN_PROGRESS;
    if (state != SchemaState::Ready) return PERSISTENCE_BOOT_SCHEMA_INTEGRITY_FAULT;
    return PERSISTENCE_BOOT_READY;
}

PersistenceBootStatus PersistenceStore_FactoryInitialize(
    PersistenceFactoryProgressCallback progress)
{
    s_boot_status = PERSISTENCE_BOOT_INITIALIZATION_IN_PROGRESS;
    if (!MB85RC1MT_IsReady()) return Fail(PERSISTENCE_BOOT_FRAM_NOT_READY);

    ReportFactory(progress, PERSISTENCE_FACTORY_FORMATTING_MARKER, 0u, 2u);
    if (!WriteSchemaRecordAt(FRAM_SCHEMA_RECORD_A_ADDR, SchemaState::Formatting, 1u))
        goto io_failure;
    ReportFactory(progress, PERSISTENCE_FACTORY_FORMATTING_MARKER, 1u, 2u);
    if (!WriteSchemaRecordAt(FRAM_SCHEMA_RECORD_B_ADDR, SchemaState::Formatting, 2u))
        goto io_failure;

    ReportFactory(progress, PERSISTENCE_FACTORY_INVALIDATING_JOURNAL, 0u, 1u);
    if (!PatternStore_DiscardJournal()) goto io_failure;
    for (uint8_t index = 0u; index < FRAM_PATTERNSTORE_SLOT_COUNT; ++index)
    {
        ReportFactory(progress, PERSISTENCE_FACTORY_INVALIDATING_PATTERNS,
                      index, FRAM_PATTERNSTORE_SLOT_COUNT);
        if (!PatternStore_InvalidateSlot(index)) goto io_failure;
    }
    ReportFactory(progress, PERSISTENCE_FACTORY_INVALIDATING_SONG, 0u, 2u);
    if (!InvalidateSongHeaders()) goto io_failure;

    ReportFactory(progress, PERSISTENCE_FACTORY_INITIALIZING_MARKER, 0u, 2u);
    if (!WriteSchemaRecordAt(FRAM_SCHEMA_RECORD_A_ADDR, SchemaState::Initializing, 3u))
        goto io_failure;
    ReportFactory(progress, PERSISTENCE_FACTORY_INITIALIZING_MARKER, 1u, 2u);
    if (!WriteSchemaRecordAt(FRAM_SCHEMA_RECORD_B_ADDR, SchemaState::Initializing, 4u))
        goto io_failure;

    for (uint8_t index = 0u; index < FRAM_PATTERNSTORE_SLOT_COUNT; ++index)
    {
        ReportFactory(progress, PERSISTENCE_FACTORY_WRITING_PATTERNS,
                      index, FRAM_PATTERNSTORE_SLOT_COUNT);
        if (!PatternStore_WriteDefaultGeneration(index, 1u)) goto io_failure;
    }

    {
        PersistentSongHeaderData defaults{};
        PersistenceStore_DefaultSongHeader(&defaults);
        ReportFactory(progress, PERSISTENCE_FACTORY_WRITING_SONG, 0u, 2u);
        if (!WriteSongHeaderRecordAt(FRAM_SONG_HEADER_A_ADDR, defaults, 1u)) goto io_failure;
        ReportFactory(progress, PERSISTENCE_FACTORY_WRITING_SONG, 1u, 2u);
        if (!WriteSongHeaderRecordAt(FRAM_SONG_HEADER_B_ADDR, defaults, 2u)) goto io_failure;
    }

    ReportFactory(progress, PERSISTENCE_FACTORY_VALIDATING, 0u, 1u);
    if (!ValidateAllPatterns()) return Fail(PERSISTENCE_BOOT_PATTERN_INTEGRITY_FAULT);
    if (PersistenceStore_SelectSongHeader(nullptr, nullptr) != SONG_HEADER_SELECT_VALID)
        return Fail(PERSISTENCE_BOOT_SONG_HEADER_INTEGRITY_FAULT);

    ReportFactory(progress, PERSISTENCE_FACTORY_WRITING_READY, 0u, 2u);
    if (!WriteSchemaRecordAt(FRAM_SCHEMA_RECORD_A_ADDR, SchemaState::Ready, 5u))
        goto io_failure;
    if (!ValidateAllPatterns() ||
        PersistenceStore_SelectSongHeader(nullptr, nullptr) != SONG_HEADER_SELECT_VALID)
        return Fail(PERSISTENCE_BOOT_PATTERN_INTEGRITY_FAULT);
    ReportFactory(progress, PERSISTENCE_FACTORY_WRITING_READY, 1u, 2u);
    if (!WriteSchemaRecordAt(FRAM_SCHEMA_RECORD_B_ADDR, SchemaState::Ready, 6u))
        goto io_failure;
    if (!SchemaReadyPairValid()) return Fail(PERSISTENCE_BOOT_SCHEMA_INTEGRITY_FAULT);

    s_boot_status = PERSISTENCE_BOOT_READY;
    ReportFactory(progress, PERSISTENCE_FACTORY_COMPLETE, 1u, 1u);
    return s_boot_status;

io_failure:
    return Fail(PERSISTENCE_BOOT_IO_FAULT);
}

PersistenceBootStatus PersistenceStore_GetBootStatus(void)
{
    return s_boot_status;
}

#if defined(PATTERN_STORE_HOST_TEST)
void PersistenceStore_ResetRuntimeStateForTest(void)
{
    s_boot_status = PERSISTENCE_BOOT_NOT_RUN;
}
#endif