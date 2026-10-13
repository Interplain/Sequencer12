#include "stm32/time_commit_probe.h"

#if defined(S12_TIME_COMMIT_PROBE)

#include "stm32f4xx_hal.h"
#include "platform/fram/pattern_residency.h"
#include <cstring>

volatile TransportProbeRecord g_transport_probe_records[TRANSPORT_PROBE_RECORD_COUNT];
volatile uint32_t g_transport_probe_count = 0u;

volatile TimeCommitProbeRecord g_time_commit_probe_records[TIME_COMMIT_PROBE_RECORD_COUNT];
volatile uint32_t g_time_commit_probe_record_count = 0u;
volatile uint32_t g_time_commit_probe_active_slot = 0xFFFFFFFFu;
volatile MainGridProbeStats g_main_grid_probe;
volatile PianoFrameProbeStats g_piano_frame_probe;
volatile TimingFrameProbeStats g_timing_frame_probe;
volatile DirectChordProbeStats g_direct_chord_probe;
volatile LongPassRecord g_long_pass_records[LONG_PASS_RECORD_COUNT];
volatile uint32_t g_long_pass_count = 0u;

namespace
{

LongPassRecord s_pass;
uint8_t s_pass_active = 0u;
uint8_t s_pass_phase = LONG_PASS_PHASE_OTHER;
uint32_t s_pass_phase_start = 0u;
uint32_t s_pass_service_steps_begin = 0u;
uint8_t s_read_sources[4];
uint8_t s_read_depth = 0u;

/* Latest UI context/events, sampled into each pass record. */
uint8_t s_ui_mode = 0u;
uint8_t s_ui_selected = 0u;
uint8_t s_ui_active = 0u;
uint8_t s_ui_modal = 0u;
char s_ui_screen[8];
uint8_t s_ui_last_event_type = 0u;
int16_t s_ui_last_event_value = 0;
uint32_t s_ui_last_event_pass = 0u;
uint8_t s_ui_last_matrix = 0u;
uint8_t s_ui_last_encoder_type = 0u;
int16_t s_ui_last_encoder_value = 0;

void PassRecordRead(uint32_t address, uint16_t length)
{
    if (!s_pass_active) return;
    ++s_pass.fram_read_calls;
    s_pass.fram_read_bytes += length;
    if (address >= 0x8000u && address < 0x1C800u)
    {
        ++s_pass.ps_read_calls;
        s_pass.ps_read_bytes += length;
    }
    const uint8_t depth = (s_read_depth > 4u) ? 4u : s_read_depth;
    const uint8_t outer = depth ? s_read_sources[0] : (uint8_t)READ_SOURCE_UNTAGGED;
    const uint8_t inner = depth ? s_read_sources[depth - 1u] : (uint8_t)READ_SOURCE_UNTAGGED;
    if (s_pass.fram_read_calls == 1u)
    {
        s_pass.first_read_outer = outer;
        s_pass.first_read_inner = inner;
        s_pass.first_read_phase = s_pass_phase;
    }
    ++s_pass.reads_by_phase[s_pass_phase];
    ++s_pass.reads_by_outer_source[outer & (READ_SOURCE_COUNT - 1u)];
    ++s_pass.reads_by_inner_source[inner & (READ_SOURCE_COUNT - 1u)];
}
uint32_t s_loop_start_cycles = 0u;
uint8_t s_grid_transition_active = 0u;
uint32_t s_grid_transition_cycles = 0u;
uint32_t s_grid_transition_lcd_base = 0u;
uint32_t s_grid_transition_box_base = 0u;
uint32_t s_grid_transition_full_base = 0u;
uint8_t s_frame_active = 0u;
uint8_t s_frame_pass = 0u;
uint32_t s_frame_begin_cycles = 0u;
uint32_t s_frame_strips = 0u;
uint32_t s_strip_begin_cycles = 0u;
uint32_t s_piano_draw_cycles = 0u;
uint32_t s_piano_draw_lcd_base = 0u;
uint8_t s_piano_frame_active = 0u;
uint8_t s_piano_frame_pass = 0u;
uint32_t s_piano_frame_cycles = 0u;
uint32_t s_piano_frame_jobs = 0u;
uint32_t s_piano_frame_bytes = 0u;
uint32_t s_piano_job_cycles = 0u;
uint32_t s_piano_return_cycles = 0u;
uint32_t s_piano_return_frames = 0u;
uint8_t s_timing_active = 0u, s_timing_pass = 0u;
uint8_t s_direct_probe_active = 0u, s_direct_probe_pass = 0u, s_direct_probe_category = 0u;
uint32_t s_direct_probe_start = 0u, s_direct_probe_bytes = 0u, s_direct_probe_job = 0u, s_direct_probe_jobs = 0u;
uint32_t s_timing_start = 0u, s_timing_bytes = 0u, s_timing_jobs = 0u, s_timing_job_start = 0u;
volatile TransportProbeRecord* s_transport = nullptr;
uint32_t s_transport_bridge_cycles = 0u;
uint32_t s_transport_device_cycles = 0u;
uint32_t s_transport_ensure_cycles = 0u;
uint32_t s_transport_wait_cycles = 0u;
uint32_t s_phase_start_cycles = 0u;
uint32_t s_phase_start_ms = 0u;
uint32_t s_commit_start_cycles = 0u;
uint8_t s_phase_active = 0xFFu;
uint32_t s_catchup_service_steps_start = 0u;
uint32_t s_writeback_start_ms = 0u;
uint8_t s_writeback_slot = 0xFFu;
uint8_t s_pattern_read_active = 0u;
uint8_t s_post_slot = 0xFFu;
uint8_t s_post_service_base_valid = 0u;
uint32_t s_post_start_ms = 0u;
uint32_t s_post_last_callbacks = 0u;
uint32_t s_post_last_callback_ms = 0u;
uint32_t s_post_service_base = 0u;

volatile TimeCommitProbeRecord* PostRecord(void)
{
    return s_post_slot < TIME_COMMIT_PROBE_RECORD_COUNT ? &g_time_commit_probe_records[s_post_slot] : nullptr;
}

void EnableCycleCounter(void)
{
    if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0u) return;
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0u;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

volatile TimeCommitProbeRecord* ActiveRecord(void)
{
    const uint32_t slot = g_time_commit_probe_active_slot;
    return slot < TIME_COMMIT_PROBE_RECORD_COUNT ? &g_time_commit_probe_records[slot] : nullptr;
}

volatile TimeCommitProbeRecord* LatestRecord(void)
{
    if (g_time_commit_probe_record_count == 0u) return nullptr;
    const uint32_t slot = (g_time_commit_probe_record_count - 1u) % TIME_COMMIT_PROBE_RECORD_COUNT;
    return &g_time_commit_probe_records[slot];
}

}

