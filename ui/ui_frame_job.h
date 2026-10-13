#ifndef UI_FRAME_JOB_H
#define UI_FRAME_JOB_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Updates that arrived while a full frame was being sliced out; re-applied after it. */
#define UI_FRAME_DEFER_BOXES  0x01u
#define UI_FRAME_DEFER_HEADER 0x02u
#define UI_FRAME_DEFER_STATUS 0x04u

typedef struct
{
    uint8_t active;
    uint8_t next_strip;
    uint8_t strip_count;
    uint8_t deferred;
} UiFrameJob;

/* Returns 1 when an unfinished frame was replaced. */
uint8_t UiFrameJob_Begin(UiFrameJob* job, uint8_t strip_count);
uint8_t UiFrameJob_NextStrip(const UiFrameJob* job, uint8_t* strip);
/* Returns 1 when that strip finished the frame. */
uint8_t UiFrameJob_CompleteStrip(UiFrameJob* job);
void    UiFrameJob_Cancel(UiFrameJob* job);
/* Returns 1 when the update was deferred because a frame is in progress. */
uint8_t UiFrameJob_Defer(UiFrameJob* job, uint8_t bits);
uint8_t UiFrameJob_TakeDeferred(UiFrameJob* job);

#ifdef __cplusplus
}
#endif

#endif
