#ifndef UI_SCREENSAVER_H
#define UI_SCREENSAVER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    UI_SCREENSAVER_INACTIVE = 0,  /* not showing; normal UI update should run */
    UI_SCREENSAVER_ACTIVE,        /* showing; normal UI update should be skipped this frame */
    UI_SCREENSAVER_JUST_WOKE      /* woke this call; caller should force one full repaint */
} UiScreensaverResult;

/* Seeds internal counters. Call once from UI_Sequencer_Init(). */
void UI_Screensaver_Init(void);

/* Call every ~10ms from UI_Sequencer_Update(), before any other input handling.
 * Internally rate-limits its own rendering to ~4-8Hz. */
UiScreensaverResult UI_Screensaver_Tick(void);

uint8_t UI_Screensaver_IsActive(void);

#ifdef __cplusplus
}
#endif

#endif