extern "C" void TimeCommitProbe_Begin(uint32_t flags,
                                      uint32_t callbacks,
                                      uint32_t pending_ticks,
                                      uint32_t pending_steps,
                                      uint32_t service_steps)
{
    EnableCycleCounter();
    const uint32_t sequence = g_time_commit_probe_record_count++;
    const uint32_t slot = sequence % TIME_COMMIT_PROBE_RECORD_COUNT;
    volatile TimeCommitProbeRecord& record = g_time_commit_probe_records[slot];
    memset((void*)&record, 0, sizeof(record));
    record.sequence = sequence + 1u;
    record.flags = flags;
    record.start_ms = HAL_GetTick();
    s_commit_start_cycles = DWT->CYCCNT;
    record.callbacks_start = callbacks;
    record.pending_ticks_start = pending_ticks;
    record.pending_steps_start = pending_steps;
    s_catchup_service_steps_start = service_steps;
    record.writeback_complete = 1u;
    record.catchup_complete = 0u;
    g_time_commit_probe_active_slot = slot;
    s_phase_active = 0xFFu;
}

extern "C" void TimeCommitProbe_PhaseBegin(uint8_t phase)
{
    if (!ActiveRecord() || phase >= 5u) return;
    s_phase_active = phase;
    s_phase_start_cycles = DWT->CYCCNT;
    s_phase_start_ms = HAL_GetTick();
}

extern "C" void TimeCommitProbe_PhaseEnd(uint8_t phase)
{
    volatile TimeCommitProbeRecord* record = ActiveRecord();
    if (!record || phase >= 5u || phase != s_phase_active) return;
    record->phase_cycles[phase] += DWT->CYCCNT - s_phase_start_cycles;
    record->phase_ms[phase] += HAL_GetTick() - s_phase_start_ms;
    s_phase_active = 0xFFu;
}

extern "C" void TimeCommitProbe_End(uint32_t callbacks,
                                    uint32_t pending_ticks,
                                    uint32_t pending_steps)
{
    volatile TimeCommitProbeRecord* record = ActiveRecord();
    if (!record) return;
    if (s_phase_active < 5u) TimeCommitProbe_PhaseEnd(s_phase_active);
    record->end_ms = HAL_GetTick();
    record->elapsed_ms = record->end_ms - record->start_ms;
    record->elapsed_cycles = DWT->CYCCNT - s_commit_start_cycles;
    record->callbacks_end = callbacks;
    record->callbacks_delta = callbacks - record->callbacks_start;
    record->pending_ticks_end = pending_ticks;
    record->pending_ticks_delta = pending_ticks - record->pending_ticks_start;
    record->pending_steps_end = pending_steps;
    record->complete = 1u;
    record->catchup_peak_pending_ticks = pending_ticks;
    record->catchup_peak_pending_steps = pending_steps;
    record->catchup_complete = (pending_ticks == 0u && pending_steps == 0u) ? 1u : 0u;
    record->catchup_elapsed_ms = 0u;
    record->catchup_service_calls = 0u;
    record->catchup_steps_serviced = 0u;
    g_time_commit_probe_active_slot = 0xFFFFFFFFu;

    s_post_slot = (uint8_t)(record - g_time_commit_probe_records);
    s_post_start_ms = record->end_ms;
    s_post_last_callbacks = callbacks;
    s_post_last_callback_ms = record->end_ms;
    s_post_service_base_valid = 0u;
}

extern "C" void TimeCommitProbe_ObserveService(uint32_t callbacks,
                                               uint32_t pending_ticks,
                                               uint32_t pending_steps,
                                               uint32_t service_steps)
{
    volatile TimeCommitProbeRecord* record = LatestRecord();
    if (!record || !record->complete || record->catchup_complete) return;
    ++record->catchup_service_calls;
    if (pending_ticks > record->catchup_peak_pending_ticks)
        record->catchup_peak_pending_ticks = pending_ticks;
    if (pending_steps > record->catchup_peak_pending_steps)
        record->catchup_peak_pending_steps = pending_steps;
    if (service_steps >= s_catchup_service_steps_start)
        record->catchup_steps_serviced = service_steps - s_catchup_service_steps_start;
    (void)callbacks;
    if (pending_ticks == 0u && pending_steps == 0u)
    {
        record->catchup_elapsed_ms = HAL_GetTick() - record->end_ms;
        record->catchup_complete = 1u;
    }
}

extern "C" void TimeCommitProbe_RecordDirectory(uint8_t was_unknown, uint8_t is_valid)
{
    if (s_pass_active)
    {
        ++s_pass.dir_lookups;
        if (was_unknown) ++s_pass.dir_unknown;
    }
    volatile TimeCommitProbeRecord* record = ActiveRecord();
    if (!record) return;
    ++record->directory_lookups;
    if (was_unknown) ++record->directory_unknown;
    if (!is_valid) ++record->directory_invalid;
}

