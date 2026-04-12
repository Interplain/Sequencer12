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
uint32_t Bridge_GetCurrentStep(void);
uint8_t  Bridge_IsPlaying(void);
uint32_t Bridge_GetElapsedMs(void);

#ifdef __cplusplus
}
#endif

#endif