#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core/pitch_mapping.h"
#include "devices/sequencer/arp_engine.h"
#include "devices/sequencer/sequencer_device.h"
#include "devices/sequencer/sequencer_types.h"

namespace {

static constexpr uint8_t kSongMagic[4] = {0x53, 0x31, 0x32, 0x53};
static constexpr uint32_t kSongBlobVersion = 3u;
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

static constexpr uint16_t H_FRAM_TOTAL_SIZE = 0x8000u;
static constexpr uint16_t H_FRAM_USERCHORDS_ADDR = 0x0020u;
static constexpr uint16_t H_FRAM_USERCHORDS_SIZE = 0x0B00u;
static constexpr uint16_t H_FRAM_SETTINGS_SIZE = 0x0100u;
static constexpr uint16_t H_FRAM_FUTURE_SIZE = 0x0100u;
static constexpr uint16_t H_FRAM_SETTINGS_ADDR = (H_FRAM_TOTAL_SIZE - H_FRAM_SETTINGS_SIZE - H_FRAM_FUTURE_SIZE);
static constexpr uint16_t H_FRAM_SONGDATA_ADDR = (H_FRAM_USERCHORDS_ADDR + H_FRAM_USERCHORDS_SIZE);
static constexpr uint16_t H_FRAM_SONGDATA_SIZE = (H_FRAM_SETTINGS_ADDR - H_FRAM_SONGDATA_ADDR);

static float PitchVoltsOptionB(uint8_t midi_note) {
    int16_t clamped = (int16_t)midi_note;
    if (clamped < kPitchMidiMin) clamped = kPitchMidiMin;
    if (clamped > kPitchMidiMax) clamped = kPitchMidiMax;
    return ((float)clamped - (float)kPitchZeroVoltMidi) * S12_PITCH_VOLTS_PER_SEMITONE;
}

struct SongBlobHeader {
    uint8_t magic[4];
    uint32_t version;
    uint32_t payload_size;
    uint32_t checksum;
};

static inline uint32_t SongChecksumUpdate(uint32_t h, uint8_t byte) {
    h ^= byte;
    h *= 16777619u;
    return h;
}

class BitWriter {
public:
    BitWriter(uint8_t *buffer, uint32_t capacity_bytes)
        : buffer_(buffer), capacity_bytes_(capacity_bytes), bit_pos_(0u), ok_(true) {
        if (buffer_ && capacity_bytes_ > 0u) {
            memset(buffer_, 0, capacity_bytes_);
        }
    }

    bool WriteBits(uint32_t value, uint8_t bit_count) {
        if (!ok_) return false;
        for (uint8_t i = 0u; i < bit_count; ++i) {
            if (bit_pos_ >= (uint64_t)capacity_bytes_ * 8u) {
                ok_ = false;
                return false;
            }
            const uint8_t bit = (uint8_t)((value >> i) & 0x1u);
            if (bit) {
                const uint32_t byte_index = (uint32_t)(bit_pos_ >> 3u);
                const uint8_t bit_index = (uint8_t)(bit_pos_ & 0x7u);
                buffer_[byte_index] = (uint8_t)(buffer_[byte_index] | (uint8_t)(1u << bit_index));
            }
            ++bit_pos_;
        }
        return true;
    }

    bool WriteU8(uint8_t v) { return WriteBits(v, 8u); }
    bool WriteU16(uint16_t v) {
        return WriteU8((uint8_t)(v & 0xFFu)) && WriteU8((uint8_t)((v >> 8u) & 0xFFu));
    }
    bool WriteU32(uint32_t v) {
        return WriteU8((uint8_t)(v & 0xFFu)) && WriteU8((uint8_t)((v >> 8u) & 0xFFu)) &&
               WriteU8((uint8_t)((v >> 16u) & 0xFFu)) && WriteU8((uint8_t)((v >> 24u) & 0xFFu));
    }

    bool Finalize() { return ok_; }
    bool ok() const { return ok_; }
    uint32_t bytes_used() const { return (uint32_t)((bit_pos_ + 7u) / 8u); }

private:
    uint8_t *buffer_;
    uint32_t capacity_bytes_;
    uint64_t bit_pos_;
    bool ok_;
};

class BitReader {
public:
    BitReader(const uint8_t *buffer, uint32_t size_bytes)
        : buffer_(buffer), size_bytes_(size_bytes), bit_pos_(0u), ok_(true) {}