extern "C" void TimeCommitProbe_RecordPatternReadBegin(uint32_t address,
                                                       uint16_t length)
{
    volatile TimeCommitProbeRecord* record = ActiveRecord();
    s_pattern_read_active = 0u;
    PassRecordRead(address, length);
    if (s_transport)
    {
        ++s_transport->fram_read_calls;
        s_transport->fram_read_bytes += length;
        if (address >= 0x8000u && address < 0x1C800u)
        {
            ++s_transport->ps_read_calls;
            s_transport->ps_read_bytes += length;
        }
    }
    volatile TimeCommitProbeRecord* post = PostRecord();
    if (post)
    {
        ++post->post_fram_read_calls;
        post->post_fram_read_bytes += length;
    }
    if (!record) return;
    if (address >= 0x8000u && address < 0x1C800u)
    {
        const uint32_t pattern_index = (address - 0x8000u) / 0xA40u;
        s_pattern_read_active = 1u;
        ++record->patternstore_read_calls;
        record->patternstore_read_bytes += length;
        if (pattern_index < 32u) ++record->pattern_reads_by_index[pattern_index];
    }
}

extern "C" void TimeCommitProbe_RecordPatternReadEnd(uint8_t ok)
{
    volatile TimeCommitProbeRecord* record = ActiveRecord();
    if (record && s_pattern_read_active && !ok) ++record->patternstore_read_errors;
    s_pattern_read_active = 0u;
}

extern "C" void TimeCommitProbe_RecordPatternWrite(uint32_t address,
                                                    uint16_t length,
                                                    uint8_t ok)
{
    volatile TimeCommitProbeRecord* record = ActiveRecord();
    volatile TimeCommitProbeRecord* post = PostRecord();
    if (s_transport) ++s_transport->fram_write_calls;
    if (s_pass_active)
    {
        ++s_pass.fram_write_calls;
        s_pass.fram_write_bytes += length;
        if (address >= 0x8000u && address < 0x1C800u)
        {
            ++s_pass.ps_write_calls;
            s_pass.ps_write_bytes += length;
        }
    }
    if (post)
    {
        ++post->post_fram_write_calls;
        post->post_fram_write_bytes += length;
    }
    if (address < 0x8000u || address >= 0x1C800u) return;
    if (record)
    {
        ++record->fram_write_calls;
        record->fram_write_bytes += length;
        if (!ok) ++record->fram_write_errors;
    }
    if (s_writeback_slot < TIME_COMMIT_PROBE_RECORD_COUNT)
    {
        volatile TimeCommitProbeRecord& writeback = g_time_commit_probe_records[s_writeback_slot];
        ++writeback.writeback_fram_write_calls;
        writeback.writeback_fram_write_bytes += length;
    }
}

extern "C" void TimeCommitProbe_RecordCrcBytes(uint16_t length)
{
    volatile TimeCommitProbeRecord* record = ActiveRecord();
    if (record) record->crc_scan_bytes += length;
}

extern "C" void TimeCommitProbe_RecordHal(uint8_t receive,
                                           uint16_t length,
                                           uint32_t timeout_ms,
                                           uint8_t ok)
{
    volatile TimeCommitProbeRecord* record = ActiveRecord();
    volatile TimeCommitProbeRecord* post = PostRecord();
    if (s_pass_active)
    {
        if (receive) ++s_pass.i2c_rx;
        else ++s_pass.i2c_tx;
        if (!ok) ++s_pass.i2c_failures;
        if (timeout_ms > s_pass.i2c_timeout_ms_max) s_pass.i2c_timeout_ms_max = timeout_ms;
    }
    if (s_transport)
    {
        if (receive) ++s_transport->hal_rx_calls;
        else ++s_transport->hal_tx_calls;
    }
    if (post)
    {
        if (receive) ++post->post_hal_rx_calls;
        else ++post->post_hal_tx_calls;
    }
    if (record)
    {
        if (receive)
        {
            ++record->hal_rx_calls;
            record->hal_rx_timeout_sum_ms += timeout_ms;
            if (timeout_ms > record->hal_rx_timeout_max_ms)
                record->hal_rx_timeout_max_ms = timeout_ms;
        }
        else
        {
            ++record->hal_tx_calls;
            record->hal_tx_timeout_sum_ms += timeout_ms;
            if (timeout_ms > record->hal_tx_timeout_max_ms)
                record->hal_tx_timeout_max_ms = timeout_ms;
        }
        if (!ok) ++record->hal_failures;
        (void)length;
    }
    if (s_writeback_slot < TIME_COMMIT_PROBE_RECORD_COUNT)
    {
        volatile TimeCommitProbeRecord& writeback = g_time_commit_probe_records[s_writeback_slot];
        if (receive) ++writeback.writeback_hal_rx_calls;
        else ++writeback.writeback_hal_tx_calls;
    }
}

extern "C" void TimeCommitProbe_RecordI2cRecovery(void)
{
    if (s_pass_active) ++s_pass.i2c_recoveries;
    if (s_transport) ++s_transport->i2c_recoveries;
    volatile TimeCommitProbeRecord* record = ActiveRecord();
    if (record) ++record->i2c_recoveries;
}

extern "C" void TimeCommitProbe_WritebackStart(void)
{
    if (s_writeback_slot < TIME_COMMIT_PROBE_RECORD_COUNT) return;
    if (g_time_commit_probe_record_count == 0u) return;
    s_writeback_slot = (uint8_t)((g_time_commit_probe_record_count - 1u) % TIME_COMMIT_PROBE_RECORD_COUNT);
    s_writeback_start_ms = HAL_GetTick();
    volatile TimeCommitProbeRecord& record = g_time_commit_probe_records[s_writeback_slot];
    record.writeback_complete = 0u;
}

extern "C" void TimeCommitProbe_WritebackService(uint8_t dirty_or_busy)
{
    if (s_writeback_slot >= TIME_COMMIT_PROBE_RECORD_COUNT) return;
    volatile TimeCommitProbeRecord& record = g_time_commit_probe_records[s_writeback_slot];
    ++record.writeback_service_calls;
    if (!dirty_or_busy)
    {
        record.writeback_elapsed_ms = HAL_GetTick() - s_writeback_start_ms;
        record.writeback_complete = 1u;
        s_writeback_slot = 0xFFu;
    }
}

extern "C" void TimeCommitProbe_SetEditGeneration(uint32_t generation)
{
    volatile TimeCommitProbeRecord* record = ActiveRecord();
    if (!record) return;
    if (record->edit_generation_start == 0u) record->edit_generation_start = generation;
    else record->edit_generation_end = generation;
}

