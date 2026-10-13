#ifndef TIME_COMMIT_PROBE_H
#define TIME_COMMIT_PROBE_H

#include <stdint.h>

#if defined(S12_TIME_COMMIT_PROBE)

#define TIME_COMMIT_PROBE_RECORD_COUNT 8u
#define TIME_COMMIT_PROBE_FLAG_PLAYING          (1u << 0)
#define TIME_COMMIT_PROBE_FLAG_TIMING_CHANGED   (1u << 1)
#define TIME_COMMIT_PROBE_FLAG_LOOP_CHANGED     (1u << 2)
#define TIME_COMMIT_PROBE_FLAG_SWING_CHANGED    (1u << 3)
#define TIME_COMMIT_PROBE_PHASE_TIMING          0u
#define TIME_COMMIT_PROBE_PHASE_LOOP            1u
#define TIME_COMMIT_PROBE_PHASE_SWING           2u
#define TIME_COMMIT_PROBE_PHASE_PERSIST_END     3u
#define TIME_COMMIT_PROBE_PHASE_EXIT_TRANSITION 4u

/* Bar/Piano Roll editor exit records share the same ring. */
#define TIME_COMMIT_PROBE_FLAG_BAR_EXIT         (1u << 8)
#define TIME_COMMIT_PROBE_FLAG_BAR_SAVE         (1u << 9)
#define TIME_COMMIT_PROBE_FLAG_BAR_TOUCHED      (1u << 10)
#define TIME_COMMIT_PROBE_FLAG_RESIDENCY_DIRTY  (1u << 11)
#define TIME_COMMIT_PROBE_FLAG_RESIDENCY_IO     (1u << 12)
#define TIME_COMMIT_PROBE_FLAG_V3_PENDING       (1u << 13)
#define TIME_COMMIT_PROBE_FLAG_PIANO_ENTER      (1u << 14)
#define TIME_COMMIT_PROBE_FLAG_BAR_SHIFT        16u
#define TIME_COMMIT_PROBE_PHASE_BAR_SESSION_END 0u
#define TIME_COMMIT_PROBE_PHASE_BAR_STATE_RESET 1u
#define TIME_COMMIT_PROBE_PHASE_BAR_ROUTER_POP  2u
#define TIME_COMMIT_PROBE_PHASE_BAR_FALLBACK    3u
#define TIME_COMMIT_PROBE_PHASE_PIANO_PREPARE   0u
#define TIME_COMMIT_PROBE_PHASE_PIANO_PUSH_DRAW 2u
#define TIME_COMMIT_PROBE_PHASE_PIANO_FALLBACK  3u
#define TIME_COMMIT_PROBE_POST_WINDOW_MS        3000u

