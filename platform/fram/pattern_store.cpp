#include "platform/fram/pattern_store.h"
#include "platform/fram/mb85rc1mt.h"
#include <cstring>

namespace
{

struct PatternStoreHeader
{
    uint32_t magic;
    uint16_t version;
    uint16_t payload_size;
    uint32_t crc32;
};

constexpr uint32_t kPatternStoreMagic   = 0x53313250u; /* "S12P" */
constexpr uint16_t kPatternStoreVersion = 1u;

/* Stage 1 scratch: also the buffer the future pattern-cache "sweep" work
 * (SetPatternTiming/EncodeSongCore/DecodeSongPayload one-pattern-at-a-time
 * conversion) is expected to reuse -- not extra throwaway RAM. */
uint8_t s_pattern_store_scratch[sizeof(sequencer::Pattern)];

} // namespace

static_assert(sizeof(PatternStoreHeader) == 12u,
              "PatternStoreHeader layout changed -- update FRAM_PATTERNSTORE_SLOT_STRIDE sizing");
static_assert(sizeof(PatternStoreHeader) + sizeof(sequencer::Pattern) <= FRAM_PATTERNSTORE_SLOT_STRIDE,
              "pattern store slot too small for header + Pattern payload");
static_assert(FRAM_PATTERNSTORE_ADDR == 0x8000u,
              "pattern store must start immediately after the legacy 0x0000-0x7FFF region");
static_assert(FRAM_PATTERNSTORE_END <= 0x1C800u,
              "all 32 pattern-store slots must end at or below 0x1C800");
static_assert(FRAM_PATTERNSTORE_END <= MB85RC1MT_SIZE_BYTES,
              "pattern store exceeds the MB85RC1MT's actual 0x20000-byte capacity");

/* Standard zlib/ISO-hdlc CRC32, matching every other CRC32 already used in
 * this codebase's FRAM tooling (fram_backup_dump.c, fram_restore_migrate.c). */
