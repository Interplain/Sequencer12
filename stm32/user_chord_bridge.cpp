#include "user_chord_bridge.h"
#include "devices/sequencer/chords/user_chords.h"
#include <cstring>

using namespace sequencer;

// Global user chords instance
static UserChords g_user_chords;

void Bridge_UserChord_Init(void)
{
    g_user_chords.Init();
}

uint8_t Bridge_UserChord_GetCount(void)
{
    return g_user_chords.UsedCount();
}

const UserChordInfo* Bridge_UserChord_Get(uint8_t index)
{
    if (!g_user_chords.IsSlotUsed(index)) {
        return nullptr;
    }

    static UserChordInfo info;
    const auto& chord = g_user_chords.GetChord(index);
    
    info.note_mask = chord.note_mask;
    strncpy(info.name, chord.name, 16);
    info.name[16] = '\0';
    
    return &info;
}

uint8_t Bridge_UserChord_Save(const char* name, uint16_t note_mask)
{
    uint32_t slot = g_user_chords.FindFreeSlot();
    if (slot >= g_user_chords.Capacity()) {
        return 0xFF;  // Storage full
    }
    
    g_user_chords.SaveChord(slot, name, note_mask);
    return (uint8_t)slot;
}

void Bridge_UserChord_Delete(uint8_t index)
{
    g_user_chords.DeleteChord(index);
}