typedef struct
{
    uint32_t sequence;
    uint32_t flags;
    uint32_t start_ms;
    uint32_t end_ms;
    uint32_t elapsed_ms;
    uint32_t elapsed_cycles;
    uint32_t phase_ms[5];
    uint32_t phase_cycles[5];
    uint32_t directory_lookups;
    uint32_t directory_unknown;
    uint32_t directory_invalid;
    uint32_t patternstore_read_calls;
    uint32_t patternstore_read_bytes;
    uint32_t patternstore_read_errors;
    uint32_t pattern_reads_by_index[32];
    uint32_t fram_write_calls;
    uint32_t fram_write_bytes;
    uint32_t fram_write_errors;
    uint32_t crc_scan_bytes;
    uint32_t hal_tx_calls;
    uint32_t hal_rx_calls;
    uint32_t hal_tx_timeout_sum_ms;
    uint32_t hal_rx_timeout_sum_ms;
    uint32_t hal_tx_timeout_max_ms;
    uint32_t hal_rx_timeout_max_ms;
    uint32_t hal_failures;
    uint32_t i2c_recoveries;
    uint32_t callbacks_start;
    uint32_t callbacks_end;
    uint32_t callbacks_delta;
    uint32_t pending_ticks_start;
    uint32_t pending_ticks_end;
    uint32_t pending_ticks_delta;
    uint32_t pending_steps_start;
    uint32_t pending_steps_end;
    uint32_t catchup_elapsed_ms;
    uint32_t catchup_service_calls;
    uint32_t catchup_steps_serviced;
    uint32_t catchup_peak_pending_ticks;
    uint32_t catchup_peak_pending_steps;
    uint32_t writeback_service_calls;
    uint32_t writeback_elapsed_ms;
    uint32_t writeback_hal_tx_calls;
    uint32_t writeback_hal_rx_calls;
    uint32_t writeback_fram_write_calls;
    uint32_t writeback_fram_write_bytes;
    uint32_t edit_generation_start;
    uint32_t edit_generation_end;
    uint32_t edit_generation_post;
    uint32_t v3_save_calls;
    uint32_t v3_save_ms_max;
    /* Main-loop observations for POST_WINDOW_MS after End(). */
    uint32_t post_elapsed_ms;
    uint32_t post_loop_passes;
    uint32_t post_max_loop_ms;
    uint32_t post_max_loop_at_ms;
    uint32_t post_max_musical_ms;
    uint32_t post_max_input_ms;
    uint32_t post_max_ui_ms;
    uint32_t post_max_persist_ms;
    uint32_t post_callbacks_delta;
    uint32_t post_max_callback_gap_ms;
    uint32_t post_max_callback_gap_at_ms;
    uint32_t post_max_pending_ticks;
    uint32_t post_max_pending_steps;
    uint32_t post_steps_serviced;
    uint32_t post_boundary_held_ms;
    uint32_t post_fram_read_calls;
    uint32_t post_fram_read_bytes;
    uint32_t post_fram_write_calls;
    uint32_t post_fram_write_bytes;
    uint32_t post_hal_tx_calls;
    uint32_t post_hal_rx_calls;
    uint32_t post_v3_save_calls;
    uint32_t post_v3_save_ms_max;
    uint32_t lcd_bytes;
    uint32_t grid_full_redraws;
    uint32_t grid_box_redraws;
    uint32_t post_lcd_bytes;
    uint32_t post_grid_full_redraws;
    uint32_t post_grid_box_redraws;
    uint8_t post_complete;
    uint8_t complete;
    uint8_t catchup_complete;
    uint8_t writeback_complete;
    uint8_t reserved;
} TimeCommitProbeRecord;

extern volatile TimeCommitProbeRecord g_time_commit_probe_records[TIME_COMMIT_PROBE_RECORD_COUNT];
extern volatile uint32_t g_time_commit_probe_record_count;
extern volatile uint32_t g_time_commit_probe_active_slot;

/* Main Grid playback-indicator stats; write reset_request=1 over SWD to clear. */
typedef struct
{
    uint32_t reset_request;
    uint32_t transitions;
    uint32_t flash_only_transitions;
    uint32_t last_cycles;
    uint32_t max_cycles;
    uint32_t last_lcd_bytes;
    uint32_t max_lcd_bytes;
    uint32_t last_box_redraws;
    uint32_t max_box_redraws;
    uint32_t full_redraws_in_transitions;
    uint32_t full_redraws_total;
    uint32_t box_redraws_total;
    uint32_t lcd_bytes_total;
    uint32_t loop_passes;
    uint32_t loop_max_cycles;
    uint32_t loop_max_pending_ticks;
    uint32_t loop_max_pending_steps;
    /* Sliced full-frame (Main Grid) jobs. */
    uint32_t frames_started;
    uint32_t frames_completed;
    uint32_t frames_cancelled;
    uint32_t frames_restarted;
    uint32_t strips_total;
    uint32_t last_frame_strips;
    uint32_t last_strip_cycles;
    uint32_t max_strip_cycles;
    uint32_t last_frame_cycles;
    uint32_t max_frame_cycles;
    uint32_t frame_max_loop_cycles;
    uint32_t frame_max_pending_ticks;
    uint32_t frame_max_pending_steps;
    uint32_t deferred_updates;
    /* Piano Roll on_draw. */
    uint32_t piano_draws;
    uint32_t piano_last_draw_cycles;
    uint32_t piano_max_draw_cycles;
    uint32_t piano_last_draw_lcd_bytes;
    uint32_t piano_max_draw_lcd_bytes;
} MainGridProbeStats;

