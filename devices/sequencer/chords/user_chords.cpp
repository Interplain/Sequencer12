#include "devices/sequencer/chords/user_chords.h"
#include <cstring>

namespace sequencer
{

void UserChords::Init()
{
    for (auto& slot : slots_)
    {
        slot = UserChord{};
    }
}

// ─────────────────────────────────────────────
// FRAM stub
// ─────────────────────────────────────────────

void UserChords::Load(const uint8_t* src, uint32_t len)
{
    if (len < kUserChordBlockSize) return;
    std::memcpy(slots_.data(), src, kUserChordBlockSize);
}

void UserChords::Save(uint8_t* dst, uint32_t len) const
{
    if (len < kUserChordBlockSize) return;
    std::memcpy(dst, slots_.data(), kUserChordBlockSize);
}

// ─────────────────────────────────────────────
// Slot access
// ─────────────────────────────────────────────

bool UserChords::IsSlotUsed(uint32_t index) const
{
    if (!IndexValid(index)) return false;
    return slots_[index].in_use;
}

const UserChord& UserChords::GetChord(uint32_t index) const
{
    static const UserChord empty{};
    if (!IndexValid(index)) return empty;
    return slots_[index];
}

bool UserChords::SaveChord(uint32_t index,
                            const char* name,
                            uint16_t note_mask)
{
    if (!IndexValid(index)) return false;

    UserChord& slot = slots_[index];

    std::strncpy(slot.name, name, kUserChordNameLen);
    slot.name[kUserChordNameLen] = '\0';   // always null terminate
    slot.note_mask = note_mask;
    slot.in_use    = true;

    return true;
}

void UserChords::DeleteChord(uint32_t index)
{
    if (!IndexValid(index)) return;
    slots_[index] = UserChord{};
}

uint32_t UserChords::FindFreeSlot() const
{
    for (uint32_t i = 0; i < kUserChordCount; ++i)
    {
        if (!slots_[i].in_use) return i;
    }
    return kUserChordCount;   // sentinel — no free slot
}

uint32_t UserChords::UsedCount() const
{
    uint32_t count = 0;
    for (const auto& slot : slots_)
    {
        if (slot.in_use) ++count;
    }
    return count;
}

} // namespace sequencer