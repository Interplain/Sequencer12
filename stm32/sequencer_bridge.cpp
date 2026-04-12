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
    uint32_t Bridge_GetCurrentStep(void)    { return g_sequencer.GetCurrentStep(); }
    uint8_t  Bridge_IsPlaying(void)         { return g_sequencer.IsPlaying() ? 1u : 0u; }
    uint32_t Bridge_GetElapsedMs(void)      { return g_sequencer.GetElapsedMs(); }
}