extern volatile MainGridProbeStats g_main_grid_probe;

typedef struct
{
    uint32_t frames_requested;
    uint32_t frames_completed;
    uint32_t frames_cancelled;
    uint32_t frames_replaced;
    uint32_t jobs;
    uint32_t last_frame_jobs;
    uint32_t max_job_cycles;
    uint32_t last_frame_cycles;
    uint32_t max_frame_cycles;
    uint32_t last_frame_bytes;
    uint32_t max_loop_cycles;
    uint32_t max_pending_ticks;
    uint32_t max_pending_steps;
    uint32_t time_returns;
    uint32_t arp_returns;
    uint32_t other_returns;
    uint32_t max_return_cycles;
    uint32_t last_return_frames;
} PianoFrameProbeStats;
extern volatile PianoFrameProbeStats g_piano_frame_probe;

typedef struct
{
    uint32_t requested, completed, cancelled, replaced, deferred;
    uint32_t jobs, last_frame_jobs, max_job_cycles;
    uint32_t last_frame_cycles, max_frame_cycles, last_frame_bytes;
    uint32_t max_loop_cycles, max_pending_ticks, max_pending_steps;
    uint32_t duplicates_suppressed;
} TimingFrameProbeStats;
extern volatile TimingFrameProbeStats g_timing_frame_probe;

typedef struct
{
    /* entry, cursor, Root, Type, preview, status */
    uint32_t regions[6], bytes[6], max_job_cycles[6], max_region_cycles[6];
    uint32_t jobs, full_frames, cancelled, last_frame_jobs, last_frame_bytes;
    uint32_t max_loop_cycles, max_pending_ticks, max_pending_steps;
} DirectChordProbeStats;
extern volatile DirectChordProbeStats g_direct_chord_probe;

#define TRANSPORT_PROBE_RECORD_COUNT 8u
#define TRANSPORT_PROBE_OP_START     1u
#define TRANSPORT_PROBE_OP_RESET     2u
#define TRANSPORT_PROBE_NO_PATTERN   0xFFu

/* Engine/residency view sampled by the bridge before and after Start/Reset. */
typedef struct
{
    uint8_t edit_index;
    uint8_t play_index;
    uint8_t play_role_pattern;
    uint8_t edit_role_pattern;
    uint8_t next_role_pattern;
    uint8_t play_role_entry;
    uint8_t edit_role_entry;
    uint8_t playing;
    uint32_t callbacks;
    uint32_t pending_ticks;
    uint32_t pending_steps;
} TransportProbeState;

typedef struct
{
    uint32_t sequence;
    uint32_t op;
    TransportProbeState before;
    TransportProbeState after;
    uint32_t ensure_calls;
    uint32_t ensure_pattern;
    uint32_t ensure_ok;
    uint32_t playing_at_ensure_entry;
    uint32_t directory_generation;
    uint32_t request_status;
    uint32_t resident;
    uint32_t wait_iterations;
    uint32_t entry_index;
    uint32_t entry_pattern;
    uint32_t entry_residency_state;
    uint32_t entry_io_state;
    uint32_t entry_generation;
    uint32_t pending_ticks_at_ensure_end;
    uint32_t cycles_bridge;
    uint32_t cycles_device;
    uint32_t cycles_ensure;
    uint32_t cycles_wait;
    uint32_t ps_read_calls;
    uint32_t ps_read_bytes;
    uint32_t fram_read_calls;
    uint32_t fram_read_bytes;
    uint32_t fram_write_calls;
    uint32_t hal_tx_calls;
    uint32_t hal_rx_calls;
    uint32_t i2c_recoveries;
    uint32_t complete;
} TransportProbeRecord;