extern "C" void TimeCommitProbe_RecordV3Save(uint32_t elapsed_ms)
{
    if (s_pass_active)
    {
        ++s_pass.v3_saves;
        if (elapsed_ms > s_pass.v3_ms_max) s_pass.v3_ms_max = elapsed_ms;
    }
    volatile TimeCommitProbeRecord* record = ActiveRecord();
    if (record)
    {
        ++record->v3_save_calls;
        if (elapsed_ms > record->v3_save_ms_max) record->v3_save_ms_max = elapsed_ms;
    }
    volatile TimeCommitProbeRecord* post = PostRecord();
    if (post)
    {
        ++post->post_v3_save_calls;
        if (elapsed_ms > post->post_v3_save_ms_max) post->post_v3_save_ms_max = elapsed_ms;
    }
}

extern "C" void TimeCommitProbe_LoopSample(uint32_t pending_ticks, uint32_t pending_steps)
{
    EnableCycleCounter();
    if (g_main_grid_probe.reset_request)
        memset((void*)&g_main_grid_probe, 0, sizeof(g_main_grid_probe));
    s_loop_start_cycles = DWT->CYCCNT;
    s_direct_probe_pass = s_direct_probe_active;
    s_timing_pass = s_timing_active;
    if (s_timing_active)
    {
        if (pending_ticks > g_timing_frame_probe.max_pending_ticks)
            g_timing_frame_probe.max_pending_ticks = pending_ticks;
        if (pending_steps > g_timing_frame_probe.max_pending_steps)
            g_timing_frame_probe.max_pending_steps = pending_steps;
    }
    s_piano_frame_pass = s_piano_frame_active;
    if (s_piano_frame_active)
    {
        if (pending_ticks > g_piano_frame_probe.max_pending_ticks)
            g_piano_frame_probe.max_pending_ticks = pending_ticks;
        if (pending_steps > g_piano_frame_probe.max_pending_steps)
            g_piano_frame_probe.max_pending_steps = pending_steps;
    }
    if (pending_ticks > g_main_grid_probe.loop_max_pending_ticks)
        g_main_grid_probe.loop_max_pending_ticks = pending_ticks;
    if (pending_steps > g_main_grid_probe.loop_max_pending_steps)
        g_main_grid_probe.loop_max_pending_steps = pending_steps;
    s_frame_pass = s_frame_active;
    if (s_frame_active)
    {
        if (pending_ticks > g_main_grid_probe.frame_max_pending_ticks)
            g_main_grid_probe.frame_max_pending_ticks = pending_ticks;
        if (pending_steps > g_main_grid_probe.frame_max_pending_steps)
            g_main_grid_probe.frame_max_pending_steps = pending_steps;
    }

    volatile TimeCommitProbeRecord* post = PostRecord();
    if (!post) return;
    if (pending_ticks > post->post_max_pending_ticks) post->post_max_pending_ticks = pending_ticks;
    if (pending_steps > post->post_max_pending_steps) post->post_max_pending_steps = pending_steps;
}

extern "C" void TimeCommitProbe_LoopPass(uint32_t loop_ms,
                                         uint32_t musical_ms,
                                         uint32_t input_ms,
                                         uint32_t ui_ms,
                                         uint32_t persist_ms,
                                         uint32_t callbacks,
                                         uint32_t service_steps,
                                         uint8_t boundary_held,
                                         uint32_t edit_generation)
{
    const uint32_t loop_cycles = DWT->CYCCNT - s_loop_start_cycles;
    ++g_main_grid_probe.loop_passes;
    if (s_direct_probe_pass && loop_cycles > g_direct_chord_probe.max_loop_cycles)
        g_direct_chord_probe.max_loop_cycles = loop_cycles;
    s_direct_probe_pass = 0u;
    if (s_timing_pass && loop_cycles > g_timing_frame_probe.max_loop_cycles)
        g_timing_frame_probe.max_loop_cycles = loop_cycles;
    s_timing_pass = 0u;
    if (s_piano_frame_pass && loop_cycles > g_piano_frame_probe.max_loop_cycles)
        g_piano_frame_probe.max_loop_cycles = loop_cycles;
    s_piano_frame_pass = 0u;
    if (loop_cycles > g_main_grid_probe.loop_max_cycles)
        g_main_grid_probe.loop_max_cycles = loop_cycles;
    if (s_frame_pass && loop_cycles > g_main_grid_probe.frame_max_loop_cycles)
        g_main_grid_probe.frame_max_loop_cycles = loop_cycles;
    s_frame_pass = 0u;

    volatile TimeCommitProbeRecord* post = PostRecord();
    if (!post) return;
    const uint32_t now = HAL_GetTick();
    const uint32_t since_start = now - s_post_start_ms;

    ++post->post_loop_passes;
    if (loop_ms > post->post_max_loop_ms)
    {
        post->post_max_loop_ms = loop_ms;
        post->post_max_loop_at_ms = since_start;
    }
    if (musical_ms > post->post_max_musical_ms) post->post_max_musical_ms = musical_ms;
    if (input_ms > post->post_max_input_ms) post->post_max_input_ms = input_ms;
    if (ui_ms > post->post_max_ui_ms) post->post_max_ui_ms = ui_ms;
    if (persist_ms > post->post_max_persist_ms) post->post_max_persist_ms = persist_ms;

    if (callbacks != s_post_last_callbacks)
    {
        const uint32_t gap = now - s_post_last_callback_ms;
        if (gap > post->post_max_callback_gap_ms)
        {
            post->post_max_callback_gap_ms = gap;
            post->post_max_callback_gap_at_ms = since_start;
        }
        s_post_last_callbacks = callbacks;
        s_post_last_callback_ms = now;
    }
    post->post_callbacks_delta = callbacks - post->callbacks_end;

    if (!s_post_service_base_valid)
    {
        s_post_service_base = service_steps;
        s_post_service_base_valid = 1u;
    }
    post->post_steps_serviced = service_steps - s_post_service_base;
    if (boundary_held) post->post_boundary_held_ms += loop_ms;
    post->edit_generation_post = edit_generation;

    if (since_start >= TIME_COMMIT_PROBE_POST_WINDOW_MS)
    {
        post->post_elapsed_ms = since_start;
        post->post_complete = 1u;
        s_post_slot = 0xFFu;
    }
}