static uint32_t Crc32Compute(const uint8_t* data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0u; i < len; ++i)
    {
        crc ^= data[i];
        for (uint8_t bit = 0u; bit < 8u; ++bit)
        {
            uint32_t mask = (uint32_t)(-(int32_t)(crc & 1u));
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

static uint32_t SlotAddress(uint8_t index)
{
    return FRAM_PATTERNSTORE_ADDR + (uint32_t)index * FRAM_PATTERNSTORE_SLOT_STRIDE;
}

static uint8_t ReadHeaderAndPayload(uint8_t index, PatternStoreHeader* out_hdr)
{
    const uint32_t base = SlotAddress(index);

    if (!MB85RC1MT_Read(base, reinterpret_cast<uint8_t*>(out_hdr), (uint16_t)sizeof(PatternStoreHeader)))
    {
        return 0u;
    }
    if (out_hdr->magic != kPatternStoreMagic) return 0u;
    if (out_hdr->version != kPatternStoreVersion) return 0u;
    if (out_hdr->payload_size != (uint16_t)sizeof(sequencer::Pattern)) return 0u;

    if (!MB85RC1MT_Read(base + (uint32_t)sizeof(PatternStoreHeader),
                        s_pattern_store_scratch,
                        (uint16_t)sizeof(sequencer::Pattern)))
    {
        return 0u;
    }

    return (Crc32Compute(s_pattern_store_scratch, (uint32_t)sizeof(sequencer::Pattern)) == out_hdr->crc32) ? 1u : 0u;
}

uint8_t PatternStore_Read(uint8_t index, sequencer::Pattern* out_pattern)
{
    if (index >= FRAM_PATTERNSTORE_SLOT_COUNT || out_pattern == nullptr)
    {
        return 0u;
    }

    PatternStoreHeader hdr{};
    if (!ReadHeaderAndPayload(index, &hdr))
    {
        return 0u; /* out_pattern left completely untouched */
    }

    memcpy(out_pattern, s_pattern_store_scratch, sizeof(sequencer::Pattern));
    return 1u;
}

uint8_t PatternStore_Write(uint8_t index, const sequencer::Pattern* pattern)
{
    if (index >= FRAM_PATTERNSTORE_SLOT_COUNT || pattern == nullptr)
    {
        return 0u;
    }

    memcpy(s_pattern_store_scratch, pattern, sizeof(sequencer::Pattern));

    PatternStoreHeader hdr;
    hdr.magic = kPatternStoreMagic;
    hdr.version = kPatternStoreVersion;
    hdr.payload_size = (uint16_t)sizeof(sequencer::Pattern);
    hdr.crc32 = Crc32Compute(s_pattern_store_scratch, (uint32_t)sizeof(sequencer::Pattern));

    const uint32_t base = SlotAddress(index);

    /* Payload first, header (with magic) last: a slot only reads back as
     * valid once its payload write has actually completed. */
    if (!MB85RC1MT_Write(base + (uint32_t)sizeof(PatternStoreHeader),
                        s_pattern_store_scratch,
                        (uint16_t)sizeof(sequencer::Pattern)))
    {
        return 0u;
    }
    if (!MB85RC1MT_Write(base, reinterpret_cast<const uint8_t*>(&hdr), (uint16_t)sizeof(hdr)))
    {
        return 0u;
    }

    return 1u;
}

uint8_t PatternStore_IsValid(uint8_t index)
{
    if (index >= FRAM_PATTERNSTORE_SLOT_COUNT)
    {
        return 0u;
    }

    PatternStoreHeader hdr{};
    return ReadHeaderAndPayload(index, &hdr);
}

namespace
{

enum class AsyncPhase : uint8_t { Idle, WritePayload, WriteHeader };

AsyncPhase s_async_phase = AsyncPhase::Idle;
uint8_t s_async_index = 0xFFu;
uint16_t s_async_offset = 0u;
PatternStoreHeader s_async_hdr{};

} // namespace

uint8_t PatternStore_WriteAsyncBegin(uint8_t index, const sequencer::Pattern* pattern)
{
    if (index >= FRAM_PATTERNSTORE_SLOT_COUNT || pattern == nullptr)
    {
        return 0u;
    }
    if (s_async_phase != AsyncPhase::Idle)
    {
        return 0u; /* in progress -- caller must wait for Busy() to clear first */
    }

    /* Reuses the same scratch buffer as the synchronous API above; safe
     * because the two writers are never active at the same time in
     * practice (async is the only one used on the live edit path). */
    memcpy(s_pattern_store_scratch, pattern, sizeof(sequencer::Pattern));

    s_async_hdr.magic = kPatternStoreMagic;
    s_async_hdr.version = kPatternStoreVersion;
    s_async_hdr.payload_size = (uint16_t)sizeof(sequencer::Pattern);
    s_async_hdr.crc32 = Crc32Compute(s_pattern_store_scratch, (uint32_t)sizeof(sequencer::Pattern));

    s_async_index = index;
    s_async_offset = 0u;
    s_async_phase = AsyncPhase::WritePayload;
    return 1u;
}

PatternStoreAsyncStatus PatternStore_WriteAsyncService(void)
{
    if (s_async_phase == AsyncPhase::Idle)
    {
        return PATTERNSTORE_ASYNC_IDLE;
    }

    const uint32_t base = SlotAddress(s_async_index);

    if (s_async_phase == AsyncPhase::WritePayload)
    {
        const uint16_t payload_size = (uint16_t)sizeof(sequencer::Pattern);
        const uint16_t remaining = (uint16_t)(payload_size - s_async_offset);
        const uint16_t chunk = (remaining < PATTERNSTORE_CHUNK_BYTES) ? remaining : (uint16_t)PATTERNSTORE_CHUNK_BYTES;

        if (!MB85RC1MT_Write(base + (uint32_t)sizeof(PatternStoreHeader) + s_async_offset,
                            s_pattern_store_scratch + s_async_offset,
                            chunk))
        {
            s_async_phase = AsyncPhase::Idle;
            s_async_index = 0xFFu;
            return PATTERNSTORE_ASYNC_FAILED;
        }

        s_async_offset = (uint16_t)(s_async_offset + chunk);
        if (s_async_offset >= payload_size)
        {
            s_async_phase = AsyncPhase::WriteHeader;
        }
        return PATTERNSTORE_ASYNC_BUSY;
    }

    /* WriteHeader: single small (12-byte) write, header/magic last so the
     * slot only becomes valid once the payload write above fully landed. */
    if (!MB85RC1MT_Write(base, reinterpret_cast<const uint8_t*>(&s_async_hdr), (uint16_t)sizeof(s_async_hdr)))
    {
        s_async_phase = AsyncPhase::Idle;
        s_async_index = 0xFFu;
        return PATTERNSTORE_ASYNC_FAILED;
    }

    s_async_phase = AsyncPhase::Idle;
    s_async_index = 0xFFu;
    return PATTERNSTORE_ASYNC_DONE;
}

uint8_t PatternStore_WriteAsyncBusy(void)
{
    return (s_async_phase != AsyncPhase::Idle) ? 1u : 0u;
}

namespace
{

enum class ReadAsyncPhase : uint8_t { Idle, ReadingPayload };

ReadAsyncPhase s_read_phase = ReadAsyncPhase::Idle;
uint8_t s_read_index = 0xFFu;
uint16_t s_read_offset = 0u;
uint32_t s_read_expected_crc = 0u;
sequencer::Pattern* s_read_target = nullptr;

} // namespace

uint8_t PatternStore_ReadAsyncBegin(uint8_t index, sequencer::Pattern* out_pattern)
{
    if (index >= FRAM_PATTERNSTORE_SLOT_COUNT || out_pattern == nullptr)
    {
        return 0u;
    }
    if (s_read_phase != ReadAsyncPhase::Idle)
    {
        return 0u; /* already busy -- caller must wait for Busy() to clear first */
    }

    const uint32_t base = SlotAddress(index);
    PatternStoreHeader hdr{};
    if (!MB85RC1MT_Read(base, reinterpret_cast<uint8_t*>(&hdr), (uint16_t)sizeof(hdr)))
    {
        return 0u;
    }
    if (hdr.magic != kPatternStoreMagic) return 0u;
    if (hdr.version != kPatternStoreVersion) return 0u;
    if (hdr.payload_size != (uint16_t)sizeof(sequencer::Pattern)) return 0u;

    s_read_index = index;
    s_read_offset = 0u;
    s_read_expected_crc = hdr.crc32;
    s_read_target = out_pattern;
    s_read_phase = ReadAsyncPhase::ReadingPayload;
    return 1u;
}

PatternStoreAsyncStatus PatternStore_ReadAsyncService(void)
{
    if (s_read_phase == ReadAsyncPhase::Idle)
    {
        return PATTERNSTORE_ASYNC_IDLE;
    }

    const uint32_t base = SlotAddress(s_read_index);
    const uint16_t payload_size = (uint16_t)sizeof(sequencer::Pattern);
    const uint16_t remaining = (uint16_t)(payload_size - s_read_offset);
    const uint16_t chunk = (remaining < PATTERNSTORE_CHUNK_BYTES) ? remaining : (uint16_t)PATTERNSTORE_CHUNK_BYTES;

    uint8_t* dst = reinterpret_cast<uint8_t*>(s_read_target) + s_read_offset;
    if (!MB85RC1MT_Read(base + (uint32_t)sizeof(PatternStoreHeader) + s_read_offset, dst, chunk))
    {
        s_read_phase = ReadAsyncPhase::Idle;
        s_read_target = nullptr;
        return PATTERNSTORE_ASYNC_FAILED;
    }

    s_read_offset = (uint16_t)(s_read_offset + chunk);
    if (s_read_offset >= payload_size)
    {
        const uint32_t actual_crc = Crc32Compute(reinterpret_cast<const uint8_t*>(s_read_target), payload_size);
        s_read_phase = ReadAsyncPhase::Idle;
        s_read_target = nullptr;
        return (actual_crc == s_read_expected_crc) ? PATTERNSTORE_ASYNC_DONE : PATTERNSTORE_ASYNC_FAILED;
    }
    return PATTERNSTORE_ASYNC_BUSY;
}

uint8_t PatternStore_ReadAsyncBusy(void)
{
    return (s_read_phase != ReadAsyncPhase::Idle) ? 1u : 0u;
}