extern volatile TransportProbeRecord g_transport_probe_records[TRANSPORT_PROBE_RECORD_COUNT];
extern volatile uint32_t g_transport_probe_count;

/* Main-loop phases measured for every pass. */
#define LONG_PASS_PHASE_MUSICAL 0u
#define LONG_PASS_PHASE_INPUT   1u
#define LONG_PASS_PHASE_UI      2u
#define LONG_PASS_PHASE_PERSIST 3u
#define LONG_PASS_PHASE_DISPLAY 4u
#define LONG_PASS_PHASE_OTHER   5u
#define LONG_PASS_PHASE_COUNT   6u

/* Who asked for an FRAM read (outermost and innermost tag are both kept). */
#define READ_SOURCE_UNTAGGED            0u
#define READ_SOURCE_DIRECTORY_POPULATE  1u
#define READ_SOURCE_RESIDENCY_IO        2u
#define READ_SOURCE_PLAY_LOAD_SYNC      3u
#define READ_SOURCE_PATTERN_SELECT      4u
#define READ_SOURCE_TIME_TS_VALIDATION  5u
#define READ_SOURCE_NEXT_PREPARE        6u
#define READ_SOURCE_RESIDENCY_SERVICE   7u
#define READ_SOURCE_RESIDENCY_INIT      8u
#define READ_SOURCE_PERSIST_PATTERNSTORE 9u
#define READ_SOURCE_SONG_V3             10u
#define READ_SOURCE_USER_CHORDS         11u
#define READ_SOURCE_SETTINGS            12u
#define READ_SOURCE_PATTERN_CACHE_PILOT 13u
#define READ_SOURCE_COUNT               16u

#define UI_PROBE_EVENT_NONE        0u
#define UI_PROBE_EVENT_PLAY        1u
#define UI_PROBE_EVENT_SHIFT_PLAY  2u
#define UI_PROBE_EVENT_REC         3u
#define UI_PROBE_EVENT_SHIFT_REC   4u
#define UI_PROBE_EVENT_STEP        5u
#define UI_PROBE_EVENT_SHIFT_STEP  6u
#define UI_PROBE_EVENT_ENC_SHORT   7u
#define UI_PROBE_EVENT_ENC_LONG    8u
#define UI_PROBE_EVENT_ENC_DELTA   9u
#define UI_PROBE_EVENT_SHIFT_TAP   10u
#define UI_PROBE_EVENT_SS_WAKE     11u

#define LONG_PASS_RECORD_COUNT 8u

