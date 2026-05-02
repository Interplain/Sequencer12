#include "sequencer_bridge.h"
#include "devices/sequencer/sequencer_device.h"
#include "devices/sequencer/chords/chord_library.h"
#include "platform/fram/mb85rc256.h"
#include "platform/fram/fram_layout.h"
#include "platform/dac8564/dac8564.h"
#include <cstdio>
#include <cstring>

static SequencerDevice g_sequencer;
static uint8_t s_persist_depth = 0u;
static uint8_t s_persist_dirty = 0u;

namespace {

static constexpr uint8_t kSongMagic[4] = { 0x53, 0x31, 0x32, 0x53 }; // "S12S"
static constexpr uint32_t kSongBlobVersion = 1u;

typedef struct
{
    uint8_t  magic[4];
    uint32_t version;
    uint32_t payload_size;
    uint32_t checksum;
} SongBlobHeader;

static uint32_t SongChecksum(const uint8_t* data, uint32_t len)
{
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < len; ++i)
    {
        h ^= data[i];
        h *= 16777619u;
    }
    return h;
}

static uint8_t SaveSongToFram(void)
{
    sequencer::Song song{};
    g_sequencer.ExportSong(&song);

    SongBlobHeader hdr{};
    memcpy(hdr.magic, kSongMagic, sizeof(kSongMagic));
    hdr.version = kSongBlobVersion;
    hdr.payload_size = (uint32_t)sizeof(song);
    hdr.checksum = SongChecksum(reinterpret_cast<const uint8_t*>(&song), hdr.payload_size);

    const uint16_t hdr_size = (uint16_t)sizeof(SongBlobHeader);
    const uint16_t payload_size = (uint16_t)sizeof(song);
    if ((uint32_t)hdr_size + (uint32_t)payload_size > FRAM_SONGDATA_SIZE)
    {
        return 0u;
    }

    if (!MB85RC256_WriteAndVerify(FRAM_SONGDATA_ADDR, reinterpret_cast<const uint8_t*>(&hdr), hdr_size))
    {
        return 0u;
    }
    if (!MB85RC256_WriteAndVerify((uint16_t)(FRAM_SONGDATA_ADDR + hdr_size),
                                  reinterpret_cast<const uint8_t*>(&song),
                                  payload_size))
    {
        return 0u;
    }
    return 1u;
}

static uint8_t __attribute__((unused)) LoadSongFromFram(void)
{
    SongBlobHeader hdr{};
    if (!MB85RC256_IsReady()) return 0u;
    if (!MB85RC256_Read(FRAM_SONGDATA_ADDR, reinterpret_cast<uint8_t*>(&hdr), (uint16_t)sizeof(hdr))) return 0u;

    if (memcmp(hdr.magic, kSongMagic, sizeof(kSongMagic)) != 0) return 0u;
    if (hdr.version != kSongBlobVersion) return 0u;
    if (hdr.payload_size != sizeof(sequencer::Song)) return 0u;
    if ((uint32_t)sizeof(SongBlobHeader) + hdr.payload_size > FRAM_SONGDATA_SIZE) return 0u;

    sequencer::Song song{};
    if (!MB85RC256_Read((uint16_t)(FRAM_SONGDATA_ADDR + (uint16_t)sizeof(SongBlobHeader)),
                        reinterpret_cast<uint8_t*>(&song),
                        (uint16_t)sizeof(song)))
    {
        return 0u;
    }

    const uint32_t checksum = SongChecksum(reinterpret_cast<const uint8_t*>(&song), (uint32_t)sizeof(song));
    if (checksum != hdr.checksum) return 0u;

    g_sequencer.ImportSong(song);
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

} // namespace

extern "C"
{
    void     Bridge_Init(void)
    {
        g_sequencer.Init();
        /* Keep runtime step state volatile: do not restore song step data on boot. */
    }
    void     Bridge_Tick1ms(void)           { g_sequencer.Tick1ms(); }
    void     Bridge_Process(void)
    {
        uint8_t note = 0;
        bool    gate = false;
        if (g_sequencer.ConsumeCvEvent(&note, &gate))
        {
            /* 1V/oct: semitone 0 = 0V, each semitone = 1/12 V.
             * All 4 channels output the same CV for now (monophonic). */
            float volts = gate ? (note / 12.0f) : 0.0f;
            uint16_t code_a = DAC8564_PitchVoltsToCodeForChannel(DAC8564_CH_A, volts);
            uint16_t code_b = DAC8564_PitchVoltsToCodeForChannel(DAC8564_CH_B, volts);
            uint16_t code_c = DAC8564_PitchVoltsToCodeForChannel(DAC8564_CH_C, volts);
            uint16_t code_d = DAC8564_PitchVoltsToCodeForChannel(DAC8564_CH_D, volts);
            DAC8564_SetAllRaw(code_a, code_b, code_c, code_d);
        }
        g_sequencer.Process();
    }
    void     Bridge_Start(void)             { g_sequencer.Start(); }
    void     Bridge_Stop(void)              { g_sequencer.Stop(); }
    void     Bridge_Reset(void)             { g_sequencer.Reset(); }
    void     Bridge_SetBpm(uint32_t bpm)    { g_sequencer.SetBpm(bpm); }
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
    void     Bridge_SetPatternStepDivision(uint8_t step_division) { g_sequencer.SetPatternStepDivision(step_division); PersistSong(); }
    uint8_t  Bridge_GetPatternStepDivision(void) { return g_sequencer.GetPatternStepDivision(); }
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
    uint32_t Bridge_GetCurrentStep(void)    { return g_sequencer.GetCurrentStep(); }
    uint8_t  Bridge_IsPlaying(void)         { return g_sequencer.IsPlaying() ? 1u : 0u; }
    uint32_t Bridge_GetElapsedMs(void)      { return g_sequencer.GetElapsedMs(); }


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


}