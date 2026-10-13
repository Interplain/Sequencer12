#ifndef UI_TIMING_COMMIT_H
#define UI_TIMING_COMMIT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint8_t step_count;
    uint8_t step_division;
    uint8_t ts_num;
    uint8_t ts_den;
    uint8_t swing;
} UiTimingValues;

/* Applies only the TIME draft fields that differ from the committed values.
 * Returns 0 if validation rejects the draft; nothing is applied in that case. */
uint8_t UI_TimingCommit_Apply(const UiTimingValues* draft);

#ifdef __cplusplus
}
#endif

#endif
