#include "sequencer_bridge.h"
#include "devices/sequencer/sequencer_device.h"
#include "devices/sequencer/chords/chord_library.h"
#include "platform/fram/mb85rc256.h"
#include "platform/fram/fram_layout.h"
#include "platform/fram/fram_settings.h"
#include "platform/dac8564/dac8564.h"
#include "core/pitch_mapping.h"
#include "uClock.h"
#include <cstdio>
#include <cstring>

static SequencerDevice g_sequencer;
static uint8_t s_persist_depth = 0u;
static uint8_t s_persist_dirty = 0u;
static uint8_t s_tick_enabled = 1u;
static uint8_t s_last_substep_index = 0xFFu;

extern "C" void uClock_PC1_ClockOut_Toggle(uint32_t tick);

enum CvRouterMode : uint8_t
{
    kCvRouterSplit  = 0u,
    kCvRouterUnison = 1u,
};

namespace {

static constexpr uint8_t kSongMagic[4] = { 0x53, 0x31, 0x32, 0x53 }; // "S12S"
static constexpr uint32_t kSongBlobVersion = 3u;
/* Logical lanes 0..3 correspond to CV1..CV4 in UI/gate routing. */
static constexpr Dac8564Channel kLaneToDac[4] = {
    DAC8564_CH_A, /* lane 0 (CV1 logical) */
    DAC8564_CH_B, /* lane 1 (CV2 logical) */
    DAC8564_CH_C, /* lane 2 (CV3 logical) */
    DAC8564_CH_D  /* lane 3 (CV4 logical) */
};
static uint32_t s_last_step_index = 0xFFFFFFFFu;
static uint16_t s_last_step_mask = 0xFFFFu;
static uint8_t s_last_arp_note = 0xFFu;
static bool s_cv_init_done = false;
static uint16_t s_cv_zero_code[4] = {40363u, 40451u, 40330u, 40221u};
static uint8_t s_cv_router_mode = kCvRouterSplit;
static volatile uint8_t s_gate_channel_mask = 0u;
static uint8_t s_gate_hold_active = 0u;
static uint8_t s_gate_step_pulsed = 0u;
static uint8_t s_gate_block_ticks = 0u;
static uint32_t s_gate_prev_step = 0xFFFFFFFFu;
static uint32_t s_gate_prev_loops = 0xFFFFFFFFu;
static uint8_t s_gate_prev_substep = 0xFFu;

static sequencer::LedgerSlot ToSequencerLedgerSlot(const BridgeLedgerSlot* in)
{
    sequencer::LedgerSlot out{};
    sequencer::LedgerSlotClear(out);
    if (!in) return out;

    for (uint8_t i = 0u; i < 4u; ++i)
    {
        const uint8_t note = in->notes[i];
        if (!sequencer::IsMidiNoteValid(note)) continue;
        (void)sequencer::LedgerSlotAdd(out, note);
    }
    return out;
}

static BridgeLedgerSlot ToBridgeLedgerSlot(const sequencer::LedgerSlot& in)
{
    BridgeLedgerSlot out{{sequencer::kMidiNoteNone,
                          sequencer::kMidiNoteNone,
                          sequencer::kMidiNoteNone,
                          sequencer::kMidiNoteNone}};
    for (uint8_t i = 0u; i < 4u && i < in.notes.size(); ++i)
    {
        out.notes[i] = in.notes[i];
    }
    return out;
}

static sequencer::ChordType UiChordTypeToLibraryType(uint8_t chord_type)
{
    switch (chord_type)
    {
        case 1:  return sequencer::ChordType::Major;
        case 2:  return sequencer::ChordType::Minor;
        case 3:  return sequencer::ChordType::Diminished;
        case 4:  return sequencer::ChordType::Augmented;
        case 5:  return sequencer::ChordType::Sus4;
        case 6:  return sequencer::ChordType::Dom7;
        case 7:  return sequencer::ChordType::Major7;
        case 8:  return sequencer::ChordType::Minor7;
        case 9:  return sequencer::ChordType::Dim7;
        case 10: return sequencer::ChordType::Dom7;   /* 7sus4 fallback */
        case 11: return sequencer::ChordType::Major6;
        case 12: return sequencer::ChordType::Minor6;
        case 13: return sequencer::ChordType::Dom9;
        case 14: return sequencer::ChordType::Major9;
        case 15: return sequencer::ChordType::Minor9;
        case 16: return sequencer::ChordType::Dom11;
        default: return sequencer::ChordType::Major;
    }
}

static void Bridge_UClockMusicalCallback(uint32_t tick)
{
    (void)tick;
    g_sequencer.NotifyUClockMusicalCallback();
}

typedef struct
{
    uint8_t  magic[4];
    uint32_t version;
    uint32_t payload_size;
    uint32_t checksum;
} SongBlobHeader;

static constexpr uint8_t kSongFormatIdV2 = 2u;
static constexpr uint8_t kSongFormatIdV3 = 3u;
static constexpr uint8_t kV2PatternCount = 32u;
static constexpr uint8_t kV2StepsPerPattern = 12u;
static constexpr uint8_t kV2PositionsPerStep = 32u;
static constexpr uint8_t kV2ChainCapacity = 32u;
static constexpr uint8_t kV2FlagHasBarStateBitstream = 0x01u;
static constexpr uint8_t kV3FlagHasEventLengths = 0x02u;
static constexpr uint16_t kV2TotalBars = (uint16_t)kV2PatternCount * (uint16_t)kV2StepsPerPattern;
static constexpr char kNameAlphabet[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-";

class BitWriter
{
public:
    BitWriter(uint8_t* buffer, uint32_t capacity_bytes)
        : buffer_(buffer), capacity_bytes_(capacity_bytes), bit_pos_(0u), ok_(true)
    {
        if (buffer_ && capacity_bytes_ > 0u)
        {
            memset(buffer_, 0, capacity_bytes_);
        }
    }

    bool WriteBits(uint32_t value, uint8_t bit_count)
    {
        if (!ok_) return false;
        for (uint8_t i = 0u; i < bit_count; ++i)
        {
            if (bit_pos_ >= (uint64_t)capacity_bytes_ * 8u)
            {
                ok_ = false;
                return false;
            }

            const uint8_t bit = (uint8_t)((value >> i) & 0x1u);
            if (bit)
            {
                const uint32_t byte_index = (uint32_t)(bit_pos_ >> 3u);
                const uint8_t bit_index = (uint8_t)(bit_pos_ & 0x7u);
                buffer_[byte_index] = (uint8_t)(buffer_[byte_index] | (uint8_t)(1u << bit_index));
            }
            ++bit_pos_;
        }
        return true;
    }

    bool WriteU8(uint8_t v) { return WriteBits(v, 8u); }
    bool WriteU16(uint16_t v)
    {
        return WriteU8((uint8_t)(v & 0xFFu)) &&
               WriteU8((uint8_t)((v >> 8u) & 0xFFu));
    }
    bool WriteU32(uint32_t v)
    {
        return WriteU8((uint8_t)(v & 0xFFu)) &&
               WriteU8((uint8_t)((v >> 8u) & 0xFFu)) &&
               WriteU8((uint8_t)((v >> 16u) & 0xFFu)) &&
               WriteU8((uint8_t)((v >> 24u) & 0xFFu));
    }

    bool Finalize() { return ok_; }
    bool ok() const { return ok_; }
    uint32_t bytes_used() const { return (uint32_t)((bit_pos_ + 7u) / 8u); }

private:
    uint8_t* buffer_;
    uint32_t capacity_bytes_;
    uint64_t bit_pos_;
    bool ok_;
};

static inline uint32_t SongChecksumUpdate(uint32_t h, uint8_t byte)
{
    h ^= byte;
    h *= 16777619u;
    return h;
}

class CountingChecksumBitWriter
{
public:
    CountingChecksumBitWriter()
        : current_byte_(0u), bit_index_(0u), byte_count_(0u), checksum_(2166136261u), ok_(true)
    {
    }

    bool WriteBits(uint32_t value, uint8_t bit_count)
    {
        if (!ok_) return false;
        for (uint8_t i = 0u; i < bit_count; ++i)
        {
            const uint8_t bit = (uint8_t)((value >> i) & 0x1u);
            if (bit)
            {
                current_byte_ = (uint8_t)(current_byte_ | (uint8_t)(1u << bit_index_));
            }

            ++bit_index_;
            if (bit_index_ == 8u)
            {
                checksum_ = SongChecksumUpdate(checksum_, current_byte_);
                ++byte_count_;
                current_byte_ = 0u;
                bit_index_ = 0u;
            }
        }
        return true;
    }

    bool WriteU8(uint8_t v) { return WriteBits(v, 8u); }
    bool WriteU16(uint16_t v)
    {
        return WriteU8((uint8_t)(v & 0xFFu)) &&
               WriteU8((uint8_t)((v >> 8u) & 0xFFu));
    }
    bool WriteU32(uint32_t v)
    {
        return WriteU8((uint8_t)(v & 0xFFu)) &&
               WriteU8((uint8_t)((v >> 8u) & 0xFFu)) &&
               WriteU8((uint8_t)((v >> 16u) & 0xFFu)) &&
               WriteU8((uint8_t)((v >> 24u) & 0xFFu));
    }

    bool Finalize()
    {
        if (!ok_) return false;
        if (bit_index_ != 0u)
        {
            checksum_ = SongChecksumUpdate(checksum_, current_byte_);
            ++byte_count_;
            current_byte_ = 0u;
            bit_index_ = 0u;
        }
        return true;
    }

    bool ok() const { return ok_; }
    uint32_t bytes_used() const { return byte_count_; }
    uint32_t checksum() const { return checksum_; }

private:
    uint8_t current_byte_;
    uint8_t bit_index_;
    uint32_t byte_count_;
    uint32_t checksum_;
    bool ok_;
};

class FramStreamBitWriter
{
public:
    FramStreamBitWriter(uint16_t start_addr, uint32_t max_bytes)
        : start_addr_(start_addr),
          max_bytes_(max_bytes),
          current_byte_(0u),
          bit_index_(0u),
          byte_count_(0u),
          chunk_len_(0u),
          checksum_(2166136261u),
          ok_(true)
    {
    }

    bool WriteBits(uint32_t value, uint8_t bit_count)
    {
        if (!ok_) return false;
        for (uint8_t i = 0u; i < bit_count; ++i)
        {
            const uint8_t bit = (uint8_t)((value >> i) & 0x1u);
            if (bit)
            {
                current_byte_ = (uint8_t)(current_byte_ | (uint8_t)(1u << bit_index_));
            }

            ++bit_index_;
            if (bit_index_ == 8u)
            {
                if (!EmitByte(current_byte_)) return false;
                current_byte_ = 0u;
                bit_index_ = 0u;
            }
        }
        return true;
    }

    bool WriteU8(uint8_t v) { return WriteBits(v, 8u); }
    bool WriteU16(uint16_t v)
    {
        return WriteU8((uint8_t)(v & 0xFFu)) &&
               WriteU8((uint8_t)((v >> 8u) & 0xFFu));
    }
    bool WriteU32(uint32_t v)
    {
        return WriteU8((uint8_t)(v & 0xFFu)) &&
               WriteU8((uint8_t)((v >> 8u) & 0xFFu)) &&
               WriteU8((uint8_t)((v >> 16u) & 0xFFu)) &&
               WriteU8((uint8_t)((v >> 24u) & 0xFFu));
    }

    bool Finalize()
    {
        if (!ok_) return false;
        if (bit_index_ != 0u)
        {
            if (!EmitByte(current_byte_)) return false;
            current_byte_ = 0u;
            bit_index_ = 0u;
        }
        return FlushChunk();
    }

    bool ok() const { return ok_; }
    uint32_t bytes_used() const { return byte_count_; }
    uint32_t checksum() const { return checksum_; }

private:
    bool EmitByte(uint8_t byte)
    {
        if (!ok_) return false;
        if (byte_count_ >= max_bytes_)
        {
            ok_ = false;
            return false;
        }

        chunk_[chunk_len_++] = byte;
        checksum_ = SongChecksumUpdate(checksum_, byte);
        ++byte_count_;

        if (chunk_len_ == (uint8_t)sizeof(chunk_))
        {
            return FlushChunk();
        }
        return true;
    }

    bool FlushChunk()
    {
        if (!ok_) return false;
        if (chunk_len_ == 0u) return true;

        const uint32_t chunk_start_offset = byte_count_ - chunk_len_;
        const uint32_t addr32 = (uint32_t)start_addr_ + chunk_start_offset;
        if (addr32 > 0xFFFFu)
        {
            ok_ = false;
            return false;
        }

        if (!MB85RC256_Write((uint16_t)addr32, chunk_, chunk_len_))
        {
            ok_ = false;
            return false;
        }

        chunk_len_ = 0u;
        return true;
    }

    uint16_t start_addr_;
    uint32_t max_bytes_;
    uint8_t current_byte_;
    uint8_t bit_index_;
    uint32_t byte_count_;
    uint8_t chunk_[32];
    uint8_t chunk_len_;
    uint32_t checksum_;
    bool ok_;
};

class BitReader
{
public:
    BitReader(const uint8_t* buffer, uint32_t size_bytes)
        : buffer_(buffer), size_bytes_(size_bytes), bit_pos_(0u), ok_(true)
    {
    }

    bool ReadBits(uint8_t bit_count, uint32_t* out)
    {
        if (!ok_ || !out) return false;
        uint32_t value = 0u;
        for (uint8_t i = 0u; i < bit_count; ++i)
        {
            if (bit_pos_ >= (uint64_t)size_bytes_ * 8u)
            {
                ok_ = false;
                return false;
            }

            const uint32_t byte_index = (uint32_t)(bit_pos_ >> 3u);
            const uint8_t bit_index = (uint8_t)(bit_pos_ & 0x7u);
            const uint32_t bit = (uint32_t)((buffer_[byte_index] >> bit_index) & 0x1u);
            value |= (uint32_t)(bit << i);
            ++bit_pos_;
        }
        *out = value;
        return true;
    }

    bool ReadU8(uint8_t* out)
    {
        uint32_t v = 0u;
        if (!ReadBits(8u, &v)) return false;
        *out = (uint8_t)v;
        return true;
    }

    bool ReadU16(uint16_t* out)
    {
        uint8_t lo = 0u;
        uint8_t hi = 0u;
        if (!ReadU8(&lo) || !ReadU8(&hi)) return false;
        *out = (uint16_t)((uint16_t)lo | (uint16_t)(hi << 8u));
        return true;
    }

    bool ReadU32(uint32_t* out)
    {
        uint8_t b0 = 0u, b1 = 0u, b2 = 0u, b3 = 0u;
        if (!ReadU8(&b0) || !ReadU8(&b1) || !ReadU8(&b2) || !ReadU8(&b3)) return false;
        *out = (uint32_t)b0 |
               ((uint32_t)b1 << 8u) |
               ((uint32_t)b2 << 16u) |
               ((uint32_t)b3 << 24u);
        return true;
    }

    bool ok() const { return ok_; }

private:
    const uint8_t* buffer_;
    uint32_t size_bytes_;
    uint64_t bit_pos_;
    bool ok_;
};

static uint8_t EncodeStepDivision(uint8_t division)
{
    switch (division)
    {
        case 1u: return 0u;
        case 2u: return 1u;
        case 4u: return 2u;
        case 8u: return 3u;
        default: return 2u;
    }
}

static uint8_t DecodeStepDivision(uint8_t code)
{
    switch (code & 0x03u)
    {
        case 0u: return 1u;
        case 1u: return 2u;
        case 2u: return 4u;
        case 3u: return 8u;
        default: return 4u;
    }
}

static uint8_t EncodeDurationCode(uint32_t duration_multiplier)
{
    switch (duration_multiplier)
    {
        case 1u: return 0u;
        case 2u: return 1u;
        case 4u: return 2u;
        case 3u: return 3u;
        default: return 0u;
    }
}

static uint32_t DecodeDurationCode(uint8_t code)
{
    switch (code & 0x03u)
    {
        case 0u: return 1u;
        case 1u: return 2u;
        case 2u: return 4u;
        case 3u: return 3u;
        default: return 1u;
    }
}

static uint8_t NameCharToCode(char c)
{
    for (uint8_t i = 0u; i < (uint8_t)(sizeof(kNameAlphabet) - 1u); ++i)
    {
        if (kNameAlphabet[i] == c) return i;
    }
    return 0u;
}

static char NameCodeToChar(uint8_t code)
{
    if (code >= (uint8_t)(sizeof(kNameAlphabet) - 1u)) return ' ';
    return kNameAlphabet[code];
}

static uint8_t NameLength16(const char name[17])
{
    uint8_t len = 0u;
    while (len < 16u && name[len] != '\0') ++len;
    return len;
}

static uint32_t Comb(uint32_t n, uint32_t k)
{
    if (k > n) return 0u;
    if (k == 0u || k == n) return 1u;
    if (k > (n - k)) k = n - k;

    uint64_t result = 1u;
    for (uint32_t i = 1u; i <= k; ++i)
    {
        result = (result * (uint64_t)(n - k + i)) / i;
    }
    return (uint32_t)result;
}

static uint32_t RankCombination(const uint8_t* notes, uint8_t count)
{
    uint32_t rank = 0u;
    for (uint8_t i = 0u; i < count; ++i)
    {
        rank += Comb((uint32_t)notes[i], (uint32_t)(i + 1u));
    }
    return rank;
}

static bool UnrankCombination(uint8_t count, uint32_t rank, uint8_t* out_notes)
{
    if (!out_notes || count < 2u || count > 4u) return false;
    const uint32_t max_rank = Comb(128u, count);
    if (rank >= max_rank) return false;

    for (int32_t i = (int32_t)count; i >= 1; --i)
    {
        uint32_t x = 127u;
        while (Comb(x, (uint32_t)i) > rank)
        {
            if (x == 0u) return false;
            --x;
        }
        out_notes[(uint8_t)(i - 1)] = (uint8_t)x;
        rank -= Comb(x, (uint32_t)i);
    }
    return true;
}

static uint8_t ExtractValidSortedNotes(const sequencer::LedgerSlot& slot, uint8_t out_notes[4])
{
    uint8_t count = 0u;
    for (uint8_t i = 0u; i < slot.notes.size() && count < 4u; ++i)
    {
        const uint8_t n = slot.notes[i];
        if (!sequencer::IsMidiNoteValid(n)) continue;
        out_notes[count++] = n;
    }
    return count;
}

static uint16_t DerivePitchMaskFromStepLedger(const sequencer::StepSlot& slot)
{
    uint16_t mask = 0u;
    for (uint8_t pos = 0u; pos < sequencer::kStepLedgerMax; ++pos)
    {
        mask |= sequencer::LedgerSlotToPitchClassMask(slot.note_ledger[pos]);
    }
    return mask;
}

static sequencer::BarState LegacyStepTypeToBarStateCompat(sequencer::StepType type)
{
    return (type == sequencer::StepType::Skip) ? sequencer::BarState::Skip : sequencer::BarState::Active;
}

static bool PersistIsEventStart(const sequencer::StepSlot& step, uint8_t canonical_pos)
{
    if (canonical_pos >= kV2PositionsPerStep) return false;
    if (sequencer::LedgerSlotIsEmpty(step.note_ledger[canonical_pos])) return false;
    if (canonical_pos == 0u) return true;
    return sequencer::LedgerSlotIsEmpty(step.note_ledger[(uint8_t)(canonical_pos - 1u)]);
}

static uint8_t PersistPackedEventLengthRawGet(const sequencer::StepSlot& step, uint8_t canonical_pos)
{
    if (canonical_pos >= kV2PositionsPerStep) return 0u;

    const uint16_t bit_index = (uint16_t)canonical_pos * 5u;
    const uint8_t byte_index = (uint8_t)(bit_index >> 3u);
    const uint8_t bit_offset = (uint8_t)(bit_index & 0x7u);
    uint32_t chunk = 0u;

    chunk |= (uint32_t)step.event_length_packed[byte_index];
    if ((uint8_t)(byte_index + 1u) < sequencer::kStepEventLengthPackedBytes)
    {
        chunk |= (uint32_t)step.event_length_packed[(uint8_t)(byte_index + 1u)] << 8u;
    }
    if ((uint8_t)(byte_index + 2u) < sequencer::kStepEventLengthPackedBytes)
    {
        chunk |= (uint32_t)step.event_length_packed[(uint8_t)(byte_index + 2u)] << 16u;
    }

    return (uint8_t)((chunk >> bit_offset) & 0x1Fu);
}

static void PersistPackedEventLengthRawSet(sequencer::StepSlot& step, uint8_t canonical_pos, uint8_t raw_len_minus_1)
{
    if (canonical_pos >= kV2PositionsPerStep) return;

    raw_len_minus_1 &= 0x1Fu;
    const uint16_t bit_index = (uint16_t)canonical_pos * 5u;
    const uint8_t byte_index = (uint8_t)(bit_index >> 3u);
    const uint8_t bit_offset = (uint8_t)(bit_index & 0x7u);

    uint32_t chunk = 0u;
    chunk |= (uint32_t)step.event_length_packed[byte_index];
    if ((uint8_t)(byte_index + 1u) < sequencer::kStepEventLengthPackedBytes)
    {
        chunk |= (uint32_t)step.event_length_packed[(uint8_t)(byte_index + 1u)] << 8u;
    }
    if ((uint8_t)(byte_index + 2u) < sequencer::kStepEventLengthPackedBytes)
    {
        chunk |= (uint32_t)step.event_length_packed[(uint8_t)(byte_index + 2u)] << 16u;
    }

    const uint32_t mask = (uint32_t)0x1Fu << bit_offset;
    chunk = (chunk & ~mask) | ((uint32_t)raw_len_minus_1 << bit_offset);

    step.event_length_packed[byte_index] = (uint8_t)(chunk & 0xFFu);
    if ((uint8_t)(byte_index + 1u) < sequencer::kStepEventLengthPackedBytes)
    {
        step.event_length_packed[(uint8_t)(byte_index + 1u)] = (uint8_t)((chunk >> 8u) & 0xFFu);
    }
    if ((uint8_t)(byte_index + 2u) < sequencer::kStepEventLengthPackedBytes)
    {
        step.event_length_packed[(uint8_t)(byte_index + 2u)] = (uint8_t)((chunk >> 16u) & 0xFFu);
    }
}

template <typename WriterT>
static bool EncodeSongCore(const sequencer::Song& song,
                           WriterT& w,
                           uint32_t* out_payload_size,
                           uint8_t format_id,
                           uint8_t flags,
                           bool include_event_lengths)
{
    if (!out_payload_size) return false;

    if (!w.WriteU8(format_id)) return false;
    if (!w.WriteU8(flags)) return false;
    if (!w.WriteU16((uint16_t)(song.global_bpm & 0xFFFFu))) return false;
    if (!w.WriteU8((uint8_t)song.key_root)) return false;
    if (!w.WriteU8((uint8_t)song.key_scale)) return false;
    if (!w.WriteU8(song.time_sig.numerator)) return false;
    if (!w.WriteU8(song.time_sig.denominator)) return false;
    if (!w.WriteU8(song.swing)) return false;
    if (!w.WriteU8((uint8_t)song.transpose)) return false;
    if (!w.WriteU8(kV2PatternCount)) return false;
    if (!w.WriteU8(kV2StepsPerPattern)) return false;
    if (!w.WriteU8(kV2PositionsPerStep)) return false;
    if (!w.WriteU8(kV2ChainCapacity)) return false;
    if (!w.WriteU16(0u)) return false;

    uint8_t name_lengths[kV2PatternCount * kV2StepsPerPattern] = {0u};
    uint16_t step_index = 0u;

    for (uint8_t p = 0u; p < kV2PatternCount; ++p)
    {
        const sequencer::Pattern& pat = song.patterns[p];
        uint32_t tempo_bits = 0u;
        memcpy(&tempo_bits, &pat.tempo_multiplier, sizeof(tempo_bits));
        if (!w.WriteU32(tempo_bits)) return false;

        uint8_t packed0 = (uint8_t)((pat.step_count & 0x0Fu) |
                          ((EncodeStepDivision(pat.step_division) & 0x03u) << 4u) |
                          (((uint8_t)pat.playback_mode & 0x03u) << 6u));
        uint8_t packed1 = (uint8_t)((pat.repeat_count & 0x1Fu) |
                          (((uint8_t)pat.arp_mode & 0x07u) << 5u));
        uint8_t packed2 = (uint8_t)(((uint8_t)pat.arp_rate & 0x03u));

        if (!w.WriteU8(packed0) || !w.WriteU8(packed1) || !w.WriteU8(packed2)) return false;

        for (uint8_t s = 0u; s < kV2StepsPerPattern; ++s, ++step_index)
        {
            const sequencer::StepSlot& step = pat.steps[s];
            const uint8_t name_len = NameLength16(step.custom_chord_name);
            name_lengths[step_index] = name_len;

            if (!w.WriteBits((uint8_t)step.type & 0x03u, 2u)) return false;
            if (!w.WriteBits(EncodeDurationCode(step.duration_multiplier) & 0x03u, 2u)) return false;
            if (!w.WriteBits((step.velocity > 127u) ? 127u : step.velocity, 7u)) return false;
            if (!w.WriteBits((step.probability > 100u) ? 100u : step.probability, 7u)) return false;
            if (!w.WriteBits(name_len & 0x1Fu, 5u)) return false;
        }
    }

    for (uint8_t p = 0u; p < kV2PatternCount; ++p)
    {
        const sequencer::Pattern& pat = song.patterns[p];
        for (uint8_t s = 0u; s < kV2StepsPerPattern; ++s)
        {
            const sequencer::StepSlot& step = pat.steps[s];
            const uint8_t bit = (step.bar_state == sequencer::BarState::Skip) ? 1u : 0u;
            if (!w.WriteBits(bit, 1u)) return false;
        }
    }

    step_index = 0u;
    for (uint8_t p = 0u; p < kV2PatternCount; ++p)
    {
        const sequencer::Pattern& pat = song.patterns[p];
        for (uint8_t s = 0u; s < kV2StepsPerPattern; ++s, ++step_index)
        {
            const sequencer::StepSlot& step = pat.steps[s];
            const uint8_t name_len = name_lengths[step_index];
            for (uint8_t i = 0u; i < name_len; ++i)
            {
                if (!w.WriteBits(NameCharToCode(step.custom_chord_name[i]) & 0x3Fu, 6u)) return false;
            }
        }
    }

    if (include_event_lengths)
    {
        for (uint8_t p = 0u; p < kV2PatternCount; ++p)
        {
            const sequencer::Pattern& pat = song.patterns[p];
            for (uint8_t s = 0u; s < kV2StepsPerPattern; ++s)
            {
                const sequencer::StepSlot& step = pat.steps[s];
                for (uint8_t pos = 0u; pos < kV2PositionsPerStep; ++pos)
                {
                    if (!PersistIsEventStart(step, pos)) continue;
                    if (!w.WriteBits(PersistPackedEventLengthRawGet(step, pos), 5u)) return false;
                }
            }
        }
    }

    for (uint8_t p = 0u; p < kV2PatternCount; ++p)
    {
        const sequencer::Pattern& pat = song.patterns[p];
        for (uint8_t s = 0u; s < kV2StepsPerPattern; ++s)
        {
            const sequencer::StepSlot& step = pat.steps[s];
            uint32_t occupancy = 0u;
            for (uint8_t pos = 0u; pos < kV2PositionsPerStep; ++pos)
            {
                if (!sequencer::LedgerSlotIsEmpty(step.note_ledger[pos]))
                {
                    occupancy |= (uint32_t)(1u << pos);
                }
            }
            if (!w.WriteU32(occupancy)) return false;
        }
    }

    for (uint8_t p = 0u; p < kV2PatternCount; ++p)
    {
        const sequencer::Pattern& pat = song.patterns[p];
        for (uint8_t s = 0u; s < kV2StepsPerPattern; ++s)
        {
            const sequencer::StepSlot& step = pat.steps[s];
            for (uint8_t pos = 0u; pos < kV2PositionsPerStep; ++pos)
            {
                const sequencer::LedgerSlot& cell = step.note_ledger[pos];
                if (sequencer::LedgerSlotIsEmpty(cell)) continue;

                uint8_t notes[4] = {0u, 0u, 0u, 0u};
                const uint8_t count = ExtractValidSortedNotes(cell, notes);
                if (count == 0u) continue;

                if (count == 1u)
                {
                    if (!w.WriteBits(0u, 1u)) return false;
                    if (!w.WriteBits(notes[0], 7u)) return false;
                }
                else
                {
                    if (!w.WriteBits(1u, 1u)) return false;
                    uint8_t kind = 0u;
                    uint8_t rank_bits = 13u;
                    if (count == 2u)
                    {
                        kind = 0u;
                        rank_bits = 13u;
                    }
                    else if (count == 3u)
                    {
                        kind = 1u;
                        rank_bits = 19u;
                    }
                    else
                    {
                        kind = 2u;
                        rank_bits = 24u;
                    }
                    if (!w.WriteBits(kind, 2u)) return false;
                    const uint32_t rank = RankCombination(notes, count);
                    if (!w.WriteBits(rank, rank_bits)) return false;
                }
            }
        }
    }

    if (!w.WriteU8(song.chain.length)) return false;
    if (!w.WriteU8(song.chain.position)) return false;
    for (uint8_t i = 0u; i < kV2ChainCapacity; ++i)
    {
        if (!w.WriteBits(song.chain.pattern_indices[i] & 0x1Fu, 5u)) return false;
    }

    if (!w.Finalize()) return false;
    if (!w.ok()) return false;
    *out_payload_size = w.bytes_used();
    return true;
}

template <typename WriterT>
static bool EncodeSongV2Core(const sequencer::Song& song, WriterT& w, uint32_t* out_payload_size)
{
    return EncodeSongCore(song,
                          w,
                          out_payload_size,
                          kSongFormatIdV2,
                          kV2FlagHasBarStateBitstream,
                          false);
}

template <typename WriterT>
static bool EncodeSongV3Core(const sequencer::Song& song, WriterT& w, uint32_t* out_payload_size)
{
    return EncodeSongCore(song,
                          w,
                          out_payload_size,
                          kSongFormatIdV3,
                          (uint8_t)(kV2FlagHasBarStateBitstream | kV3FlagHasEventLengths),
                          true);
}

static bool __attribute__((optimize("Os"), noinline)) DecodeSongPayload(const uint8_t* payload, uint32_t payload_size, sequencer::Song* out_song)
{
    if (!payload || !out_song) return false;

    BitReader r(payload, payload_size);
    sequencer::Song& song = *out_song;
    song = sequencer::Song{};

    uint8_t format_id = 0u;
    uint8_t flags = 0u;
    uint16_t bpm = 0u;
    uint8_t key_root = 0u;
    uint8_t key_scale = 0u;
    uint8_t time_num = 0u;
    uint8_t time_den = 0u;
    uint8_t swing = 0u;
    uint8_t transpose_u8 = 0u;
    uint8_t pattern_count = 0u;
    uint8_t steps_per_pattern = 0u;
    uint8_t positions_per_step = 0u;
    uint8_t chain_capacity = 0u;
    uint16_t reserved = 0u;

    if (!r.ReadU8(&format_id) || !r.ReadU8(&flags) || !r.ReadU16(&bpm) ||
        !r.ReadU8(&key_root) || !r.ReadU8(&key_scale) ||
        !r.ReadU8(&time_num) || !r.ReadU8(&time_den) || !r.ReadU8(&swing) ||
        !r.ReadU8(&transpose_u8) || !r.ReadU8(&pattern_count) ||
        !r.ReadU8(&steps_per_pattern) || !r.ReadU8(&positions_per_step) ||
        !r.ReadU8(&chain_capacity) || !r.ReadU16(&reserved))
    {
        return false;
    }

    if (format_id != kSongFormatIdV2 && format_id != kSongFormatIdV3) return false;
    if (pattern_count != kV2PatternCount) return false;
    if (steps_per_pattern != kV2StepsPerPattern) return false;
    if (positions_per_step != kV2PositionsPerStep) return false;
    if (chain_capacity != kV2ChainCapacity) return false;
    (void)reserved;

    song.global_bpm = bpm;
    song.key_root = (sequencer::KeyRoot)(key_root % 12u);
    song.key_scale = (sequencer::KeyScale)(key_scale % 9u);
    song.time_sig.numerator = time_num;
    song.time_sig.denominator = time_den;
    song.swing = swing;
    song.transpose = (int8_t)transpose_u8;

    uint8_t name_lengths[kV2PatternCount * kV2StepsPerPattern] = {0u};
    uint16_t step_index = 0u;

    for (uint8_t p = 0u; p < kV2PatternCount; ++p)
    {
        sequencer::Pattern& pat = song.patterns[p];

        uint32_t tempo_bits = 0u;
        uint8_t packed0 = 0u;
        uint8_t packed1 = 0u;
        uint8_t packed2 = 0u;
        if (!r.ReadU32(&tempo_bits) || !r.ReadU8(&packed0) || !r.ReadU8(&packed1) || !r.ReadU8(&packed2)) return false;

        memcpy(&pat.tempo_multiplier, &tempo_bits, sizeof(tempo_bits));
        pat.step_count = (uint8_t)(packed0 & 0x0Fu);
        pat.step_division = DecodeStepDivision((uint8_t)((packed0 >> 4u) & 0x03u));
        pat.playback_mode = (sequencer::PlaybackMode)((packed0 >> 6u) & 0x03u);
        pat.repeat_count = (uint8_t)(packed1 & 0x1Fu);
        pat.arp_mode = (sequencer::ArpMode)((packed1 >> 5u) & 0x07u);
        pat.arp_rate = (sequencer::ArpRate)(packed2 & 0x03u);

        for (uint8_t s = 0u; s < kV2StepsPerPattern; ++s, ++step_index)
        {
            sequencer::StepSlot& step = pat.steps[s];
            uint32_t type_bits = 0u;
            uint32_t duration_bits = 0u;
            uint32_t velocity_bits = 0u;
            uint32_t probability_bits = 0u;
            uint32_t name_len_bits = 0u;

            if (!r.ReadBits(2u, &type_bits) ||
                !r.ReadBits(2u, &duration_bits) ||
                !r.ReadBits(7u, &velocity_bits) ||
                !r.ReadBits(7u, &probability_bits) ||
                !r.ReadBits(5u, &name_len_bits))
            {
                return false;
            }

            step.type = (sequencer::StepType)(type_bits & 0x03u);
            step.bar_state = LegacyStepTypeToBarStateCompat(step.type);
            step.duration_multiplier = DecodeDurationCode((uint8_t)duration_bits);
            step.velocity = (uint8_t)velocity_bits;
            step.probability = (uint8_t)probability_bits;
            const uint8_t name_len = (uint8_t)(name_len_bits & 0x1Fu);
            name_lengths[step_index] = (name_len > 16u) ? 16u : name_len;
            memset(step.custom_chord_name, 0, sizeof(step.custom_chord_name));
            memset(step.event_length_packed.data(), 0, step.event_length_packed.size());

            for (uint8_t pos = 0u; pos < kV2PositionsPerStep; ++pos)
            {
                sequencer::LedgerSlotClear(step.note_ledger[pos]);
            }
        }
    }

    if ((flags & kV2FlagHasBarStateBitstream) != 0u)
    {
        for (uint16_t i = 0u; i < kV2TotalBars; ++i)
        {
            uint32_t state_bit = 0u;
            if (!r.ReadBits(1u, &state_bit)) return false;
            const uint8_t p = (uint8_t)(i / kV2StepsPerPattern);
            const uint8_t s = (uint8_t)(i % kV2StepsPerPattern);
            song.patterns[p].steps[s].bar_state =
                (state_bit != 0u) ? sequencer::BarState::Skip : sequencer::BarState::Active;
        }
    }
    else
    {
        for (uint16_t i = 0u; i < kV2TotalBars; ++i)
        {
            const uint8_t p = (uint8_t)(i / kV2StepsPerPattern);
            const uint8_t s = (uint8_t)(i % kV2StepsPerPattern);
            sequencer::StepSlot& step = song.patterns[p].steps[s];
            step.bar_state = LegacyStepTypeToBarStateCompat(step.type);
        }
    }

    step_index = 0u;
    for (uint8_t p = 0u; p < kV2PatternCount; ++p)
    {
        sequencer::Pattern& pat = song.patterns[p];
        for (uint8_t s = 0u; s < kV2StepsPerPattern; ++s, ++step_index)
        {
            sequencer::StepSlot& step = pat.steps[s];
            const uint8_t name_len = name_lengths[step_index];
            for (uint8_t i = 0u; i < name_len; ++i)
            {
                uint32_t char_code = 0u;
                if (!r.ReadBits(6u, &char_code)) return false;
                step.custom_chord_name[i] = NameCodeToChar((uint8_t)char_code);
            }
            step.custom_chord_name[name_len] = '\0';
        }
    }

    uint32_t occupancy[kV2PatternCount * kV2StepsPerPattern] = {0u};
    for (uint16_t i = 0u; i < (uint16_t)(kV2PatternCount * kV2StepsPerPattern); ++i)
    {
        if (!r.ReadU32(&occupancy[i])) return false;
    }

    step_index = 0u;
    for (uint8_t p = 0u; p < kV2PatternCount; ++p)
    {
        sequencer::Pattern& pat = song.patterns[p];
        for (uint8_t s = 0u; s < kV2StepsPerPattern; ++s, ++step_index)
        {
            sequencer::StepSlot& step = pat.steps[s];
            const uint32_t occ = occupancy[step_index];
            for (uint8_t pos = 0u; pos < kV2PositionsPerStep; ++pos)
            {
                if ((occ & (uint32_t)(1u << pos)) == 0u) continue;

                uint32_t first = 0u;
                if (!r.ReadBits(1u, &first)) return false;

                if (first == 0u)
                {
                    uint32_t note = 0u;
                    if (!r.ReadBits(7u, &note)) return false;
                    if (!sequencer::LedgerSlotAdd(step.note_ledger[pos], (uint8_t)note)) return false;
                }
                else
                {
                    uint32_t kind = 0u;
                    if (!r.ReadBits(2u, &kind)) return false;

                    uint8_t count = 0u;
                    uint8_t rank_bits = 0u;
                    if (kind == 0u)
                    {
                        count = 2u;
                        rank_bits = 13u;
                    }
                    else if (kind == 1u)
                    {
                        count = 3u;
                        rank_bits = 19u;
                    }
                    else if (kind == 2u)
                    {
                        count = 4u;
                        rank_bits = 24u;
                    }
                    else
                    {
                        return false;
                    }

                    uint32_t rank = 0u;
                    if (!r.ReadBits(rank_bits, &rank)) return false;

                    uint8_t notes[4] = {0u, 0u, 0u, 0u};
                    if (!UnrankCombination(count, rank, notes)) return false;
                    for (uint8_t i = 0u; i < count; ++i)
                    {
                        if (!sequencer::LedgerSlotAdd(step.note_ledger[pos], notes[i])) return false;
                    }
                }
            }

            step.note_mask = DerivePitchMaskFromStepLedger(step);
        }
    }

    if (format_id == kSongFormatIdV3 && (flags & kV3FlagHasEventLengths) != 0u)
    {
        for (uint8_t p = 0u; p < kV2PatternCount; ++p)
        {
            sequencer::Pattern& pat = song.patterns[p];
            for (uint8_t s = 0u; s < kV2StepsPerPattern; ++s)
            {
                sequencer::StepSlot& step = pat.steps[s];
                for (uint8_t pos = 0u; pos < kV2PositionsPerStep; ++pos)
                {
                    if (!PersistIsEventStart(step, pos)) continue;

                    uint32_t raw_len_minus_1 = 0u;
                    if (!r.ReadBits(5u, &raw_len_minus_1)) return false;
                    PersistPackedEventLengthRawSet(step, pos, (uint8_t)(raw_len_minus_1 & 0x1Fu));
                }
            }
        }
    }

    if (!r.ReadU8(&song.chain.length)) return false;
    if (!r.ReadU8(&song.chain.position)) return false;
    for (uint8_t i = 0u; i < kV2ChainCapacity; ++i)
    {
        uint32_t idx = 0u;
        if (!r.ReadBits(5u, &idx)) return false;
        song.chain.pattern_indices[i] = (uint8_t)idx;
    }

    return true;
}

static uint32_t SongChecksum(const uint8_t* data, uint32_t len)
{
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < len; ++i)
    {
        h = SongChecksumUpdate(h, data[i]);
    }
    return h;
}

static uint8_t __attribute__((optimize("Os"), noinline)) SaveSongToFram(void)
{
    const sequencer::Song& song = g_sequencer.GetSongView();

    uint32_t payload_size = 0u;
    CountingChecksumBitWriter preflight;
    if (!EncodeSongV3Core(song, preflight, &payload_size))
    {
        printf("S12: song too large for FRAM V3\n");
        return 0u;
    }

    const uint16_t hdr_size = (uint16_t)sizeof(SongBlobHeader);
    if ((uint32_t)hdr_size + payload_size > FRAM_SONGDATA_SIZE)
    {
        printf("S12: song too large for FRAM V3 (%lu bytes)\n", (unsigned long)(payload_size + hdr_size));
        return 0u;
    }

    SongBlobHeader hdr{};
    memcpy(hdr.magic, kSongMagic, sizeof(kSongMagic));
    hdr.version = kSongBlobVersion;
    hdr.payload_size = payload_size;
    hdr.checksum = preflight.checksum();

    const uint16_t payload_addr = (uint16_t)(FRAM_SONGDATA_ADDR + hdr_size);
    FramStreamBitWriter stream_writer(payload_addr, payload_size);
    uint32_t streamed_size = 0u;
    if (!EncodeSongV3Core(song, stream_writer, &streamed_size))
    {
        return 0u;
    }
    if (streamed_size != payload_size) return 0u;
    if (stream_writer.checksum() != hdr.checksum) return 0u;

    if (!MB85RC256_WriteAndVerify(FRAM_SONGDATA_ADDR, reinterpret_cast<const uint8_t*>(&hdr), hdr_size))
    {
        return 0u;
    }
    return 1u;
}

static uint8_t __attribute__((unused, optimize("Os"), noinline)) LoadSongFromFram(void)
{
    SongBlobHeader hdr{};
    if (!MB85RC256_IsReady()) return 0u;
    if (!MB85RC256_Read(FRAM_SONGDATA_ADDR, reinterpret_cast<uint8_t*>(&hdr), (uint16_t)sizeof(hdr))) return 0u;

    if (memcmp(hdr.magic, kSongMagic, sizeof(kSongMagic)) != 0) return 0u;
    if (hdr.version != 2u && hdr.version != 3u) return 0u;
    if ((uint32_t)sizeof(SongBlobHeader) + hdr.payload_size > FRAM_SONGDATA_SIZE) return 0u;

    static uint8_t payload[FRAM_SONGDATA_SIZE];
    if (!MB85RC256_Read((uint16_t)(FRAM_SONGDATA_ADDR + (uint16_t)sizeof(SongBlobHeader)),
                        payload,
                        (uint16_t)hdr.payload_size))
    {
        return 0u;
    }

    const uint32_t checksum = SongChecksum(payload, hdr.payload_size);
    if (checksum != hdr.checksum) return 0u;

    sequencer::Song* song = g_sequencer.GetMutableSongForImport();
    if (!song) return 0u;
    if (!DecodeSongPayload(payload, hdr.payload_size, song)) return 0u;
    g_sequencer.FinalizeImportedSong();
    return 1u;
}

static void PersistSong(void)
{
    if (s_persist_depth > 0u)
    {
        s_persist_dirty = 1u;
        return;
    }
    (void)SaveSongToFram();
}

static void LoadCvRouterModeSetting(void)
{
    FramSettingsBlob settings{};
    if (!FramSettings_Read(&settings) || settings.payload_size < 1u)
    {
        s_cv_router_mode = kCvRouterSplit;
        return;
    }

    const uint8_t mode = settings.reserved[0];
    s_cv_router_mode = (mode == kCvRouterUnison) ? kCvRouterUnison : kCvRouterSplit;
}

static void SaveCvRouterModeSetting(void)
{
    FramSettingsBlob settings{};
    if (!FramSettings_Read(&settings))
    {
        FramSettings_Default(&settings);
    }

    settings.payload_size = 1u;
    settings.reserved[0] = s_cv_router_mode;
    settings.checksum = FramSettings_CalcChecksum(&settings);
    (void)FramSettings_Write(&settings);
}

static float NoteToPitchVolts(uint8_t note)
{
    int16_t clamped = (int16_t)note;
    if (clamped < kPitchMidiMin) clamped = kPitchMidiMin;
    if (clamped > kPitchMidiMax) clamped = kPitchMidiMax;
    return ((float)clamped - (float)kPitchZeroVoltMidi) * S12_PITCH_VOLTS_PER_SEMITONE;
}

static uint16_t ComputeZeroCodeFromCalibration(Dac8564Channel channel, uint16_t fallback_code)
{
    uint16_t code_vlow = 0u;
    uint16_t code_vhigh = 0u;
    DAC8564_GetPitchCalibrationForChannel(channel, &code_vlow, &code_vhigh);
    if (code_vlow == code_vhigh)
    {
        return fallback_code;
    }

    /* Derive the per-channel 0V code from the active low/high calibration span. */
    return DAC8564_PitchVoltsToCodeForChannel(channel, 0.0f);
}

static void Bridge_WriteLogicalLanes(const uint16_t logical_out[4])
{
    uint16_t dac_out[4] = {0u, 0u, 0u, 0u};
    for (uint8_t lane = 0u; lane < 4u; ++lane)
    {
        const uint8_t dac_index = (uint8_t)kLaneToDac[lane];
        dac_out[dac_index] = logical_out[lane];
    }
    DAC8564_SetAllRaw(dac_out[0], dac_out[1], dac_out[2], dac_out[3]);
}

static void RefreshCvZeroCodes(void)
{
    static const uint16_t kFallback[4] = {40363u, 40451u, 40330u, 40221u};
    for (uint8_t lane = 0u; lane < 4u; ++lane)
    {
        s_cv_zero_code[lane] = ComputeZeroCodeFromCalibration(kLaneToDac[lane], kFallback[lane]);
    }
}

} // namespace

