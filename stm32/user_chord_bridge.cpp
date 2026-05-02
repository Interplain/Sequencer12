#include "user_chord_bridge.h"
#include "devices/sequencer/chords/user_chords.h"
#include "platform/fram/mb85rc256.h"
#include "platform/fram/fram_layout.h"
#include <cstring>

using namespace sequencer;

// FRAM layout for user chord block now in fram_layout.h
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

static void FramLoadAll(void)
{
    if (s_fram_checked) return;
    s_fram_checked = 1;

    if (!kFramEnabled) return;

    s_fram_ready = MB85RC256_IsReady() ? 1u : 0u;
    if (!s_fram_ready)
    {
        g_user_chords.Init();   /* ensure clean state even when FRAM absent */
        return;
    }

    // Read magic - if not present, no chords saved yet
    uint8_t magic[4] = {0};
    if (!MB85RC256_Read(FRAM_MAGIC_ADDR, magic, sizeof(magic)))
    {
        s_last_io_ok = 0;
        g_user_chords.Init();   /* read failed — reset to empty so count is 0 */
        return;
    }

    if (memcmp(magic, kFramMagic, sizeof(kFramMagic)) != 0)
    {
        // No saved chords (magic not set) - this is normal for first boot
        g_user_chords.Init();
        return;
    }

    // Magic found - read chord data
    static uint8_t s_buf[kUserChordBlockSize];
    if (MB85RC256_Read(FRAM_USERCHORDS_ADDR, s_buf, (uint16_t)kUserChordBlockSize))
    {
        g_user_chords.Load(s_buf, kUserChordBlockSize);
        s_loaded_from_fram = 1;
        s_last_io_ok = 1;
    }
    else
    {
        s_last_io_ok = 0;
        g_user_chords.Init();   /* read failed — reset so count doesn't lie */
    }
}

static void FramWriteSlot(uint8_t index, uint8_t* out_ok)
{
    if (out_ok) *out_ok = 0;
    
    if (!kFramEnabled) return;

    // Step 1: Write chord block
    static uint8_t s_buf[kUserChordBlockSize];
    g_user_chords.Save(s_buf, kUserChordBlockSize);
    uint8_t ok_block = MB85RC256_Write(FRAM_USERCHORDS_ADDR, s_buf, (uint16_t)kUserChordBlockSize);
    
    if (!ok_block)
    {
        s_last_io_ok = 0;
        if (out_ok) *out_ok = 0;
        return;
    }
    
    // Step 2: Write magic (this marks chords as valid)
    uint8_t ok_magic = MB85RC256_Write(FRAM_MAGIC_ADDR, kFramMagic, sizeof(kFramMagic));
    
    s_last_io_ok = ok_magic ? 1u : 0u;
    if (out_ok) *out_ok = s_last_io_ok;
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
    uint8_t ok = 0;
    FramWriteSlot((uint8_t)slot, &ok);
    return ok ? SlotToOrdinal(slot) : 0xFF;
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
    if (ok) {
        FramWriteSlot((uint8_t)slot, &ok);
    }
    return ok;
}

void Bridge_UserChord_Delete(uint8_t index)
{
    Bridge_UserChord_EnsureLoaded();
    uint32_t slot = OrdinalToSlot(index);
    if (slot >= g_user_chords.Capacity()) return;

    g_user_chords.DeleteChord(slot);
    uint8_t ok = 0;
    FramWriteSlot((uint8_t)slot, &ok);
}