extern "C" void TimeCommitProbe_RecordLcdBytes(uint32_t bytes)
{
    if (s_pass_active) s_pass.lcd_bytes += bytes;
    g_main_grid_probe.lcd_bytes_total += bytes;
    volatile TimeCommitProbeRecord* record = ActiveRecord();
    if (record) record->lcd_bytes += bytes;
    volatile TimeCommitProbeRecord* post = PostRecord();
    if (post) post->post_lcd_bytes += bytes;
}

extern "C" void TimeCommitProbe_RecordGridFull(void)
{
    if (s_pass_active) ++s_pass.full_redraws;
    ++g_main_grid_probe.full_redraws_total;
    volatile TimeCommitProbeRecord* record = ActiveRecord();
    if (record) ++record->grid_full_redraws;
    volatile TimeCommitProbeRecord* post = PostRecord();
    if (post) ++post->post_grid_full_redraws;
}

extern "C" void TimeCommitProbe_RecordGridBox(void)
{
    if (s_pass_active) ++s_pass.boxes;
    ++g_main_grid_probe.box_redraws_total;
    volatile TimeCommitProbeRecord* record = ActiveRecord();
    if (record) ++record->grid_box_redraws;
    volatile TimeCommitProbeRecord* post = PostRecord();
    if (post) ++post->post_grid_box_redraws;
}

extern "C" void TimeCommitProbe_GridTransitionBegin(void)
{
    EnableCycleCounter();
    s_grid_transition_active = 1u;
    s_grid_transition_cycles = DWT->CYCCNT;
    s_grid_transition_lcd_base = g_main_grid_probe.lcd_bytes_total;
    s_grid_transition_box_base = g_main_grid_probe.box_redraws_total;
    s_grid_transition_full_base = g_main_grid_probe.full_redraws_total;
}

extern "C" void TimeCommitProbe_GridTransitionEnd(uint8_t flash_only)
{
    if (!s_grid_transition_active) return;
    s_grid_transition_active = 0u;
    volatile MainGridProbeStats& stats = g_main_grid_probe;
    const uint32_t cycles = DWT->CYCCNT - s_grid_transition_cycles;
    const uint32_t lcd_bytes = stats.lcd_bytes_total - s_grid_transition_lcd_base;
    const uint32_t boxes = stats.box_redraws_total - s_grid_transition_box_base;
    if (flash_only) ++stats.flash_only_transitions;
    else ++stats.transitions;
    stats.last_cycles = cycles;
    stats.last_lcd_bytes = lcd_bytes;
    stats.last_box_redraws = boxes;
    if (cycles > stats.max_cycles) stats.max_cycles = cycles;
    if (lcd_bytes > stats.max_lcd_bytes) stats.max_lcd_bytes = lcd_bytes;
    if (boxes > stats.max_box_redraws) stats.max_box_redraws = boxes;
    stats.full_redraws_in_transitions += stats.full_redraws_total - s_grid_transition_full_base;
}

extern "C" void TimeCommitProbe_FrameBegin(uint8_t restarted)
{
    EnableCycleCounter();
    ++g_main_grid_probe.frames_started;
    if (restarted) ++g_main_grid_probe.frames_restarted;
    s_frame_active = 1u;
    s_frame_begin_cycles = DWT->CYCCNT;
    s_frame_strips = 0u;
}

extern "C" void TimeCommitProbe_FrameStripBegin(void)
{
    s_frame_pass = 1u;
    s_strip_begin_cycles = DWT->CYCCNT;
}

extern "C" void TimeCommitProbe_FrameStripEnd(void)
{
    const uint32_t cycles = DWT->CYCCNT - s_strip_begin_cycles;
    if (s_pass_active) ++s_pass.strips;
    ++g_main_grid_probe.strips_total;
    ++s_frame_strips;
    g_main_grid_probe.last_strip_cycles = cycles;
    if (cycles > g_main_grid_probe.max_strip_cycles) g_main_grid_probe.max_strip_cycles = cycles;
}

extern "C" void TimeCommitProbe_FrameComplete(void)
{
    const uint32_t cycles = DWT->CYCCNT - s_frame_begin_cycles;
    ++g_main_grid_probe.frames_completed;
    g_main_grid_probe.last_frame_strips = s_frame_strips;
    g_main_grid_probe.last_frame_cycles = cycles;
    if (cycles > g_main_grid_probe.max_frame_cycles) g_main_grid_probe.max_frame_cycles = cycles;
    s_frame_active = 0u;
}

extern "C" void TimeCommitProbe_FrameCancel(void)
{
    ++g_main_grid_probe.frames_cancelled;
    g_main_grid_probe.last_frame_strips = s_frame_strips;
    s_frame_active = 0u;
}

extern "C" void TimeCommitProbe_FrameDeferred(void)
{
    ++g_main_grid_probe.deferred_updates;
}

extern "C" void TimeCommitProbe_PianoDrawBegin(void)
{
    EnableCycleCounter();
    s_piano_draw_cycles = DWT->CYCCNT;
    s_piano_draw_lcd_base = g_main_grid_probe.lcd_bytes_total;
}

extern "C" void TimeCommitProbe_PianoFrameBegin(uint8_t replaced)
{
    EnableCycleCounter();
    ++g_piano_frame_probe.frames_requested;
    if (replaced) ++g_piano_frame_probe.frames_replaced;
    s_piano_frame_active = 1u;
    s_piano_frame_cycles = DWT->CYCCNT;
    s_piano_frame_jobs = 0u;
    s_piano_frame_bytes = g_main_grid_probe.lcd_bytes_total;
}

extern "C" void TimeCommitProbe_TimingFrameBegin(uint8_t replaced)
{
    EnableCycleCounter();
    ++g_timing_frame_probe.requested;
    if (replaced) ++g_timing_frame_probe.replaced;
    s_timing_active = 1u;
    s_timing_start = DWT->CYCCNT;
    s_timing_bytes = g_main_grid_probe.lcd_bytes_total;
    s_timing_jobs = 0u;
}

