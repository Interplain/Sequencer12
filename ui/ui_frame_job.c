#include "ui_frame_job.h"

uint8_t UiFrameJob_Begin(UiFrameJob* job, uint8_t strip_count)
{
    if (!job) return 0u;
    const uint8_t restarted = job->active;
    job->active = (strip_count > 0u) ? 1u : 0u;
    job->next_strip = 0u;
    job->strip_count = strip_count;
    return restarted;
}

uint8_t UiFrameJob_NextStrip(const UiFrameJob* job, uint8_t* strip)
{
    if (!job || !job->active || job->next_strip >= job->strip_count) return 0u;
    if (strip) *strip = job->next_strip;
    return 1u;
}

uint8_t UiFrameJob_CompleteStrip(UiFrameJob* job)
{
    if (!job || !job->active) return 0u;
    ++job->next_strip;
    if (job->next_strip < job->strip_count) return 0u;
    job->active = 0u;
    return 1u;
}

void UiFrameJob_Cancel(UiFrameJob* job)
{
    if (!job) return;
    job->active = 0u;
    job->next_strip = 0u;
    job->deferred = 0u;
}

uint8_t UiFrameJob_Defer(UiFrameJob* job, uint8_t bits)
{
    if (!job || !job->active) return 0u;
    job->deferred |= bits;
    return 1u;
}

uint8_t UiFrameJob_TakeDeferred(UiFrameJob* job)
{
    if (!job || job->active) return 0u;
    const uint8_t bits = job->deferred;
    job->deferred = 0u;
    return bits;
}
