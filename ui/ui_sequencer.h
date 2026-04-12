#ifndef UI_SEQUENCER_H
#define UI_SEQUENCER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* ── Init ────────────────────────────────────────────────────────────────── */
void UI_Sequencer_Init(void);

/* ── Update — call every 10ms from main loop ─────────────────────────────── */
void UI_Sequencer_Update(void);

/* ── Called from SysTick — 1ms ───────────────────────────────────────────── */
void UI_Sequencer_Tick1ms(void);

#ifdef __cplusplus
}
#endif

#endif