extern "C" void TimeCommitProbe_DirectBegin(uint8_t category)
{
    EnableCycleCounter();
    s_direct_probe_category = category;
    s_direct_probe_active = 1u;
    s_direct_probe_start = DWT->CYCCNT;
    s_direct_probe_bytes = g_main_grid_probe.lcd_bytes_total;
    s_direct_probe_jobs = 0u;
    ++g_direct_chord_probe.regions[category];
    if (category == 0u) ++g_direct_chord_probe.full_frames;
}

extern "C" void TimeCommitProbe_DirectJobBegin(void)
{
    s_direct_probe_pass = 1u;
    s_direct_probe_job = DWT->CYCCNT;
}

extern "C" void TimeCommitProbe_DirectJobEnd(void)
{
    const uint32_t cycles = DWT->CYCCNT - s_direct_probe_job;
    ++g_direct_chord_probe.jobs;
    ++s_direct_probe_jobs;
    if (cycles > g_direct_chord_probe.max_job_cycles[s_direct_probe_category])
        g_direct_chord_probe.max_job_cycles[s_direct_probe_category] = cycles;
    if (s_pass_active) ++s_pass.strips;
}

extern "C" void TimeCommitProbe_DirectComplete(void)
{
    const uint32_t cycles = DWT->CYCCNT - s_direct_probe_start;
    const uint32_t bytes = g_main_grid_probe.lcd_bytes_total - s_direct_probe_bytes;
    g_direct_chord_probe.bytes[s_direct_probe_category] += bytes;
    if (cycles > g_direct_chord_probe.max_region_cycles[s_direct_probe_category])
        g_direct_chord_probe.max_region_cycles[s_direct_probe_category] = cycles;
    if (s_direct_probe_category == 0u)
    {
        g_direct_chord_probe.last_frame_jobs = s_direct_probe_jobs;
        g_direct_chord_probe.last_frame_bytes = bytes;
    }
    s_direct_probe_active = 0u;
}

extern "C" void TimeCommitProbe_DirectCancel(void)
{
    ++g_direct_chord_probe.cancelled;
    s_direct_probe_active = 0u;
}

extern "C" void TimeCommitProbe_TimingFrameComplete(void)
{
    const uint32_t cycles = DWT->CYCCNT - s_timing_start;
    ++g_timing_frame_probe.completed;
    g_timing_frame_probe.last_frame_jobs = s_timing_jobs;
    g_timing_frame_probe.last_frame_cycles = cycles;
    if (cycles > g_timing_frame_probe.max_frame_cycles) g_timing_frame_probe.max_frame_cycles = cycles;
    g_timing_frame_probe.last_frame_bytes = g_main_grid_probe.lcd_bytes_total - s_timing_bytes;
    s_timing_active = 0u;
}

extern "C" void TimeCommitProbe_TimingFrameCancel(void)
{
    ++g_timing_frame_probe.cancelled;
    s_timing_active = 0u;
}

extern "C" void TimeCommitProbe_TimingDeferred(uint8_t duplicate)
{
    ++g_timing_frame_probe.deferred;
    if (duplicate) ++g_timing_frame_probe.duplicates_suppressed;
}

extern "C" void TimeCommitProbe_TimingJobBegin(void)
{
    s_timing_pass = 1u;
    s_timing_job_start = DWT->CYCCNT;
}

extern "C" void TimeCommitProbe_TimingJobEnd(void)
{
    const uint32_t cycles = DWT->CYCCNT - s_timing_job_start;
    ++g_timing_frame_probe.jobs;
    ++s_timing_jobs;
    if (cycles > g_timing_frame_probe.max_job_cycles) g_timing_frame_probe.max_job_cycles = cycles;
    if (s_pass_active) ++s_pass.strips;
}

extern "C" void TimeCommitProbe_PianoReturnBegin(uint8_t source)
{
    EnableCycleCounter();
    if (source == 1u) ++g_piano_frame_probe.time_returns;
    else if (source == 2u) ++g_piano_frame_probe.arp_returns;
    else ++g_piano_frame_probe.other_returns;
    s_piano_return_cycles = DWT->CYCCNT;
    s_piano_return_frames = g_piano_frame_probe.frames_requested;
}

extern "C" void TimeCommitProbe_PianoReturnEnd(void)
{
    const uint32_t cycles = DWT->CYCCNT - s_piano_return_cycles;
    if (cycles > g_piano_frame_probe.max_return_cycles)
        g_piano_frame_probe.max_return_cycles = cycles;
    g_piano_frame_probe.last_return_frames =
        g_piano_frame_probe.frames_requested - s_piano_return_frames;
}

extern "C" void TimeCommitProbe_PianoFrameComplete(void)
{
    const uint32_t cycles = DWT->CYCCNT - s_piano_frame_cycles;
    ++g_piano_frame_probe.frames_completed;
    g_piano_frame_probe.last_frame_jobs = s_piano_frame_jobs;
    g_piano_frame_probe.last_frame_cycles = cycles;
    g_piano_frame_probe.last_frame_bytes = g_main_grid_probe.lcd_bytes_total - s_piano_frame_bytes;
    if (cycles > g_piano_frame_probe.max_frame_cycles) g_piano_frame_probe.max_frame_cycles = cycles;
    s_piano_frame_active = 0u;
}

extern "C" void TimeCommitProbe_PianoFrameCancel(void)
{
    ++g_piano_frame_probe.frames_cancelled;
    s_piano_frame_active = 0u;
}

extern "C" void TimeCommitProbe_PianoJobBegin(void)
{
    s_piano_frame_pass = 1u;
    s_piano_job_cycles = DWT->CYCCNT;
}

extern "C" void TimeCommitProbe_PianoJobEnd(void)
{
    const uint32_t cycles = DWT->CYCCNT - s_piano_job_cycles;
    ++g_piano_frame_probe.jobs;
    ++s_piano_frame_jobs;
    if (cycles > g_piano_frame_probe.max_job_cycles) g_piano_frame_probe.max_job_cycles = cycles;
    if (s_pass_active) ++s_pass.strips;
}

