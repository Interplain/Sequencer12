#ifndef SEQUENCER_BRIDGE_H
#define SEQUENCER_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct
{
	uint8_t notes[4];
} BridgeLedgerSlot;

void     Bridge_Init(void);
void     Bridge_SetTickEnabled(uint8_t enabled);
void     Bridge_SetCvRouterMode(uint8_t mode);
uint8_t  Bridge_GetCvRouterMode(void);
void     Bridge_Tick1ms(void);
void     Bridge_TickMusical(void);
void     Bridge_ServiceMusicalEvents(void);
void     Bridge_WriteCurrentStepSnapshot(void);
void     Bridge_Process(void);
void     Bridge_ApplyZeroOutputCodes(void);
void     Bridge_Start(void);
void     Bridge_Stop(void);
void     Bridge_Reset(void);
void     Bridge_SetBpm(uint32_t bpm);
void     Bridge_PersistBegin(void);
void     Bridge_PersistEnd(void);
/* Pumps the real-time-safe persistence engine. Call exactly once per
 * main-loop pass, after UI_Sequencer_Update(). Performs at most one small
 * PatternStore chunk write (bounded, always safe during playback) plus,
 * only once editing has gone idle while transport is stopped, the slower
 * full legacy V3 song snapshot. Never blocks for the full V3 save while
 * playing -- see Bridge_FlushPersistence() for that. */
void     Bridge_ServicePersistence(void);
/* Forces an immediate full legacy V3 song snapshot if one is pending,
 * bypassing the idle-checkpoint wait. Safe to call only when it is
 * acceptable to block for ~200ms (e.g. transport stop, explicit save). */
void     Bridge_FlushPersistence(void);
/* Current+Next Pattern cache validation pilot (diagnostic/shadow only):
 * pumps at most one small chunked PatternStore read per call, comparing
 * completed loads against the resident SRAM Song. Does not affect
 * playback -- resident song_.patterns[] remains fully authoritative.
 * Results reported via the g_pc_* globals (see sequencer_bridge.cpp). */
void     Bridge_ServicePatternCache(void);
/* Stage 2 FRAM Pattern Store debug/verification path: byte-for-byte compares
 * the resident SRAM Pattern at `pattern_index` against its FRAM shadow slot.
 * Not called anywhere in production yet -- diagnostic only. */
uint8_t  Bridge_PatternStoreVerify(uint8_t pattern_index);
/* Diagnostic/read-only: checks all 32 PatternStore slots against the
 * resident SRAM Song, byte-for-byte. Never writes to PatternStore or V3
 * FRAM, never alters SRAM contents. Blocks for the duration of the check
 * (~32 slots worth of FRAM reads) -- only call when idle/stopped. Results
 * are reported via the g_ps_verify_* globals (see sequencer_bridge.cpp). */
void     Bridge_PatternStoreVerifyAll(void);
void     Bridge_SetPatternStepCount(uint8_t step_count);
uint8_t  Bridge_GetPatternStepCount(void);
uint8_t  Bridge_SetPatternTiming(uint8_t step_division, uint8_t numerator, uint8_t denominator);
void     Bridge_SetPatternStepDivision(uint8_t step_division);
uint8_t  Bridge_GetPatternStepDivision(void);
void     Bridge_SetPatternArpMode(uint8_t arp_mode);
void     Bridge_SetPatternArpRate(uint8_t arp_rate);
uint8_t  Bridge_GetPatternArpRate(void);
void     Bridge_SetTimeSignature(uint8_t numerator, uint8_t denominator);
uint8_t  Bridge_GetTimeSigNumerator(void);
uint8_t  Bridge_GetTimeSigDenominator(void);
void     Bridge_SetSwing(uint8_t swing);
uint8_t  Bridge_GetSwing(void);
void     Bridge_SetStepChordParams(uint8_t step_index,
								   uint8_t root_key,
								   uint8_t chord_type,
								   uint8_t arp_pattern,
								   uint8_t duration,
								   uint8_t repeat_count);
