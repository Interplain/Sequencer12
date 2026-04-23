#include "sequencer_bridge.h"
#include "devices/sequencer/sequencer_device.h"
#include "devices/sequencer/chords/chord_library.h"
#include <cstdio>
#include <cstring>

static SequencerDevice g_sequencer;

extern "C"
{
    void     Bridge_Init(void)              { g_sequencer.Init(); }
    void     Bridge_Tick1ms(void)           { g_sequencer.Tick1ms(); }
    void     Bridge_Process(void)           { g_sequencer.Process(); }
    void     Bridge_Start(void)             { g_sequencer.Start(); }
    void     Bridge_Stop(void)              { g_sequencer.Stop(); }
    void     Bridge_Reset(void)             { g_sequencer.Reset(); }
    void     Bridge_SetBpm(uint32_t bpm)    { g_sequencer.SetBpm(bpm); }
    void     Bridge_SetPatternStepCount(uint8_t step_count) { g_sequencer.SetPatternStepCount(step_count); }
    uint8_t  Bridge_GetPatternStepCount(void) { return g_sequencer.GetPatternStepCount(); }
    void     Bridge_SetPatternStepDivision(uint8_t step_division) { g_sequencer.SetPatternStepDivision(step_division); }
    uint8_t  Bridge_GetPatternStepDivision(void) { return g_sequencer.GetPatternStepDivision(); }
    void     Bridge_SetTimeSignature(uint8_t numerator, uint8_t denominator) { g_sequencer.SetTimeSignature(numerator, denominator); }
    uint8_t  Bridge_GetTimeSigNumerator(void) { return g_sequencer.GetTimeSigNumerator(); }
    uint8_t  Bridge_GetTimeSigDenominator(void) { return g_sequencer.GetTimeSigDenominator(); }
    void     Bridge_SetSwing(uint8_t swing) { g_sequencer.SetSwing(swing); }
    uint8_t  Bridge_GetSwing(void) { return g_sequencer.GetSwing(); }
    void     Bridge_SetStepChordParams(uint8_t step_index,
                                       uint8_t root_key,
                                       uint8_t chord_type,
                                       uint8_t arp_pattern,
                                       uint8_t duration,
                                       uint8_t repeat_count)
    {
        g_sequencer.SetStepChordParams(step_index, root_key, chord_type, arp_pattern, duration, repeat_count);
    }
    void     Bridge_SetStepCustomNoteMask(uint8_t step_index, uint16_t note_mask)
    {
        g_sequencer.SetStepCustomNoteMask(step_index, note_mask);
    }
    void     Bridge_SetStepCustomUserChord(uint8_t step_index, uint16_t note_mask, const char* name)
    {
        g_sequencer.SetStepCustomUserChord(step_index, note_mask, name);
    }
    void     Bridge_SetPatternRepeatCount(uint8_t repeat_count)
    {
        g_sequencer.SetPatternRepeatCount(repeat_count);
    }
    uint8_t  Bridge_GetPatternRepeatCount(void)
    {
        return g_sequencer.GetPatternRepeatCount();
    }
    void     Bridge_SetCurrentPattern(uint8_t pattern_index)
    {
        g_sequencer.SetCurrentPatternIndex(pattern_index);
    }
    void     Bridge_SetChainLength(uint8_t length)
    {
        g_sequencer.SetChainLength(length);
    }
    uint8_t  Bridge_GetChainLength(void)
    {
        return g_sequencer.GetChainLength();
    }
    void     Bridge_SetChainPatternAt(uint8_t pos, uint8_t pattern_index)
    {
        g_sequencer.SetChainPatternAt(pos, pattern_index);
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