extern "C" void TimeCommitProbe_PianoDrawEnd(void)
{
    volatile MainGridProbeStats& stats = g_main_grid_probe;
    const uint32_t cycles = DWT->CYCCNT - s_piano_draw_cycles;
    const uint32_t bytes = stats.lcd_bytes_total - s_piano_draw_lcd_base;
    if (s_pass_active)
    {
        ++s_pass.piano_draws;
        s_pass.piano_cycles += cycles;
    }
    ++stats.piano_draws;
    stats.piano_last_draw_cycles = cycles;
    stats.piano_last_draw_lcd_bytes = bytes;
    if (cycles > stats.piano_max_draw_cycles) stats.piano_max_draw_cycles = cycles;
    if (bytes > stats.piano_max_draw_lcd_bytes) stats.piano_max_draw_lcd_bytes = bytes;
}

static void CopyTransportState(volatile TransportProbeState* dst, const TransportProbeState* src)
{
    dst->edit_index = src->edit_index;
    dst->play_index = src->play_index;
    dst->play_role_pattern = src->play_role_pattern;
    dst->edit_role_pattern = src->edit_role_pattern;
    dst->next_role_pattern = src->next_role_pattern;
    dst->play_role_entry = src->play_role_entry;
    dst->edit_role_entry = src->edit_role_entry;
    dst->playing = src->playing;
    dst->callbacks = src->callbacks;
    dst->pending_ticks = src->pending_ticks;
    dst->pending_steps = src->pending_steps;
}

extern "C" void TransportProbe_Begin(uint8_t op, const TransportProbeState* state)
{
    EnableCycleCounter();
    const uint32_t sequence = g_transport_probe_count++;
    volatile TransportProbeRecord& record =
        g_transport_probe_records[sequence % TRANSPORT_PROBE_RECORD_COUNT];
    memset((void*)&record, 0, sizeof(record));
    record.sequence = sequence + 1u;
    record.op = op;
    record.ensure_pattern = TRANSPORT_PROBE_NO_PATTERN;
    record.entry_index = 0xFFFFFFFFu;
    if (state) CopyTransportState(&record.before, state);
    s_transport = &record;
    s_transport_bridge_cycles = DWT->CYCCNT;
}

extern "C" void TransportProbe_End(const TransportProbeState* state)
{
    if (!s_transport) return;
    s_transport->cycles_bridge = DWT->CYCCNT - s_transport_bridge_cycles;
    if (state) CopyTransportState(&s_transport->after, state);
    s_transport->complete = 1u;
    s_transport = nullptr;
}

extern "C" void TransportProbe_DeviceBegin(void)
{
    s_transport_device_cycles = DWT->CYCCNT;
}

extern "C" void TransportProbe_DeviceEnd(void)
{
    if (s_transport) s_transport->cycles_device = DWT->CYCCNT - s_transport_device_cycles;
}

extern "C" void TransportProbe_EnsureBegin(uint8_t pattern_index, uint8_t playing,
                                           uint32_t directory_generation)
{
    if (!s_transport) return;
    ++s_transport->ensure_calls;
    s_transport->ensure_pattern = pattern_index;
    s_transport->playing_at_ensure_entry = playing;
    s_transport->directory_generation = directory_generation;
    s_transport_ensure_cycles = DWT->CYCCNT;
}

extern "C" void TransportProbe_EnsureRequest(uint8_t request_status)
{
    if (!s_transport) return;
    s_transport->request_status = request_status;
    s_transport->resident = (request_status == PATTERN_CACHE_REQUEST_READY) ? 1u : 0u;
}

extern "C" void TransportProbe_EnsureWaitBegin(void)
{
    s_transport_wait_cycles = DWT->CYCCNT;
}

extern "C" void TransportProbe_EnsureWaitIteration(void)
{
    if (s_pass_active) ++s_pass.sync_wait_iterations;
    if (s_transport) ++s_transport->wait_iterations;
}

extern "C" void TransportProbe_EnsureWaitEnd(void)
{
    if (s_transport) s_transport->cycles_wait += DWT->CYCCNT - s_transport_wait_cycles;
}

extern "C" void TransportProbe_EnsureEnd(uint8_t ok, int8_t entry_index, uint32_t pending_ticks)
{
    if (!s_transport) return;
    s_transport->cycles_ensure = DWT->CYCCNT - s_transport_ensure_cycles;
    s_transport->ensure_ok = ok;
    s_transport->pending_ticks_at_ensure_end = pending_ticks;
    if (entry_index < 0) return;
    const PatternCacheEntry* entry = PatternResidency_GetCacheEntry((uint8_t)entry_index);
    if (!entry) return;
    s_transport->entry_index = (uint32_t)entry_index;
    s_transport->entry_pattern = entry->pattern_index;
    s_transport->entry_residency_state = entry->residency_state;
    s_transport->entry_io_state = entry->io_state;
    s_transport->entry_generation = entry->current_generation;
}

static void CopyScreenName(char* dst, const char* src)
{
    uint8_t i = 0u;
    for (; src && src[i] && i < 7u; ++i) dst[i] = src[i];
    for (; i < 8u; ++i) dst[i] = '\0';
}

extern "C" void TimeCommitProbe_SetUiContext(uint8_t mode, uint8_t selected, uint8_t active,
                                             uint8_t modal, const char* screen_name)
{
    s_ui_mode = mode;
    s_ui_selected = selected;
    s_ui_active = active;
    s_ui_modal = modal;
    CopyScreenName(s_ui_screen, screen_name);
}