typedef struct
{
    uint32_t pass_number;
    uint32_t start_cycles;
    uint32_t total_cycles;
    uint32_t phase_cycles[LONG_PASS_PHASE_COUNT];
    /* Context: *_before is the state at pass start, *_after at pass end. */
    uint8_t mode_before;
    uint8_t mode_after;
    uint8_t selected_before;
    uint8_t selected_after;
    uint8_t active_before;
    uint8_t active_after;
    uint8_t modal_before;
    uint8_t modal_after;
    uint8_t playing_before;
    uint8_t playing_after;
    uint8_t pattern_before;
    uint8_t pattern_after;
    uint8_t play_pattern_before;
    uint8_t play_pattern_after;
    uint8_t events_in_pass;
    uint8_t reserved0;
    char screen_before[8];
    char screen_after[8];
    uint8_t first_event_type;
    uint8_t last_event_type;
    uint8_t last_matrix_button;
    uint8_t last_encoder_type;
    int16_t first_event_value;
    int16_t last_event_value;
    int16_t last_encoder_value;
    uint16_t reserved1;
    uint32_t last_event_pass;
    /* Storage. */
    uint32_t ps_read_calls;
    uint32_t ps_read_bytes;
    uint32_t ps_write_calls;
    uint32_t ps_write_bytes;
    uint32_t fram_read_calls;
    uint32_t fram_read_bytes;
    uint32_t fram_write_calls;
    uint32_t fram_write_bytes;
    uint32_t i2c_tx;
    uint32_t i2c_rx;
    uint32_t i2c_failures;
    uint32_t i2c_recoveries;
    uint32_t i2c_timeout_ms_max;
    uint32_t dir_lookups;
    uint32_t dir_unknown;
    uint32_t residency_load_requests;
    uint32_t sync_wait_iterations;
    uint32_t v3_saves;
    uint32_t v3_ms_max;
    uint16_t reads_by_phase[LONG_PASS_PHASE_COUNT];
    uint16_t reads_by_outer_source[READ_SOURCE_COUNT];
    uint16_t reads_by_inner_source[READ_SOURCE_COUNT];
    uint8_t first_read_outer;
    uint8_t first_read_inner;
    uint8_t first_read_phase;
    uint8_t reserved2;
    /* Playback. */
    uint32_t callbacks_begin;
    uint32_t callbacks_end;
    uint32_t pending_ticks_begin;
    uint32_t pending_ticks_end;
    uint32_t pending_steps_begin;
    uint32_t pending_steps_end;
    uint32_t steps_serviced;
    /* Display. */
    uint32_t lcd_bytes;
    uint32_t full_redraws;
    uint32_t strips;
    uint32_t boxes;
    uint32_t piano_draws;
    uint32_t piano_cycles;
} LongPassRecord;

extern volatile LongPassRecord g_long_pass_records[LONG_PASS_RECORD_COUNT];
extern volatile uint32_t g_long_pass_count;

