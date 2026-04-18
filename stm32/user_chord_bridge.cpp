#include "user_chord_bridge.h"
#include "devices/sequencer/chords/user_chords.h"
#include "platform/fram/mb85rc256.h"
#include <cstring>

using namespace sequencer;

/* FRAM layout for user chord block */
#define FRAM_MAGIC_ADDR  0x0000u
#define FRAM_CHORDS_ADDR 0x0010u
static const uint8_t kFramMagic[4] = { 0xC4, 0x0D, 0x55, 0x01 };

// Global user chords instance
static UserChords g_user_chords;
static uint8_t s_fram_checked = 0;
static const uint8_t kFramEnabled = 1;
static uint8_t s_fram_ready = 0;
static uint8_t s_loaded_from_fram = 0;
static uint8_t s_last_io_ok = 1;

static uint32_t OrdinalToSlot(uint8_t ordinal)
{
    uint32_t visible_index = 0;
    for (uint32_t slot = 0; slot < g_user_chords.Capacity(); ++slot)
    {
        if (!g_user_chords.IsSlotUsed(slot)) continue;
        if (visible_index == ordinal) return slot;
        ++visible_index;
    }
    return g_user_chords.Capacity();
}

static uint8_t SlotToOrdinal(uint32_t slot_index)
{
    uint8_t visible_index = 0;
    for (uint32_t slot = 0; slot < g_user_chords.Capacity(); ++slot)
    {
        if (!g_user_chords.IsSlotUsed(slot)) continue;
        if (slot == slot_index) return visible_index;
        ++visible_index;
    }
    return 0xFFu;
}

static uint16_t FramSlotAddr(uint8_t index)
{
    return (uint16_t)(FRAM_CHORDS_ADDR + (uint16_t)(sizeof(UserChord) * index));
}

static void FramLoadAll(void)
{
    if (s_fram_checked) return;
    s_fram_checked = 1;

    if (!kFramEnabled) return;

    s_fram_ready = MB85RC256_IsReady() ? 1u : 0u;
    if (!s_fram_ready)
    {
        s_last_io_ok = 0;
        return;
    }

    uint8_t magic[4] = {0};
    if (!MB85RC256_Read(FRAM_MAGIC_ADDR, magic, sizeof(magic)))
    {
        s_last_io_ok = 0;
        return;
    }
    if (memcmp(magic, kFramMagic, sizeof(kFramMagic)) != 0)
    {
        s_last_io_ok = 1;
        return;
    }

    static uint8_t s_buf[kUserChordBlockSize];
    if (MB85RC256_Read(FRAM_CHORDS_ADDR, s_buf, (uint16_t)kUserChordBlockSize))
    {
        g_user_chords.Load(s_buf, kUserChordBlockSize);
        s_loaded_from_fram = 1;
        s_last_io_ok = 1;
    }
    else
    {
        s_last_io_ok = 0;
    }
}

static void FramWriteSlot(uint8_t index)
{
    if (!kFramEnabled) return;
    if (!s_fram_ready) return;

    const UserChord& chord = g_user_chords.GetChord(index);
    uint8_t ok_magic = MB85RC256_Write(FRAM_MAGIC_ADDR, kFramMagic, sizeof(kFramMagic));
    uint8_t ok_slot = MB85RC256_Write(FramSlotAddr(index),
                                      reinterpret_cast<const uint8_t*>(&chord),
                                      (uint16_t)sizeof(UserChord));
    s_last_io_ok = (ok_magic && ok_slot) ? 1u : 0u;
}

void Bridge_UserChord_Init(void)
{
    g_user_chords.Init();
    s_fram_checked = 0;
    s_fram_ready = 0;
    s_loaded_from_fram = 0;
    s_last_io_ok = 1;

    /* Preload FRAM contents during boot so menus stay responsive later. */
    FramLoadAll();
}

void Bridge_UserChord_EnsureLoaded(void)
{
    FramLoadAll();
}

uint8_t Bridge_UserChord_IsFramEnabled(void)
{
    return kFramEnabled;
}

uint8_t Bridge_UserChord_IsFramReady(void)
{
    return s_fram_ready;
}

uint8_t Bridge_UserChord_WasLoadedFromFram(void)
{
    return s_loaded_from_fram;
}

uint8_t Bridge_UserChord_LastIoOk(void)
{
    return s_last_io_ok;
}

uint8_t Bridge_UserChord_GetCount(void)
{
    Bridge_UserChord_EnsureLoaded();
    return g_user_chords.UsedCount();
}

const UserChordInfo* Bridge_UserChord_Get(uint8_t index)
{
    Bridge_UserChord_EnsureLoaded();
    uint32_t slot = OrdinalToSlot(index);
    if (slot >= g_user_chords.Capacity()) {
        return nullptr;
    }

    static UserChordInfo info;
    const auto& chord = g_user_chords.GetChord(slot);
    
    info.note_mask = chord.note_mask;
    strncpy(info.name, chord.name, 16);
    info.name[16] = '\0';
    
    return &info;
}

uint8_t Bridge_UserChord_Save(const char* name, uint16_t note_mask)
{
    Bridge_UserChord_EnsureLoaded();

    uint32_t slot = g_user_chords.FindFreeSlot();
    if (slot >= g_user_chords.Capacity()) {
        return 0xFF;  // Storage full
    }
    
    g_user_chords.SaveChord(slot, name, note_mask);
    FramWriteSlot((uint8_t)slot);
    return SlotToOrdinal(slot);
}

uint8_t Bridge_UserChord_Rename(uint8_t index, const char* name)
{
    Bridge_UserChord_EnsureLoaded();

    uint32_t slot = OrdinalToSlot(index);
    if (slot >= g_user_chords.Capacity()) {
        return 0;
    }

    const auto& chord = g_user_chords.GetChord(slot);
    uint8_t ok = g_user_chords.SaveChord(slot, name, chord.note_mask) ? 1 : 0;
    if (ok) FramWriteSlot((uint8_t)slot);
    return ok;
}

void Bridge_UserChord_Delete(uint8_t index)
{
    Bridge_UserChord_EnsureLoaded();
    uint32_t slot = OrdinalToSlot(index);
    if (slot >= g_user_chords.Capacity()) return;

    g_user_chords.DeleteChord(slot);
    FramWriteSlot((uint8_t)slot);
}
