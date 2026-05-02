#pragma once
#include <cstdint>
#include <array>
#include "devices/sequencer/sequencer_types.h"
#include "platform/fram/fram_layout.h"

namespace sequencer
{

constexpr uint32_t kUserChordCount    = 128;
constexpr uint32_t kUserChordNameLen  = 16;   // chars + null terminator

// ─────────────────────────────────────────────
// User chord slot
// ─────────────────────────────────────────────

struct UserChord
{
    char     name[kUserChordNameLen + 1] = {};  // 16 chars + null
    uint16_t note_mask                   = 0;
    bool     in_use                      = false;
};

// ─────────────────────────────────────────────
// FRAM stub — flat byte buffer
// In hardware this will be read/written via I2C FRAM
// ─────────────────────────────────────────────

constexpr uint32_t kUserChordBlockSize =
    sizeof(UserChord) * kUserChordCount;

static_assert(kUserChordBlockSize == FRAM_USERCHORDS_SIZE,
              "FRAM_USERCHORDS_SIZE must match UserChord storage footprint");

// ─────────────────────────────────────────────
// User chord library
// ─────────────────────────────────────────────

class UserChords
{
public:

    void Init();

    // FRAM stub — load/save entire block
    void Load(const uint8_t* src, uint32_t len);
    void Save(uint8_t* dst, uint32_t len) const;

    // Slot access
    bool             IsSlotUsed(uint32_t index) const;
    const UserChord& GetChord(uint32_t index) const;

    // Save a chord into a slot (overwrites if exists)
    // name is truncated to kUserChordNameLen if longer
    bool SaveChord(uint32_t index,
                   const char* name,
                   uint16_t note_mask);

    // Delete a slot — marks as not in_use, clears data
    void DeleteChord(uint32_t index);

    // Find first free slot — returns kUserChordCount if none free
    uint32_t FindFreeSlot() const;

    // Count how many slots are in use
    uint32_t UsedCount() const;

    uint32_t Capacity() const { return kUserChordCount; }

private:
    std::array<UserChord, kUserChordCount> slots_{};

    bool IndexValid(uint32_t index) const
    {
        return index < kUserChordCount;
    }
};

} // namespace sequencer