extern "C"
{
    void     Bridge_Init(void)
    {
        LoadCvRouterModeSetting();
        /* For current firmware behavior, enforce strict per-channel split routing. */
        if (s_cv_router_mode != kCvRouterSplit)
        {
            s_cv_router_mode = kCvRouterSplit;
            SaveCvRouterModeSetting();
        }
      g_sequencer.Init();

#if defined(S12_USE_UCLOCK_MUSICAL_STEP_CLOCK) && S12_USE_UCLOCK_MUSICAL_STEP_CLOCK
    /*
     * uClock is the musical transport clock.
     * Register the 96 PPQN callback before initialising the hardware timer.
     */
    uClock.setOnSync(
    umodular::clock::uClockClass::PPQN_96,
    Bridge_UClockMusicalCallback);
    uClock.setOnSync(
    umodular::clock::uClockClass::PPQN_24,
    uClock_PC1_ClockOut_Toggle);
    uClock.init();
    uClock.setTempo(120.0f);
    uClock.start();
#endif
        /* Keep runtime step state volatile: do not restore song step data on boot. */

        Bridge_ApplyZeroOutputCodes();
    }
    void     Bridge_ApplyZeroOutputCodes(void)
    {
        RefreshCvZeroCodes();
        Bridge_WriteLogicalLanes(s_cv_zero_code);
        s_cv_init_done = false;
        s_last_step_index = 0xFFFFFFFFu;
        s_last_step_mask = 0xFFFFu;
        s_last_arp_note = 0xFFu;
        s_last_substep_index = 0xFFu;
        s_gate_channel_mask = 0u;
        s_gate_hold_active = 0u;
        s_gate_step_pulsed = 0u;
        s_gate_block_ticks = 0u;
        s_gate_prev_step = 0xFFFFFFFFu;
        s_gate_prev_loops = 0xFFFFFFFFu;
        s_gate_prev_substep = 0xFFu;
    }
    void     Bridge_SetTickEnabled(uint8_t enabled)
    {
        s_tick_enabled = enabled ? 1u : 0u;
    }
    void     Bridge_SetCvRouterMode(uint8_t mode)
    {
        (void)mode;
        const uint8_t normalized = kCvRouterSplit;
        if (normalized == s_cv_router_mode)
        {
            return;
        }

        s_cv_router_mode = normalized;
        SaveCvRouterModeSetting();

        /* Force immediate recompute on next process pass. */
        s_last_step_index = 0xFFFFFFFFu;
        s_last_step_mask = 0xFFFFu;
        s_last_arp_note = 0xFFu;
        s_last_substep_index = 0xFFu;
    }
    uint8_t  Bridge_GetCvRouterMode(void)
    {
        return s_cv_router_mode;
    }
    void     Bridge_Tick1ms(void)
    {
        if (!s_tick_enabled)
        {
            return;
        }

        g_sequencer.Tick1ms();

        const uint32_t step_index = g_sequencer.GetCurrentStep();
        const uint32_t loops = g_sequencer.GetCompletedLoops();
        const uint8_t substep = g_sequencer.GetCurrentStepSubIndex();
        const bool position_changed =
            step_index != s_gate_prev_step ||
            loops != s_gate_prev_loops ||
            substep != s_gate_prev_substep;
        if (position_changed)
        {
            s_gate_prev_step = step_index;
            s_gate_prev_loops = loops;
            s_gate_prev_substep = substep;
            s_gate_step_pulsed = 0u;
            s_gate_hold_active = 0u;
            s_gate_block_ticks = 1u;
        }
        else if (s_gate_block_ticks > 0u)
        {
            --s_gate_block_ticks;
        }

        const bool gate_window_open = (s_gate_block_ticks == 0u) &&
                          g_sequencer.IsGateActive();
        uint8_t gate_mask = 0u;
        if (!gate_window_open)
        {
            s_gate_hold_active = 0u;
        }
        else
        {
            if (s_gate_hold_active)
            {
                gate_mask = s_gate_channel_mask;
            }
            else if (!s_gate_step_pulsed)
            {
                gate_mask = s_gate_channel_mask;
                s_gate_hold_active = 1u;
                s_gate_step_pulsed = 1u;
            }
        }

        uint32_t bsrr = 0u;
        bsrr |= (gate_mask & 0x01u) ? GPIO_PIN_5 : ((uint32_t)GPIO_PIN_5 << 16);
        bsrr |= (gate_mask & 0x02u) ? GPIO_PIN_6 : ((uint32_t)GPIO_PIN_6 << 16);
        bsrr |= (gate_mask & 0x04u) ? GPIO_PIN_7 : ((uint32_t)GPIO_PIN_7 << 16);
        bsrr |= (gate_mask & 0x08u) ? GPIO_PIN_8 : ((uint32_t)GPIO_PIN_8 << 16);
        GPIOC->BSRR = bsrr;
    }
    void     Bridge_TickMusical(void)
    {
        if (!s_tick_enabled)
        {
            return;
        }

        if (!g_sequencer.IsPlaying())
        {
            return;
        }

        g_sequencer.TickMusical();
    }

    void Bridge_ServiceMusicalEvents(void)
    {
        if (!g_sequencer.IsPlaying())
        {
            return;
        }

        if (!g_sequencer.ServiceOnePendingStep())
        {
            return;
        }

        Bridge_WriteCurrentStepSnapshot();
    }
    void Bridge_Start(void)
    {
    s_gate_hold_active = 0u;
    s_gate_step_pulsed = 0u;
    s_gate_block_ticks = 0u;
    s_gate_prev_step = 0xFFFFFFFFu;
    s_gate_prev_loops = 0xFFFFFFFFu;
    s_gate_prev_substep = 0xFFu;

    g_sequencer.Start();

    /* Load STEP 1 CV before the musical clock begins advancing. */
    Bridge_WriteCurrentStepSnapshot();

    GPIOC->BSRR = (GPIO_PIN_1 << 16);  /* force Clock OUT low until the first PPQN tick while playing */

    Bridge_Process();
    }

   void Bridge_Stop(void)
    {
    g_sequencer.Stop();
    GPIOC->BSRR = (GPIO_PIN_1 << 16);  /* force Clock OUT low when transport stops */

    s_gate_channel_mask = 0u;
    s_gate_hold_active = 0u;
    s_gate_step_pulsed = 0u;
    s_gate_block_ticks = 0u;
    s_gate_prev_step = 0xFFFFFFFFu;
    s_gate_prev_loops = 0xFFFFFFFFu;
    s_gate_prev_substep = 0xFFu;

    Bridge_ApplyZeroOutputCodes();
    }

    void     Bridge_Reset(void)
    {
        g_sequencer.Reset();
        s_gate_channel_mask = 0u;
        s_gate_hold_active = 0u;
        s_gate_step_pulsed = 0u;
        s_gate_block_ticks = 0u;
        s_gate_prev_step = 0xFFFFFFFFu;
        s_gate_prev_loops = 0xFFFFFFFFu;
        s_gate_prev_substep = 0xFFu;

        Bridge_ApplyZeroOutputCodes();
    }

    void Bridge_SetBpm(uint32_t bpm)
    {
    g_sequencer.SetBpm(bpm);

    #if defined(S12_USE_UCLOCK_MUSICAL_STEP_CLOCK) && S12_USE_UCLOCK_MUSICAL_STEP_CLOCK
    uClock.setTempo((float)bpm);
    #endif
    }

    void     Bridge_PersistBegin(void)
    {
        if (s_persist_depth < 255u) s_persist_depth++;
    }
    void     Bridge_PersistEnd(void)
    {
        if (s_persist_depth == 0u) return;
        s_persist_depth--;
        if (s_persist_depth == 0u && s_persist_dirty)
        {
            s_persist_dirty = 0u;
            (void)SaveSongToFram();
        }
    }
    void     Bridge_SetPatternStepCount(uint8_t step_count) { g_sequencer.SetPatternStepCount(step_count); PersistSong(); }
    uint8_t  Bridge_GetPatternStepCount(void) { return g_sequencer.GetPatternStepCount(); }
    uint8_t  Bridge_SetPatternTiming(uint8_t step_division, uint8_t numerator, uint8_t denominator)
    {
        if (!g_sequencer.SetPatternTiming(step_division, numerator, denominator)) return 0u;
        PersistSong();
        return 1u;
    }
    void     Bridge_SetPatternStepDivision(uint8_t step_division)
    {
        const uint8_t before = g_sequencer.GetPatternStepDivision();
        g_sequencer.SetPatternStepDivision(step_division);
        if (g_sequencer.GetPatternStepDivision() != before)
        {
            PersistSong();
        }
    }
    uint8_t  Bridge_GetPatternStepDivision(void) { return g_sequencer.GetPatternStepDivision(); }
    void Bridge_SetPatternArpMode(uint8_t arp_mode)
    {
        sequencer::ArpMode mode = sequencer::ArpMode::Off;
        switch (arp_mode)
        {
            case 1u: mode = sequencer::ArpMode::Up; break;
            case 2u: mode = sequencer::ArpMode::Down; break;
            case 3u: mode = sequencer::ArpMode::UpDown; break;
            case 4u: mode = sequencer::ArpMode::Random; break;
            case 0u:
            default:
                mode = sequencer::ArpMode::Off;
                break;
        }

        g_sequencer.SetPatternArpMode(mode);
        PersistSong();
    }
    void Bridge_SetPatternArpRate(uint8_t arp_rate)
    {
        sequencer::ArpRate rate = sequencer::ArpRate::Sixteenth;
        switch (arp_rate)
        {
            case 0u: rate = sequencer::ArpRate::Quarter; break;
            case 1u: rate = sequencer::ArpRate::Eighth; break;
            case 2u: rate = sequencer::ArpRate::Sixteenth; break;
            case 3u: rate = sequencer::ArpRate::ThirtySecond; break;
            default: break;
        }

        g_sequencer.SetPatternArpRate(rate);
        PersistSong();
    }
    uint8_t Bridge_GetPatternArpRate(void)
    {
        return static_cast<uint8_t>(g_sequencer.GetCurrentArpRate());
    }
    void     Bridge_SetTimeSignature(uint8_t numerator, uint8_t denominator) { g_sequencer.SetTimeSignature(numerator, denominator); PersistSong(); }
    uint8_t  Bridge_GetTimeSigNumerator(void) { return g_sequencer.GetTimeSigNumerator(); }
    uint8_t  Bridge_GetTimeSigDenominator(void) { return g_sequencer.GetTimeSigDenominator(); }
    void     Bridge_SetSwing(uint8_t swing) { g_sequencer.SetSwing(swing); PersistSong(); }
    uint8_t  Bridge_GetSwing(void) { return g_sequencer.GetSwing(); }
    void     Bridge_SetStepChordParams(uint8_t step_index,
                                       uint8_t root_key,
                                       uint8_t chord_type,
                                       uint8_t arp_pattern,
                                       uint8_t duration,
                                       uint8_t repeat_count)
    {
        g_sequencer.SetStepChordParams(step_index, root_key, chord_type, arp_pattern, duration, repeat_count);
        PersistSong();
    }
    void     Bridge_SetStepCustomNoteMask(uint8_t step_index, uint16_t note_mask)
    {
        g_sequencer.SetStepCustomNoteMask(step_index, note_mask);
        /* User-loaded/custom step masks are intentionally non-persistent. */
    }
    void     Bridge_SetStepCustomUserChord(uint8_t step_index, uint16_t note_mask, const char* name)
    {
        g_sequencer.SetStepCustomUserChord(step_index, note_mask, name);
        /* User-loaded/custom step masks are intentionally non-persistent. */
    }
    void     Bridge_SetStepLedgerLength(uint8_t step_index, uint8_t length)
    {
        g_sequencer.SetStepLedgerLength(step_index, length);
        /* Step-piano ledger edits are runtime-only for now. */
    }
    void     Bridge_SetStepLedgerSlot(uint8_t step_index, uint8_t slot_index, uint16_t note_mask)
    {
        sequencer::LedgerSlot slot{};
        sequencer::LedgerSlotClear(slot);
        for (uint8_t note = 0u; note < 12u; ++note)
        {
            if ((note_mask & (uint16_t)(1u << note)) != 0u)
            {
                (void)sequencer::LedgerSlotAdd(slot, (uint8_t)(kLegacyPitchClassFallbackMidiBase + note));
            }
        }
        g_sequencer.SetStepLedgerSlot(step_index, slot_index, slot);
        /* Step-piano ledger edits are runtime-only for now. */
    }
    uint8_t  Bridge_GetStepLedgerLength(uint8_t step_index)
    {
        return g_sequencer.GetStepLedgerLength(step_index);
    }
    uint16_t Bridge_GetStepLedgerSlot(uint8_t step_index, uint8_t slot_index)
    {
        const sequencer::LedgerSlot slot = g_sequencer.GetStepLedgerSlot(step_index, slot_index);
        return sequencer::LedgerSlotToPitchClassMask(slot);
    }
    uint8_t Bridge_GetStepEventLength(uint8_t step_index, uint8_t slot_index)
    {
        return g_sequencer.GetStepEventLengthGrid(step_index, slot_index);
    }
    void Bridge_SetStepEventLength(uint8_t step_index,
                                   uint8_t slot_index,
                                   uint8_t length_positions)
    {
        g_sequencer.SetStepEventLengthGrid(step_index, slot_index, length_positions);
        PersistSong();
    }
    uint8_t Bridge_GetStepEventMaxLength(uint8_t step_index, uint8_t slot_index)
    {
        return g_sequencer.GetStepEventMaxLengthGrid(step_index, slot_index);
    }
    void     Bridge_SetStepLedgerSlotMidi(uint8_t step_index, uint8_t slot_index, const BridgeLedgerSlot* slot)
    {
        const sequencer::LedgerSlot mapped = ToSequencerLedgerSlot(slot);
        g_sequencer.SetStepLedgerSlot(step_index, slot_index, mapped);
        /* Step-piano ledger edits are runtime-only for now. */
    }
    BridgeLedgerSlot Bridge_GetStepLedgerSlotMidi(uint8_t step_index, uint8_t slot_index)
    {
        const sequencer::LedgerSlot slot = g_sequencer.GetStepLedgerSlot(step_index, slot_index);
        return ToBridgeLedgerSlot(slot);
    }
    void     Bridge_ClearStepLedger(uint8_t step_index)
    {
        g_sequencer.ClearStepLedger(step_index);
    }
    void     Bridge_SetStepBarState(uint8_t step_index, uint8_t state)
    {
        const sequencer::BarState mapped =
            (state == 1u) ? sequencer::BarState::Skip : sequencer::BarState::Active;
        g_sequencer.SetStepBarState(step_index, mapped);
        PersistSong();
    }
    uint8_t  Bridge_GetStepBarState(uint8_t step_index)
    {
        return (g_sequencer.GetStepBarState(step_index) == sequencer::BarState::Skip) ? 1u : 0u;
    }
    void     Bridge_SetPatternRepeatCount(uint8_t repeat_count)
    {
        g_sequencer.SetPatternRepeatCount(repeat_count);
        PersistSong();
    }
    uint8_t  Bridge_GetPatternRepeatCount(void)
    {
        return g_sequencer.GetPatternRepeatCount();
    }
    void     Bridge_SetCurrentPattern(uint8_t pattern_index)
    {
        g_sequencer.SetCurrentPatternIndex(pattern_index);
        PersistSong();
    }
    void     Bridge_SetChainLength(uint8_t length)
    {
        g_sequencer.SetChainLength(length);
        PersistSong();
    }
    uint8_t  Bridge_GetChainLength(void)
    {
        return g_sequencer.GetChainLength();
    }
    void     Bridge_SetChainPatternAt(uint8_t pos, uint8_t pattern_index)
    {
        g_sequencer.SetChainPatternAt(pos, pattern_index);
        PersistSong();
    }
    uint8_t  Bridge_GetChainPatternAt(uint8_t pos)
    {
        return g_sequencer.GetChainPatternAt(pos);
    }
    uint8_t  Bridge_GetChainCurrentPosition(void)
    {
        return g_sequencer.GetChainCurrentPosition();
    }
    uint8_t  Bridge_GetCurrentPatternRepeatProgress(void)
    {
        return g_sequencer.GetCurrentPatternRepeatProgress();
    }
    uint16_t Bridge_GetStepNoteMask(uint8_t step_index)
    {
        return g_sequencer.GetStepNoteMask(step_index);
    }
    uint8_t Bridge_GetSongKeyRoot(void)
    {
        return static_cast<uint8_t>(g_sequencer.GetSongView().key_root) % 12u;
    }
    uint8_t Bridge_GetSongKeyScale(void)
    {
        return static_cast<uint8_t>(g_sequencer.GetSongView().key_scale) % 9u;
    }
    uint8_t Bridge_GetCurrentPatternArpMode(void)
    {
        return static_cast<uint8_t>(g_sequencer.GetCurrentArpMode());
    }
    uint16_t Bridge_GetChordMaskForUiParams(uint8_t root_key, uint8_t chord_type)
    {
        if (chord_type == 0u)
        {
            return 0u;
        }

        const uint8_t normalized_root = (uint8_t)(root_key % 12u);
        const sequencer::ChordType mapped = UiChordTypeToLibraryType(chord_type);
        return sequencer::ChordLibrary::GetNoteMask(static_cast<sequencer::KeyRoot>(normalized_root), mapped);
    }
    uint8_t Bridge_GetStepLedgerCvLaneNotes(uint8_t step_index, uint8_t slot_index, uint8_t out_notes[4])
    {
        if (!out_notes)
        {
            return 0u;
        }

        for (uint8_t i = 0u; i < 4u; ++i)
        {
            out_notes[i] = sequencer::kMidiNoteNone;
        }

        if (g_sequencer.GetCurrentArpMode() != sequencer::ArpMode::Off)
        {
            return 0u;
        }

        const sequencer::LedgerSlot slot = g_sequencer.GetStepLedgerSlot(step_index, slot_index);
        for (uint8_t i = 0u; i < 4u && i < slot.notes.size(); ++i)
        {
            out_notes[i] = slot.notes[i];
        }
        return 1u;
    }
    uint16_t Bridge_GetCurrentOutputNoteMask(void)
    {
        if (!g_sequencer.IsPlaying())
        {
            return 0u;
        }

        const sequencer::ArpMode arp_mode = g_sequencer.GetCurrentArpMode();
        if (arp_mode == sequencer::ArpMode::Off)
        {
            uint8_t notes[4] = {sequencer::kMidiNoteNone,
                                sequencer::kMidiNoteNone,
                                sequencer::kMidiNoteNone,
                                sequencer::kMidiNoteNone};
            const uint8_t count = g_sequencer.GetCurrentStepNotesForPlayback(notes);
            uint16_t mask = 0u;
            for (uint8_t i = 0u; i < count; ++i)
            {
                if (notes[i] == sequencer::kMidiNoteNone) continue;
                mask |= (uint16_t)(1u << (notes[i] % 12u));
            }
            return mask;
        }

        const uint8_t note = g_sequencer.GetCurrentNote();
        return sequencer::IsMidiNoteValid(note) ? (uint16_t)(1u << (note % 12u)) : 0u;
    }
    int16_t Bridge_GetCurrentOutputPrimaryMilliVolts(void)
    {
        uint8_t notes[4] = {sequencer::kMidiNoteNone,
                            sequencer::kMidiNoteNone,
                            sequencer::kMidiNoteNone,
                            sequencer::kMidiNoteNone};
        const uint8_t count = g_sequencer.GetCurrentStepNotesForPlayback(notes);
        if (count == 0u || notes[0] == sequencer::kMidiNoteNone) return -1;

        const float volts = NoteToPitchVolts(notes[0]);
        return (int16_t)(volts * 1000.0f + 0.5f);
    }

    void Bridge_WriteCurrentStepSnapshot(void)
    {
        const bool playing = g_sequencer.IsPlaying();

        if (!playing)
        {
            s_gate_channel_mask = 0u;
            return;
        }

        s_cv_init_done = true;

        const uint32_t step_index = g_sequencer.GetCurrentStep();
        const uint8_t substep = g_sequencer.GetCurrentStepSubIndex();
        uint8_t step_notes[4] = {sequencer::kMidiNoteNone,
                                 sequencer::kMidiNoteNone,
                                 sequencer::kMidiNoteNone,
                                 sequencer::kMidiNoteNone};
        const uint8_t step_note_count = g_sequencer.GetCurrentStepNotesForPlayback(step_notes);
        uint16_t note_mask = 0u;
        for (uint8_t i = 0u; i < step_note_count; ++i)
        {
            const uint8_t note = step_notes[i];
            if (note == sequencer::kMidiNoteNone) continue;
            note_mask |= (uint16_t)(1u << (note % 12u));
        }
        const sequencer::ArpMode arp_mode = g_sequencer.GetCurrentArpMode();

        if (arp_mode == sequencer::ArpMode::Off)
        {
            if (step_index == s_last_step_index &&
            substep == s_last_substep_index &&
            note_mask == s_last_step_mask)
            {
                return;
            }

            uint8_t notes[4] = {sequencer::kMidiNoteNone,
                                sequencer::kMidiNoteNone,
                                sequencer::kMidiNoteNone,
                                sequencer::kMidiNoteNone};
            uint8_t note_count = g_sequencer.GetCurrentStepNotesForPlayback(notes);

            uint16_t out[4] = {s_cv_zero_code[0], s_cv_zero_code[1], s_cv_zero_code[2], s_cv_zero_code[3]};
            if (s_cv_router_mode == kCvRouterUnison)
            {
                if (note_count > 0u)
                {
                    const float volts = NoteToPitchVolts(notes[0]);
                    for (uint8_t ch = 0u; ch < 4u; ++ch)
                    {
                        out[ch] = DAC8564_PitchVoltsToCodeForChannel(kLaneToDac[ch], volts);
                    }
                    Bridge_WriteLogicalLanes(out);
                }
                s_gate_channel_mask = (note_count > 0u) ? 0x0Fu : 0u;
            }
            else
            {
                uint8_t active_lane_mask = 0u;
                for (uint8_t ch = 0u; ch < 4u; ++ch)
                {
                    if (ch >= note_count) continue;
                    if (!sequencer::IsMidiNoteValid(notes[ch])) continue;

                    const float volts = NoteToPitchVolts(notes[ch]);
                    out[ch] = DAC8564_PitchVoltsToCodeForChannel(kLaneToDac[ch], volts);
                    active_lane_mask = (uint8_t)(active_lane_mask | (uint8_t)(1u << ch));
                }

                if (active_lane_mask != 0u)
                {
                    Bridge_WriteLogicalLanes(out);
                }

                s_gate_channel_mask = active_lane_mask;
            }
                s_last_step_index = step_index;
                s_last_substep_index = substep;
                s_last_step_mask = note_mask;
                s_last_arp_note = 0xFFu;
        }
        else
        {
            const uint8_t note = g_sequencer.GetCurrentNote();
            if (step_index == s_last_step_index && note == s_last_arp_note)
            {
                return;
            }

            if (sequencer::IsMidiNoteValid(note))
            {
                const float volts = NoteToPitchVolts(note);
                uint16_t out[4] = {s_cv_zero_code[0], s_cv_zero_code[1], s_cv_zero_code[2], s_cv_zero_code[3]};
                out[0] = DAC8564_PitchVoltsToCodeForChannel(kLaneToDac[0], volts);
                Bridge_WriteLogicalLanes(out);
                s_gate_channel_mask = 0x01u;
            }
            else
            {
                Bridge_WriteLogicalLanes(s_cv_zero_code);
                s_gate_channel_mask = 0u;
            }

            s_last_step_index = step_index;
            s_last_step_mask = note_mask;
            s_last_arp_note = note;
        }
    }

    void Bridge_Process(void)
    {
        g_sequencer.Process();

#if defined(S12_USE_UCLOCK_MUSICAL_STEP_CLOCK) && S12_USE_UCLOCK_MUSICAL_STEP_CLOCK
        /* uClock musical step service writes the snapshot at the top of the loop. */
#else
        Bridge_WriteCurrentStepSnapshot();
#endif
    }

    const char* Bridge_GetStepChordDisplayName(uint8_t step_index, char* buf, uint8_t buf_len)
    {
        if (!buf || buf_len == 0) return "";
        if (g_sequencer.GetStepCustomChordName(step_index, buf, buf_len))
        {
            return buf;
        }
        return Bridge_FindChordName(g_sequencer.GetStepNoteMask(step_index), buf, buf_len);
    }

    const char* Bridge_FindChordName(uint16_t note_mask, char* buf, uint8_t buf_len)
    {
        if (!buf || buf_len == 0) return "";
        const sequencer::ChordPreset* presets = sequencer::ChordLibrary::GetPresets();
        uint32_t count = sequencer::ChordLibrary::GetPresetCount();
        for (uint32_t i = 0; i < count; ++i)
        {
            if (presets[i].note_mask == note_mask)
            {
                const char* root = sequencer::ChordLibrary::GetRootName(presets[i].root);
                const char* type = sequencer::ChordLibrary::GetTypeName(presets[i].type);
                snprintf(buf, buf_len, "%s %s", root, type);
                return buf;
            }
        }
        strncpy(buf, "Custom", buf_len);
        buf[buf_len - 1] = '\0';
        return buf;
    }
    uint8_t Bridge_GetStepChordUiParams(uint8_t step_index,
                                        uint8_t* root_key,
                                        uint8_t* chord_type,
                                        uint8_t* duration,
                                        uint8_t* repeat_count)
    {
        return g_sequencer.GetStepChordUiParams(step_index,
                                                root_key,
                                                chord_type,
                                                duration,
                                                repeat_count) ? 1u : 0u;
    }
    uint32_t Bridge_GetCurrentStep(void)          { return g_sequencer.GetCurrentStep(); }
    uint8_t  Bridge_GetCurrentStepSubIndex(void)  { return g_sequencer.GetCurrentStepSubIndex(); }
    uint8_t  Bridge_IsPlaying(void)               { return g_sequencer.IsPlaying() ? 1u : 0u; }
    uint32_t Bridge_GetElapsedMs(void)            { return g_sequencer.GetElapsedMs(); }


uint8_t Bridge_GetCurrentPattern(void)
{
    return static_cast<uint8_t>(g_sequencer.GetCurrentPatternIndex());
}

uint32_t Bridge_GetRunTimeMs(void)
{
    return g_sequencer.GetRunTimeMs();
}

uint32_t Bridge_GetCompletedLoops(void)
{
    return g_sequencer.GetCompletedLoops();
}

uint32_t Bridge_GetUClockMusicalCallbackCount(void)
{
    return g_sequencer.GetUClockMusicalCallbackCount();
}

uint32_t Bridge_GetTickMusicalCount(void)
{
    return g_sequencer.GetTickMusicalCount();
}

uint32_t Bridge_GetPendingStepEnqueueCount(void)
{
    return g_sequencer.GetPendingStepEnqueueCount();
}

uint32_t Bridge_GetServiceOneStepCount(void)
{
    return g_sequencer.GetServiceOneStepCount();
}
}