extern "C" void TimeCommitProbe_UiEvent(uint8_t type, int16_t value)
{
    s_ui_last_event_type = type;
    s_ui_last_event_value = value;
    s_ui_last_event_pass = g_long_pass_count;
    if (type == UI_PROBE_EVENT_STEP || type == UI_PROBE_EVENT_SHIFT_STEP)
        s_ui_last_matrix = (uint8_t)value;
    if (type == UI_PROBE_EVENT_ENC_SHORT || type == UI_PROBE_EVENT_ENC_LONG ||
        type == UI_PROBE_EVENT_ENC_DELTA)
    {
        s_ui_last_encoder_type = type;
        s_ui_last_encoder_value = value;
    }
    if (!s_pass_active) return;
    if (s_pass.events_in_pass == 0u)
    {
        s_pass.first_event_type = type;
        s_pass.first_event_value = value;
    }
    if (s_pass.events_in_pass < 255u) ++s_pass.events_in_pass;
}

extern "C" void TimeCommitProbe_PushReadSource(uint8_t source)
{
    if (s_read_depth < 4u) s_read_sources[s_read_depth] = source;
    if (s_read_depth < 255u) ++s_read_depth;
}

extern "C" void TimeCommitProbe_PopReadSource(void)
{
    if (s_read_depth > 0u) --s_read_depth;
}

extern "C" void TimeCommitProbe_RecordResidencyLoad(void)
{
    if (s_pass_active) ++s_pass.residency_load_requests;
}

extern "C" void TimeCommitProbe_PassBegin(uint32_t callbacks, uint32_t pending_ticks,
                                          uint32_t pending_steps, uint32_t service_steps,
                                          uint8_t playing, uint8_t pattern, uint8_t play_pattern)
{
    EnableCycleCounter();
    memset(&s_pass, 0, sizeof(s_pass));
    s_pass.pass_number = ++g_long_pass_count;
    s_pass.start_cycles = DWT->CYCCNT;
    s_pass.mode_before = s_ui_mode;
    s_pass.selected_before = s_ui_selected;
    s_pass.active_before = s_ui_active;
    s_pass.modal_before = s_ui_modal;
    memcpy(s_pass.screen_before, s_ui_screen, sizeof(s_pass.screen_before));
    s_pass.playing_before = playing;
    s_pass.pattern_before = pattern;
    s_pass.play_pattern_before = play_pattern;
    s_pass.callbacks_begin = callbacks;
    s_pass.pending_ticks_begin = pending_ticks;
    s_pass.pending_steps_begin = pending_steps;
    s_pass_service_steps_begin = service_steps;
    s_pass_phase = LONG_PASS_PHASE_MUSICAL;
    s_pass_phase_start = s_pass.start_cycles;
    s_pass_active = 1u;
}

extern "C" void TimeCommitProbe_LoopMark(uint8_t phase)
{
    if (!s_pass_active || phase >= LONG_PASS_PHASE_COUNT) return;
    const uint32_t now = DWT->CYCCNT;
    s_pass.phase_cycles[s_pass_phase] += now - s_pass_phase_start;
    s_pass_phase = phase;
    s_pass_phase_start = now;
}

extern "C" void TimeCommitProbe_PassEnd(uint32_t callbacks, uint32_t pending_ticks,
                                        uint32_t pending_steps, uint32_t service_steps,
                                        uint8_t playing, uint8_t pattern, uint8_t play_pattern)
{
    if (!s_pass_active) return;
    const uint32_t now = DWT->CYCCNT;
    s_pass.phase_cycles[s_pass_phase] += now - s_pass_phase_start;
    s_pass_active = 0u;
    s_pass.total_cycles = now - s_pass.start_cycles;

    uint32_t accounted = 0u;
    for (uint8_t i = 0u; i < LONG_PASS_PHASE_OTHER; ++i) accounted += s_pass.phase_cycles[i];
    s_pass.phase_cycles[LONG_PASS_PHASE_OTHER] =
        (s_pass.total_cycles > accounted) ? s_pass.total_cycles - accounted : 0u;

    s_pass.mode_after = s_ui_mode;
    s_pass.selected_after = s_ui_selected;
    s_pass.active_after = s_ui_active;
    s_pass.modal_after = s_ui_modal;
    memcpy(s_pass.screen_after, s_ui_screen, sizeof(s_pass.screen_after));
    s_pass.playing_after = playing;
    s_pass.pattern_after = pattern;
    s_pass.play_pattern_after = play_pattern;
    s_pass.last_event_type = s_ui_last_event_type;
    s_pass.last_event_value = s_ui_last_event_value;
    s_pass.last_event_pass = s_ui_last_event_pass;
    s_pass.last_matrix_button = s_ui_last_matrix;
    s_pass.last_encoder_type = s_ui_last_encoder_type;
    s_pass.last_encoder_value = s_ui_last_encoder_value;
    s_pass.callbacks_end = callbacks;
    s_pass.pending_ticks_end = pending_ticks;
    s_pass.pending_steps_end = pending_steps;
    s_pass.steps_serviced = service_steps - s_pass_service_steps_begin;
    if (s_direct_probe_pass)
    {
        if (pending_ticks > g_direct_chord_probe.max_pending_ticks)
            g_direct_chord_probe.max_pending_ticks = pending_ticks;
        if (pending_steps > g_direct_chord_probe.max_pending_steps)
            g_direct_chord_probe.max_pending_steps = pending_steps;
    }
    if (s_timing_pass)
    {
        if (pending_ticks > g_timing_frame_probe.max_pending_ticks)
            g_timing_frame_probe.max_pending_ticks = pending_ticks;
        if (pending_steps > g_timing_frame_probe.max_pending_steps)
            g_timing_frame_probe.max_pending_steps = pending_steps;
    }
    if (s_piano_frame_pass)
    {
        if (pending_ticks > g_piano_frame_probe.max_pending_ticks)
            g_piano_frame_probe.max_pending_ticks = pending_ticks;
        if (pending_steps > g_piano_frame_probe.max_pending_steps)
            g_piano_frame_probe.max_pending_steps = pending_steps;
    }

    uint8_t slot = 0u;
    for (uint8_t i = 1u; i < LONG_PASS_RECORD_COUNT; ++i)
    {
        if (g_long_pass_records[i].total_cycles < g_long_pass_records[slot].total_cycles) slot = i;
    }
    if (s_pass.total_cycles > g_long_pass_records[slot].total_cycles)
    {
        memcpy((void*)&g_long_pass_records[slot], &s_pass, sizeof(s_pass));
    }
}

#endif