#ifdef __cplusplus
extern "C" {
#endif

void TimeCommitProbe_Begin(uint32_t flags, uint32_t callbacks,
                           uint32_t pending_ticks, uint32_t pending_steps,
                           uint32_t service_steps);
void TimeCommitProbe_PhaseBegin(uint8_t phase);
void TimeCommitProbe_PhaseEnd(uint8_t phase);
void TimeCommitProbe_End(uint32_t callbacks, uint32_t pending_ticks,
                         uint32_t pending_steps);
void TimeCommitProbe_ObserveService(uint32_t callbacks, uint32_t pending_ticks,
                                    uint32_t pending_steps, uint32_t service_steps);
void TimeCommitProbe_RecordDirectory(uint8_t was_unknown, uint8_t is_valid);
void TimeCommitProbe_RecordPatternReadBegin(uint32_t address, uint16_t length);
void TimeCommitProbe_RecordPatternReadEnd(uint8_t ok);
void TimeCommitProbe_RecordPatternWrite(uint32_t address, uint16_t length, uint8_t ok);
void TimeCommitProbe_RecordCrcBytes(uint16_t length);
void TimeCommitProbe_RecordHal(uint8_t receive, uint16_t length,
                               uint32_t timeout_ms, uint8_t ok);
void TimeCommitProbe_RecordI2cRecovery(void);
void TimeCommitProbe_WritebackStart(void);
void TimeCommitProbe_WritebackService(uint8_t dirty_or_busy);
void TimeCommitProbe_SetEditGeneration(uint32_t generation);
void TimeCommitProbe_RecordV3Save(uint32_t elapsed_ms);
void TransportProbe_Begin(uint8_t op, const TransportProbeState* state);
void TransportProbe_End(const TransportProbeState* state);
void TransportProbe_DeviceBegin(void);
void TransportProbe_DeviceEnd(void);
void TransportProbe_EnsureBegin(uint8_t pattern_index, uint8_t playing,
                                uint32_t directory_generation);
void TransportProbe_EnsureRequest(uint8_t request_status);
void TransportProbe_EnsureWaitBegin(void);
void TransportProbe_EnsureWaitIteration(void);
void TransportProbe_EnsureWaitEnd(void);
void TransportProbe_EnsureEnd(uint8_t ok, int8_t entry_index, uint32_t pending_ticks);
void TimeCommitProbe_LoopSample(uint32_t pending_ticks, uint32_t pending_steps);
void TimeCommitProbe_LoopPass(uint32_t loop_ms, uint32_t musical_ms, uint32_t input_ms,
                              uint32_t ui_ms, uint32_t persist_ms, uint32_t callbacks,
                              uint32_t service_steps, uint8_t boundary_held,
                              uint32_t edit_generation);
void TimeCommitProbe_RecordLcdBytes(uint32_t bytes);
void TimeCommitProbe_RecordGridFull(void);
void TimeCommitProbe_RecordGridBox(void);
void TimeCommitProbe_GridTransitionBegin(void);
void TimeCommitProbe_GridTransitionEnd(uint8_t flash_only);
void TimeCommitProbe_FrameBegin(uint8_t restarted);
void TimeCommitProbe_FrameStripBegin(void);
void TimeCommitProbe_FrameStripEnd(void);
void TimeCommitProbe_FrameComplete(void);
void TimeCommitProbe_FrameCancel(void);
void TimeCommitProbe_FrameDeferred(void);
void TimeCommitProbe_PianoDrawBegin(void);
void TimeCommitProbe_PianoDrawEnd(void);
void TimeCommitProbe_PianoFrameBegin(uint8_t replaced);
void TimeCommitProbe_PianoFrameComplete(void);
void TimeCommitProbe_PianoFrameCancel(void);
void TimeCommitProbe_PianoJobBegin(void);
void TimeCommitProbe_PianoJobEnd(void);
void TimeCommitProbe_PianoReturnBegin(uint8_t source);
void TimeCommitProbe_PianoReturnEnd(void);
void TimeCommitProbe_TimingFrameBegin(uint8_t replaced);
void TimeCommitProbe_TimingFrameComplete(void);
void TimeCommitProbe_TimingFrameCancel(void);
void TimeCommitProbe_TimingDeferred(uint8_t duplicate);
void TimeCommitProbe_TimingJobBegin(void);
void TimeCommitProbe_TimingJobEnd(void);
void TimeCommitProbe_DirectBegin(uint8_t category);
void TimeCommitProbe_DirectJobBegin(void);
void TimeCommitProbe_DirectJobEnd(void);
void TimeCommitProbe_DirectComplete(void);
void TimeCommitProbe_DirectCancel(void);
void TimeCommitProbe_PassBegin(uint32_t callbacks, uint32_t pending_ticks, uint32_t pending_steps,
                               uint32_t service_steps, uint8_t playing, uint8_t pattern,
                               uint8_t play_pattern);
void TimeCommitProbe_PassEnd(uint32_t callbacks, uint32_t pending_ticks, uint32_t pending_steps,
                             uint32_t service_steps, uint8_t playing, uint8_t pattern,
                             uint8_t play_pattern);
void TimeCommitProbe_LoopMark(uint8_t phase);
void TimeCommitProbe_SetUiContext(uint8_t mode, uint8_t selected, uint8_t active,
                                  uint8_t modal, const char* screen_name);
void TimeCommitProbe_UiEvent(uint8_t type, int16_t value);
void TimeCommitProbe_PushReadSource(uint8_t source);
void TimeCommitProbe_PopReadSource(void);
void TimeCommitProbe_RecordResidencyLoad(void);

#ifdef __cplusplus
}

struct ProbeReadSourceScope
{
    explicit ProbeReadSourceScope(uint8_t source) { TimeCommitProbe_PushReadSource(source); }
    ~ProbeReadSourceScope() { TimeCommitProbe_PopReadSource(); }
};
#define PROBE_READ_SOURCE(source) ProbeReadSourceScope probe_read_source_scope(source)
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
}
#endif

#else

