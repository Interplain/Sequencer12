#include "ui_timing_commit.h"
#include "sequencer_bridge.h"
#include "stm32/time_commit_probe.h"

#if defined(S12_TIME_COMMIT_PROBE)
#define TIMING_COMMIT_PHASE_BEGIN(phase) Bridge_TimeCommitProbePhaseBegin(phase)
#define TIMING_COMMIT_PHASE_END(phase) Bridge_TimeCommitProbePhaseEnd(phase)
#else
#define TIMING_COMMIT_PHASE_BEGIN(phase) ((void)0)
#define TIMING_COMMIT_PHASE_END(phase) ((void)0)
#endif

uint8_t UI_TimingCommit_Apply(const UiTimingValues* draft)
{
    if (!draft) return 0u;

    const uint8_t loop_changed = draft->step_count != Bridge_GetPatternStepCount();
    const uint8_t division_changed = draft->step_division != Bridge_GetPatternStepDivision();
    const uint8_t time_sig_changed = draft->ts_num != Bridge_GetTimeSigNumerator() ||
                                     draft->ts_den != Bridge_GetTimeSigDenominator();
    const uint8_t swing_changed = draft->swing != Bridge_GetSwing();

    if (!loop_changed && !division_changed && !time_sig_changed && !swing_changed) return 1u;

    /* Coalesces all Pattern setters below into a single EDIT generation. */
    Bridge_PersistBegin();

    /* The only fallible operations run first so a rejection leaves every field unchanged. */
    uint8_t accepted = 1u;
    if (time_sig_changed || division_changed)
    {
        TIMING_COMMIT_PHASE_BEGIN(TIME_COMMIT_PROBE_PHASE_TIMING);
        accepted = time_sig_changed
            ? Bridge_SetPatternTiming(draft->step_division, draft->ts_num, draft->ts_den)
            : Bridge_SetPatternStepDivision(draft->step_division);
        TIMING_COMMIT_PHASE_END(TIME_COMMIT_PROBE_PHASE_TIMING);
    }

    if (accepted && loop_changed)
    {
        TIMING_COMMIT_PHASE_BEGIN(TIME_COMMIT_PROBE_PHASE_LOOP);
        Bridge_SetPatternStepCount(draft->step_count);
        TIMING_COMMIT_PHASE_END(TIME_COMMIT_PROBE_PHASE_LOOP);
    }

    if (accepted && swing_changed)
    {
        TIMING_COMMIT_PHASE_BEGIN(TIME_COMMIT_PROBE_PHASE_SWING);
        Bridge_SetSwing(draft->swing);
        TIMING_COMMIT_PHASE_END(TIME_COMMIT_PROBE_PHASE_SWING);
    }

    TIMING_COMMIT_PHASE_BEGIN(TIME_COMMIT_PROBE_PHASE_PERSIST_END);
    Bridge_PersistEnd();
    TIMING_COMMIT_PHASE_END(TIME_COMMIT_PROBE_PHASE_PERSIST_END);
    return accepted;
}
