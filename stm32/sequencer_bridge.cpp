#include "sequencer_bridge.h"
#include "devices/sequencer/sequencer_device.h"

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
                                       uint8_t duration)
    {
        g_sequencer.SetStepChordParams(step_index, root_key, chord_type, arp_pattern, duration);
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