#define TimeCommitProbe_Begin(flags, callbacks, ticks, steps, service_steps) ((void)0)
#define TimeCommitProbe_PhaseBegin(phase) ((void)0)
#define TimeCommitProbe_PhaseEnd(phase) ((void)0)
#define TimeCommitProbe_End(callbacks, ticks, steps) ((void)0)
#define TimeCommitProbe_ObserveService(callbacks, ticks, steps, service_steps) ((void)0)
#define TimeCommitProbe_RecordDirectory(unknown, valid) ((void)0)
#define TimeCommitProbe_RecordPatternReadBegin(address, length) ((void)0)
#define TimeCommitProbe_RecordPatternReadEnd(ok) ((void)0)
#define TimeCommitProbe_RecordPatternWrite(address, length, ok) ((void)0)
#define TimeCommitProbe_RecordCrcBytes(length) ((void)0)
#define TimeCommitProbe_RecordHal(receive, length, timeout, ok) ((void)0)
#define TimeCommitProbe_RecordI2cRecovery() ((void)0)
#define TimeCommitProbe_WritebackStart() ((void)0)
#define TimeCommitProbe_WritebackService(dirty_or_busy) ((void)0)
#define TimeCommitProbe_SetEditGeneration(generation) ((void)0)
#define TimeCommitProbe_RecordV3Save(elapsed_ms) ((void)(elapsed_ms))
#define TimeCommitProbe_RecordLcdBytes(bytes) ((void)0)
#define TimeCommitProbe_RecordGridFull() ((void)0)
#define TimeCommitProbe_RecordGridBox() ((void)0)
#define TimeCommitProbe_GridTransitionBegin() ((void)0)
#define TimeCommitProbe_GridTransitionEnd(flash_only) ((void)0)
#define TimeCommitProbe_FrameBegin(restarted) ((void)(restarted))
#define TimeCommitProbe_FrameStripBegin() ((void)0)
#define TimeCommitProbe_FrameStripEnd() ((void)0)
#define TimeCommitProbe_FrameComplete() ((void)0)
#define TimeCommitProbe_FrameCancel() ((void)0)
#define TimeCommitProbe_FrameDeferred() ((void)0)
#define TimeCommitProbe_PianoDrawBegin() ((void)0)
#define TimeCommitProbe_PianoDrawEnd() ((void)0)
#define TimeCommitProbe_PianoFrameBegin(replaced) ((void)(replaced))
#define TimeCommitProbe_PianoFrameComplete() ((void)0)
#define TimeCommitProbe_PianoFrameCancel() ((void)0)
#define TimeCommitProbe_PianoJobBegin() ((void)0)
#define TimeCommitProbe_PianoJobEnd() ((void)0)
#define TimeCommitProbe_PianoReturnBegin(source) ((void)0)
#define TimeCommitProbe_PianoReturnEnd() ((void)0)
#define TimeCommitProbe_TimingFrameBegin(replaced) ((void)(replaced))
#define TimeCommitProbe_TimingFrameComplete() ((void)0)
#define TimeCommitProbe_TimingFrameCancel() ((void)0)
#define TimeCommitProbe_TimingDeferred(duplicate) ((void)0)
#define TimeCommitProbe_TimingJobBegin() ((void)0)
#define TimeCommitProbe_TimingJobEnd() ((void)0)
#define TimeCommitProbe_DirectBegin(category) ((void)(category))
#define TimeCommitProbe_DirectJobBegin() ((void)0)
#define TimeCommitProbe_DirectJobEnd() ((void)0)
#define TimeCommitProbe_DirectComplete() ((void)0)
#define TimeCommitProbe_DirectCancel() ((void)0)
#define TimeCommitProbe_LoopMark(phase) ((void)0)
#define TimeCommitProbe_UiEvent(type, value) ((void)0)
#define TimeCommitProbe_RecordResidencyLoad() ((void)0)
#define PROBE_READ_SOURCE(source) ((void)0)

#endif

#endif