void     Bridge_SetStepCustomNoteMask(uint8_t step_index, uint16_t note_mask);
void     Bridge_SetStepCustomUserChord(uint8_t step_index, uint16_t note_mask, const char* name);
void     Bridge_SetStepLedgerLength(uint8_t step_index, uint8_t length);
void     Bridge_SetStepLedgerSlot(uint8_t step_index, uint8_t slot_index, uint16_t note_mask);
uint8_t  Bridge_GetStepLedgerLength(uint8_t step_index);
uint16_t Bridge_GetStepLedgerSlot(uint8_t step_index, uint8_t slot_index);
uint8_t  Bridge_GetStepEventLength(uint8_t step_index, uint8_t slot_index);
void     Bridge_SetStepEventLength(uint8_t step_index,
								   uint8_t slot_index,
								   uint8_t length_positions);
uint8_t  Bridge_GetStepEventMaxLength(uint8_t step_index, uint8_t slot_index);
void     Bridge_SetStepLedgerSlotMidi(uint8_t step_index, uint8_t slot_index, const BridgeLedgerSlot* slot);
BridgeLedgerSlot Bridge_GetStepLedgerSlotMidi(uint8_t step_index, uint8_t slot_index);
void     Bridge_ClearStepLedger(uint8_t step_index);
void     Bridge_SetStepBarState(uint8_t step_index, uint8_t state);
uint8_t  Bridge_GetStepBarState(uint8_t step_index);
void     Bridge_SetPatternRepeatCount(uint8_t repeat_count);
uint8_t  Bridge_GetPatternRepeatCount(void);
void     Bridge_SetCurrentPattern(uint8_t pattern_index);
void     Bridge_SetPlaybackSource(uint8_t source);
uint8_t  Bridge_GetPlaybackSource(void);
void     Bridge_SetChainLength(uint8_t length);
uint8_t  Bridge_GetChainLength(void);
void     Bridge_SetChainPatternAt(uint8_t pos, uint8_t pattern_index);
uint8_t  Bridge_GetChainPatternAt(uint8_t pos);
uint8_t  Bridge_GetChainCurrentPosition(void);
uint8_t  Bridge_GetCurrentPatternRepeatProgress(void);
uint16_t Bridge_GetStepNoteMask(uint8_t step_index);
uint8_t  Bridge_GetSongKeyRoot(void);
uint8_t  Bridge_GetSongKeyScale(void);
uint8_t  Bridge_GetCurrentPatternArpMode(void);
uint16_t Bridge_GetChordMaskForUiParams(uint8_t root_key, uint8_t chord_type);
uint8_t  Bridge_GetStepLedgerCvLaneNotes(uint8_t step_index, uint8_t slot_index, uint8_t out_notes[4]);
uint16_t Bridge_GetCurrentOutputNoteMask(void);
int16_t  Bridge_GetCurrentOutputPrimaryMilliVolts(void);
const char* Bridge_GetStepChordDisplayName(uint8_t step_index, char* buf, uint8_t buf_len);
const char* Bridge_FindChordName(uint16_t note_mask, char* buf, uint8_t buf_len);
uint8_t  Bridge_GetStepChordUiParams(uint8_t step_index,
									 uint8_t* root_key,
									 uint8_t* chord_type,
									 uint8_t* duration,
									 uint8_t* repeat_count);
uint32_t Bridge_GetCurrentStep(void);
uint8_t  Bridge_GetCurrentStepSubIndex(void);
uint8_t  Bridge_IsPlaying(void);
uint32_t Bridge_GetElapsedMs(void);
uint8_t  Bridge_GetCurrentPattern(void);
uint8_t  Bridge_GetPlayPattern(void);
uint32_t Bridge_GetRunTimeMs(void);
uint32_t Bridge_GetCompletedLoops(void);
uint32_t Bridge_GetUClockMusicalCallbackCount(void);
uint32_t Bridge_GetTickMusicalCount(void);
uint32_t Bridge_GetPendingStepEnqueueCount(void);
uint32_t Bridge_GetServiceOneStepCount(void);

#ifdef __cplusplus
}
#endif

#endif