    bool ReadBits(uint8_t bit_count, uint32_t *out) {
        if (!ok_ || !out) return false;
        uint32_t value = 0u;
        for (uint8_t i = 0u; i < bit_count; ++i) {
            if (bit_pos_ >= (uint64_t)size_bytes_ * 8u) {
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

    bool ReadU8(uint8_t *out) {
        uint32_t v = 0u;
        if (!ReadBits(8u, &v)) return false;
        *out = (uint8_t)v;
        return true;
    }
    bool ReadU16(uint16_t *out) {
        uint8_t lo = 0u, hi = 0u;
        if (!ReadU8(&lo) || !ReadU8(&hi)) return false;
        *out = (uint16_t)((uint16_t)lo | (uint16_t)(hi << 8u));
        return true;
    }
    bool ReadU32(uint32_t *out) {
        uint8_t b0 = 0u, b1 = 0u, b2 = 0u, b3 = 0u;
        if (!ReadU8(&b0) || !ReadU8(&b1) || !ReadU8(&b2) || !ReadU8(&b3)) return false;
        *out = (uint32_t)b0 | ((uint32_t)b1 << 8u) | ((uint32_t)b2 << 16u) | ((uint32_t)b3 << 24u);
        return true;
    }

private:
    const uint8_t *buffer_;
    uint32_t size_bytes_;
    uint64_t bit_pos_;
    bool ok_;
};

class CountingChecksumBitWriter {
public:
    CountingChecksumBitWriter()
        : current_byte_(0u), bit_index_(0u), byte_count_(0u), checksum_(2166136261u), ok_(true) {}

    bool WriteBits(uint32_t value, uint8_t bit_count) {
        if (!ok_) return false;
        for (uint8_t i = 0u; i < bit_count; ++i) {
            const uint8_t bit = (uint8_t)((value >> i) & 0x1u);
            if (bit) current_byte_ = (uint8_t)(current_byte_ | (uint8_t)(1u << bit_index_));
            ++bit_index_;
            if (bit_index_ == 8u) {
                checksum_ = SongChecksumUpdate(checksum_, current_byte_);
                ++byte_count_;
                current_byte_ = 0u;
                bit_index_ = 0u;
            }
        }
        return true;
    }

    bool WriteU8(uint8_t v) { return WriteBits(v, 8u); }
    bool WriteU16(uint16_t v) { return WriteU8((uint8_t)(v & 0xFFu)) && WriteU8((uint8_t)((v >> 8u) & 0xFFu)); }
    bool WriteU32(uint32_t v) {
        return WriteU8((uint8_t)(v & 0xFFu)) && WriteU8((uint8_t)((v >> 8u) & 0xFFu)) &&
               WriteU8((uint8_t)((v >> 16u) & 0xFFu)) && WriteU8((uint8_t)((v >> 24u) & 0xFFu));
    }

    bool Finalize() {
        if (!ok_) return false;
        if (bit_index_ != 0u) {
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

struct WriteOp {
    uint16_t addr;
    uint16_t len;
};

class MockFram {
public:
    MockFram() : mem_(H_FRAM_TOTAL_SIZE, 0xFFu), fail_after_payload_bytes_(-1) {}

    bool Read(uint16_t addr, uint8_t *dst, uint16_t len) {
        if (!dst) return false;
        if ((uint32_t)addr + len > mem_.size()) return false;
        memcpy(dst, &mem_[addr], len);
        return true;
    }

    bool Write(uint16_t addr, const uint8_t *src, uint16_t len) {
        if (!src) return false;
        if ((uint32_t)addr + len > mem_.size()) return false;
        if (fail_after_payload_bytes_ >= 0) {
            if (addr >= (uint16_t)(H_FRAM_SONGDATA_ADDR + sizeof(SongBlobHeader))) {
                for (uint16_t i = 0; i < len; ++i) {
                    if (payload_written_ >= fail_after_payload_bytes_) return false;
                    mem_[addr + i] = src[i];
                    ++payload_written_;
                }
                writes_.push_back({addr, len});
                return true;
            }
        }
        memcpy(&mem_[addr], src, len);
        writes_.push_back({addr, len});
        return true;
    }

    bool WriteAndVerify(uint16_t addr, const uint8_t *src, uint16_t len) {
        if (!Write(addr, src, len)) return false;
        std::vector<uint8_t> tmp(len);
        if (!Read(addr, tmp.data(), len)) return false;
        return memcmp(tmp.data(), src, len) == 0;
    }

    void ClearWrites() { writes_.clear(); }
    const std::vector<WriteOp> &writes() const { return writes_; }

    void set_fail_after_payload_bytes(int n) {
        fail_after_payload_bytes_ = n;
        payload_written_ = 0;
    }

private:
    std::vector<uint8_t> mem_;
    std::vector<WriteOp> writes_;
    int fail_after_payload_bytes_;
    int payload_written_ = 0;
};

class FramStreamBitWriter {
public:
    FramStreamBitWriter(MockFram &fram, uint16_t start_addr, uint32_t max_bytes)
        : fram_(fram), start_addr_(start_addr), max_bytes_(max_bytes), current_byte_(0u), bit_index_(0u),
          byte_count_(0u), chunk_len_(0u), checksum_(2166136261u), ok_(true) {}

    bool WriteBits(uint32_t value, uint8_t bit_count) {
        if (!ok_) return false;
        for (uint8_t i = 0u; i < bit_count; ++i) {
            const uint8_t bit = (uint8_t)((value >> i) & 0x1u);
            if (bit) current_byte_ = (uint8_t)(current_byte_ | (uint8_t)(1u << bit_index_));
            ++bit_index_;
            if (bit_index_ == 8u) {
                if (!EmitByte(current_byte_)) return false;
                current_byte_ = 0u;
                bit_index_ = 0u;
            }
        }
        return true;
    }

    bool WriteU8(uint8_t v) { return WriteBits(v, 8u); }
    bool WriteU16(uint16_t v) { return WriteU8((uint8_t)(v & 0xFFu)) && WriteU8((uint8_t)((v >> 8u) & 0xFFu)); }
    bool WriteU32(uint32_t v) {
        return WriteU8((uint8_t)(v & 0xFFu)) && WriteU8((uint8_t)((v >> 8u) & 0xFFu)) &&
               WriteU8((uint8_t)((v >> 16u) & 0xFFu)) && WriteU8((uint8_t)((v >> 24u) & 0xFFu));
    }

    bool Finalize() {
        if (!ok_) return false;
        if (bit_index_ != 0u) {
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
    bool EmitByte(uint8_t byte) {
        if (!ok_) return false;
        if (byte_count_ >= max_bytes_) {
            ok_ = false;
            return false;
        }
        chunk_[chunk_len_++] = byte;
        checksum_ = SongChecksumUpdate(checksum_, byte);
        ++byte_count_;
        if (chunk_len_ == sizeof(chunk_)) return FlushChunk();
        return true;
    }

    bool FlushChunk() {
        if (!ok_) return false;
        if (chunk_len_ == 0u) return true;
        const uint32_t chunk_start_offset = byte_count_ - (uint32_t)chunk_len_;
        const uint32_t addr32 = (uint32_t)start_addr_ + chunk_start_offset;
        if (addr32 > 0xFFFFu) {
            ok_ = false;
            return false;
        }
        if (!fram_.Write((uint16_t)addr32, chunk_.data(), (uint16_t)chunk_len_)) {
            ok_ = false;
            return false;
        }
        chunk_len_ = 0u;
        return true;
    }

    MockFram &fram_;
    uint16_t start_addr_;
    uint32_t max_bytes_;
    uint8_t current_byte_;
    uint8_t bit_index_;
    uint32_t byte_count_;
    std::array<uint8_t, 32> chunk_{};
    size_t chunk_len_;
    uint32_t checksum_;
    bool ok_;
};

static uint8_t EncodeStepDivision(uint8_t division) {
    switch (division) {
    case 1u: return 0u;
    case 2u: return 1u;
    case 4u: return 2u;
    case 8u: return 3u;
    default: return 2u;
    }
}

static uint8_t DecodeStepDivision(uint8_t code) {
    switch (code & 0x03u) {
    case 0u: return 1u;
    case 1u: return 2u;
    case 2u: return 4u;
    case 3u: return 8u;
    default: return 4u;
    }
}

static uint8_t EncodeDurationCode(uint32_t duration_multiplier) {
    switch (duration_multiplier) {
    case 1u: return 0u;
    case 2u: return 1u;
    case 4u: return 2u;
    case 3u: return 3u;
    default: return 0u;
    }
}

static uint32_t DecodeDurationCode(uint8_t code) {
    switch (code & 0x03u) {
    case 0u: return 1u;
    case 1u: return 2u;
    case 2u: return 4u;
    case 3u: return 3u;
    default: return 1u;
    }
}

static uint8_t NameCharToCode(char c) {
    for (uint8_t i = 0u; i < (uint8_t)(sizeof(kNameAlphabet) - 1u); ++i) {
        if (kNameAlphabet[i] == c) return i;
    }
    return 0u;
}

static char NameCodeToChar(uint8_t code) {
    if (code >= (uint8_t)(sizeof(kNameAlphabet) - 1u)) return ' ';
    return kNameAlphabet[code];
}

static uint8_t NameLength16(const char name[17]) {
    uint8_t len = 0u;
    while (len < 16u && name[len] != '\0') ++len;
    return len;
}

static uint32_t Comb(uint32_t n, uint32_t k) {
    if (k > n) return 0u;
    if (k == 0u || k == n) return 1u;
    if (k > (n - k)) k = n - k;
    uint64_t result = 1u;
    for (uint32_t i = 1u; i <= k; ++i) {
        result = (result * (uint64_t)(n - k + i)) / i;
    }
    return (uint32_t)result;
}

static uint32_t RankCombination(const uint8_t *notes, uint8_t count) {
    uint32_t rank = 0u;
    for (uint8_t i = 0u; i < count; ++i) {
        rank += Comb((uint32_t)notes[i], (uint32_t)(i + 1u));
    }
    return rank;
}

static bool UnrankCombination(uint8_t count, uint32_t rank, uint8_t *out_notes) {
    if (!out_notes || count < 2u || count > 4u) return false;
    const uint32_t max_rank = Comb(128u, count);
    if (rank >= max_rank) return false;

    for (int32_t i = (int32_t)count; i >= 1; --i) {
        uint32_t x = 127u;
        while (Comb(x, (uint32_t)i) > rank) {
            if (x == 0u) return false;
            --x;
        }
        out_notes[(uint8_t)(i - 1)] = (uint8_t)x;
        rank -= Comb(x, (uint32_t)i);
    }
    return true;
}

static uint8_t ExtractValidSortedNotes(const sequencer::LedgerSlot &slot, uint8_t out_notes[4]) {
    uint8_t count = 0u;
    for (uint8_t i = 0u; i < slot.notes.size() && count < 4u; ++i) {
        const uint8_t n = slot.notes[i];
        if (!sequencer::IsMidiNoteValid(n)) continue;
        out_notes[count++] = n;
    }
    return count;
}

static uint16_t DerivePitchMaskFromStepLedger(const sequencer::StepSlot &slot) {
    uint16_t mask = 0u;
    for (uint8_t pos = 0u; pos < sequencer::kStepLedgerMax; ++pos) {
        mask |= sequencer::LedgerSlotToPitchClassMask(slot.note_ledger[pos]);
    }
    return mask;
}

static sequencer::BarState LegacyStepTypeToBarStateCompat(sequencer::StepType type) {
    return (type == sequencer::StepType::Skip) ? sequencer::BarState::Skip : sequencer::BarState::Active;
}

static bool PersistIsEventStart(const sequencer::StepSlot &step, uint8_t canonical_pos) {
    if (canonical_pos >= kV2PositionsPerStep) return false;
    if (sequencer::LedgerSlotIsEmpty(step.note_ledger[canonical_pos])) return false;
    if (canonical_pos == 0u) return true;
    return sequencer::LedgerSlotIsEmpty(step.note_ledger[(uint8_t)(canonical_pos - 1u)]);
}

static uint8_t PersistPackedEventLengthRawGet(const sequencer::StepSlot &step, uint8_t canonical_pos) {
    if (canonical_pos >= kV2PositionsPerStep) return 0u;

    const uint16_t bit_index = (uint16_t)canonical_pos * 5u;
    const uint8_t byte_index = (uint8_t)(bit_index >> 3u);
    const uint8_t bit_offset = (uint8_t)(bit_index & 0x7u);
    uint32_t chunk = 0u;

    chunk |= (uint32_t)step.event_length_packed[byte_index];
    if ((uint8_t)(byte_index + 1u) < sequencer::kStepEventLengthPackedBytes) {
        chunk |= (uint32_t)step.event_length_packed[(uint8_t)(byte_index + 1u)] << 8u;
    }
    if ((uint8_t)(byte_index + 2u) < sequencer::kStepEventLengthPackedBytes) {
        chunk |= (uint32_t)step.event_length_packed[(uint8_t)(byte_index + 2u)] << 16u;
    }

    return (uint8_t)((chunk >> bit_offset) & 0x1Fu);
}

static void PersistPackedEventLengthRawSet(sequencer::StepSlot &step,
                                           uint8_t canonical_pos,
                                           uint8_t raw_len_minus_1) {
    if (canonical_pos >= kV2PositionsPerStep) return;

    raw_len_minus_1 &= 0x1Fu;
    const uint16_t bit_index = (uint16_t)canonical_pos * 5u;
    const uint8_t byte_index = (uint8_t)(bit_index >> 3u);
    const uint8_t bit_offset = (uint8_t)(bit_index & 0x7u);

    uint32_t chunk = 0u;
    chunk |= (uint32_t)step.event_length_packed[byte_index];
    if ((uint8_t)(byte_index + 1u) < sequencer::kStepEventLengthPackedBytes) {
        chunk |= (uint32_t)step.event_length_packed[(uint8_t)(byte_index + 1u)] << 8u;
    }
    if ((uint8_t)(byte_index + 2u) < sequencer::kStepEventLengthPackedBytes) {
        chunk |= (uint32_t)step.event_length_packed[(uint8_t)(byte_index + 2u)] << 16u;
    }

    const uint32_t mask = (uint32_t)0x1Fu << bit_offset;
    chunk = (chunk & ~mask) | ((uint32_t)raw_len_minus_1 << bit_offset);

    step.event_length_packed[byte_index] = (uint8_t)(chunk & 0xFFu);
    if ((uint8_t)(byte_index + 1u) < sequencer::kStepEventLengthPackedBytes) {
        step.event_length_packed[(uint8_t)(byte_index + 1u)] = (uint8_t)((chunk >> 8u) & 0xFFu);
    }
    if ((uint8_t)(byte_index + 2u) < sequencer::kStepEventLengthPackedBytes) {
        step.event_length_packed[(uint8_t)(byte_index + 2u)] = (uint8_t)((chunk >> 16u) & 0xFFu);
    }
}

static void UpdateLegacyStepTypeProjection(sequencer::StepSlot &slot) {
    if (slot.bar_state == sequencer::BarState::Skip) {
        slot.type = sequencer::StepType::Skip;
        return;
    }
    slot.type = (slot.note_mask != 0u) ? sequencer::StepType::Chord : sequencer::StepType::Empty;
}

static uint8_t StepsPerBarForHarness(const sequencer::TimeSig &time_sig, uint8_t step_division) {
    if (step_division == 0u) return 0u;
    const uint16_t units = (uint16_t)time_sig.numerator * 16u;
    const uint16_t denom = (uint16_t)time_sig.denominator * step_division;
    if (denom == 0u) return 0u;
    const uint16_t steps = (uint16_t)(units / denom);
    if (steps == 0u || steps > sequencer::kStepLedgerMax) return 0u;
    return (uint8_t)steps;
}

static uint8_t ClampLedgerLengthForHarness(uint8_t length) {
    if (length < 1u) return 1u;
    if (length > sequencer::kStepLedgerMax) return sequencer::kStepLedgerMax;
    return length;
}

static void NormalizePatternDivisionCadenceForHarness(sequencer::Pattern &pattern, const sequencer::TimeSig &time_sig) {
    if (StepsPerBarForHarness(time_sig, pattern.step_division) == 0u) {
        pattern.step_division = 4u;
    }
    const uint8_t fixed_len = StepsPerBarForHarness(time_sig, pattern.step_division);
    for (uint8_t i = 0u; i < kV2StepsPerPattern; ++i) {
        sequencer::StepSlot &slot = pattern.steps[i];
        slot.repeat_count = ClampLedgerLengthForHarness(fixed_len);
        slot.note_mask = DerivePitchMaskFromStepLedger(slot);
        UpdateLegacyStepTypeProjection(slot);
    }
}

template <typename WriterT>
static bool EncodeSongCoreInternal(const sequencer::Song &song,
                                   WriterT &w,
                                   uint32_t *out_payload_size,
                                   uint8_t format_id,
                                   bool include_bar_state_bitstream,
                                   bool include_event_lengths,
                                   uint8_t flags) {
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

    for (uint8_t p = 0u; p < kV2PatternCount; ++p) {
        const sequencer::Pattern &pat = song.patterns[p];
        uint32_t tempo_bits = 0u;
        memcpy(&tempo_bits, &pat.tempo_multiplier, sizeof(tempo_bits));
        if (!w.WriteU32(tempo_bits)) return false;

        uint8_t packed0 = (uint8_t)((pat.step_count & 0x0Fu) |
                                    ((EncodeStepDivision(pat.step_division) & 0x03u) << 4u) |
                                    (((uint8_t)pat.playback_mode & 0x03u) << 6u));
        uint8_t packed1 =
            (uint8_t)((pat.repeat_count & 0x1Fu) | (((uint8_t)pat.arp_mode & 0x07u) << 5u));
        uint8_t packed2 = (uint8_t)(((uint8_t)pat.arp_rate & 0x03u));

        if (!w.WriteU8(packed0) || !w.WriteU8(packed1) || !w.WriteU8(packed2)) return false;

        for (uint8_t s = 0u; s < kV2StepsPerPattern; ++s, ++step_index) {
            const sequencer::StepSlot &step = pat.steps[s];
            const uint8_t name_len = NameLength16(step.custom_chord_name);
            name_lengths[step_index] = name_len;

            if (!w.WriteBits((uint8_t)step.type & 0x03u, 2u)) return false;
            if (!w.WriteBits(EncodeDurationCode(step.duration_multiplier) & 0x03u, 2u)) return false;
            if (!w.WriteBits((step.velocity > 127u) ? 127u : step.velocity, 7u)) return false;
            if (!w.WriteBits((step.probability > 100u) ? 100u : step.probability, 7u)) return false;
            if (!w.WriteBits(name_len & 0x1Fu, 5u)) return false;
        }
    }

    if (include_bar_state_bitstream) {
        for (uint8_t p = 0u; p < kV2PatternCount; ++p) {
            const sequencer::Pattern &pat = song.patterns[p];
            for (uint8_t s = 0u; s < kV2StepsPerPattern; ++s) {
                const sequencer::StepSlot &step = pat.steps[s];
                const uint8_t bit = (step.bar_state == sequencer::BarState::Skip) ? 1u : 0u;
                if (!w.WriteBits(bit, 1u)) return false;
            }
        }
    }

    step_index = 0u;
    for (uint8_t p = 0u; p < kV2PatternCount; ++p) {
        const sequencer::Pattern &pat = song.patterns[p];
        for (uint8_t s = 0u; s < kV2StepsPerPattern; ++s, ++step_index) {
            const sequencer::StepSlot &step = pat.steps[s];
            const uint8_t name_len = name_lengths[step_index];
            for (uint8_t i = 0u; i < name_len; ++i) {
                if (!w.WriteBits(NameCharToCode(step.custom_chord_name[i]) & 0x3Fu, 6u)) return false;
            }
        }
    }

    for (uint8_t p = 0u; p < kV2PatternCount; ++p) {
        const sequencer::Pattern &pat = song.patterns[p];
        for (uint8_t s = 0u; s < kV2StepsPerPattern; ++s) {
            const sequencer::StepSlot &step = pat.steps[s];
            uint32_t occupancy = 0u;
            for (uint8_t pos = 0u; pos < kV2PositionsPerStep; ++pos) {
                if (!sequencer::LedgerSlotIsEmpty(step.note_ledger[pos])) occupancy |= (uint32_t)(1u << pos);
            }
            if (!w.WriteU32(occupancy)) return false;
        }
    }

    for (uint8_t p = 0u; p < kV2PatternCount; ++p) {
        const sequencer::Pattern &pat = song.patterns[p];
        for (uint8_t s = 0u; s < kV2StepsPerPattern; ++s) {
            const sequencer::StepSlot &step = pat.steps[s];
            for (uint8_t pos = 0u; pos < kV2PositionsPerStep; ++pos) {
                const sequencer::LedgerSlot &cell = step.note_ledger[pos];
                if (sequencer::LedgerSlotIsEmpty(cell)) continue;
                uint8_t notes[4] = {0u, 0u, 0u, 0u};
                const uint8_t count = ExtractValidSortedNotes(cell, notes);
                if (count == 0u) continue;
                if (count == 1u) {
                    if (!w.WriteBits(0u, 1u)) return false;
                    if (!w.WriteBits(notes[0], 7u)) return false;
                } else {
                    if (!w.WriteBits(1u, 1u)) return false;
                    uint8_t kind = 0u;
                    uint8_t rank_bits = 13u;
                    if (count == 2u) {
                        kind = 0u;
                        rank_bits = 13u;
                    } else if (count == 3u) {
                        kind = 1u;
                        rank_bits = 19u;
                    } else {
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

    if (include_event_lengths) {
        for (uint8_t p = 0u; p < kV2PatternCount; ++p) {
            const sequencer::Pattern &pat = song.patterns[p];
            for (uint8_t s = 0u; s < kV2StepsPerPattern; ++s) {
                const sequencer::StepSlot &step = pat.steps[s];
                for (uint8_t pos = 0u; pos < kV2PositionsPerStep; ++pos) {
                    if (!PersistIsEventStart(step, pos)) continue;
                    if (!w.WriteBits(PersistPackedEventLengthRawGet(step, pos), 5u)) return false;
                }
            }
        }
    }

    if (!w.WriteU8(song.chain.length)) return false;
    if (!w.WriteU8(song.chain.position)) return false;
    for (uint8_t i = 0u; i < kV2ChainCapacity; ++i) {
        if (!w.WriteBits(song.chain.pattern_indices[i] & 0x1Fu, 5u)) return false;
    }

    if (!w.Finalize()) return false;
    if (!w.ok()) return false;
    *out_payload_size = w.bytes_used();
    return true;
}

template <typename WriterT>
static bool EncodeSongV2Core(const sequencer::Song &song, WriterT &w, uint32_t *out_payload_size) {
    return EncodeSongCoreInternal(song,
                                  w,
                                  out_payload_size,
                                  kSongFormatIdV2,
                                  true,
                                  false,
                                  kV2FlagHasBarStateBitstream);
}

template <typename WriterT>
static bool EncodeSongV3Core(const sequencer::Song &song, WriterT &w, uint32_t *out_payload_size) {
    return EncodeSongCoreInternal(song,
                                  w,
                                  out_payload_size,
                                  kSongFormatIdV3,
                                  true,
                                  true,
                                  (uint8_t)(kV2FlagHasBarStateBitstream | kV3FlagHasEventLengths));
}

template <typename WriterT>
static bool EncodeSongV2CoreLegacyNoBarState(const sequencer::Song &song,
                                              WriterT &w,
                                              uint32_t *out_payload_size) {
    return EncodeSongCoreInternal(song, w, out_payload_size, kSongFormatIdV2, false, false, 0u);
}

static bool DecodeSongV2(const uint8_t *payload, uint32_t payload_size, sequencer::Song *out_song) {
    if (!payload || !out_song) return false;

    BitReader r(payload, payload_size);
    sequencer::Song &song = *out_song;
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

    if (!r.ReadU8(&format_id) || !r.ReadU8(&flags) || !r.ReadU16(&bpm) || !r.ReadU8(&key_root) ||
        !r.ReadU8(&key_scale) || !r.ReadU8(&time_num) || !r.ReadU8(&time_den) || !r.ReadU8(&swing) ||
        !r.ReadU8(&transpose_u8) || !r.ReadU8(&pattern_count) || !r.ReadU8(&steps_per_pattern) ||
        !r.ReadU8(&positions_per_step) || !r.ReadU8(&chain_capacity) || !r.ReadU16(&reserved)) {
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

    for (uint8_t p = 0u; p < kV2PatternCount; ++p) {
        sequencer::Pattern &pat = song.patterns[p];

        uint32_t tempo_bits = 0u;
        uint8_t packed0 = 0u;
        uint8_t packed1 = 0u;
        uint8_t packed2 = 0u;
        if (!r.ReadU32(&tempo_bits) || !r.ReadU8(&packed0) || !r.ReadU8(&packed1) || !r.ReadU8(&packed2))
            return false;

        memcpy(&pat.tempo_multiplier, &tempo_bits, sizeof(tempo_bits));
        pat.step_count = (uint8_t)(packed0 & 0x0Fu);
        pat.step_division = DecodeStepDivision((uint8_t)((packed0 >> 4u) & 0x03u));
        pat.playback_mode = (sequencer::PlaybackMode)((packed0 >> 6u) & 0x03u);
        pat.repeat_count = (uint8_t)(packed1 & 0x1Fu);
        pat.arp_mode = (sequencer::ArpMode)((packed1 >> 5u) & 0x07u);
        pat.arp_rate = (sequencer::ArpRate)(packed2 & 0x03u);

        for (uint8_t s = 0u; s < kV2StepsPerPattern; ++s, ++step_index) {
            sequencer::StepSlot &step = pat.steps[s];
            uint32_t type_bits = 0u;
            uint32_t duration_bits = 0u;
            uint32_t velocity_bits = 0u;
            uint32_t probability_bits = 0u;
            uint32_t name_len_bits = 0u;

            if (!r.ReadBits(2u, &type_bits) || !r.ReadBits(2u, &duration_bits) ||
                !r.ReadBits(7u, &velocity_bits) || !r.ReadBits(7u, &probability_bits) ||
                !r.ReadBits(5u, &name_len_bits)) {
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

            for (uint8_t pos = 0u; pos < kV2PositionsPerStep; ++pos) {
                sequencer::LedgerSlotClear(step.note_ledger[pos]);
            }
        }
    }

    if ((flags & kV2FlagHasBarStateBitstream) != 0u) {
        for (uint16_t i = 0u; i < kV2TotalBars; ++i) {
            uint32_t state_bit = 0u;
            if (!r.ReadBits(1u, &state_bit)) return false;
            const uint8_t p = (uint8_t)(i / kV2StepsPerPattern);
            const uint8_t s = (uint8_t)(i % kV2StepsPerPattern);
            song.patterns[p].steps[s].bar_state =
                (state_bit != 0u) ? sequencer::BarState::Skip : sequencer::BarState::Active;
        }
    }

    step_index = 0u;
    for (uint8_t p = 0u; p < kV2PatternCount; ++p) {
        sequencer::Pattern &pat = song.patterns[p];
        for (uint8_t s = 0u; s < kV2StepsPerPattern; ++s, ++step_index) {
            sequencer::StepSlot &step = pat.steps[s];
            const uint8_t name_len = name_lengths[step_index];
            for (uint8_t i = 0u; i < name_len; ++i) {
                uint32_t char_code = 0u;
                if (!r.ReadBits(6u, &char_code)) return false;
                step.custom_chord_name[i] = NameCodeToChar((uint8_t)char_code);
            }
            step.custom_chord_name[name_len] = '\0';
        }
    }

    uint32_t occupancy[kV2PatternCount * kV2StepsPerPattern] = {0u};
    for (uint16_t i = 0u; i < (uint16_t)(kV2PatternCount * kV2StepsPerPattern); ++i) {
        if (!r.ReadU32(&occupancy[i])) return false;
    }

    step_index = 0u;
    for (uint8_t p = 0u; p < kV2PatternCount; ++p) {
        sequencer::Pattern &pat = song.patterns[p];
        for (uint8_t s = 0u; s < kV2StepsPerPattern; ++s, ++step_index) {
            sequencer::StepSlot &step = pat.steps[s];
            const uint32_t occ = occupancy[step_index];
            for (uint8_t pos = 0u; pos < kV2PositionsPerStep; ++pos) {
                if ((occ & (uint32_t)(1u << pos)) == 0u) continue;

                uint32_t first = 0u;
                if (!r.ReadBits(1u, &first)) return false;

                if (first == 0u) {
                    uint32_t note = 0u;
                    if (!r.ReadBits(7u, &note)) return false;
                    if (!sequencer::LedgerSlotAdd(step.note_ledger[pos], (uint8_t)note)) return false;
                } else {
                    uint32_t kind = 0u;
                    if (!r.ReadBits(2u, &kind)) return false;

                    uint8_t count = 0u;
                    uint8_t rank_bits = 0u;
                    if (kind == 0u) {
                        count = 2u;
                        rank_bits = 13u;
                    } else if (kind == 1u) {
                        count = 3u;
                        rank_bits = 19u;
                    } else if (kind == 2u) {
                        count = 4u;
                        rank_bits = 24u;
                    } else {
                        return false;
                    }

                    uint32_t rank = 0u;
                    if (!r.ReadBits(rank_bits, &rank)) return false;

                    uint8_t notes[4] = {0u, 0u, 0u, 0u};
                    if (!UnrankCombination(count, rank, notes)) return false;
                    for (uint8_t i = 0u; i < count; ++i) {
                        if (!sequencer::LedgerSlotAdd(step.note_ledger[pos], notes[i])) return false;
                    }
                }
            }

            step.note_mask = DerivePitchMaskFromStepLedger(step);
        }
    }

    if (format_id == kSongFormatIdV3 && (flags & kV3FlagHasEventLengths) != 0u) {
        for (uint8_t p = 0u; p < kV2PatternCount; ++p) {
            sequencer::Pattern &pat = song.patterns[p];
            for (uint8_t s = 0u; s < kV2StepsPerPattern; ++s) {
                sequencer::StepSlot &step = pat.steps[s];
                for (uint8_t pos = 0u; pos < kV2PositionsPerStep; ++pos) {
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
    for (uint8_t i = 0u; i < kV2ChainCapacity; ++i) {
        uint32_t idx = 0u;
        if (!r.ReadBits(5u, &idx)) return false;
        song.chain.pattern_indices[i] = (uint8_t)idx;
    }

    return true;
}

static bool SongsEqual(const sequencer::Song &a, const sequencer::Song &b) {
    if (a.global_bpm != b.global_bpm) return false;
    if (a.key_root != b.key_root) return false;
    if (a.key_scale != b.key_scale) return false;
    if (a.time_sig.numerator != b.time_sig.numerator) return false;
    if (a.time_sig.denominator != b.time_sig.denominator) return false;
    if (a.swing != b.swing) return false;
    if (a.transpose != b.transpose) return false;
    if (a.chain.length != b.chain.length || a.chain.position != b.chain.position) return false;
    for (size_t i = 0; i < a.chain.pattern_indices.size(); ++i) {
        if (a.chain.pattern_indices[i] != b.chain.pattern_indices[i]) return false;
    }

    for (size_t p = 0; p < a.patterns.size(); ++p) {
        const auto &pa = a.patterns[p];
        const auto &pb = b.patterns[p];
        if (memcmp(&pa.tempo_multiplier, &pb.tempo_multiplier, sizeof(float)) != 0) return false;
        if (pa.step_count != pb.step_count || pa.step_division != pb.step_division || pa.playback_mode != pb.playback_mode ||
            pa.repeat_count != pb.repeat_count || pa.arp_mode != pb.arp_mode || pa.arp_rate != pb.arp_rate)
            return false;

        for (size_t s = 0; s < pa.steps.size(); ++s) {
            const auto &sa = pa.steps[s];
            const auto &sb = pb.steps[s];
            if (sa.type != sb.type || sa.duration_multiplier != sb.duration_multiplier || sa.repeat_count != sb.repeat_count ||
                sa.note_mask != sb.note_mask || sa.velocity != sb.velocity || sa.probability != sb.probability ||
                sa.bar_state != sb.bar_state)
                return false;
            if (memcmp(sa.custom_chord_name, sb.custom_chord_name, sizeof(sa.custom_chord_name)) != 0) return false;
            if (memcmp(sa.event_length_packed.data(),
                       sb.event_length_packed.data(),
                       sa.event_length_packed.size()) != 0)
                return false;
            for (size_t i = 0; i < sa.note_ledger.size(); ++i) {
                if (sa.note_ledger[i].notes != sb.note_ledger[i].notes) return false;
            }
        }
    }
    return true;
}

static bool SaveSongToFram(MockFram &fram, const sequencer::Song &song, uint32_t *out_payload_size = nullptr) {
    uint32_t payload_size = 0u;
    CountingChecksumBitWriter preflight;
    if (!EncodeSongV3Core(song, preflight, &payload_size)) return false;

    const uint16_t hdr_size = (uint16_t)sizeof(SongBlobHeader);
    if ((uint32_t)hdr_size + payload_size > H_FRAM_SONGDATA_SIZE) return false;

    SongBlobHeader hdr{};
    memcpy(hdr.magic, kSongMagic, sizeof(kSongMagic));
    hdr.version = kSongBlobVersion;
    hdr.payload_size = payload_size;
    hdr.checksum = preflight.checksum();

    const uint16_t payload_addr = (uint16_t)(H_FRAM_SONGDATA_ADDR + hdr_size);
    FramStreamBitWriter stream_writer(fram, payload_addr, payload_size);
    uint32_t streamed_size = 0u;
    if (!EncodeSongV3Core(song, stream_writer, &streamed_size)) return false;
    if (streamed_size != payload_size) return false;
    if (stream_writer.checksum() != hdr.checksum) return false;

    if (!fram.WriteAndVerify(H_FRAM_SONGDATA_ADDR, reinterpret_cast<const uint8_t *>(&hdr), hdr_size)) return false;

    if (out_payload_size) *out_payload_size = payload_size;
    return true;
}

static bool LoadSongFromFram(MockFram &fram, sequencer::Song *song) {
    if (!song) return false;
    SongBlobHeader hdr{};
    if (!fram.Read(H_FRAM_SONGDATA_ADDR, reinterpret_cast<uint8_t *>(&hdr), (uint16_t)sizeof(hdr))) return false;
    if (memcmp(hdr.magic, kSongMagic, sizeof(kSongMagic)) != 0) return false;
    if (hdr.version != 2u && hdr.version != 3u) return false;
    if ((uint32_t)sizeof(SongBlobHeader) + hdr.payload_size > H_FRAM_SONGDATA_SIZE) return false;

    std::vector<uint8_t> payload(hdr.payload_size);
    if (!fram.Read((uint16_t)(H_FRAM_SONGDATA_ADDR + (uint16_t)sizeof(SongBlobHeader)), payload.data(),
                   (uint16_t)hdr.payload_size))
        return false;

    uint32_t checksum = 2166136261u;
    for (uint8_t b : payload) checksum = SongChecksumUpdate(checksum, b);
    if (checksum != hdr.checksum) return false;

    return DecodeSongV2(payload.data(), hdr.payload_size, song);
}

static void FillSparseSong(sequencer::Song &song) {
    song.global_bpm = 137;
    song.key_root = sequencer::KeyRoot::Fs;
    song.key_scale = sequencer::KeyScale::Dorian;
    song.time_sig.numerator = 7;
    song.time_sig.denominator = 8;
    song.swing = 19;
    song.transpose = 3;

    auto &pat = song.patterns[3];
    pat.step_count = 12;
    pat.step_division = 8;
    pat.repeat_count = 4;
    pat.arp_mode = sequencer::ArpMode::UpDown;
    pat.arp_rate = sequencer::ArpRate::ThirtySecond;
    pat.steps[2].type = sequencer::StepType::Chord;
    pat.steps[2].duration_multiplier = 3;
    pat.steps[2].velocity = 88;
    pat.steps[2].probability = 67;
    strcpy(pat.steps[2].custom_chord_name, "SPARSE");
    sequencer::LedgerSlotAdd(pat.steps[2].note_ledger[0], 60);
    sequencer::LedgerSlotAdd(pat.steps[2].note_ledger[7], 64);
    pat.steps[2].note_mask = DerivePitchMaskFromStepLedger(pat.steps[2]);

    song.chain.length = 3;
    song.chain.position = 1;
    song.chain.pattern_indices[0] = 0;
    song.chain.pattern_indices[1] = 3;
    song.chain.pattern_indices[2] = 9;
}

static void FillOversizedSong(sequencer::Song &song) {
    song.global_bpm = 200;
    for (uint8_t p = 0; p < kV2PatternCount; ++p) {
        auto &pat = song.patterns[p];
        pat.step_count = 12;
        pat.step_division = 8;
        for (uint8_t s = 0; s < kV2StepsPerPattern; ++s) {
            auto &step = pat.steps[s];
            step.type = sequencer::StepType::Chord;
            step.duration_multiplier = 4;
            step.velocity = 127;
            step.probability = 100;
            strcpy(step.custom_chord_name, "XXXXXXXXXXXXXXXX");
            for (uint8_t pos = 0; pos < kV2PositionsPerStep; ++pos) {
                sequencer::LedgerSlotAdd(step.note_ledger[pos], 24);
                sequencer::LedgerSlotAdd(step.note_ledger[pos], 48);
                sequencer::LedgerSlotAdd(step.note_ledger[pos], 72);
                sequencer::LedgerSlotAdd(step.note_ledger[pos], 96);
            }
            step.note_mask = DerivePitchMaskFromStepLedger(step);
        }
    }
}

static bool SequenceEquals(const std::vector<uint8_t>& actual, const std::vector<uint8_t>& expected)
{
    if (actual.size() != expected.size()) return false;
    for (size_t i = 0; i < actual.size(); ++i)
    {
        if (actual[i] != expected[i]) return false;
    }
    return true;
}

static uint8_t HarnessCanonicalStrideForDivision(uint8_t grid_division)
{
    if (!(grid_division == 1u || grid_division == 2u || grid_division == 4u || grid_division == 8u)) return 1u;
    return (uint8_t)(8u / grid_division);
}

static uint8_t HarnessIsCanonicalEventStart(const sequencer::StepSlot &slot, uint8_t canonical_index)
{
    if (canonical_index >= sequencer::kStepLedgerMax) return 0u;
    return sequencer::LedgerSlotIsEmpty(slot.note_ledger[canonical_index]) ? 0u : 1u;
}

static uint8_t HarnessFindNextEventStartCanonical(const sequencer::StepSlot &slot,
                                                  uint8_t canonical_start,
                                                  uint8_t canonical_positions)
{
    if (canonical_positions > sequencer::kStepLedgerMax) canonical_positions = sequencer::kStepLedgerMax;
    for (uint8_t idx = (uint8_t)(canonical_start + 1u); idx < canonical_positions; ++idx)
    {
        if (HarnessIsCanonicalEventStart(slot, idx)) return idx;
    }
    return canonical_positions;
}

static uint8_t HarnessEventLengthCanonicalForStart(const sequencer::StepSlot &slot,
                                                   uint8_t canonical_start,
                                                   uint8_t canonical_positions)
{
    if (!HarnessIsCanonicalEventStart(slot, canonical_start)) return 0u;

    uint8_t max_len = 1u;
    const uint8_t next_start =
        HarnessFindNextEventStartCanonical(slot, canonical_start, canonical_positions);
    if (next_start > canonical_start)
    {
        max_len = (uint8_t)(next_start - canonical_start);
    }

    uint8_t len = (uint8_t)(PersistPackedEventLengthRawGet(slot, canonical_start) + 1u);
    if (len < 1u) len = 1u;
    if (len > max_len) len = max_len;
    return len;
}

static bool HarnessStepSlotCompatibleWithStride(const sequencer::StepSlot &slot, uint8_t stride)
{
    if (stride == 0u) return false;
    for (uint8_t start = 0u; start < sequencer::kStepLedgerMax; ++start)
    {
        if (!HarnessIsCanonicalEventStart(slot, start)) continue;

        const uint8_t len =
            HarnessEventLengthCanonicalForStart(slot, start, sequencer::kStepLedgerMax);
        if (len == 0u) return false;
        if ((start % stride) != 0u) return false;
        if ((len % stride) != 0u) return false;
    }
    return true;
}

static bool HarnessPatternCompatibleWithCoarserDivision(const sequencer::Pattern &pattern,
                                                        uint8_t proposed_division)
{
    const uint8_t stride = HarnessCanonicalStrideForDivision(proposed_division);
    for (uint8_t bar = 0u; bar < kV2StepsPerPattern; ++bar)
    {
        if (!HarnessStepSlotCompatibleWithStride(pattern.steps[bar], stride)) return false;
    }
    return true;
}

static bool HarnessApplyPatternDivisionNonDestructive(sequencer::Song &song,
                                                       uint8_t pattern_index,
                                                       uint8_t proposed_division)
{
    if (pattern_index >= kV2PatternCount) return false;
    if (!(proposed_division == 1u || proposed_division == 2u || proposed_division == 4u || proposed_division == 8u))
        return false;

    sequencer::Pattern &pattern = song.patterns[pattern_index];
    const uint8_t current_stride = HarnessCanonicalStrideForDivision(pattern.step_division);
    const uint8_t proposed_stride = HarnessCanonicalStrideForDivision(proposed_division);

    if (proposed_stride > current_stride)
    {
        if (!HarnessPatternCompatibleWithCoarserDivision(pattern, proposed_division))
        {
            return false;
        }
    }

    pattern.step_division = proposed_division;
    return true;
}

static std::vector<uint8_t> CaptureArpSequence(sequencer::ArpEngine& arp, uint8_t total_notes)
{
    std::vector<uint8_t> out;
    if (!arp.HasNotes() || total_notes == 0u)
    {
        return out;
    }

    out.push_back(arp.CurrentNote());
    for (uint8_t i = 1u; i < total_notes; ++i)
    {
        out.push_back(arp.Advance());
    }
    return out;
}

static uint32_t CountArpOpportunities(uint8_t event_length_steps,
                                      uint8_t step_division,
                                      sequencer::ArpRate rate)
{
    if (event_length_steps == 0u || step_division == 0u) return 0u;

    const uint32_t ticks_per_step = 96u / step_division;
    const uint32_t event_ticks = ticks_per_step * (uint32_t)event_length_steps;
    const uint32_t arp_ticks = sequencer::ArpEngine::TicksPerEvent(rate);
    if (arp_ticks == 0u || event_ticks == 0u) return 0u;

    uint32_t opportunities = 1u; // immediate note at event start
    for (uint32_t t = arp_ticks; t < event_ticks; t += arp_ticks)
    {
        ++opportunities;
    }
    return opportunities;
}

static void SimulateArpCvFrame(uint8_t midi_note,
                               const float zero_volts[4],
                               float out_volts[4],
                               uint8_t* out_gate_mask)
{
    for (uint8_t i = 0u; i < 4u; ++i)
    {
        out_volts[i] = zero_volts[i];
    }

    if (sequencer::IsMidiNoteValid(midi_note))
    {
        out_volts[0] = PitchVoltsOptionB(midi_note);
        if (out_gate_mask) *out_gate_mask = 0x01u;
    }
    else
    {
        if (out_gate_mask) *out_gate_mask = 0u;
    }
}

static bool RunArpOwnershipIsolationSequence(SequencerDevice& dev,
                                             sequencer::ArpMode expect_mode,
                                             sequencer::ArpRate expect_rate,
                                             std::string* details)
{
    auto check = [&](const char* tag) -> bool
    {
        if (dev.GetCurrentArpMode() != expect_mode)
        {
            if (details) *details += std::string(tag) + ": arp_mode changed; ";
            return false;
        }
        if (dev.GetCurrentArpRate() != expect_rate)
        {
            if (details) *details += std::string(tag) + ": arp_rate changed; ";
            return false;
        }
        return true;
    };

    /* 2) Edit chord content in another Bar. */
    dev.SetStepChordParams(3u, 7u, 2u, 4u, 2u, 1u);
    if (!check("SetStepChordParams")) return false;

    /* 3) Clear notes in another Bar. */
    dev.SetStepCustomNoteMask(4u, 0u);
    if (!check("SetStepCustomNoteMask(clear)")) return false;

    /* 4) Place direct-M1 notes/chords via ledger placement path. */
    {
        sequencer::LedgerSlot slot{};
        sequencer::LedgerSlotClear(slot);
        (void)sequencer::LedgerSlotAdd(slot, 60u);
        (void)sequencer::LedgerSlotAdd(slot, 64u);
        (void)sequencer::LedgerSlotAdd(slot, 67u);
        dev.SetStepLedgerSlot(5u, 0u, slot);
    }
    if (!check("SetStepLedgerSlot(direct-M1 placement)")) return false;

    /* 5) Change event length. */
    dev.SetStepEventLengthGrid(5u, 0u, 2u);
    if (!check("SetStepEventLengthGrid")) return false;

    /* 6) Toggle Bar Skip. */
    dev.SetStepBarState(6u, sequencer::BarState::Skip);
    if (!check("SetStepBarState(Skip)")) return false;
    dev.SetStepBarState(6u, sequencer::BarState::Active);
    if (!check("SetStepBarState(Active)")) return false;

    return true;
}

struct TestResult {
    std::string name;
    bool pass;
    uint32_t encoded_size;
    std::string details;
};

} // namespace

int main() {
    std::vector<TestResult> results;

    auto add_roundtrip = [&](const char *name, const sequencer::Song &src) {
        std::array<uint8_t, H_FRAM_SONGDATA_SIZE> payload{};
        uint32_t size = 0u;
        bool ok = false;
        sequencer::Song decoded{};
        BitWriter bw(payload.data(), (uint32_t)payload.size());
        if (EncodeSongV3Core(src, bw, &size)) {
            ok = DecodeSongV2(payload.data(), size, &decoded) && SongsEqual(src, decoded);
        }
        results.push_back({name, ok, size, ok ? "" : "round-trip mismatch"});
    };

    // 1) empty Song round-trip
    add_roundtrip("empty Song round-trip", sequencer::Song{});

    // 1a) all Bars Active persisted explicitly
    {
        sequencer::Song active_song{};
        bool all_active = true;
        for (uint8_t p = 0; p < kV2PatternCount; ++p) {
            for (uint8_t s = 0; s < kV2StepsPerPattern; ++s) {
                if (active_song.patterns[p].steps[s].bar_state != sequencer::BarState::Active) {
                    all_active = false;
                }
            }
        }
        std::array<uint8_t, H_FRAM_SONGDATA_SIZE> payload{};
        uint32_t size = 0u;
        bool ok = false;
        sequencer::Song decoded{};
        BitWriter bw(payload.data(), (uint32_t)payload.size());
        if (all_active && EncodeSongV3Core(active_song, bw, &size) && DecodeSongV2(payload.data(), size, &decoded)) {
            ok = true;
            for (uint8_t p = 0; p < kV2PatternCount && ok; ++p) {
                for (uint8_t s = 0; s < kV2StepsPerPattern; ++s) {
                    if (decoded.patterns[p].steps[s].bar_state != sequencer::BarState::Active) {
                        ok = false;
                        break;
                    }
                }
            }
        }
        results.push_back({"all Bars Active remain Active", ok, size, ok ? "" : "bar_state mismatch"});
    }

    // 1b) BAR01 Skip survives round-trip
    {
        sequencer::Song s{};
        s.patterns[0].steps[0].bar_state = sequencer::BarState::Skip;
        UpdateLegacyStepTypeProjection(s.patterns[0].steps[0]);
        std::array<uint8_t, H_FRAM_SONGDATA_SIZE> payload{};
        uint32_t size = 0u;
        bool ok = false;
        sequencer::Song out{};
        BitWriter bw(payload.data(), (uint32_t)payload.size());
        if (EncodeSongV3Core(s, bw, &size) && DecodeSongV2(payload.data(), size, &out)) {
            ok = (out.patterns[0].steps[0].bar_state == sequencer::BarState::Skip);
        }
        results.push_back({"BAR01 Skip survives round-trip", ok, size, ok ? "" : "BAR01 not skipped"});
    }

    // 1c) BAR12 Skip survives round-trip
    {
        sequencer::Song s{};
        s.patterns[0].steps[11].bar_state = sequencer::BarState::Skip;
        UpdateLegacyStepTypeProjection(s.patterns[0].steps[11]);
        std::array<uint8_t, H_FRAM_SONGDATA_SIZE> payload{};
        uint32_t size = 0u;
        bool ok = false;
        sequencer::Song out{};
        BitWriter bw(payload.data(), (uint32_t)payload.size());
        if (EncodeSongV3Core(s, bw, &size) && DecodeSongV2(payload.data(), size, &out)) {
            ok = (out.patterns[0].steps[11].bar_state == sequencer::BarState::Skip);
        }
        results.push_back({"BAR12 Skip survives round-trip", ok, size, ok ? "" : "BAR12 not skipped"});
    }

    // 1d) mixed Active/Skip Bars in one Pattern
    {
        sequencer::Song s{};
        const uint8_t skips[] = {1u, 3u, 8u, 10u};
        for (uint8_t idx : skips) {
            s.patterns[0].steps[idx].bar_state = sequencer::BarState::Skip;
            UpdateLegacyStepTypeProjection(s.patterns[0].steps[idx]);
        }
        std::array<uint8_t, H_FRAM_SONGDATA_SIZE> payload{};
        uint32_t size = 0u;
        bool ok = false;
        sequencer::Song out{};
        BitWriter bw(payload.data(), (uint32_t)payload.size());
        if (EncodeSongV3Core(s, bw, &size) && DecodeSongV2(payload.data(), size, &out)) {
            ok = true;
            for (uint8_t i = 0u; i < kV2StepsPerPattern; ++i) {
                const bool expect_skip = (i == 1u || i == 3u || i == 8u || i == 10u);
                const bool got_skip = (out.patterns[0].steps[i].bar_state == sequencer::BarState::Skip);
                if (expect_skip != got_skip) {
                    ok = false;
                    break;
                }
            }
        }
        results.push_back({"mixed Active/Skip Bars in one Pattern", ok, size, ok ? "" : "mixed skip mismatch"});
    }

    // 1e) Skip states across several Patterns
    {
        sequencer::Song s{};
        s.patterns[0].steps[0].bar_state = sequencer::BarState::Skip;
        s.patterns[7].steps[6].bar_state = sequencer::BarState::Skip;
        s.patterns[31].steps[11].bar_state = sequencer::BarState::Skip;
        UpdateLegacyStepTypeProjection(s.patterns[0].steps[0]);
        UpdateLegacyStepTypeProjection(s.patterns[7].steps[6]);
        UpdateLegacyStepTypeProjection(s.patterns[31].steps[11]);
        std::array<uint8_t, H_FRAM_SONGDATA_SIZE> payload{};
        uint32_t size = 0u;
        bool ok = false;
        sequencer::Song out{};
        BitWriter bw(payload.data(), (uint32_t)payload.size());
        if (EncodeSongV3Core(s, bw, &size) && DecodeSongV2(payload.data(), size, &out)) {
            ok = (out.patterns[0].steps[0].bar_state == sequencer::BarState::Skip) &&
                 (out.patterns[7].steps[6].bar_state == sequencer::BarState::Skip) &&
                 (out.patterns[31].steps[11].bar_state == sequencer::BarState::Skip);
        }
        results.push_back({"Skip states across several Patterns", ok, size, ok ? "" : "multi-pattern skip mismatch"});
    }

    // 1f) empty Active Bar remains Active
    {
        sequencer::Song s{};
        s.patterns[2].steps[5].bar_state = sequencer::BarState::Active;
        sequencer::LedgerSlotClear(s.patterns[2].steps[5].note_ledger[0]);
        s.patterns[2].steps[5].note_mask = DerivePitchMaskFromStepLedger(s.patterns[2].steps[5]);
        UpdateLegacyStepTypeProjection(s.patterns[2].steps[5]);
        std::array<uint8_t, H_FRAM_SONGDATA_SIZE> payload{};
        uint32_t size = 0u;
        bool ok = false;
        sequencer::Song out{};
        BitWriter bw(payload.data(), (uint32_t)payload.size());
        if (EncodeSongV3Core(s, bw, &size) && DecodeSongV2(payload.data(), size, &out)) {
            ok = (out.patterns[2].steps[5].bar_state == sequencer::BarState::Active);
        }
        results.push_back({"empty Active Bar remains Active", ok, size, ok ? "" : "empty active became skipped"});
    }

    // 1g) empty skipped Bar remains Skip
    {
        sequencer::Song s{};
        s.patterns[4].steps[9].bar_state = sequencer::BarState::Skip;
        s.patterns[4].steps[9].note_mask = 0u;
        UpdateLegacyStepTypeProjection(s.patterns[4].steps[9]);
        std::array<uint8_t, H_FRAM_SONGDATA_SIZE> payload{};
        uint32_t size = 0u;
        bool ok = false;
        sequencer::Song out{};
        BitWriter bw(payload.data(), (uint32_t)payload.size());
        if (EncodeSongV3Core(s, bw, &size) && DecodeSongV2(payload.data(), size, &out)) {
            ok = (out.patterns[4].steps[9].bar_state == sequencer::BarState::Skip);
        }
        results.push_back({"empty skipped Bar remains Skip", ok, size, ok ? "" : "empty skip lost"});
    }

    // 1h) Skip survives decode followed by timing normalization
    {
        sequencer::Song s{};
        s.time_sig.numerator = 7u;
        s.time_sig.denominator = 8u;
        s.patterns[1].step_division = 8u;
        s.patterns[1].steps[2].bar_state = sequencer::BarState::Skip;
        UpdateLegacyStepTypeProjection(s.patterns[1].steps[2]);

        std::array<uint8_t, H_FRAM_SONGDATA_SIZE> payload{};
        uint32_t size = 0u;
        bool ok = false;
        sequencer::Song out{};
        BitWriter bw(payload.data(), (uint32_t)payload.size());
        if (EncodeSongV3Core(s, bw, &size) && DecodeSongV2(payload.data(), size, &out)) {
            out.time_sig.numerator = 4u;
            out.time_sig.denominator = 4u;
            out.patterns[1].step_division = 4u;
            NormalizePatternDivisionCadenceForHarness(out.patterns[1], out.time_sig);
            ok = (out.patterns[1].steps[2].bar_state == sequencer::BarState::Skip);
        }
        results.push_back({"Skip survives encode/decode + timing normalization", ok, size, ok ? "" : "skip lost after normalization"});
    }

    // 1i) old V2 payload without BarState flag maps legacy StepType::Skip
    {
        sequencer::Song s{};
        s.patterns[0].steps[4].type = sequencer::StepType::Skip;
        s.patterns[0].steps[4].bar_state = sequencer::BarState::Active;

        std::array<uint8_t, H_FRAM_SONGDATA_SIZE> payload{};
        uint32_t size = 0u;
        bool ok = false;
        sequencer::Song out{};
        BitWriter bw(payload.data(), (uint32_t)payload.size());
        if (EncodeSongV2CoreLegacyNoBarState(s, bw, &size) && DecodeSongV2(payload.data(), size, &out)) {
            ok = (out.patterns[0].steps[4].bar_state == sequencer::BarState::Skip);
        }
        results.push_back({"legacy V2 no-bar-state flag maps StepType::Skip", ok, size, ok ? "" : "legacy skip mapping failed"});
    }

    // 1j) V3 persists canonical event lengths for event starts
    {
        sequencer::Song s{};
        sequencer::StepSlot &step = s.patterns[0].steps[0];
        step.type = sequencer::StepType::Chord;
        sequencer::LedgerSlotAdd(step.note_ledger[0], 60u);
        sequencer::LedgerSlotAdd(step.note_ledger[1], 60u);
        sequencer::LedgerSlotAdd(step.note_ledger[2], 60u);
        sequencer::LedgerSlotAdd(step.note_ledger[5], 67u);
        step.note_mask = DerivePitchMaskFromStepLedger(step);
        PersistPackedEventLengthRawSet(step, 0u, 2u);  // length 3
        PersistPackedEventLengthRawSet(step, 5u, 7u);  // length 8

        std::array<uint8_t, H_FRAM_SONGDATA_SIZE> payload{};
        uint32_t size = 0u;
        bool ok = false;
        sequencer::Song out{};
        BitWriter bw(payload.data(), (uint32_t)payload.size());
        if (EncodeSongV3Core(s, bw, &size) && DecodeSongV2(payload.data(), size, &out)) {
            const sequencer::StepSlot &out_step = out.patterns[0].steps[0];
            ok = PersistPackedEventLengthRawGet(out_step, 0u) == 2u &&
                 PersistPackedEventLengthRawGet(out_step, 5u) == 7u;
        }
        results.push_back({"V3 event-start lengths persist", ok, size, ok ? "" : "event length mismatch"});
    }

    // 1k) legacy V2 payload defaults lengths to 1x (raw 0)
    {
        sequencer::Song s{};
        sequencer::StepSlot &step = s.patterns[0].steps[0];
        step.type = sequencer::StepType::Chord;
        sequencer::LedgerSlotAdd(step.note_ledger[0], 60u);
        step.note_mask = DerivePitchMaskFromStepLedger(step);

        std::array<uint8_t, H_FRAM_SONGDATA_SIZE> payload{};
        uint32_t size = 0u;
        bool ok = false;
        sequencer::Song out{};
        BitWriter bw(payload.data(), (uint32_t)payload.size());
        if (EncodeSongV2Core(s, bw, &size) && DecodeSongV2(payload.data(), size, &out)) {
            ok = (PersistPackedEventLengthRawGet(out.patterns[0].steps[0], 0u) == 0u);
        }
        results.push_back({"legacy V2 defaults event length to 1x", ok, size, ok ? "" : "legacy default length mismatch"});
    }

    // 2) sparse Song round-trip
    sequencer::Song sparse{};
    FillSparseSong(sparse);
    add_roundtrip("sparse Song round-trip", sparse);

    // 3) one-note ledger cell round-trip
    sequencer::Song one_note{};
    one_note.patterns[0].steps[0].type = sequencer::StepType::Chord;
    sequencer::LedgerSlotAdd(one_note.patterns[0].steps[0].note_ledger[5], 60);
    one_note.patterns[0].steps[0].note_mask = DerivePitchMaskFromStepLedger(one_note.patterns[0].steps[0]);
    add_roundtrip("one-note ledger cell round-trip", one_note);

    // 4) four-note ledger cell round-trip
    sequencer::Song four_note{};
    four_note.patterns[0].steps[0].type = sequencer::StepType::Chord;
    auto &slot = four_note.patterns[0].steps[0].note_ledger[4];
    sequencer::LedgerSlotAdd(slot, 36);
    sequencer::LedgerSlotAdd(slot, 48);
    sequencer::LedgerSlotAdd(slot, 60);
    sequencer::LedgerSlotAdd(slot, 72);
    four_note.patterns[0].steps[0].note_mask = DerivePitchMaskFromStepLedger(four_note.patterns[0].steps[0]);
    add_roundtrip("four-note ledger cell round-trip", four_note);

    // 5) C1 and C4 preserved simultaneously
    sequencer::Song oct_song{};
    oct_song.patterns[0].steps[1].type = sequencer::StepType::Chord;
    auto &oct = oct_song.patterns[0].steps[1].note_ledger[0];
    sequencer::LedgerSlotAdd(oct, 24); // C1
    sequencer::LedgerSlotAdd(oct, 60); // C4
    oct_song.patterns[0].steps[1].note_mask = DerivePitchMaskFromStepLedger(oct_song.patterns[0].steps[1]);
    add_roundtrip("C1 and C4 preserved simultaneously", oct_song);

    // 6) +12 transpose preserves absolute octave in ledger
    sequencer::Song trans_song{};
    trans_song.transpose = 12;
    trans_song.patterns[0].steps[2].type = sequencer::StepType::Chord;
    sequencer::LedgerSlotAdd(trans_song.patterns[0].steps[2].note_ledger[0], 60);
    trans_song.patterns[0].steps[2].note_mask = DerivePitchMaskFromStepLedger(trans_song.patterns[0].steps[2]);
    add_roundtrip("+12 transpose preserves absolute octave", trans_song);

    // 7) division mappings
    bool div_ok = true;
    std::string div_details;
    for (uint8_t div : {1u, 2u, 4u, 8u}) {
        sequencer::Song s{};
        s.patterns[0].step_division = div;
        std::array<uint8_t, H_FRAM_SONGDATA_SIZE> payload{};
        uint32_t size = 0u;
        BitWriter bw(payload.data(), (uint32_t)payload.size());
        sequencer::Song out{};
        if (!(EncodeSongV3Core(s, bw, &size) && DecodeSongV2(payload.data(), size, &out) &&
              out.patterns[0].step_division == div)) {
            div_ok = false;
            div_details += "div " + std::to_string(div) + " failed; ";
        }
    }
    results.push_back({"1/4,1/8,1/16,1/32 mappings", div_ok, 0u, div_details});

    // 7b) 1/32 -> 1/16 compatible event
    {
        sequencer::Song s{};
        s.patterns[0].step_division = 8u;
        sequencer::StepSlot &slot = s.patterns[0].steps[0];
        slot.type = sequencer::StepType::Chord;
        sequencer::LedgerSlotAdd(slot.note_ledger[2], 60u);
        slot.note_mask = DerivePitchMaskFromStepLedger(slot);
        PersistPackedEventLengthRawSet(slot, 2u, 1u); // len=2

        const bool ok = HarnessApplyPatternDivisionNonDestructive(s, 0u, 4u) &&
                        s.patterns[0].step_division == 4u;
        results.push_back({"1/32->1/16 compatible event", ok, 0u, ok ? "" : "unexpected reject"});
    }

    // 7c) 1/32 -> 1/16 incompatible start
    {
        sequencer::Song s{};
        s.patterns[0].step_division = 8u;
        sequencer::StepSlot &slot = s.patterns[0].steps[0];
        slot.type = sequencer::StepType::Chord;
        sequencer::LedgerSlotAdd(slot.note_ledger[1], 60u);
        slot.note_mask = DerivePitchMaskFromStepLedger(slot);
        PersistPackedEventLengthRawSet(slot, 1u, 1u); // len=2

        const sequencer::Song before = s;
        const bool applied = HarnessApplyPatternDivisionNonDestructive(s, 0u, 4u);
        const bool unchanged = memcmp(&before, &s, sizeof(sequencer::Song)) == 0;
        const bool ok = (!applied) && unchanged && (s.patterns[0].step_division == 8u);
        results.push_back({"1/32->1/16 incompatible start rejected", ok, 0u,
                           ok ? "" : "reject/unchanged contract failed"});
    }

    // 7d) 1/32 -> 1/16 incompatible length
    {
        sequencer::Song s{};
        s.patterns[0].step_division = 8u;
        sequencer::StepSlot &slot = s.patterns[0].steps[0];
        slot.type = sequencer::StepType::Chord;
        sequencer::LedgerSlotAdd(slot.note_ledger[2], 60u);
        slot.note_mask = DerivePitchMaskFromStepLedger(slot);
        PersistPackedEventLengthRawSet(slot, 2u, 2u); // len=3

        const sequencer::Song before = s;
        const bool applied = HarnessApplyPatternDivisionNonDestructive(s, 0u, 4u);
        const bool unchanged = memcmp(&before, &s, sizeof(sequencer::Song)) == 0;
        const bool ok = (!applied) && unchanged && (s.patterns[0].step_division == 8u);
        results.push_back({"1/32->1/16 incompatible length rejected", ok, 0u,
                           ok ? "" : "reject/unchanged contract failed"});
    }

    // 7e) 1/16 -> 1/8 compatible event
    {
        sequencer::Song s{};
        s.patterns[0].step_division = 4u;
        sequencer::StepSlot &slot = s.patterns[0].steps[0];
        slot.type = sequencer::StepType::Chord;
        sequencer::LedgerSlotAdd(slot.note_ledger[4], 64u);
        slot.note_mask = DerivePitchMaskFromStepLedger(slot);
        PersistPackedEventLengthRawSet(slot, 4u, 3u); // len=4

        const bool ok = HarnessApplyPatternDivisionNonDestructive(s, 0u, 2u) &&
                        s.patterns[0].step_division == 2u;
        results.push_back({"1/16->1/8 compatible event", ok, 0u, ok ? "" : "unexpected reject"});
    }

    // 7f) 1/16 -> 1/8 incompatible event
    {
        sequencer::Song s{};
        s.patterns[0].step_division = 4u;
        sequencer::StepSlot &slot = s.patterns[0].steps[0];
        slot.type = sequencer::StepType::Chord;
        sequencer::LedgerSlotAdd(slot.note_ledger[2], 64u);
        slot.note_mask = DerivePitchMaskFromStepLedger(slot);
        PersistPackedEventLengthRawSet(slot, 2u, 1u); // len=2

        const sequencer::Song before = s;
        const bool applied = HarnessApplyPatternDivisionNonDestructive(s, 0u, 2u);
        const bool unchanged = memcmp(&before, &s, sizeof(sequencer::Song)) == 0;
        const bool ok = (!applied) && unchanged && (s.patterns[0].step_division == 4u);
        results.push_back({"1/16->1/8 incompatible event rejected", ok, 0u,
                           ok ? "" : "reject/unchanged contract failed"});
    }

    // 7g) finer-grid transition preserves canonical start/length exactly
    {
        sequencer::Song s{};
        s.patterns[0].step_division = 4u;
        sequencer::StepSlot &slot = s.patterns[0].steps[0];
        slot.type = sequencer::StepType::Chord;
        sequencer::LedgerSlotAdd(slot.note_ledger[6], 67u);
        slot.note_mask = DerivePitchMaskFromStepLedger(slot);
        PersistPackedEventLengthRawSet(slot, 6u, 5u); // len=6

        const uint8_t start_before = 6u;
        const uint8_t len_before = HarnessEventLengthCanonicalForStart(slot, start_before, sequencer::kStepLedgerMax);
        const uint8_t raw_before = PersistPackedEventLengthRawGet(slot, start_before);

        const bool applied = HarnessApplyPatternDivisionNonDestructive(s, 0u, 8u);
        const sequencer::StepSlot &after = s.patterns[0].steps[0];
        const uint8_t len_after = HarnessEventLengthCanonicalForStart(after, start_before, sequencer::kStepLedgerMax);
        const uint8_t raw_after = PersistPackedEventLengthRawGet(after, start_before);

        const bool ok = applied && (s.patterns[0].step_division == 8u) &&
                        HarnessIsCanonicalEventStart(after, start_before) &&
                        (len_before == len_after) && (raw_before == raw_after);
        results.push_back({"finer-grid preserves canonical start/length", ok, 0u,
                           ok ? "" : "canonical event changed on finer grid"});
    }

    // 7a) payload growth from V2 -> V3 for same Song
    {
        sequencer::Song s{};
        sequencer::StepSlot &step = s.patterns[0].steps[0];
        step.type = sequencer::StepType::Chord;
        sequencer::LedgerSlotAdd(step.note_ledger[0], 60u);
        sequencer::LedgerSlotAdd(step.note_ledger[1], 60u);
        sequencer::LedgerSlotAdd(step.note_ledger[4], 64u);
        step.note_mask = DerivePitchMaskFromStepLedger(step);
        PersistPackedEventLengthRawSet(step, 0u, 3u);
        PersistPackedEventLengthRawSet(step, 4u, 1u);

        std::array<uint8_t, H_FRAM_SONGDATA_SIZE> payload{};
        uint32_t size_v2 = 0u;
        uint32_t size_v3 = 0u;
        BitWriter bw_v2(payload.data(), (uint32_t)payload.size());
        BitWriter bw_v3(payload.data(), (uint32_t)payload.size());

        const bool ok = EncodeSongV2Core(s, bw_v2, &size_v2) && EncodeSongV3Core(s, bw_v3, &size_v3) &&
                        size_v3 >= size_v2;
        const std::string details = "v2=" + std::to_string(size_v2) + " v3=" + std::to_string(size_v3) +
                                    " delta=" + std::to_string((int32_t)size_v3 - (int32_t)size_v2);
        results.push_back({"payload growth V2->V3", ok, size_v3, ok ? details : ("size compare failed; " + details)});
    }

    // 8) streamed V3 output decodes identically
    {
        MockFram fram;
        uint32_t size = 0u;
        sequencer::Song src{};
        FillSparseSong(src);
        bool ok = SaveSongToFram(fram, src, &size);
        sequencer::Song loaded{};
        ok = ok && LoadSongFromFram(fram, &loaded) && SongsEqual(src, loaded);
        results.push_back({"streamed V3 output decodes identically", ok, size, ok ? "" : "streamed save/load mismatch"});
    }

    // 9) oversized song rejected before any FRAM write
    {
        MockFram fram;
        sequencer::Song big{};
        FillOversizedSong(big);
        uint32_t size = 0u;
        bool ok = !SaveSongToFram(fram, big, &size) && fram.writes().empty();
        results.push_back({"oversized Song rejected before any FRAM write", ok, size,
                           ok ? "" : "unexpected write occurred"});
    }

    // 10) save path writes payload then valid header only after completion
    {
        MockFram fram;
        sequencer::Song src{};
        FillSparseSong(src);
        uint32_t size = 0u;
        bool ok = SaveSongToFram(fram, src, &size);
        bool order_ok = false;
        bool hdr_ok = false;
        if (ok && !fram.writes().empty()) {
            const auto &w = fram.writes();
            const WriteOp last = w.back();
            order_ok = (last.addr == H_FRAM_SONGDATA_ADDR && last.len == sizeof(SongBlobHeader));
            SongBlobHeader hdr{};
            hdr_ok = fram.Read(H_FRAM_SONGDATA_ADDR, reinterpret_cast<uint8_t *>(&hdr), sizeof(hdr)) &&
                     memcmp(hdr.magic, kSongMagic, sizeof(kSongMagic)) == 0 && hdr.version == kSongBlobVersion &&
                     hdr.payload_size == size;
        }
        results.push_back({"save writes payload then valid header after completion", ok && order_ok && hdr_ok, size,
                           (ok && order_ok && hdr_ok) ? "" : "write order/header invalid"});

        // negative: failed payload write must not commit valid header
        MockFram fail_fram;
        fail_fram.set_fail_after_payload_bytes(10);
        bool save_ok = SaveSongToFram(fail_fram, src, nullptr);
        SongBlobHeader hdr{};
        bool has_valid_header = fail_fram.Read(H_FRAM_SONGDATA_ADDR, reinterpret_cast<uint8_t *>(&hdr), sizeof(hdr)) &&
                                memcmp(hdr.magic, kSongMagic, sizeof(kSongMagic)) == 0 &&
                                hdr.version == kSongBlobVersion;
        results.push_back({"save failure does not commit valid header", (!save_ok) && (!has_valid_header), 0u,
                           ((!save_ok) && (!has_valid_header)) ? "" : "header committed on failed payload"});
    }

    // 11) load rejects bad checksum/version/length safely
    {
        MockFram fram;
        sequencer::Song src{};
        FillSparseSong(src);
        bool ok_save = SaveSongToFram(fram, src, nullptr);
        bool ok = ok_save;

        // bad checksum
        SongBlobHeader hdr{};
        fram.Read(H_FRAM_SONGDATA_ADDR, reinterpret_cast<uint8_t *>(&hdr), sizeof(hdr));
        hdr.checksum ^= 0x1u;
        fram.Write(H_FRAM_SONGDATA_ADDR, reinterpret_cast<const uint8_t *>(&hdr), sizeof(hdr));
        sequencer::Song out{};
        ok = ok && (!LoadSongFromFram(fram, &out));

        // restore valid then bad version
        SaveSongToFram(fram, src, nullptr);
        fram.Read(H_FRAM_SONGDATA_ADDR, reinterpret_cast<uint8_t *>(&hdr), sizeof(hdr));
        hdr.version = 99u;
        fram.Write(H_FRAM_SONGDATA_ADDR, reinterpret_cast<const uint8_t *>(&hdr), sizeof(hdr));
        ok = ok && (!LoadSongFromFram(fram, &out));

        // restore valid then bad length
        SaveSongToFram(fram, src, nullptr);
        fram.Read(H_FRAM_SONGDATA_ADDR, reinterpret_cast<uint8_t *>(&hdr), sizeof(hdr));
        hdr.payload_size = H_FRAM_SONGDATA_SIZE; // guaranteed too big once header included
        fram.Write(H_FRAM_SONGDATA_ADDR, reinterpret_cast<const uint8_t *>(&hdr), sizeof(hdr));
        ok = ok && (!LoadSongFromFram(fram, &out));

        results.push_back({"load rejects bad checksum/version/length", ok, 0u,
                           ok ? "" : "one or more bad headers were accepted"});
    }

    // 12) Option B octave anchors: C0..C8 -> -2..6V
    {
        const struct { uint8_t midi; float volts; const char* name; } points[] = {
            {12u, -2.0f, "C0"},
            {24u, -1.0f, "C1"},
            {36u, 0.0f, "C2"},
            {48u, 1.0f, "C3"},
            {60u, 2.0f, "C4"},
            {72u, 3.0f, "C5"},
            {84u, 4.0f, "C6"},
            {96u, 5.0f, "C7"},
            {108u, 6.0f, "C8"}
        };
        bool ok = true;
        std::string details;
        for (const auto& p : points) {
            const float got = PitchVoltsOptionB(p.midi);
            const float err = got - p.volts;
            const float abs_err = (err < 0.0f) ? -err : err;
            if (abs_err > 0.0001f) {
                ok = false;
                details += std::string(p.name) + " mismatch; ";
            }
        }
        results.push_back({"Option B C0..C8 anchors", ok, 0u, details});
    }

    // 13) Legal piano-roll range 12..108 stays in -2..6V
    {
        bool ok = true;
        std::string details;
        for (uint8_t midi = (uint8_t)kPitchMidiMin; midi <= (uint8_t)kPitchMidiMax; ++midi) {
            const float v = PitchVoltsOptionB(midi);
            if (v < -2.0001f || v > 6.0001f) {
                ok = false;
                details = "legal note mapped outside -2..6V";
                break;
            }
        }
        results.push_back({"legal range maps inside -2..6V", ok, 0u, details});
    }

    // 14) ARP Up uses sorted absolute MIDI notes from [36,43,48,51]
    {
        sequencer::ArpEngine arp;
        const uint8_t src[4] = {36u, 43u, 48u, 51u};
        arp.LoadNotes(src, 4u, sequencer::ArpMode::Up);

        const std::vector<uint8_t> got = CaptureArpSequence(arp, 9u);
        const std::vector<uint8_t> expect = {36u, 43u, 48u, 51u, 36u, 43u, 48u, 51u, 36u};

        const bool ok = SequenceEquals(got, expect);
        results.push_back({"ARP Up absolute pool [36,43,48,51]", ok, 0u, ok ? "" : "unexpected up sequence"});
    }

    // 15) ARP Down uses absolute MIDI notes in reverse
    {
        sequencer::ArpEngine arp;
        const uint8_t src[4] = {36u, 43u, 48u, 51u};
        arp.LoadNotes(src, 4u, sequencer::ArpMode::Down);

        const std::vector<uint8_t> got = CaptureArpSequence(arp, 9u);
        const std::vector<uint8_t> expect = {51u, 48u, 43u, 36u, 51u, 48u, 43u, 36u, 51u};

        const bool ok = SequenceEquals(got, expect);
        results.push_back({"ARP Down absolute pool [36,43,48,51]", ok, 0u, ok ? "" : "unexpected down sequence"});
    }

    // 16) ARP UpDown bounces without endpoint duplication: 0,1,2,3,2,1,0,1...
    {
        sequencer::ArpEngine arp;
        const uint8_t src[4] = {36u, 43u, 48u, 51u};
        arp.LoadNotes(src, 4u, sequencer::ArpMode::UpDown);

        const std::vector<uint8_t> got = CaptureArpSequence(arp, 12u);
        const std::vector<uint8_t> expect =
            {36u, 43u, 48u, 51u, 48u, 43u, 36u, 43u, 48u, 51u, 48u, 43u};

        const bool ok = SequenceEquals(got, expect);
        results.push_back({"ARP UpDown bounce no duplicate endpoints", ok, 0u,
                           ok ? "" : "unexpected updown sequence"});
    }

    // 17) ARP Random stays inside source pool
    {
        sequencer::ArpEngine arp;
        const uint8_t src[4] = {36u, 43u, 48u, 51u};
        srand(1);
        arp.LoadNotes(src, 4u, sequencer::ArpMode::Random);

        bool ok = arp.HasNotes();
        std::string details;
        for (uint8_t i = 0u; i < 64u && ok; ++i)
        {
            const uint8_t n = (i == 0u) ? arp.CurrentNote() : arp.Advance();
            if (!(n == 36u || n == 43u || n == 48u || n == 51u))
            {
                ok = false;
                details = "note outside source pool";
            }
        }

        results.push_back({"ARP Random stays inside absolute pool", ok, 0u, details});
    }

    // 18) ARP cadence mapping is 96/48/24/12 ticks
    {
        const bool ok =
            (sequencer::ArpEngine::TicksPerEvent(sequencer::ArpRate::Quarter) == 96u) &&
            (sequencer::ArpEngine::TicksPerEvent(sequencer::ArpRate::Eighth) == 48u) &&
            (sequencer::ArpEngine::TicksPerEvent(sequencer::ArpRate::Sixteenth) == 24u) &&
            (sequencer::ArpEngine::TicksPerEvent(sequencer::ArpRate::ThirtySecond) == 12u);
        results.push_back({"ARP cadence ticks Quarter/Eighth/16th/32nd", ok, 0u,
                           ok ? "" : "ticks-per-event mismatch"});
    }

    // 19) 2-note vs 4-note pool does not alter ARP rate
    {
        bool ok = true;
        std::string details;

        const uint32_t expect = sequencer::ArpEngine::TicksPerEvent(sequencer::ArpRate::ThirtySecond);

        const uint8_t two_note[4] = {36u, 51u, sequencer::kMidiNoteNone, sequencer::kMidiNoteNone};
        const uint8_t four_note[4] = {36u, 43u, 48u, 51u};

        sequencer::ArpEngine arp_two;
        sequencer::ArpEngine arp_four;
        arp_two.LoadNotes(two_note, 2u, sequencer::ArpMode::Up);
        arp_four.LoadNotes(four_note, 4u, sequencer::ArpMode::Up);

        if (!arp_two.HasNotes() || !arp_four.HasNotes())
        {
            ok = false;
            details = "source pool failed to load";
        }

        if (ok)
        {
            const uint32_t two_ticks = sequencer::ArpEngine::TicksPerEvent(sequencer::ArpRate::ThirtySecond);
            const uint32_t four_ticks = sequencer::ArpEngine::TicksPerEvent(sequencer::ArpRate::ThirtySecond);
            if (two_ticks != expect || four_ticks != expect)
            {
                ok = false;
                details = "ticks-per-event changed with pool size";
            }
        }

        results.push_back({"ARP rate independent from pool size", ok, 0u, details});
    }

    // 20) Event length 8 at 1/16 with ARP rate 1/32 gives 16 opportunities
    {
        const uint32_t count = CountArpOpportunities(8u, 4u, sequencer::ArpRate::ThirtySecond);
        const bool ok = (count == 16u);
        results.push_back({"event len8 @1/16 with arp 1/32 => 16 opportunities", ok, 0u,
                           ok ? "" : "opportunity count mismatch"});
    }

    // 21) Event replacement switches source pool and resets phase
    {
        sequencer::ArpEngine arp;
        const uint8_t pool_a[4] = {36u, 43u, 48u, 51u};
        const uint8_t pool_b[4] = {60u, 67u, sequencer::kMidiNoteNone, sequencer::kMidiNoteNone};

        arp.LoadNotes(pool_a, 4u, sequencer::ArpMode::Up);
        (void)arp.Advance();
        (void)arp.Advance();
        const uint8_t before = arp.CurrentNote();

        arp.LoadNotes(pool_b, 2u, sequencer::ArpMode::Up);
        const uint8_t after = arp.CurrentNote();
        const uint8_t next = arp.Advance();

        const bool ok = (before != after) && (after == 60u) && (next == 67u);
        results.push_back({"event replacement reloads pool and resets arp phase", ok, 0u,
                           ok ? "" : "pool/phase reset mismatch"});
    }

    // 22) Event expiry clears Gate1 and returns CV1 to calibrated zero
    {
        const float zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        float out[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        uint8_t gate_mask = 0xFFu;

        SimulateArpCvFrame(sequencer::kMidiNoteNone, zero, out, &gate_mask);

        const bool ok = (out[0] == 0.0f) && (out[1] == 0.0f) && (out[2] == 0.0f) && (out[3] == 0.0f) &&
                        (gate_mask == 0u);
        results.push_back({"event expiry clears Gate1/CV1", ok, 0u,
                           ok ? "" : "gate/cv did not clear to zero"});
    }

    // 23) Contested-writer guard: non-ARP setters must not mutate Pattern arp_mode/arp_rate
    {
        bool ok = true;
        std::string details;

        SequencerDevice dev;
        dev.Init();

        /* Seed with multi-note chord material. */
        dev.SetStepChordParams(0u, 0u, 1u, 0u, 1u, 1u);

        /* Mode sequence: set explicit mode Up, then run non-ARP edits. */
        dev.SetPatternArpMode(sequencer::ArpMode::Up);
        dev.SetPatternArpRate(sequencer::ArpRate::Sixteenth);
        if (dev.GetCurrentArpMode() != sequencer::ArpMode::Up)
        {
            ok = false;
            details += "SetPatternArpMode(Up) did not stick; ";
        }

        if (ok)
        {
            ok = RunArpOwnershipIsolationSequence(dev,
                                                  sequencer::ArpMode::Up,
                                                  sequencer::ArpRate::Sixteenth,
                                                  &details);
        }

        /* Rate sequence: set explicit rate 1/32, then run equivalent non-ARP edits. */
        if (ok)
        {
            dev.SetPatternArpRate(sequencer::ArpRate::ThirtySecond);
            if (dev.GetCurrentArpRate() != sequencer::ArpRate::ThirtySecond)
            {
                ok = false;
                details += "SetPatternArpRate(1/32) did not stick; ";
            }
        }

        if (ok)
        {
            ok = RunArpOwnershipIsolationSequence(dev,
                                                  sequencer::ArpMode::Up,
                                                  sequencer::ArpRate::ThirtySecond,
                                                  &details);
        }

        results.push_back({"non-ARP setters cannot mutate pattern ARP mode/rate", ok, 0u, details});
    }

    bool all_ok = true;
    printf("V2 Stage-4 Host Harness Results\n");
    printf("================================\n");
    for (const auto &r : results) {
        printf("[%s] %s", r.pass ? "PASS" : "FAIL", r.name.c_str());
        if (r.encoded_size > 0u) {
            printf(" | encoded_size=%u", r.encoded_size);
        }
        if (!r.details.empty()) {
            printf(" | %s", r.details.c_str());
        }
        printf("\n");
        if (!r.pass) all_ok = false;
    }

    return all_ok ? 0 : 1;
}
