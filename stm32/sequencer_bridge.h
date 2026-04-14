#ifndef SEQUENCER_BRIDGE_H
#define SEQUENCER_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

void     Bridge_Init(void);
void     Bridge_Tick1ms(void);
void     Bridge_Process(void);
void     Bridge_Start(void);
void     Bridge_Stop(void);
void     Bridge_Reset(void);
void     Bridge_SetBpm(uint32_t bpm);
void     Bridge_SetPatternStepCount(uint8_t step_count);
uint8_t  Bridge_GetPatternStepCount(void);
void     Bridge_SetPatternStepDivision(uint8_t step_division);
uint8_t  Bridge_GetPatternStepDivision(void);
void     Bridge_SetTimeSignature(uint8_t numerator, uint8_t denominator);
uint8_t  Bridge_GetTimeSigNumerator(void);
uint8_t  Bridge_GetTimeSigDenominator(void);
void     Bridge_SetSwing(uint8_t swing);
uint8_t  Bridge_GetSwing(void);
void     Bridge_SetStepChordParams(uint8_t step_index,
								   uint8_t root_key,
								   uint8_t chord_type,
								   uint8_t arp_pattern,
								   uint8_t duration);
void     Bridge_SetPatternRepeatCount(uint8_t repeat_count);
uint8_t  Bridge_GetPatternRepeatCount(void);
void     Bridge_SetCurrentPattern(uint8_t pattern_index);
uint32_t Bridge_GetCurrentStep(void);
uint8_t  Bridge_IsPlaying(void);
uint32_t Bridge_GetElapsedMs(void);
uint8_t  Bridge_IsPlaying(void);
uint8_t  Bridge_GetCurrentPattern(void);
uint32_t Bridge_GetRunTimeMs(void);
uint32_t Bridge_GetCompletedLoops(void);

#ifdef __cplusplus
}
#endif

#endif