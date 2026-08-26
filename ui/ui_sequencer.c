#include "ui_sequencer.h"
#include "ui_display.h"
#include "ui_input.h"
#include "ui_screen_router.h"
#include "ui_transport.h"
#include "screens/ui_chord_menu_screen.h"
#include "screens/ui_chord_params_screen.h"
#include "screens/ui_main_grid_screen.h"
#include "screens/ui_screen_piano_roll.h"
#include "sequencer_bridge.h"
#include "core/pitch_mapping.h"
#include "st7789.h"
#include "stm32/user_chord_bridge.h"
#include "stm32f4xx_hal.h"
#include <string.h>
#include <stdio.h>

/* ── UI Modes ────────────────────────────────────────────────────────────── */
typedef enum {
    UI_MODE_GRID,             /* main 12-step grid view */
    UI_MODE_STEP_PIANO,       /* per-step piano-roll note view */
    UI_MODE_CHORD_MENU,       /* chord selection submenu for current step */
    UI_MODE_CHORD_PARAMS,     /* chord parameter editor */
    UI_MODE_ARP_MENU,         /* pattern arp mode/rate editor */
    UI_MODE_TIMING_MENU,      /* pattern timing editor */
    UI_MODE_QUANT_TIMING,     /* quantiser timing editor */
    UI_MODE_QUAN_ROUTER,      /* quantiser CV router editor */
    UI_MODE_SONG_CHAIN,       /* pattern chain editor */
    UI_MODE_USER_CHORD_MENU,  /* user chord: Create/Load/Name */
    UI_MODE_USER_CHORD_CREATE,/* user chord: piano keyboard editor */
    UI_MODE_USER_CHORD_LOAD,  /* user chord: load from library */
    UI_MODE_USER_CHORD_NAME   /* user chord: name editor */
} UiMode;

typedef enum {
    UI_RETURN_TARGET_GRID = 0,
    UI_RETURN_TARGET_BAR_EDITOR
} UiReturnTarget;

typedef enum {
    UI_DIRECT_SHORTCUT_NONE = 0,
    UI_DIRECT_SHORTCUT_CHORD,
    UI_DIRECT_SHORTCUT_TIMING,
    UI_DIRECT_SHORTCUT_ARP
} UiDirectShortcut;

typedef enum {
    UI_EXIT_CONFIRM_ACTION_NONE = 0,
    UI_EXIT_CONFIRM_ACTION_CHORD,
    UI_EXIT_CONFIRM_ACTION_TIMING,
    UI_EXIT_CONFIRM_ACTION_ARP
} UiExitConfirmAction;

typedef enum {
    UI_DIRECT_CHORD_SOURCE_NONE = 0,
    UI_DIRECT_CHORD_SOURCE_ROOT_TYPE,
    UI_DIRECT_CHORD_SOURCE_MASK,
    UI_DIRECT_CHORD_SOURCE_CLEAR
} UiDirectChordSource;

typedef struct {
    uint8_t active;
    uint8_t step_index;   /* 0..11 */
    uint8_t slot_index;   /* horizontal ledger slot */
    uint8_t anchor_midi;  /* piano cursor at M1 entry */
} UiDirectChordContext;

typedef struct {
    uint8_t active;
    uint8_t yes_selected; /* 0=NO, 1=YES */
    UiDirectChordSource source;
    uint16_t note_mask;
    uint8_t root_key;
    uint8_t chord_type;
} UiDirectChordConfirmState;

/* ── State ───────────────────────────────────────────────────────────────── */
static UiMode      s_ui_mode        = UI_MODE_GRID;
static uint8_t     s_selected_step  = 1;   /* cursor position 1-12      */
static uint8_t     s_active_step    = 0;   /* currently playing step    */
static uint8_t     s_last_active    = 0;   /* previous playing step     */
static uint8_t     s_menu_step      = 1;   /* which step's chord we're editing */
static ChordParams s_step_chords[12];     /* chord parameters for each step */
static ChordParams s_pattern_step_chords[32][12];
static uint8_t     s_cached_pattern = 0xFF;
static uint8_t     s_param_cursor   = 0;   /* which parameter we're editing (0=root, 1=type, 2=arp, 3=duration) */
static uint8_t     s_timing_cursor  = 0;   /* timing field cursor */
static uint8_t     s_timing_step_count = 12;
static uint8_t     s_timing_step_division = 4;
static uint8_t     s_timing_ts_num = 4;
static uint8_t     s_timing_ts_den = 4;
static uint8_t     s_timing_swing = 0;
static uint8_t     s_timing_save_rejected = 0;
static uint8_t     s_arp_mode = 0;
static uint8_t     s_arp_rate = 2;
static uint8_t     s_arp_cursor = 0;
static uint8_t     s_quant_cursor = 0;
static uint8_t     s_quant_enabled = 1;
/* Quant is the internal ledger-column duration, not the outer step duration. */
static uint8_t     s_quant_grid_division = 0; /* 0=1/4,1=1/8,2=1/16,3=1/32 */
static uint8_t     s_quant_strength = 80;
static uint8_t     s_quant_humanize_ms = 0;
static uint8_t     s_quant_lag_ms = 0;
static uint8_t     s_quant_saved_enabled = 1;
static uint8_t     s_quant_saved_grid_division = 0;
static uint8_t     s_quant_saved_strength = 80;
static uint8_t     s_quant_saved_humanize_ms = 0;
static uint8_t     s_quant_saved_lag_ms = 0;
static uint8_t     s_quan_router_cursor = 0;  /* slot 0..7 maps to step 3..10 */
static uint8_t     s_quan_router_field = 0;   /* 0=source, 1=target */
static uint8_t     s_quan_router_source[8] = {0};
static uint8_t     s_quan_router_target[8] = {0};
static uint8_t     s_quan_router_saved_source[8] = {0};
static uint8_t     s_quan_router_saved_target[8] = {0};
static uint8_t     s_last_predefined_chord = 0;
static UiMainMode  s_main_mode = UI_MAIN_MODE_STEP;
static uint8_t     s_song_chain[32] = {0};
static uint8_t     s_song_length = 1;
static uint8_t     s_song_cursor = 0;
static uint8_t     s_song_blink_on = 1;
static uint32_t    s_song_blink_ms = 0;
static uint8_t     s_step_input_guard_ticks = 0;
static uint8_t     s_step_matrix_rearm_required = 0;
static uint8_t     s_chord_params_timing_quick = 0;
#define UI_STEP_LEDGER_MAX 32u
/* Must match the physically rendered rows in Step Piano body (154/12=12). */
#define UI_STEP_PIANO_VISIBLE_ROWS 12u
static uint8_t     s_step_piano_slot = 0;
static uint8_t     s_step_piano_slot_count = 1;
static uint8_t     s_step_piano_selected_midi = (uint8_t)kLegacyPitchClassFallbackMidiBase;
static uint8_t     s_step_piano_top_midi = 71u;
static uint8_t     s_step_piano_confirm_active = 0u;
static uint8_t     s_step_piano_confirm_yes_selected = 0u;
static uint8_t     s_step_piano_confirm_action = 0u;
static char        s_step_piano_confirm_message[24] = "";
static uint8_t     s_step_piano_length_mode_active = 0u;
static uint8_t     s_step_piano_length_pending = 1u;
static char        s_step_piano_title[24] = "S1/1";
static char        s_step_piano_key_label[8] = "C";
static char        s_step_piano_scale_label[20] = "MAJ";
static uint8_t     s_step_piano_session_active = 0;
static uint8_t     s_step_piano_touched[12] = {0};
static uint8_t     s_step_piano_saved_len[12] = {0};
static BridgeLedgerSlot s_step_piano_saved_slots[12][UI_STEP_LEDGER_MAX] = {{{{0}}}};
static UiReturnTarget s_return_target = UI_RETURN_TARGET_GRID;
static UiDirectShortcut s_direct_shortcut = UI_DIRECT_SHORTCUT_NONE;
static uint8_t     s_exit_confirm_active = 0u;
static uint8_t     s_exit_confirm_yes_selected = 0u;
static UiExitConfirmAction s_exit_confirm_action = UI_EXIT_CONFIRM_ACTION_NONE;
static char        s_exit_confirm_message[24] = "";
static UiDirectChordContext s_direct_chord_ctx = {0u, 0u, 0u, (uint8_t)kLegacyPitchClassFallbackMidiBase};
static UiDirectChordConfirmState s_direct_chord_confirm = {0u, 0u, UI_DIRECT_CHORD_SOURCE_NONE, 0u, 0u, 0u};
static char        s_direct_chord_confirm_message[24] = "";

/* ── User Chord State ────────────────────────────────────────────────────── */
static uint16_t    s_user_chord_note_mask = 0; /* note mask being created */
static uint8_t     s_last_saved_user_chord = 0xFF;
static char        s_user_chord_name_edit[17] = {0};
static uint8_t     s_user_chord_name_cursor = 0;

static uint8_t UI_Sequencer_TimingDraftIsDirty(void);
static void UI_Sequencer_CommitChordDraft(void);
static void UI_Sequencer_EnterStepPianoView(uint8_t step);
static uint8_t UI_Sequencer_StepHasNotes(uint8_t step);
static void UI_Sequencer_RefreshStepPianoContext(uint8_t step);
static uint8_t UI_Sequencer_NormalizeStepPianoSlots(uint8_t slots);
static uint8_t UI_Sequencer_GetFirstMidiFromSlot(const BridgeLedgerSlot* slot, uint8_t fallback);
static void UI_Sequencer_GetStepKeyScaleLabels(uint8_t step_index,
                                               char* key_buf,
                                               uint8_t key_buf_len,
                                               char* scale_buf,
                                               uint8_t scale_buf_len);
static void UI_StepPiano_ActionMovePitch(int8_t delta);
static void UI_StepPiano_ActionNextPosition(void);
static void UI_StepPiano_ActionToggleBarSkip(void);
static void UI_StepPiano_ActionToggleNote(void);
static void UI_StepPiano_ActionClearBarRequest(void);
static void UI_StepPiano_ActionToggleLengthMode(void);
static void UI_StepPiano_ActionOctaveUp(void);
static void UI_StepPiano_ActionOctaveDown(void);
static void UI_StepPiano_ActionExitBarRequest(void);
static void UI_StepPiano_ActionConfirmModal(void);
static void UI_StepPiano_ActionExitBar(void);
static uint8_t UI_StepPiano_ClampTopMidi(int16_t top);
static uint8_t UI_StepPiano_GetBottomMidi(uint8_t top);
static uint8_t UI_StepPiano_ClampCursorToViewport(uint8_t midi_note);
static void UI_Sequencer_BeginStepPianoSession(void);
static void UI_Sequencer_EndStepPianoSession(uint8_t save);
static void UI_Sequencer_MarkStepPianoTouched(uint8_t step_index);
static void UI_Sequencer_SetMainMode(UiMainMode mode);
static void UI_Sequencer_HandleChordParamAction(uint8_t action);
static void UI_Sequencer_LoadSongChainDraft(void);
static void UI_Sequencer_DrawSongChainMenu(void);
static void UI_Sequencer_SaveChordDraftForPattern(void);
static void UI_Sequencer_LoadChordDraftForPattern(uint8_t pattern);
static void UI_Sequencer_HydrateStepChordFromEngine(uint8_t step);
static void UI_Sequencer_SetActiveScreen(UiScreen* screen);
static void UI_Sequencer_EnterChordMenu(uint8_t step);
static void UI_Sequencer_EnterChordParams(uint8_t step);
static void UI_Sequencer_EnterChordParamsAtCursor(uint8_t step, uint8_t param_cursor, UiDirectShortcut shortcut_tag);
static void UI_Sequencer_RefreshChordParamsScreen(void);
static void UI_Sequencer_UpdateDirectChordDisplayInfo(void);
static void UI_Sequencer_ExitToGrid(void);
static void UI_Sequencer_ExitStepPiano(uint8_t save);
static void UI_Sequencer_EnterArpMenu(uint8_t initial_cursor);
static void UI_Sequencer_ExitArpMenu(uint8_t commit);
static void UI_Sequencer_EnterTimingMenu(uint8_t initial_cursor);
static void UI_Sequencer_ExitTimingMenu(uint8_t commit);
static void UI_Sequencer_EnterQuantTimingMenu(uint8_t initial_cursor);
static void UI_Sequencer_ExitQuantTimingMenu(uint8_t commit);
static void UI_Sequencer_EnterQuanRouterMenu(uint8_t initial_slot);
static void UI_Sequencer_ExitQuanRouterMenu(uint8_t commit);
static void UI_Sequencer_EnterSongChain(uint8_t requested_slot);
static void UI_Sequencer_ExitSongChain(void);
static void UI_Sequencer_EnterUserChordMenu(void);
static void UI_Sequencer_EnterUserChordCreate(void);
static void UI_Sequencer_EnterUserChordLoad(void);
static void UI_Sequencer_EnterUserChordName(void);
static void UI_Sequencer_ExitUserChordToGrid(void);
static void UI_Sequencer_ExitUserChordSubToMenu(void);
static void UI_Sequencer_DrawTimingMenu(void);
static void UI_Sequencer_DrawArpMenu(void);
static void UI_Sequencer_LoadArpDraft(void);
static void UI_Sequencer_CommitArpDraft(void);
static uint8_t UI_Sequencer_ArpDraftIsDirty(void);
static void UI_Sequencer_LoadTimingDraft(void);
static uint8_t UI_Sequencer_CommitTimingDraft(void);
static void UI_Sequencer_DrawQuantTimingMenu(void);
static void UI_Sequencer_LoadQuantTimingDraft(void);
static void UI_Sequencer_CommitQuantTimingDraft(void);
static uint8_t UI_Sequencer_QuantTimingDraftIsDirty(void);
static void UI_Sequencer_DrawQuanRouterMenu(void);
static void UI_Sequencer_LoadQuanRouterDraft(void);
static void UI_Sequencer_CommitQuanRouterDraft(void);
static uint8_t UI_Sequencer_QuanRouterDraftIsDirty(void);
static void UI_Sequencer_StartUserChordNameEdit(void);
static uint8_t UI_Sequencer_ReturnToBarEditor(ScreenExitReason reason);
static void UI_Sequencer_BeginDirectShortcut(UiDirectShortcut shortcut);
static void UI_Sequencer_EndDirectShortcut(void);
static uint8_t UI_Sequencer_IsDirectShortcutActive(void);
static void UI_Sequencer_BeginExitConfirm(const char* message, UiExitConfirmAction action);
static void UI_Sequencer_DismissExitConfirm(void);
static void UI_Sequencer_HandleExitConfirmDelta(int8_t delta);
static void UI_Sequencer_ConfirmExitConfirmSelection(void);
static uint8_t UI_Sequencer_HandleEncoderLongPressForDirectShortcut(void);
static void UI_Sequencer_EnterChordFromBarEditor(void);
static void UI_Sequencer_EnterTimingFromBarEditor(void);
static void UI_Sequencer_EnterArpFromBarEditor(void);
static void UI_Sequencer_BeginDirectChordContext(void);
static void UI_Sequencer_EndDirectChordContext(void);
static uint8_t UI_Sequencer_IsDirectChordContextActive(void);
static uint8_t UI_Sequencer_IsSlotOccupied(uint8_t step_index, uint8_t slot_index);
static uint8_t UI_Sequencer_FindNearestRootMidi(uint8_t root_key, uint8_t anchor_midi, uint8_t* out_root_midi);
static uint8_t UI_Sequencer_BuildAbsoluteNotesFromMask(uint16_t note_mask,
                                                       uint8_t root_key,
                                                       uint8_t anchor_midi,
                                                       BridgeLedgerSlot* out_slot);
static uint8_t UI_Sequencer_BuildAbsoluteNotesFromRootType(uint8_t root_key,
                                                           uint8_t chord_type,
                                                           uint8_t anchor_midi,
                                                           BridgeLedgerSlot* out_slot);
static void UI_Sequencer_BeginDirectChordConfirm(const char* message,
                                                 UiDirectChordSource source,
                                                 uint16_t note_mask,
                                                 uint8_t root_key,
                                                 uint8_t chord_type);
static void UI_Sequencer_DismissDirectChordConfirm(void);
static void UI_Sequencer_HandleDirectChordConfirmDelta(int8_t delta);
static uint8_t UI_Sequencer_ConfirmDirectChordSelection(void);
static uint8_t UI_Sequencer_CommitDirectChordToSlot(UiDirectChordSource source,
                                                    uint16_t note_mask,
                                                    uint8_t root_key,
                                                    uint8_t chord_type,
                                                    uint8_t allow_replace);
static void UI_Sequencer_HandleShiftTapEvent(uint8_t shift_tap);
static void UI_Sequencer_HandlePlayRecInput(uint8_t play_pressed,
                                            uint8_t shift_play_pressed,
                                            uint8_t rec_pressed,
                                            uint8_t shift_rec_pressed);
static void UI_Sequencer_HandleStepMatrixInput(uint8_t step_press, uint8_t shift_step_press);
static void UI_Sequencer_HandleEncoderPressInput(uint8_t encoder_pressed);
static void UI_Sequencer_HandleEncoderDeltaInput(int8_t delta);

typedef void (*UiRouteActionFn)(void);

typedef struct {
    UiMode mode;
    UiRouteActionFn on_play;
    UiRouteActionFn on_shift_play;
    UiRouteActionFn on_rec;
    UiRouteActionFn on_shift_rec;
} UiInputRoute;

typedef void (*UiStepRouteFn)(uint8_t step);

typedef struct {
    UiMode mode;
    uint8_t main_mode; /* UI_MAIN_MODE_* or UI_MAIN_MODE_ANY */
    uint8_t shifted;   /* 0=normal step, 1=shift+step */
    UiStepRouteFn on_step;
} UiStepInputRoute;

typedef struct {
    UiMode mode;
    UiStepRouteFn on_select;
} UiStepSelectRoute;

typedef struct {
    UiMode mode;
    UiRouteActionFn on_shift_tap;
} UiShiftTapRoute;

typedef void (*UiEncoderDeltaRouteFn)(int8_t delta);

typedef struct {
    UiMode mode;
    UiRouteActionFn on_press;
    UiRouteActionFn on_shift_press;
} UiEncoderPressRoute;

typedef struct {
    UiMode mode;
    UiEncoderDeltaRouteFn on_delta;
    UiEncoderDeltaRouteFn on_shift_delta;
} UiEncoderDeltaRoute;

static const UiInputRoute* UI_Sequencer_FindInputRoute(UiMode mode);
static void UI_Route_PlayGrid(void);
static void UI_Route_ShiftPlayGrid(void);
static void UI_Route_RecGrid(void);
static void UI_Route_ShiftRecGrid(void);
static void UI_Route_PlayChordMenu(void);
static void UI_Route_PlayChordParams(void);
static void UI_Route_PlayStepPiano(void);
static void UI_Route_PlayArpMenu(void);
static void UI_Route_PlayTimingMenu(void);
static void UI_Route_PlayQuantTimingMenu(void);
static void UI_Route_PlayQuanRouterMenu(void);
static void UI_Route_PlaySongChain(void);
static void UI_Route_PlayUserChordMenu(void);
static void UI_Route_PlayUserChordCreate(void);
static void UI_Route_PlayUserChordLoad(void);
static void UI_Route_PlayUserChordName(void);
static void UI_Route_RecExitToGrid(void);
static void UI_Route_RecStepPiano(void);
static void UI_Route_RecArpMenu(void);
static void UI_Route_RecTimingMenu(void);
static void UI_Route_RecQuantTimingMenu(void);
static void UI_Route_RecQuanRouterMenu(void);
static void UI_Route_RecSongChain(void);
static void UI_Route_RecBackToChordMenu(void);
static void UI_Route_RecUserChordMenu(void);
static void UI_Route_RecUserChordCreate(void);
static void UI_Route_RecUserChordLoad(void);
static void UI_Route_RecUserChordName(void);
static void UI_Route_ShiftRecUserChordLoad(void);
static void UI_StepRoute_ChordParamsFooterOrSelect(uint8_t step);
static void UI_StepRoute_SongChain(uint8_t step);
static void UI_StepRoute_GridChord(uint8_t step);
static void UI_StepRoute_GridStep(uint8_t step);
static void UI_StepRoute_GridTiming(uint8_t step);
static void UI_StepRoute_GridPattern(uint8_t step);
static void UI_StepRoute_GridQuan(uint8_t step);
static void UI_StepRoute_ShiftGridTiming(uint8_t step);
static void UI_StepRoute_ShiftGridPattern(uint8_t step);
static void UI_StepRoute_SelectStep(uint8_t step);
static void UI_StepRoute_StepPianoEditor(uint8_t step);
static void UI_SelectStep_Grid(uint8_t step);
static void UI_SelectStep_ChordMenu(uint8_t step);
static void UI_SelectStep_ChordParams(uint8_t step);
static const UiStepInputRoute* UI_Sequencer_FindStepRoute(UiMode mode, uint8_t main_mode, uint8_t shifted);
static const UiStepSelectRoute* UI_Sequencer_FindStepSelectRoute(UiMode mode);
static void UI_Sequencer_DispatchStepPress(uint8_t step, uint8_t shifted);
static const UiShiftTapRoute* UI_Sequencer_FindShiftTapRoute(UiMode mode);
static const UiEncoderPressRoute* UI_Sequencer_FindEncoderPressRoute(UiMode mode);
static const UiEncoderDeltaRoute* UI_Sequencer_FindEncoderDeltaRoute(UiMode mode);
static void UI_Route_ShiftTapGrid(void);
static void UI_EncoderPress_Grid(void);
static void UI_EncoderPress_ChordMenu(void);
static void UI_EncoderPress_ChordMenuShift(void);
static void UI_EncoderPress_ChordParams(void);
static void UI_EncoderPress_ChordParamsShift(void);
static void UI_EncoderPress_ArpMenu(void);
static void UI_EncoderPress_ArpMenuShift(void);
static void UI_EncoderPress_TimingMenu(void);
static void UI_EncoderPress_TimingMenuShift(void);
static void UI_EncoderPress_QuantTimingMenu(void);
static void UI_EncoderPress_QuantTimingMenuShift(void);
static void UI_EncoderPress_QuanRouterMenu(void);
static void UI_EncoderPress_QuanRouterMenuShift(void);
static void UI_EncoderPress_UserChordMenuShift(void);
static void UI_EncoderPress_UserChordCreate(void);
static void UI_EncoderPress_UserChordName(void);
static void UI_EncoderPress_StepPiano(void);
static void UI_EncoderPress_StepPianoShift(void);
static void UI_EncoderLongPress_StepPiano(void);
static void UI_EncoderPress_Default(void);
static void UI_EncoderDelta_NoOp(int8_t delta);
static void UI_EncoderDelta_GridShiftBpm(int8_t delta);
static void UI_EncoderDelta_TimingMenu(int8_t delta);
static void UI_EncoderDelta_QuantTimingMenu(int8_t delta);
static void UI_EncoderDelta_QuanRouterMenu(int8_t delta);
static void UI_EncoderDelta_StepPiano(int8_t delta);
static void UI_EncoderDelta_StepPianoShift(int8_t delta);
static void UI_EncoderDelta_ChordParams(int8_t delta);
static void UI_EncoderDelta_ArpMenu(int8_t delta);
static void UI_EncoderDelta_ChordMenu(int8_t delta);
static void UI_EncoderDelta_UserChordMenu(int8_t delta);
static void UI_EncoderDelta_UserChordCreate(int8_t delta);
static void UI_EncoderDelta_UserChordLoad(int8_t delta);
static void UI_EncoderDelta_UserChordName(int8_t delta);
static void UI_EncoderDelta_SongChain(int8_t delta);

/* ── Status row caching (prevent flicker) ─────────────────────────────────── */
static uint8_t  s_last_pattern    = 0xFF;
static uint8_t  s_last_step       = 0xFF;
static uint8_t  s_last_substep    = 0xFF;
static uint32_t s_last_loops      = 0xFFFFFFFF;
static uint32_t s_last_run_time   = 0xFFFFFFFF;
static uint8_t  s_repeat_flash_on = 0;
static uint32_t s_repeat_flash_ms = 0;
static uint8_t  s_repeat_flash_was_enabled = 0;
static uint32_t s_last_status_ms = 0;

static void TimingMenu_OnEnter(void)
{
    /* No-op: timing state is prepared by UI_Sequencer_EnterTimingMenu. */
}

static void ArpMenu_OnEnter(void)
{
    /* No-op: arp state is prepared by UI_Sequencer_EnterArpMenu. */
}

static void ArpMenu_OnUpdate(void)
{
    UI_Sequencer_DrawArpMenu();
}

static void ArpMenu_OnInput(InputType input_type, int8_t value)
{
    (void)input_type;
    (void)value;
}

static void ArpMenu_OnExit(ScreenExitReason reason)
{
    (void)reason;
}

static void ArpMenu_OnDraw(void)
{
    UI_Sequencer_DrawArpMenu();
}

static uint8_t s_arp_render_buffer[64];
static UiScreen s_arp_screen = {
    .name = "ArpMenu",
    .on_enter = ArpMenu_OnEnter,
    .on_update = ArpMenu_OnUpdate,
    .on_input = ArpMenu_OnInput,
    .on_exit = ArpMenu_OnExit,
    .on_draw = ArpMenu_OnDraw,
    .owned_region = {0, 0, UI_SCREEN_W, UI_SCREEN_H},
    .render_plan = {
        .rects = {UI_REGION_MENU_TOP, UI_REGION_MENU_FOOTER, UI_REGION_MENU_LIST},
        .region_count = 3,
        .pending = 1,
        .has_buffer = 1,
        .buffer_ready = 1,
        .buffer_size = sizeof(s_arp_render_buffer),
        .buffer = s_arp_render_buffer,
        .transaction_id = 1
    },
    .is_active = 0,
    .is_dirty = 1
};

static void TimingMenu_OnUpdate(void)
{
    /* No periodic redraw; timing menu is event-driven. */
}

static void TimingMenu_OnInput(InputType input_type, int8_t value)
{
    (void)input_type;
    (void)value;
}

static void TimingMenu_OnExit(ScreenExitReason reason)
{
    (void)reason;
}

static void TimingMenu_OnDraw(void)
{
    UI_Sequencer_DrawTimingMenu();
}

static uint8_t s_timing_render_buffer[64];
static UiScreen s_timing_screen = {
    .name = "TimingMenu",
    .on_enter = TimingMenu_OnEnter,
    .on_update = TimingMenu_OnUpdate,
    .on_input = TimingMenu_OnInput,
    .on_exit = TimingMenu_OnExit,
    .on_draw = TimingMenu_OnDraw,
    .owned_region = {0, 0, UI_SCREEN_W, UI_SCREEN_H},
    .render_plan = {
        .rects = {UI_REGION_MENU_TOP, UI_REGION_MENU_FOOTER, UI_REGION_MENU_LIST},
        .region_count = 3,
        .pending = 1,
        .has_buffer = 1,
        .buffer_ready = 1,
        .buffer_size = sizeof(s_timing_render_buffer),
        .buffer = s_timing_render_buffer,
        .transaction_id = 1
    },
    .is_active = 0,
    .is_dirty = 1
};

static void QuantTimingMenu_OnEnter(void)
{
    /* No-op: quant timing state is prepared by UI_Sequencer_EnterQuantTimingMenu. */
}

static void QuantTimingMenu_OnUpdate(void)
{
    /* No periodic task; redraw is event-driven. */
}

static void QuantTimingMenu_OnInput(InputType input_type, int8_t value)
{
    (void)input_type;
    (void)value;
}

static void QuantTimingMenu_OnExit(ScreenExitReason reason)
{
    (void)reason;
}

static void QuantTimingMenu_OnDraw(void)
{
    UI_Sequencer_DrawQuantTimingMenu();
}

static uint8_t s_quant_timing_render_buffer[64];
static UiScreen s_quant_timing_screen = {
    .name = "QuantTimingMenu",
    .on_enter = QuantTimingMenu_OnEnter,
    .on_update = QuantTimingMenu_OnUpdate,
    .on_input = QuantTimingMenu_OnInput,
    .on_exit = QuantTimingMenu_OnExit,
    .on_draw = QuantTimingMenu_OnDraw,
    .owned_region = {0, 0, UI_SCREEN_W, UI_SCREEN_H},
    .render_plan = {
        .rects = {UI_REGION_MENU_TOP, UI_REGION_MENU_FOOTER, UI_REGION_MENU_LIST},
        .region_count = 3,
        .pending = 1,
        .has_buffer = 1,
        .buffer_ready = 1,
        .buffer_size = sizeof(s_quant_timing_render_buffer),
        .buffer = s_quant_timing_render_buffer,
        .transaction_id = 1
    },
    .is_active = 0,
    .is_dirty = 1
};

static void QuanRouterMenu_OnEnter(void)
{
    /* No-op: router state is prepared by UI_Sequencer_EnterQuanRouterMenu. */
}

static void QuanRouterMenu_OnUpdate(void)
{
    /* No periodic task; redraw is event-driven. */
}

static void QuanRouterMenu_OnInput(InputType input_type, int8_t value)
{
    (void)input_type;
    (void)value;
}

static void QuanRouterMenu_OnExit(ScreenExitReason reason)
{
    (void)reason;
}

static void QuanRouterMenu_OnDraw(void)
{
    UI_Sequencer_DrawQuanRouterMenu();
}

static uint8_t s_quan_router_render_buffer[64];
static UiScreen s_quan_router_screen = {
    .name = "QuanRouterMenu",
    .on_enter = QuanRouterMenu_OnEnter,
    .on_update = QuanRouterMenu_OnUpdate,
    .on_input = QuanRouterMenu_OnInput,
    .on_exit = QuanRouterMenu_OnExit,
    .on_draw = QuanRouterMenu_OnDraw,
    .owned_region = {0, 0, UI_SCREEN_W, UI_SCREEN_H},
    .render_plan = {
        .rects = {UI_REGION_MENU_TOP, UI_REGION_MENU_FOOTER, UI_REGION_MENU_LIST},
        .region_count = 3,
        .pending = 1,
        .has_buffer = 1,
        .buffer_ready = 1,
        .buffer_size = sizeof(s_quan_router_render_buffer),
        .buffer = s_quan_router_render_buffer,
        .transaction_id = 1
    },
    .is_active = 0,
    .is_dirty = 1
};

static void SongChain_OnEnter(void)
{
    /* No-op: song chain state is prepared by UI_Sequencer_EnterSongChain. */
}

static void SongChain_OnUpdate(void)
{
    /* Blink redraw is tick-driven by sequencer update loop. */
}

static void SongChain_OnInput(InputType input_type, int8_t value)
{
    (void)input_type;
    (void)value;
}

static void SongChain_OnExit(ScreenExitReason reason)
{
    (void)reason;
}

static void SongChain_OnDraw(void)
{
    UI_Sequencer_DrawSongChainMenu();
}

static uint8_t s_song_chain_render_buffer[64];
static UiScreen s_song_chain_screen = {
    .name = "SongChain",
    .on_enter = SongChain_OnEnter,
    .on_update = SongChain_OnUpdate,
    .on_input = SongChain_OnInput,
    .on_exit = SongChain_OnExit,
    .on_draw = SongChain_OnDraw,
    .owned_region = {0, 0, UI_SCREEN_W, UI_SCREEN_H},
    .render_plan = {
        .rects = {UI_REGION_MENU_TOP, UI_REGION_MENU_FOOTER, UI_REGION_MENU_LIST},
        .region_count = 3,
        .pending = 1,
        .has_buffer = 1,
        .buffer_ready = 1,
        .buffer_size = sizeof(s_song_chain_render_buffer),
        .buffer = s_song_chain_render_buffer,
        .transaction_id = 1
    },
    .is_active = 0,
    .is_dirty = 1
};

static void UserChordMenu_OnEnter(void) { }
static void UserChordMenu_OnUpdate(void) { }
static void UserChordMenu_OnInput(InputType input_type, int8_t value) { (void)input_type; (void)value; }
static void UserChordMenu_OnExit(ScreenExitReason reason) { (void)reason; }
static void UserChordMenu_OnDraw(void)
{
    UI_Display_DrawUserChordMenu();
}

static uint8_t s_user_chord_menu_render_buffer[64];
static UiScreen s_user_chord_menu_screen = {
    .name = "UserChordMenu",
    .on_enter = UserChordMenu_OnEnter,
    .on_update = UserChordMenu_OnUpdate,
    .on_input = UserChordMenu_OnInput,
    .on_exit = UserChordMenu_OnExit,
    .on_draw = UserChordMenu_OnDraw,
    .owned_region = {0, 0, UI_SCREEN_W, UI_SCREEN_H},
    .render_plan = {
        .rects = {UI_REGION_MENU_TOP, UI_REGION_MENU_FOOTER, UI_REGION_MENU_LIST},
        .region_count = 3,
        .pending = 1,
        .has_buffer = 1,
        .buffer_ready = 1,
        .buffer_size = sizeof(s_user_chord_menu_render_buffer),
        .buffer = s_user_chord_menu_render_buffer,
        .transaction_id = 1
    },
    .is_active = 0,
    .is_dirty = 1
};

static void UserChordCreate_OnEnter(void) { }
static void UserChordCreate_OnUpdate(void) { }
static void UserChordCreate_OnInput(InputType input_type, int8_t value) { (void)input_type; (void)value; }
static void UserChordCreate_OnExit(ScreenExitReason reason) { (void)reason; }
static void UserChordCreate_OnDraw(void)
{
    UI_Display_DrawPianoKeyboard(UI_Display_GetCurrentNoteMask(), UI_Display_GetSelectedPianoKey());
}

static uint8_t s_user_chord_create_render_buffer[64];
static UiScreen s_user_chord_create_screen = {
    .name = "UserChordCreate",
    .on_enter = UserChordCreate_OnEnter,
    .on_update = UserChordCreate_OnUpdate,
    .on_input = UserChordCreate_OnInput,
    .on_exit = UserChordCreate_OnExit,
    .on_draw = UserChordCreate_OnDraw,
    .owned_region = {0, 0, UI_SCREEN_W, UI_SCREEN_H},
    .render_plan = {
        .rects = {UI_REGION_MENU_TOP, UI_REGION_MENU_FOOTER, UI_REGION_MENU_LIST},
        .region_count = 3,
        .pending = 1,
        .has_buffer = 1,
        .buffer_ready = 1,
        .buffer_size = sizeof(s_user_chord_create_render_buffer),
        .buffer = s_user_chord_create_render_buffer,
        .transaction_id = 1
    },
    .is_active = 0,
    .is_dirty = 1
};

static void UserChordLoad_OnEnter(void) { }
static void UserChordLoad_OnUpdate(void) { }
static void UserChordLoad_OnInput(InputType input_type, int8_t value) { (void)input_type; (void)value; }
static void UserChordLoad_OnExit(ScreenExitReason reason) { (void)reason; }
static void UserChordLoad_OnDraw(void)
{
    UI_Display_DrawUserChordLoad();
}

static uint8_t s_user_chord_load_render_buffer[64];
static UiScreen s_user_chord_load_screen = {
    .name = "UserChordLoad",
    .on_enter = UserChordLoad_OnEnter,
    .on_update = UserChordLoad_OnUpdate,
    .on_input = UserChordLoad_OnInput,
    .on_exit = UserChordLoad_OnExit,
    .on_draw = UserChordLoad_OnDraw,
    .owned_region = {0, 0, UI_SCREEN_W, UI_SCREEN_H},
    .render_plan = {
        .rects = {UI_REGION_MENU_TOP, UI_REGION_MENU_FOOTER, UI_REGION_MENU_LIST},
        .region_count = 3,
        .pending = 1,
        .has_buffer = 1,
        .buffer_ready = 1,
        .buffer_size = sizeof(s_user_chord_load_render_buffer),
        .buffer = s_user_chord_load_render_buffer,
        .transaction_id = 1
    },
    .is_active = 0,
    .is_dirty = 1
};

static void UserChordName_OnEnter(void) { }
static void UserChordName_OnUpdate(void) { }
static void UserChordName_OnInput(InputType input_type, int8_t value) { (void)input_type; (void)value; }
static void UserChordName_OnExit(ScreenExitReason reason) { (void)reason; }
static void UserChordName_OnDraw(void)
{
    UI_Display_DrawUserChordNameEditor(s_user_chord_name_edit, s_user_chord_name_cursor);
}

static uint8_t s_user_chord_name_render_buffer[64];
static UiScreen s_user_chord_name_screen = {
    .name = "UserChordName",
    .on_enter = UserChordName_OnEnter,
    .on_update = UserChordName_OnUpdate,
    .on_input = UserChordName_OnInput,
    .on_exit = UserChordName_OnExit,
    .on_draw = UserChordName_OnDraw,
    .owned_region = {0, 0, UI_SCREEN_W, UI_SCREEN_H},
    .render_plan = {
        .rects = {UI_REGION_MENU_TOP, UI_REGION_MENU_FOOTER, UI_REGION_MENU_LIST},
        .region_count = 3,
        .pending = 1,
        .has_buffer = 1,
        .buffer_ready = 1,
        .buffer_size = sizeof(s_user_chord_name_render_buffer),
        .buffer = s_user_chord_name_render_buffer,
        .transaction_id = 1
    },
    .is_active = 0,
    .is_dirty = 1
};

#define UI_MAIN_MODE_ANY 0xFFu

static const UiInputRoute s_input_routes[] = {
    { UI_MODE_GRID,             UI_Route_PlayGrid,           UI_Route_ShiftPlayGrid,       UI_Route_RecGrid,             UI_Route_ShiftRecGrid },
    { UI_MODE_CHORD_MENU,       UI_Route_PlayChordMenu,      NULL,                          UI_Route_RecExitToGrid,       NULL },
    { UI_MODE_CHORD_PARAMS,     UI_Route_PlayChordParams,    NULL,                          UI_Route_RecBackToChordMenu,  NULL },
    { UI_MODE_STEP_PIANO,       UI_Route_PlayStepPiano,      NULL,                          UI_Route_RecStepPiano,        NULL },
    { UI_MODE_ARP_MENU,         UI_Route_PlayArpMenu,        NULL,                          UI_Route_RecArpMenu,          NULL },
    { UI_MODE_TIMING_MENU,      UI_Route_PlayTimingMenu,     NULL,                          UI_Route_RecTimingMenu,       NULL },
    { UI_MODE_QUANT_TIMING,     UI_Route_PlayQuantTimingMenu,NULL,                          UI_Route_RecQuantTimingMenu,  NULL },
    { UI_MODE_QUAN_ROUTER,      UI_Route_PlayQuanRouterMenu, NULL,                          UI_Route_RecQuanRouterMenu,   NULL },
    { UI_MODE_SONG_CHAIN,       UI_Route_PlaySongChain,      NULL,                          UI_Route_RecSongChain,        NULL },
    { UI_MODE_USER_CHORD_MENU,  UI_Route_PlayUserChordMenu,  NULL,                          UI_Route_RecUserChordMenu,    NULL },
    { UI_MODE_USER_CHORD_CREATE,UI_Route_PlayUserChordCreate,NULL,                          UI_Route_RecUserChordCreate,  NULL },
    { UI_MODE_USER_CHORD_LOAD,  UI_Route_PlayUserChordLoad,  NULL,                          UI_Route_RecUserChordLoad,    UI_Route_ShiftRecUserChordLoad },
    { UI_MODE_USER_CHORD_NAME,  UI_Route_PlayUserChordName,  NULL,                          UI_Route_RecUserChordName,    NULL }
};

static const UiStepInputRoute s_step_routes[] = {
    /* Normal matrix step press */
    { UI_MODE_CHORD_PARAMS, UI_MAIN_MODE_ANY,         0, UI_StepRoute_ChordParamsFooterOrSelect },
    { UI_MODE_SONG_CHAIN,   UI_MAIN_MODE_ANY,         0, UI_StepRoute_SongChain },
    { UI_MODE_CHORD_MENU,   UI_MAIN_MODE_ANY,         0, UI_StepRoute_SelectStep },
    { UI_MODE_STEP_PIANO,   UI_MAIN_MODE_ANY,         0, UI_StepRoute_StepPianoEditor },
    { UI_MODE_GRID,         UI_MAIN_MODE_CHORD,       0, UI_StepRoute_GridChord },
    { UI_MODE_GRID,         UI_MAIN_MODE_STEP,        0, UI_StepRoute_GridStep },
    { UI_MODE_GRID,         UI_MAIN_MODE_TIMING,      0, UI_StepRoute_GridTiming },
    { UI_MODE_GRID,         UI_MAIN_MODE_PATTERN,     0, UI_StepRoute_GridPattern },
    { UI_MODE_GRID,         UI_MAIN_MODE_QUAN,        0, UI_StepRoute_GridQuan },

    /* Shift + matrix step press */
    { UI_MODE_GRID,         UI_MAIN_MODE_TIMING,      1, UI_StepRoute_ShiftGridTiming },
    { UI_MODE_CHORD_MENU,   UI_MAIN_MODE_ANY,         1, UI_StepRoute_SelectStep },
    { UI_MODE_STEP_PIANO,   UI_MAIN_MODE_ANY,         1, UI_StepRoute_StepPianoEditor },
    { UI_MODE_CHORD_PARAMS, UI_MAIN_MODE_ANY,         1, UI_StepRoute_ChordParamsFooterOrSelect },
    { UI_MODE_GRID,         UI_MAIN_MODE_PATTERN,     1, UI_StepRoute_ShiftGridPattern }
};

static const UiStepSelectRoute s_step_select_routes[] = {
    { UI_MODE_GRID,         UI_SelectStep_Grid },
    { UI_MODE_CHORD_MENU,   UI_SelectStep_ChordMenu },
    { UI_MODE_CHORD_PARAMS, UI_SelectStep_ChordParams }
};

static const UiShiftTapRoute s_shift_tap_routes[] = {
    { UI_MODE_GRID, UI_Route_ShiftTapGrid }
};

static const UiEncoderPressRoute s_encoder_press_routes[] = {
    { UI_MODE_GRID,             UI_EncoderPress_Grid,              NULL },
    { UI_MODE_CHORD_MENU,       UI_EncoderPress_ChordMenu,         UI_EncoderPress_ChordMenuShift },
    { UI_MODE_CHORD_PARAMS,     UI_EncoderPress_ChordParams,       UI_EncoderPress_ChordParamsShift },
    { UI_MODE_ARP_MENU,         UI_EncoderPress_ArpMenu,           UI_EncoderPress_ArpMenuShift },
    { UI_MODE_TIMING_MENU,      UI_EncoderPress_TimingMenu,        UI_EncoderPress_TimingMenuShift },
    { UI_MODE_QUANT_TIMING,     UI_EncoderPress_QuantTimingMenu,   UI_EncoderPress_QuantTimingMenuShift },
    { UI_MODE_QUAN_ROUTER,      UI_EncoderPress_QuanRouterMenu,    UI_EncoderPress_QuanRouterMenuShift },
    { UI_MODE_SONG_CHAIN,       UI_EncoderPress_Default,           NULL },
    { UI_MODE_USER_CHORD_MENU,  UI_Route_PlayUserChordMenu,        UI_EncoderPress_UserChordMenuShift },
    { UI_MODE_USER_CHORD_CREATE,UI_EncoderPress_UserChordCreate,   NULL },
    { UI_MODE_USER_CHORD_LOAD,  UI_Route_PlayUserChordLoad,        UI_Sequencer_ExitUserChordSubToMenu },
    { UI_MODE_USER_CHORD_NAME,  UI_EncoderPress_UserChordName,     NULL },
    { UI_MODE_STEP_PIANO,       UI_EncoderPress_StepPiano,         UI_EncoderPress_StepPianoShift }
};

static const UiEncoderDeltaRoute s_encoder_delta_routes[] = {
    { UI_MODE_GRID,             UI_EncoderDelta_NoOp,              UI_EncoderDelta_GridShiftBpm },
    { UI_MODE_TIMING_MENU,      UI_EncoderDelta_TimingMenu,        NULL },
    { UI_MODE_QUANT_TIMING,     UI_EncoderDelta_QuantTimingMenu,   NULL },
    { UI_MODE_QUAN_ROUTER,      UI_EncoderDelta_QuanRouterMenu,    NULL },
    { UI_MODE_STEP_PIANO,       UI_EncoderDelta_StepPiano,         UI_EncoderDelta_StepPianoShift },
    { UI_MODE_CHORD_PARAMS,     UI_EncoderDelta_ChordParams,       NULL },
    { UI_MODE_ARP_MENU,         UI_EncoderDelta_ArpMenu,           NULL },
    { UI_MODE_CHORD_MENU,       UI_EncoderDelta_ChordMenu,         NULL },
    { UI_MODE_USER_CHORD_MENU,  UI_EncoderDelta_UserChordMenu,     NULL },
    { UI_MODE_USER_CHORD_CREATE,UI_EncoderDelta_UserChordCreate,   NULL },
    { UI_MODE_USER_CHORD_LOAD,  UI_EncoderDelta_UserChordLoad,     NULL },
    { UI_MODE_USER_CHORD_NAME,  UI_EncoderDelta_UserChordName,     NULL },
    { UI_MODE_SONG_CHAIN,       UI_EncoderDelta_SongChain,         NULL }
};

static const UiInputRoute* UI_Sequencer_FindInputRoute(UiMode mode)
{
    for (uint8_t i = 0; i < (uint8_t)(sizeof(s_input_routes) / sizeof(s_input_routes[0])); i++)
    {
        if (s_input_routes[i].mode == mode)
        {
            return &s_input_routes[i];
        }
    }
    return NULL;
}

static const UiStepInputRoute* UI_Sequencer_FindStepRoute(UiMode mode, uint8_t main_mode, uint8_t shifted)
{
    for (uint8_t i = 0; i < (uint8_t)(sizeof(s_step_routes) / sizeof(s_step_routes[0])); i++)
    {
        if (s_step_routes[i].shifted != shifted) continue;
        if (s_step_routes[i].mode != mode) continue;
        if (s_step_routes[i].main_mode != UI_MAIN_MODE_ANY &&
            s_step_routes[i].main_mode != main_mode) continue;
        return &s_step_routes[i];
    }
    return NULL;
}

static const UiStepSelectRoute* UI_Sequencer_FindStepSelectRoute(UiMode mode)
{
    for (uint8_t i = 0; i < (uint8_t)(sizeof(s_step_select_routes) / sizeof(s_step_select_routes[0])); i++)
    {
        if (s_step_select_routes[i].mode == mode)
        {
            return &s_step_select_routes[i];
        }
    }
    return NULL;
}

static const UiShiftTapRoute* UI_Sequencer_FindShiftTapRoute(UiMode mode)
{
    for (uint8_t i = 0; i < (uint8_t)(sizeof(s_shift_tap_routes) / sizeof(s_shift_tap_routes[0])); i++)
    {
        if (s_shift_tap_routes[i].mode == mode)
        {
            return &s_shift_tap_routes[i];
        }
    }
    return NULL;
}

static const UiEncoderPressRoute* UI_Sequencer_FindEncoderPressRoute(UiMode mode)
{
    for (uint8_t i = 0; i < (uint8_t)(sizeof(s_encoder_press_routes) / sizeof(s_encoder_press_routes[0])); i++)
    {
        if (s_encoder_press_routes[i].mode == mode)
        {
            return &s_encoder_press_routes[i];
        }
    }
    return NULL;
}

static const UiEncoderDeltaRoute* UI_Sequencer_FindEncoderDeltaRoute(UiMode mode)
{
    for (uint8_t i = 0; i < (uint8_t)(sizeof(s_encoder_delta_routes) / sizeof(s_encoder_delta_routes[0])); i++)
    {
        if (s_encoder_delta_routes[i].mode == mode)
        {
            return &s_encoder_delta_routes[i];
        }
    }
    return NULL;
}

static void UI_Sequencer_DispatchStepPress(uint8_t step, uint8_t shifted)
{
    const UiStepInputRoute* route = UI_Sequencer_FindStepRoute(s_ui_mode, (uint8_t)s_main_mode, shifted);

    if (route && route->on_step)
    {
        route->on_step(step);
        return;
    }

    /* No explicit route for this mode: ignore matrix step input. */
}

static void UI_StepRoute_ChordParamsFooterOrSelect(uint8_t step)
{
    if (UI_Sequencer_IsDirectChordContextActive())
    {
        /* In direct chord context, matrix step 9 is PLACE. */
        if (step == 9u)
        {
            UI_Sequencer_HandleChordParamAction(PARAM_ACTION_SAVE);
        }
        return;
    }

    if (s_chord_params_timing_quick)
    {
        s_param_cursor = 3; /* Keep focus on per-step timing in quick timing flow. */
        UI_Sequencer_SelectStep(step);
        return;
    }

    if (step <= 4)
    {
        UI_Sequencer_HandleChordParamAction((uint8_t)(step - 1));
    }
    else
    {
        UI_Sequencer_SelectStep(step);
    }
}

static void UI_StepRoute_SongChain(uint8_t step)
{
    if (step >= 1 && step <= 4)
    {
        uint8_t page_base = (uint8_t)((s_song_cursor / 4) * 4);
        uint8_t slot = (uint8_t)(page_base + (step - 1));
        if (slot < s_song_length)
        {
            s_song_cursor = slot;
            UI_Sequencer_DrawSongChainMenu();
        }
    }
    else if (step == 5)
    {
        if (s_song_length < 32)
        {
            uint8_t fill = s_song_chain[s_song_cursor];
            s_song_chain[s_song_length] = fill;
            s_song_length++;
            Bridge_SetChainLength(s_song_length);
            Bridge_SetChainPatternAt((uint8_t)(s_song_length - 1), fill);
            s_song_cursor = (uint8_t)(s_song_length - 1);
            UI_Sequencer_DrawSongChainMenu();
        }
    }
    else if (step == 6)
    {
        if (s_song_length > 1)
        {
            s_song_length--;
            Bridge_SetChainLength(s_song_length);
            if (s_song_cursor >= s_song_length)
            {
                s_song_cursor = (uint8_t)(s_song_length - 1);
            }
            UI_Sequencer_LoadSongChainDraft();
            UI_Sequencer_DrawSongChainMenu();
        }
    }
}

static void UI_StepRoute_GridChord(uint8_t step)
{
    UI_Sequencer_EnterChordMenu(step);
}

static void UI_StepRoute_GridStep(uint8_t step)
{
    UI_Sequencer_SelectStep(step);
}

static void UI_StepRoute_GridTiming(uint8_t step)
{
    if (step == 1)
    {
        UI_Sequencer_EnterTimingMenu(1); /* duration/division */
    }
    else if (step == 2)
    {
        UI_Sequencer_EnterQuantTimingMenu(0);
    }
}

static void UI_StepRoute_GridPattern(uint8_t step)
{
    if (step >= 1 && step <= 12)
    {
        UI_Sequencer_EnterSongChain((uint8_t)(step - 1));
    }
}

static void UI_StepRoute_GridQuan(uint8_t step)
{
    if (step == 1)
    {
        UI_Sequencer_EnterQuantTimingMenu(0);
    }
    else if (step == 2)
    {
        UI_Sequencer_EnterQuantTimingMenu(1);
    }
    else if (step >= 3 && step <= 10)
    {
        UI_Sequencer_EnterQuanRouterMenu((uint8_t)(step - 3));
    }
}

static void UI_StepRoute_SelectStep(uint8_t step)
{
    UI_Sequencer_SelectStep(step);
}

static void UI_StepRoute_StepPianoEditor(uint8_t step)
{
    if (s_step_piano_confirm_active)
    {
        return;
    }

    if (step == 8u)
    {
        UI_StepPiano_ActionToggleLengthMode();
        return;
    }

    if (s_step_piano_length_mode_active)
    {
        /* In LENGTH mode, suppress all matrix actions except M8 commit/exit. */
        return;
    }

    switch (step)
    {
        case 1u:
            UI_Sequencer_EnterChordFromBarEditor();
            break;
        case 2u:
            UI_Sequencer_EnterTimingFromBarEditor();
            break;
        case 3u:
            UI_Sequencer_EnterArpFromBarEditor();
            break;
        case 5u:
            UI_StepPiano_ActionToggleBarSkip();
            break;
        case 9u:
            UI_StepPiano_ActionToggleNote();
            break;
        case 10u:
            UI_StepPiano_ActionClearBarRequest();
            break;
        case 11u:
            UI_StepPiano_ActionOctaveUp();
            break;
        case 12u:
            UI_StepPiano_ActionOctaveDown();
            break;
        default:
            break;
    }
}

static void UI_SelectStep_Grid(uint8_t step)
{
    UI_Sequencer_EnterStepPianoView(step);
}

static void UI_SelectStep_ChordMenu(uint8_t step)
{
    s_menu_step = step;
    UI_Sequencer_HydrateStepChordFromEngine(s_menu_step);
    s_last_predefined_chord = s_step_chords[s_menu_step - 1].chord_type;
    UI_ChordMenuScreen_SetContext(s_menu_step, &s_step_chords[s_menu_step - 1]);
    UI_Sequencer_SetActiveScreen(UI_ChordMenuScreen_Get());
}

static void UI_SelectStep_ChordParams(uint8_t step)
{
    UI_Sequencer_CommitChordDraft();
    s_menu_step = step;
    UI_Sequencer_HydrateStepChordFromEngine(s_menu_step);
    UI_Sequencer_RefreshChordParamsScreen();
}

static void UI_StepRoute_ShiftGridTiming(uint8_t step)
{
    s_selected_step = step;
    s_menu_step = step;
    UI_Sequencer_HydrateStepChordFromEngine(s_menu_step);
    s_ui_mode = UI_MODE_CHORD_PARAMS;
    s_param_cursor = 3; /* Gate (per-step timing length) */
    s_chord_params_timing_quick = 1;
    UI_Display_SetSelectedParamAction(PARAM_ACTION_MAIN);
    /* Consume follow-up scan(s) so the same Shift+Step press cannot trigger
       immediate MAIN/PREV/NEXT/SAVE actions in chord-params routes. */
    s_step_input_guard_ticks = 2;
    /* Require a full matrix release before accepting any next matrix action. */
    s_step_matrix_rearm_required = 1;
    UI_Sequencer_RefreshChordParamsScreen();
}

static void UI_StepRoute_ShiftGridPattern(uint8_t step)
{
    Bridge_SetCurrentPattern((uint8_t)(step - 1));
    s_last_pattern = 0xFF;
    s_last_step = 0xFF;
    s_last_substep = 0xFF;
    s_last_loops = 0xFFFFFFFF;
    s_last_run_time = 0xFFFFFFFF;
}

static void UI_Route_PlayGrid(void)
{
    UI_Transport_PlayStop();
}

static void UI_Route_ShiftPlayGrid(void)
{
    UI_Transport_Reset();
}

static void UI_Route_RecGrid(void)
{
    UI_Transport_RecArm();
}

static void UI_Route_ShiftRecGrid(void)
{
    UI_Transport_RecClear();
}

static void UI_Route_ShiftTapGrid(void)
{
    UI_Sequencer_SetMainMode((UiMainMode)(((uint8_t)s_main_mode + 1u) % 5u));
    UI_Display_DrawStatusRow(Bridge_GetCurrentPattern(),
                             Bridge_GetCurrentStep(),
                             Bridge_GetCompletedLoops(),
                             Bridge_GetRunTimeMs());
}

static void UI_Route_PlayChordMenu(void)
{
    uint8_t selected_idx = UI_ChordMenuScreen_GetSelection();
    if (selected_idx == 17)
    {
        UI_Sequencer_EnterUserChordMenu();
    }
    else if (selected_idx == 0)
    {
        if (UI_Sequencer_IsDirectChordContextActive())
        {
            if (UI_Sequencer_CommitDirectChordToSlot(UI_DIRECT_CHORD_SOURCE_CLEAR,
                                                     0u,
                                                     0u,
                                                     0u,
                                                     0u))
            {
                UI_Sequencer_SetMainMode(UI_MAIN_MODE_STEP);
                (void)UI_Sequencer_ReturnToBarEditor(SCREEN_EXIT_SAVE);
            }
            return;
        }

        uint8_t step_index = (uint8_t)(s_menu_step - 1);
        s_step_chords[step_index].chord_type = 0;

        Bridge_SetStepChordParams(step_index,
                                  s_step_chords[step_index].root_key,
                                  0,
                                  s_step_chords[step_index].arp_pattern,
                                  s_step_chords[step_index].duration,
                                  s_step_chords[step_index].loop_count);
        UI_Sequencer_SaveChordDraftForPattern();
        UI_Sequencer_SetMainMode(UI_MAIN_MODE_STEP);
        if (s_return_target == UI_RETURN_TARGET_BAR_EDITOR)
        {
            (void)UI_Sequencer_ReturnToBarEditor(SCREEN_EXIT_SAVE);
        }
        else
        {
            UI_Sequencer_ExitToGrid();
        }
    }
    else
    {
        s_step_chords[s_menu_step - 1].chord_type = selected_idx;
        UI_Sequencer_EnterChordParams(s_menu_step);
    }
}

static void UI_Route_PlayChordParams(void)
{
    if (UI_Sequencer_IsDirectChordContextActive())
    {
        /* Direct chooser: PLAY does not commit. Placement is M9 only. */
        return;
    }

    UI_Sequencer_CommitChordDraft();
    if (s_chord_params_timing_quick)
    {
        UI_Sequencer_SetMainMode(UI_MAIN_MODE_TIMING);
        s_chord_params_timing_quick = 0;
    }
    else
    {
        UI_Sequencer_SetMainMode(UI_MAIN_MODE_STEP);
    }

    if (s_return_target == UI_RETURN_TARGET_BAR_EDITOR)
    {
        (void)UI_Sequencer_ReturnToBarEditor(SCREEN_EXIT_SAVE);
    }
    else
    {
        UI_Sequencer_ExitToGrid();
    }
}

static void UI_Route_PlayStepPiano(void)
{
    UI_Sequencer_ExitStepPiano(1u);
}

static void UI_Route_PlayArpMenu(void)
{
    UI_Sequencer_ExitArpMenu(1u);
}

static void UI_Route_PlayTimingMenu(void)
{
    UI_Sequencer_ExitTimingMenu(1u);
}

static void UI_Route_PlayQuantTimingMenu(void)
{
    UI_Sequencer_ExitQuantTimingMenu(1u);
}

static void UI_Route_PlayQuanRouterMenu(void)
{
    UI_Sequencer_ExitQuanRouterMenu(1u);
}

static void UI_Route_PlaySongChain(void)
{
    UI_Sequencer_ExitSongChain();
}

static void UI_Route_PlayUserChordMenu(void)
{
    uint8_t selection = UI_Display_GetUserChordMenuSelection();
    if (selection == 0)
    {
        UI_Sequencer_EnterUserChordCreate();
    }
    else if (selection == 1)
    {
        UI_Sequencer_EnterUserChordLoad();
    }
    else
    {
        UI_Sequencer_StartUserChordNameEdit();
    }
}

static void UI_Route_PlayUserChordCreate(void)
{
    s_user_chord_note_mask = UI_Display_GetCurrentNoteMask();
    if (s_user_chord_note_mask != 0)
    {
        char auto_name[17];
        uint8_t idx = Bridge_UserChord_GetCount();
        snprintf(auto_name, sizeof(auto_name), "USER%02u", (unsigned)(idx + 1));
        s_last_saved_user_chord = Bridge_UserChord_Save(auto_name, s_user_chord_note_mask);
    }
    UI_Sequencer_ExitUserChordSubToMenu();
}

static void UI_Route_PlayUserChordLoad(void)
{
    uint8_t chord_idx = UI_Display_GetSelectedUserChord();
    const UserChordInfo *chord_info = Bridge_UserChord_Get(chord_idx);
    if (chord_info)
    {
        if (UI_Sequencer_IsDirectChordContextActive())
        {
            const uint8_t root_hint = s_step_chords[s_menu_step - 1].root_key;
            if (UI_Sequencer_CommitDirectChordToSlot(UI_DIRECT_CHORD_SOURCE_MASK,
                                                     chord_info->note_mask,
                                                     root_hint,
                                                     0u,
                                                     0u))
            {
                UI_Sequencer_SetMainMode(UI_MAIN_MODE_STEP);
                (void)UI_Sequencer_ReturnToBarEditor(SCREEN_EXIT_SAVE);
            }
            return;
        }

        s_last_saved_user_chord = chord_idx;
        s_step_chords[s_menu_step - 1].chord_type = 0;
        Bridge_SetStepCustomUserChord((uint8_t)(s_menu_step - 1), chord_info->note_mask, chord_info->name);
        UI_Sequencer_SetMainMode(UI_MAIN_MODE_STEP);
        UI_Sequencer_ExitToGrid();
    }
}

static void UI_Route_PlayUserChordName(void)
{
    char final_name[17];
    memcpy(final_name, s_user_chord_name_edit, 16);
    final_name[16] = '\0';

    for (int8_t i = 15; i >= 0; i--)
    {
        if (final_name[i] == ' ') final_name[i] = '\0';
        else break;
    }

    if (final_name[0] == '\0')
    {
        strncpy(final_name, "USER", sizeof(final_name));
    }

    Bridge_UserChord_Rename(s_last_saved_user_chord, final_name);
    UI_Sequencer_ExitUserChordSubToMenu();
}

static void UI_Route_RecExitToGrid(void)
{
    if (s_return_target == UI_RETURN_TARGET_BAR_EDITOR)
    {
        (void)UI_Sequencer_ReturnToBarEditor(SCREEN_EXIT_BACK);
    }
    else
    {
        UI_Sequencer_ExitToGrid();
    }
}

static void UI_Route_RecStepPiano(void)
{
    UI_Sequencer_ExitStepPiano(0u);
}

static void UI_Route_RecArpMenu(void)
{
    UI_Sequencer_ExitArpMenu(0u);
}

static void UI_Route_RecTimingMenu(void)
{
    UI_Sequencer_ExitTimingMenu(0u);
}

static void UI_Route_RecQuantTimingMenu(void)
{
    UI_Sequencer_ExitQuantTimingMenu(0u);
}

static void UI_Route_RecQuanRouterMenu(void)
{
    UI_Sequencer_ExitQuanRouterMenu(0u);
}

static void UI_Route_RecSongChain(void)
{
    UI_Sequencer_ExitSongChain();
}

static void UI_Route_RecBackToChordMenu(void)
{
    if (s_return_target == UI_RETURN_TARGET_BAR_EDITOR)
    {
        if (UI_Sequencer_IsDirectChordContextActive())
        {
            return;
        }

        (void)UI_Sequencer_ReturnToBarEditor(SCREEN_EXIT_BACK);
        return;
    }

    if (s_chord_params_timing_quick)
    {
        s_chord_params_timing_quick = 0;
        UI_Sequencer_SetMainMode(UI_MAIN_MODE_TIMING);
        UI_Sequencer_ExitToGrid();
        return;
    }

    s_ui_mode = UI_MODE_CHORD_MENU;
    UI_ChordMenuScreen_SetContext(s_menu_step, &s_step_chords[s_menu_step - 1]);
    if (!UI_ScreenRouter_Pop(SCREEN_EXIT_BACK))
    {
        UI_Sequencer_SetActiveScreen(UI_ChordMenuScreen_Get());
    }
}

static void UI_Route_RecUserChordMenu(void)
{
    if (UI_Sequencer_IsDirectChordContextActive())
    {
        (void)UI_Sequencer_ReturnToBarEditor(SCREEN_EXIT_BACK);
        return;
    }

    UI_Sequencer_ExitUserChordToGrid();
}

static void UI_Route_RecUserChordCreate(void)
{
    UI_Sequencer_ExitUserChordSubToMenu();
}

static void UI_Route_RecUserChordLoad(void)
{
    if (UI_Sequencer_IsDirectChordContextActive())
    {
        (void)UI_Sequencer_ReturnToBarEditor(SCREEN_EXIT_BACK);
        return;
    }

    UI_Sequencer_ExitUserChordSubToMenu();
}

static void UI_Route_RecUserChordName(void)
{
    UI_Sequencer_ExitUserChordSubToMenu();
}

static void UI_Route_ShiftRecUserChordLoad(void)
{
    uint8_t chord_idx = UI_Display_GetSelectedUserChord();
    uint8_t count = Bridge_UserChord_GetCount();

    if (count > 0)
    {
        Bridge_UserChord_Delete(chord_idx);

        count = Bridge_UserChord_GetCount();
        if (count == 0)
            UI_Display_SetSelectedUserChord(0);
        else if (chord_idx >= count)
            UI_Display_SetSelectedUserChord((uint8_t)(count - 1));

        UI_Display_DrawUserChordLoad();
    }
}

static void UI_Sequencer_SaveChordDraftForPattern(void)
{
    if (s_cached_pattern >= 32) return;
    memcpy(s_pattern_step_chords[s_cached_pattern], s_step_chords, sizeof(s_step_chords));
}

static void UI_Sequencer_LoadChordDraftForPattern(uint8_t pattern)
{
    if (pattern >= 32) pattern = 0;
    memcpy(s_step_chords, s_pattern_step_chords[pattern], sizeof(s_step_chords));
    s_cached_pattern = pattern;
}

static void UI_Sequencer_HydrateStepChordFromEngine(uint8_t step)
{
    if (step < 1 || step > 12) return;

    uint8_t step_index = (uint8_t)(step - 1);
    uint16_t note_mask = Bridge_GetStepNoteMask(step_index);

    if (note_mask == 0)
    {
        s_step_chords[step_index].chord_type = 0;
        return;
    }

    if (s_step_chords[step_index].chord_type != 0)
    {
        return;
    }

    uint8_t root_key = 0;
    uint8_t chord_type = 0;
    uint8_t duration = s_step_chords[step_index].duration;
    uint8_t repeat_count = s_step_chords[step_index].loop_count;

    if (Bridge_GetStepChordUiParams(step_index,
                                    &root_key,
                                    &chord_type,
                                    &duration,
                                    &repeat_count))
    {
        s_step_chords[step_index].root_key = root_key;
        s_step_chords[step_index].chord_type = chord_type;
        s_step_chords[step_index].duration = duration;
        s_step_chords[step_index].loop_count = repeat_count;
        UI_Sequencer_SaveChordDraftForPattern();
    }
}

static void UI_Sequencer_ExitToGrid(void)
{
    UI_Sequencer_EndDirectShortcut();
    UI_Sequencer_EndDirectChordContext();
    s_exit_confirm_active = 0u;
    s_exit_confirm_action = UI_EXIT_CONFIRM_ACTION_NONE;
    s_exit_confirm_message[0] = '\0';
    s_direct_chord_confirm.active = 0u;
    s_direct_chord_confirm.yes_selected = 0u;
    s_direct_chord_confirm.source = UI_DIRECT_CHORD_SOURCE_NONE;
    s_direct_chord_confirm_message[0] = '\0';
    s_ui_mode = UI_MODE_GRID;
    UI_MainGridScreen_SetContext(s_selected_step,
                                 s_active_step,
                                 NULL);
    UI_Sequencer_SetActiveScreen(UI_MainGridScreen_Get());
}

static void UI_Sequencer_EnterArpMenu(uint8_t initial_cursor)
{
    UiScreen* active = UI_ScreenRouter_GetActive();

    s_ui_mode = UI_MODE_ARP_MENU;
    s_arp_cursor = (initial_cursor > 1u) ? 0u : initial_cursor;
    UI_Sequencer_LoadArpDraft();

    if (active == UI_MainGridScreen_Get() || active == UI_PianoRollScreen_Get())
    {
        if (UI_ScreenRouter_Push(&s_arp_screen))
        {
            return;
        }
    }

    UI_Sequencer_DrawArpMenu();
}

static void UI_Sequencer_ExitArpMenu(uint8_t commit)
{
    UiScreen* active = UI_ScreenRouter_GetActive();

    if (commit)
    {
        UI_Sequencer_CommitArpDraft();
    }
    else
    {
        UI_Sequencer_LoadArpDraft();
    }

    if (s_return_target == UI_RETURN_TARGET_BAR_EDITOR)
    {
        UI_PianoRollScreen_ResetCache();
        UI_PianoRollScreen_ForceRedraw();
        (void)UI_Sequencer_ReturnToBarEditor(commit ? SCREEN_EXIT_SAVE : SCREEN_EXIT_BACK);
        return;
    }

    s_ui_mode = UI_MODE_GRID;
    UI_MainGridScreen_SetContext(s_selected_step,
                                 s_active_step,
                                 NULL);

    if (active == &s_arp_screen)
    {
        if (UI_ScreenRouter_Pop(commit ? SCREEN_EXIT_SAVE : SCREEN_EXIT_BACK))
        {
            return;
        }
    }

    UI_Sequencer_SetActiveScreen(UI_MainGridScreen_Get());
}

static void UI_Sequencer_EnterTimingMenu(uint8_t initial_cursor)
{
    UiScreen* active = UI_ScreenRouter_GetActive();

    s_ui_mode = UI_MODE_TIMING_MENU;
    s_timing_cursor = initial_cursor;
    UI_Sequencer_LoadTimingDraft();

    if (active == UI_MainGridScreen_Get() || active == UI_PianoRollScreen_Get())
    {
        if (UI_ScreenRouter_Push(&s_timing_screen))
        {
            return;
        }
    }

    UI_Sequencer_DrawTimingMenu();
}

static void UI_Sequencer_ExitTimingMenu(uint8_t commit)
{
    UiScreen* active = UI_ScreenRouter_GetActive();

    if (commit && !UI_Sequencer_CommitTimingDraft())
    {
        s_timing_save_rejected = 1u;
        UI_Sequencer_DrawTimingMenu();
        return;
    }

    if (s_return_target == UI_RETURN_TARGET_BAR_EDITOR)
    {
        UI_PianoRollScreen_ResetCache();
        UI_PianoRollScreen_ForceRedraw();
        (void)UI_Sequencer_ReturnToBarEditor(commit ? SCREEN_EXIT_SAVE : SCREEN_EXIT_BACK);
        return;
    }

    s_ui_mode = UI_MODE_GRID;
    UI_MainGridScreen_SetContext(s_selected_step,
                                 s_active_step,
                                 NULL);

    if (active == &s_timing_screen)
    {
        if (UI_ScreenRouter_Pop(commit ? SCREEN_EXIT_SAVE : SCREEN_EXIT_BACK))
        {
            return;
        }
    }

    UI_Sequencer_SetActiveScreen(UI_MainGridScreen_Get());
}

static void UI_Sequencer_EnterQuantTimingMenu(uint8_t initial_cursor)
{
    s_ui_mode = UI_MODE_QUANT_TIMING;
    s_quant_cursor = initial_cursor;
    UI_Sequencer_LoadQuantTimingDraft();

    if (UI_ScreenRouter_GetActive() == UI_MainGridScreen_Get())
    {
        if (UI_ScreenRouter_Push(&s_quant_timing_screen))
        {
            return;
        }
    }

    UI_Sequencer_DrawQuantTimingMenu();
}

static void UI_Sequencer_ExitQuantTimingMenu(uint8_t commit)
{
    UiScreen* active = UI_ScreenRouter_GetActive();

    if (commit)
    {
        UI_Sequencer_CommitQuantTimingDraft();
    }

    s_ui_mode = UI_MODE_GRID;
    UI_MainGridScreen_SetContext(s_selected_step,
                                 s_active_step,
                                 NULL);

    if (active == &s_quant_timing_screen)
    {
        if (UI_ScreenRouter_Pop(commit ? SCREEN_EXIT_SAVE : SCREEN_EXIT_BACK))
        {
            return;
        }
    }

    UI_Sequencer_SetActiveScreen(UI_MainGridScreen_Get());
}

static void UI_Sequencer_EnterQuanRouterMenu(uint8_t initial_slot)
{
    s_ui_mode = UI_MODE_QUAN_ROUTER;
    s_quan_router_cursor = (initial_slot < 8u) ? initial_slot : 0u;
    s_quan_router_field = 0;
    UI_Sequencer_LoadQuanRouterDraft();

    if (UI_ScreenRouter_GetActive() == UI_MainGridScreen_Get())
    {
        if (UI_ScreenRouter_Push(&s_quan_router_screen))
        {
            return;
        }
    }

    UI_Sequencer_DrawQuanRouterMenu();
}

static void UI_Sequencer_ExitQuanRouterMenu(uint8_t commit)
{
    UiScreen* active = UI_ScreenRouter_GetActive();

    if (commit)
    {
        UI_Sequencer_CommitQuanRouterDraft();
    }

    s_ui_mode = UI_MODE_GRID;
    UI_MainGridScreen_SetContext(s_selected_step,
                                 s_active_step,
                                 NULL);

    if (active == &s_quan_router_screen)
    {
        if (UI_ScreenRouter_Pop(commit ? SCREEN_EXIT_SAVE : SCREEN_EXIT_BACK))
        {
            return;
        }
    }

    UI_Sequencer_SetActiveScreen(UI_MainGridScreen_Get());
}

static void UI_Sequencer_EnterSongChain(uint8_t requested_slot)
{
    s_ui_mode = UI_MODE_SONG_CHAIN;
    s_song_blink_on = 1;
    s_song_blink_ms = HAL_GetTick();
    UI_Sequencer_LoadSongChainDraft();

    if (requested_slot < s_song_length)
    {
        s_song_cursor = requested_slot;
    }
    else if (s_song_length > 0)
    {
        s_song_cursor = (uint8_t)(s_song_length - 1);
    }
    else
    {
        s_song_cursor = 0;
    }

    if (UI_ScreenRouter_GetActive() == UI_MainGridScreen_Get())
    {
        if (UI_ScreenRouter_Push(&s_song_chain_screen))
        {
            return;
        }
    }

    UI_Sequencer_DrawSongChainMenu();
}

static void UI_Sequencer_ExitSongChain(void)
{
    UiScreen* active = UI_ScreenRouter_GetActive();

    s_ui_mode = UI_MODE_GRID;
    UI_MainGridScreen_SetContext(s_selected_step,
                                 s_active_step,
                                 NULL);

    if (active == &s_song_chain_screen)
    {
        if (UI_ScreenRouter_Pop(SCREEN_EXIT_BACK))
        {
            return;
        }
    }

    UI_Sequencer_SetActiveScreen(UI_MainGridScreen_Get());
}

static void UI_Sequencer_EnterUserChordMenu(void)
{
    UiScreen* active = UI_ScreenRouter_GetActive();

    s_ui_mode = UI_MODE_USER_CHORD_MENU;
    UI_Display_ResetUserChordMenuCache();

    if (active == UI_ChordMenuScreen_Get())
    {
        if (UI_ScreenRouter_Push(&s_user_chord_menu_screen))
        {
            return;
        }
    }

    UI_Sequencer_SetActiveScreen(&s_user_chord_menu_screen);
}

static void UI_Sequencer_EnterUserChordCreate(void)
{
    s_ui_mode = UI_MODE_USER_CHORD_CREATE;
    s_user_chord_note_mask = 0;
    UI_Display_SetPianoNoteMask(0);

    if (UI_ScreenRouter_GetActive() == &s_user_chord_menu_screen)
    {
        if (UI_ScreenRouter_Push(&s_user_chord_create_screen))
        {
            return;
        }
    }

    UI_Sequencer_SetActiveScreen(&s_user_chord_create_screen);
}

static void UI_Sequencer_EnterUserChordLoad(void)
{
    s_ui_mode = UI_MODE_USER_CHORD_LOAD;
    Bridge_UserChord_EnsureLoaded();

    if (UI_ScreenRouter_GetActive() == &s_user_chord_menu_screen)
    {
        if (UI_ScreenRouter_Push(&s_user_chord_load_screen))
        {
            return;
        }
    }

    UI_Sequencer_SetActiveScreen(&s_user_chord_load_screen);
}

static void UI_Sequencer_EnterUserChordName(void)
{
    s_ui_mode = UI_MODE_USER_CHORD_NAME;

    if (UI_ScreenRouter_GetActive() == &s_user_chord_menu_screen)
    {
        if (UI_ScreenRouter_Push(&s_user_chord_name_screen))
        {
            return;
        }
    }

    UI_Sequencer_SetActiveScreen(&s_user_chord_name_screen);
}

static void UI_Sequencer_ExitUserChordSubToMenu(void)
{
    s_ui_mode = UI_MODE_USER_CHORD_MENU;
    UI_Display_ResetUserChordMenuCache();

    if (UI_ScreenRouter_Pop(SCREEN_EXIT_BACK))
    {
        return;
    }

    UI_Sequencer_SetActiveScreen(&s_user_chord_menu_screen);
}

static void UI_Sequencer_ExitUserChordToGrid(void)
{
    s_ui_mode = UI_MODE_GRID;
    UI_MainGridScreen_SetContext(s_selected_step,
                                 s_active_step,
                                 NULL);
    UI_Sequencer_SetActiveScreen(UI_MainGridScreen_Get());
}

static uint8_t UI_Sequencer_StepHasNotes(uint8_t step)
{
    if (step < 1 || step > 12) return 0;
    if (Bridge_GetStepNoteMask((uint8_t)(step - 1)) != 0) return 1;
    return (s_step_chords[step - 1].chord_type != 0) ? 1 : 0;
}

static uint8_t UI_Sequencer_NormalizeStepPianoSlots(uint8_t slots)
{
    if (slots <= 4u) return 4u;
    if (slots <= 8u) return 8u;
    if (slots <= 16u) return 16u;
    return UI_STEP_LEDGER_MAX;
}

static uint8_t UI_Sequencer_GetFirstMidiFromSlot(const BridgeLedgerSlot* slot, uint8_t fallback)
{
    if (slot)
    {
        for (uint8_t i = 0u; i < 4u; ++i)
        {
            if (slot->notes[i] <= 127u)
            {
                return slot->notes[i];
            }
        }
    }

    if (fallback <= 127u) return fallback;
    return (uint8_t)kLegacyPitchClassFallbackMidiBase;
}

static uint8_t UI_StepPiano_ClampTopMidi(int16_t top)
{
    const int16_t min_top = (int16_t)kPitchMidiMin + (int16_t)(UI_STEP_PIANO_VISIBLE_ROWS - 1u);
    if (top > (int16_t)kPitchMidiMax) top = (int16_t)kPitchMidiMax;
    if (top < min_top)
    {
        top = min_top;
    }
    return (uint8_t)top;
}

static uint8_t UI_StepPiano_GetBottomMidi(uint8_t top)
{
    const uint8_t span = (uint8_t)(UI_STEP_PIANO_VISIBLE_ROWS - 1u);
    const uint8_t raw_bottom = (top >= span) ? (uint8_t)(top - span) : (uint8_t)kPitchMidiMin;
    return (raw_bottom < (uint8_t)kPitchMidiMin) ? (uint8_t)kPitchMidiMin : raw_bottom;
}

static uint8_t UI_StepPiano_ClampCursorToViewport(uint8_t midi_note)
{
    const uint8_t top = s_step_piano_top_midi;
    const uint8_t bottom = UI_StepPiano_GetBottomMidi(top);
    if (midi_note < (uint8_t)kPitchMidiMin) midi_note = (uint8_t)kPitchMidiMin;
    if (midi_note > (uint8_t)kPitchMidiMax) midi_note = (uint8_t)kPitchMidiMax;
    if (midi_note > top) return top;
    if (midi_note < bottom) return bottom;
    return midi_note;
}

static void UI_Sequencer_GetStepKeyScaleLabels(uint8_t step_index,
                                               char* key_buf,
                                               uint8_t key_buf_len,
                                               char* scale_buf,
                                               uint8_t scale_buf_len)
{
    (void)step_index;
    static const char* k_note_names[12] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };
    static const char* k_scale_names[9] = {
        "MAJ", "MIN", "DOR", "PHR", "LYD", "MIX", "LOC", "HMIN", "MMIN"
    };

    const uint8_t root = Bridge_GetSongKeyRoot();
    const uint8_t scale = Bridge_GetSongKeyScale();
    snprintf(key_buf, key_buf_len, "%s", k_note_names[root % 12u]);
    snprintf(scale_buf, scale_buf_len, "%s", k_scale_names[scale % 9u]);
}

static void UI_Sequencer_BeginStepPianoSession(void)
{
    s_step_piano_session_active = 1u;
    memset(s_step_piano_touched, 0, sizeof(s_step_piano_touched));
}

static void UI_Sequencer_MarkStepPianoTouched(uint8_t step_index)
{
    if (!s_step_piano_session_active) return;
    if (step_index >= 12u) return;
    if (s_step_piano_touched[step_index]) return;

    s_step_piano_saved_len[step_index] = Bridge_GetStepLedgerLength(step_index);
    if (s_step_piano_saved_len[step_index] < 1u) s_step_piano_saved_len[step_index] = 1u;
    if (s_step_piano_saved_len[step_index] > UI_STEP_LEDGER_MAX) s_step_piano_saved_len[step_index] = UI_STEP_LEDGER_MAX;

    for (uint8_t i = 0u; i < UI_STEP_LEDGER_MAX; ++i)
    {
        s_step_piano_saved_slots[step_index][i] = Bridge_GetStepLedgerSlotMidi(step_index, i);
    }
    s_step_piano_touched[step_index] = 1u;
}

static void UI_Sequencer_EndStepPianoSession(uint8_t save)
{
    if (!s_step_piano_session_active) return;

    if (!save)
    {
        for (uint8_t step_index = 0u; step_index < 12u; ++step_index)
        {
            if (!s_step_piano_touched[step_index]) continue;
            Bridge_SetStepLedgerLength(step_index, s_step_piano_saved_len[step_index]);
            for (uint8_t i = 0u; i < UI_STEP_LEDGER_MAX; ++i)
            {
                Bridge_SetStepLedgerSlotMidi(step_index, i, &s_step_piano_saved_slots[step_index][i]);
            }
            Bridge_SetStepLedgerLength(step_index, s_step_piano_saved_len[step_index]);
        }
    }

    s_step_piano_session_active = 0u;
    memset(s_step_piano_touched, 0, sizeof(s_step_piano_touched));
}

static void UI_Sequencer_RefreshStepPianoContext(uint8_t step)
{
    if (step < 1u || step > 12u) return;

    const uint8_t step_index = (uint8_t)(step - 1u);
    s_step_piano_slot_count = UI_Sequencer_NormalizeStepPianoSlots(Bridge_GetStepLedgerLength(step_index));
    if (s_step_piano_slot_count < 1u)
    {
        s_step_piano_slot_count = 1u;
    }
    if (s_step_piano_slot >= s_step_piano_slot_count)
    {
        s_step_piano_slot = (uint8_t)(s_step_piano_slot_count - 1u);
    }

    const BridgeLedgerSlot slot = Bridge_GetStepLedgerSlotMidi(step_index, s_step_piano_slot);
    uint16_t slot_mask = 0u;

    for (uint8_t i = 0u; i < 4u; ++i)
    {
        if (slot.notes[i] <= 127u)
        {
            slot_mask |= (uint16_t)(1u << (slot.notes[i] % 12u));
        }
    }

    if (slot_mask == 0u && s_step_piano_slot == 0u)
    {
        slot_mask = Bridge_GetStepNoteMask(step_index);
    }

    s_step_piano_top_midi = UI_StepPiano_ClampTopMidi((int16_t)s_step_piano_top_midi);
    s_step_piano_selected_midi = UI_StepPiano_ClampCursorToViewport(s_step_piano_selected_midi);
    UI_Sequencer_GetStepKeyScaleLabels(step_index,
                                       s_step_piano_key_label,
                                       sizeof(s_step_piano_key_label),
                                       s_step_piano_scale_label,
                                       sizeof(s_step_piano_scale_label));

    snprintf(s_step_piano_title, sizeof(s_step_piano_title), "S%u/%u",
             (unsigned)(s_step_piano_slot + 1u),
             (unsigned)s_step_piano_slot_count);

    PianoRollContext ctx;
    ctx.mode = PIANO_ROLL_MODE_STEP_ROLL;
    ctx.step = step;
    ctx.note_mask = slot_mask;
    ctx.selected_key = (uint8_t)(s_step_piano_selected_midi % 12u);
    ctx.selected_midi_note = s_step_piano_selected_midi;
    ctx.top_visible_midi = s_step_piano_top_midi;
    ctx.slot_index = s_step_piano_slot;
    ctx.slot_count = s_step_piano_slot_count;
    ctx.grid_division = Bridge_GetPatternStepDivision();
    {
        uint8_t cv_notes[4] = {0xFFu, 0xFFu, 0xFFu, 0xFFu};
        const uint8_t cv_absolute = Bridge_GetStepLedgerCvLaneNotes(step_index, s_step_piano_slot, cv_notes);
        for (uint8_t i = 0u; i < 4u; ++i)
        {
            ctx.slot_notes[i] = cv_notes[i];
        }
        ctx.cv_absolute_valid = cv_absolute;
    }
    ctx.arp_on = (Bridge_GetCurrentPatternArpMode() != 0u) ? 1u : 0u;
    ctx.length_mode_active = s_step_piano_length_mode_active ? 1u : 0u;
    ctx.pending_length = s_step_piano_length_pending;
    ctx.key_label = s_step_piano_key_label;
    ctx.scale_label = s_step_piano_scale_label;
    ctx.clear_confirm_active = s_step_piano_confirm_active;
    ctx.clear_confirm_yes_selected = s_step_piano_confirm_yes_selected;
    ctx.bar_skipped = Bridge_GetStepBarState(step_index);
    ctx.confirm_message = s_step_piano_confirm_message;
    ctx.title = s_step_piano_title;
    ctx.footer_label = "BAR EDITOR";

    UI_PianoRollScreen_SetContext(&ctx);
}

static void UI_Sequencer_EnterStepPianoView(uint8_t step)
{
    if (step < 1 || step > 12) return;

    UI_Sequencer_EndDirectShortcut();
    UI_Sequencer_EndDirectChordContext();
    s_exit_confirm_active = 0u;
    s_exit_confirm_action = UI_EXIT_CONFIRM_ACTION_NONE;
    s_exit_confirm_message[0] = '\0';
    s_direct_chord_confirm.active = 0u;
    s_direct_chord_confirm.yes_selected = 0u;
    s_direct_chord_confirm.source = UI_DIRECT_CHORD_SOURCE_NONE;
    s_direct_chord_confirm_message[0] = '\0';

    s_selected_step = step;
    s_ui_mode = UI_MODE_STEP_PIANO;
    UI_Sequencer_BeginStepPianoSession();

    {
        const uint8_t step_index = (uint8_t)(step - 1u);
        const BridgeLedgerSlot slot0 = Bridge_GetStepLedgerSlotMidi(step_index, 0u);
        s_step_piano_slot = 0u;
        s_step_piano_slot_count = UI_Sequencer_NormalizeStepPianoSlots(Bridge_GetStepLedgerLength(step_index));
        s_step_piano_selected_midi = UI_Sequencer_GetFirstMidiFromSlot(&slot0, (uint8_t)kLegacyPitchClassFallbackMidiBase);
        s_step_piano_top_midi = UI_StepPiano_ClampTopMidi((int16_t)s_step_piano_selected_midi + 9);
        s_step_piano_selected_midi = UI_StepPiano_ClampCursorToViewport(s_step_piano_selected_midi);
        s_step_piano_confirm_active = 0u;
        s_step_piano_confirm_yes_selected = 0u;
        s_step_piano_confirm_action = 0u;
        s_step_piano_confirm_message[0] = '\0';
        s_step_piano_length_mode_active = 0u;
        s_step_piano_length_pending = 1u;
    }

    UI_PianoRollScreen_ResetCache();
    ST7789_DebugSetPianoEvent(ST7789_PIANO_EVENT_ENTER);
    UI_Sequencer_RefreshStepPianoContext(step);

    if (UI_ScreenRouter_GetActive() == UI_MainGridScreen_Get())
    {
        if (UI_ScreenRouter_Push(UI_PianoRollScreen_Get()))
        {
            return;
        }
    }

    UI_Sequencer_RefreshStepPianoContext(step);
}

static void UI_Sequencer_ExitStepPiano(uint8_t save)
{
    UiScreen* active = UI_ScreenRouter_GetActive();

    UI_Sequencer_EndStepPianoSession(save);
    UI_Sequencer_EndDirectShortcut();
    s_exit_confirm_active = 0u;
    s_exit_confirm_action = UI_EXIT_CONFIRM_ACTION_NONE;
    s_exit_confirm_message[0] = '\0';
    s_step_piano_length_mode_active = 0u;
    s_step_piano_length_pending = 1u;

    s_ui_mode = UI_MODE_GRID;
    UI_MainGridScreen_SetContext(s_selected_step,
                                 s_active_step,
                                 NULL);

    if (active == UI_PianoRollScreen_Get())
    {
        if (UI_ScreenRouter_Pop(save ? SCREEN_EXIT_SAVE : SCREEN_EXIT_BACK))
        {
            return;
        }
    }

    UI_Sequencer_SetActiveScreen(UI_MainGridScreen_Get());
}

static void UI_StepPiano_ActionMovePitch(int8_t delta)
{
    if (delta == 0) return;
    if (s_step_piano_confirm_active) return;
    if (s_step_piano_length_mode_active) return;

    int16_t next = (int16_t)s_step_piano_selected_midi + (int16_t)delta;
    const uint8_t top = s_step_piano_top_midi;
    const uint8_t bottom = UI_StepPiano_GetBottomMidi(top);

    if (next > (int16_t)top) next = (int16_t)top;
    if (next < (int16_t)bottom) next = (int16_t)bottom;

    ST7789_DebugSetPianoEvent(ST7789_PIANO_EVENT_ENCODER);
    s_step_piano_selected_midi = (uint8_t)next;
    UI_Sequencer_RefreshStepPianoContext(s_selected_step);
}

static void UI_StepPiano_ActionNextPosition(void)
{
    if (s_step_piano_confirm_active)
    {
        UI_StepPiano_ActionConfirmModal();
        return;
    }
    if (s_step_piano_length_mode_active)
    {
        /* Encoder short press is intentionally ignored while LENGTH mode is active. */
        return;
    }
    if (s_step_piano_slot_count < 1u) return;

    s_step_piano_slot = (uint8_t)((s_step_piano_slot + 1u) % s_step_piano_slot_count);
    UI_Sequencer_RefreshStepPianoContext(s_selected_step);
}

static void UI_StepPiano_ActionToggleBarSkip(void)
{
    if (s_step_piano_confirm_active) return;
    if (s_selected_step < 1u || s_selected_step > 12u) return;

    const uint8_t step_index = (uint8_t)(s_selected_step - 1u);
    const uint8_t current = Bridge_GetStepBarState(step_index);
    const uint8_t next = (current == 0u) ? 1u : 0u;
    Bridge_SetStepBarState(step_index, next);
    UI_Sequencer_RefreshStepPianoContext(s_selected_step);
}

static void UI_StepPiano_ActionToggleNote(void)
{
    if (s_step_piano_confirm_active) return;
    if (s_selected_step < 1u || s_selected_step > 12u) return;

    const uint8_t step_index = (uint8_t)(s_selected_step - 1u);
    BridgeLedgerSlot slot = Bridge_GetStepLedgerSlotMidi(step_index, s_step_piano_slot);
    uint8_t found = 0xFFu;
    uint8_t empty = 0xFFu;

    UI_Sequencer_MarkStepPianoTouched(step_index);
    Bridge_SetStepLedgerLength(step_index, s_step_piano_slot_count);

    for (uint8_t i = 0u; i < 4u; ++i)
    {
        if (slot.notes[i] == s_step_piano_selected_midi)
        {
            found = i;
            break;
        }
        if (empty == 0xFFu && slot.notes[i] > 127u)
        {
            empty = i;
        }
    }

    if (found != 0xFFu)
    {
        slot.notes[found] = 0xFFu;
    }
    else if (empty != 0xFFu)
    {
        slot.notes[empty] = s_step_piano_selected_midi;
    }

    Bridge_SetStepLedgerSlotMidi(step_index, s_step_piano_slot, &slot);
    UI_Sequencer_RefreshStepPianoContext(s_selected_step);
}

static void UI_StepPiano_ActionClearBarRequest(void)
{
    if (s_step_piano_length_mode_active) return;

    s_step_piano_confirm_active = 1u;
    s_step_piano_confirm_yes_selected = 0u; /* Default NO. */
    s_step_piano_confirm_action = 1u;
    snprintf(s_step_piano_confirm_message,
             sizeof(s_step_piano_confirm_message),
             "CLEAR BAR%02u?",
             (unsigned)s_selected_step);
    UI_Sequencer_RefreshStepPianoContext(s_selected_step);
}

static void UI_StepPiano_ActionOctaveUp(void)
{
    if (s_step_piano_confirm_active) return;
    if (s_step_piano_length_mode_active) return;

    ST7789_DebugSetPianoEvent(ST7789_PIANO_EVENT_OCT_UP);

    const uint8_t prev_top = s_step_piano_top_midi;
    const uint8_t prev_bottom = UI_StepPiano_GetBottomMidi(prev_top);
    const uint8_t prev_row = (uint8_t)(prev_top - s_step_piano_selected_midi);

    s_step_piano_top_midi = UI_StepPiano_ClampTopMidi((int16_t)prev_top + 12);
    if (s_step_piano_top_midi == prev_top)
    {
        /* Upper boundary reached: do not move cursor, do not redraw. */
        return;
    }
    {
        const uint8_t new_bottom = UI_StepPiano_GetBottomMidi(s_step_piano_top_midi);
        int16_t shifted = (int16_t)s_step_piano_selected_midi + 12;
        if (shifted > (int16_t)s_step_piano_top_midi) shifted = (int16_t)s_step_piano_top_midi;
        if (shifted < (int16_t)new_bottom) shifted = (int16_t)new_bottom;

        /* Preserve relative row when clamping at MIDI bounds prevents exact +12 shift. */
        if ((uint8_t)shifted > s_step_piano_top_midi) shifted = s_step_piano_top_midi;
        if ((uint8_t)shifted < new_bottom) shifted = new_bottom;
        s_step_piano_selected_midi = (uint8_t)shifted;

        if (prev_top == s_step_piano_top_midi && prev_bottom == new_bottom)
        {
            const uint8_t candidate = (uint8_t)(s_step_piano_top_midi - prev_row);
            s_step_piano_selected_midi = UI_StepPiano_ClampCursorToViewport(candidate);
        }
    }

    UI_Sequencer_RefreshStepPianoContext(s_selected_step);
}

static void UI_StepPiano_ActionOctaveDown(void)
{
    if (s_step_piano_confirm_active) return;
    if (s_step_piano_length_mode_active) return;

    ST7789_DebugSetPianoEvent(ST7789_PIANO_EVENT_OCT_DOWN);

    const uint8_t prev_top = s_step_piano_top_midi;
    const uint8_t prev_bottom = UI_StepPiano_GetBottomMidi(prev_top);
    const uint8_t prev_row = (uint8_t)(prev_top - s_step_piano_selected_midi);

    s_step_piano_top_midi = UI_StepPiano_ClampTopMidi((int16_t)prev_top - 12);
    if (s_step_piano_top_midi == prev_top)
    {
        /* Lower boundary reached: do not move cursor, do not redraw. */
        return;
    }
    {
        const uint8_t new_bottom = UI_StepPiano_GetBottomMidi(s_step_piano_top_midi);
        int16_t shifted = (int16_t)s_step_piano_selected_midi - 12;
        if (shifted > (int16_t)s_step_piano_top_midi) shifted = (int16_t)s_step_piano_top_midi;
        if (shifted < (int16_t)new_bottom) shifted = (int16_t)new_bottom;

        if ((uint8_t)shifted > s_step_piano_top_midi) shifted = s_step_piano_top_midi;
        if ((uint8_t)shifted < new_bottom) shifted = new_bottom;
        s_step_piano_selected_midi = (uint8_t)shifted;

        if (prev_top == s_step_piano_top_midi && prev_bottom == new_bottom)
        {
            const uint8_t candidate = (uint8_t)(s_step_piano_top_midi - prev_row);
            s_step_piano_selected_midi = UI_StepPiano_ClampCursorToViewport(candidate);
        }
    }

    UI_Sequencer_RefreshStepPianoContext(s_selected_step);
}

static void UI_StepPiano_ActionExitBarRequest(void)
{
    if (s_step_piano_length_mode_active) return;

    s_step_piano_confirm_active = 1u;
    s_step_piano_confirm_yes_selected = 0u; /* Default NO. */
    s_step_piano_confirm_action = 2u;
    snprintf(s_step_piano_confirm_message,
             sizeof(s_step_piano_confirm_message),
             "LEAVE BAR%02u?",
             (unsigned)s_selected_step);
    UI_Sequencer_RefreshStepPianoContext(s_selected_step);
}

static void UI_StepPiano_ActionConfirmModal(void)
{
    if (!s_step_piano_confirm_active) return;

    if (!s_step_piano_confirm_yes_selected)
    {
        s_step_piano_confirm_active = 0u;
        s_step_piano_confirm_yes_selected = 0u;
        s_step_piano_confirm_action = 0u;
        s_step_piano_confirm_message[0] = '\0';
        UI_Sequencer_RefreshStepPianoContext(s_selected_step);
        return;
    }

    if (s_step_piano_confirm_action == 1u)
    {
        if (s_selected_step >= 1u && s_selected_step <= 12u)
        {
            const uint8_t step_index = (uint8_t)(s_selected_step - 1u);
            const BridgeLedgerSlot empty_slot = {{0xFFu, 0xFFu, 0xFFu, 0xFFu}};

            UI_Sequencer_MarkStepPianoTouched(step_index);
            Bridge_SetStepLedgerLength(step_index, s_step_piano_slot_count);
            for (uint8_t i = 0u; i < s_step_piano_slot_count; ++i)
            {
                Bridge_SetStepLedgerSlotMidi(step_index, i, &empty_slot);
            }
            Bridge_SetStepLedgerLength(step_index, s_step_piano_slot_count);
        }

        s_step_piano_slot = 0u;

        s_step_piano_confirm_active = 0u;
        s_step_piano_confirm_yes_selected = 0u;
        s_step_piano_confirm_action = 0u;
        s_step_piano_confirm_message[0] = '\0';
        UI_Sequencer_RefreshStepPianoContext(s_selected_step);
        return;
    }

    if (s_step_piano_confirm_action == 2u)
    {
        s_step_piano_confirm_active = 0u;
        s_step_piano_confirm_yes_selected = 0u;
        s_step_piano_confirm_action = 0u;
        s_step_piano_confirm_message[0] = '\0';
        UI_StepPiano_ActionExitBar();
        return;
    }

    s_step_piano_confirm_active = 0u;
    s_step_piano_confirm_yes_selected = 0u;
    s_step_piano_confirm_action = 0u;
    s_step_piano_confirm_message[0] = '\0';
    UI_Sequencer_RefreshStepPianoContext(s_selected_step);
}

static void UI_StepPiano_ActionToggleLengthMode(void)
{
    if (s_step_piano_confirm_active) return;
    if (s_selected_step < 1u || s_selected_step > 12u) return;

    const uint8_t step_index = (uint8_t)(s_selected_step - 1u);

    if (!s_step_piano_length_mode_active)
    {
        const uint8_t current_len = Bridge_GetStepEventLength(step_index, s_step_piano_slot);
        if (current_len == 0u)
        {
            /* M8 on an empty/non-start slot: no modal, no state change. */
            return;
        }

        s_step_piano_length_mode_active = 1u;
        s_step_piano_length_pending = current_len;
        UI_Sequencer_RefreshStepPianoContext(s_selected_step);
        return;
    }

    Bridge_SetStepEventLength(step_index, s_step_piano_slot, s_step_piano_length_pending);
    s_step_piano_length_mode_active = 0u;
    UI_Sequencer_RefreshStepPianoContext(s_selected_step);
}

static void UI_StepPiano_ActionExitBar(void)
{
    s_step_piano_confirm_active = 0u;
    s_step_piano_confirm_yes_selected = 0u;
    s_step_piano_confirm_action = 0u;
    s_step_piano_confirm_message[0] = '\0';
    UI_Sequencer_ExitStepPiano(1u);
}

static void UI_Sequencer_SetMainMode(UiMainMode mode)
{
    s_main_mode = mode;
    UI_Display_SetMainMode(mode);
}

static void UI_Sequencer_BeginDirectShortcut(UiDirectShortcut shortcut)
{
    s_return_target = UI_RETURN_TARGET_BAR_EDITOR;
    s_direct_shortcut = shortcut;
}

static void UI_Sequencer_EndDirectShortcut(void)
{
    s_return_target = UI_RETURN_TARGET_GRID;
    s_direct_shortcut = UI_DIRECT_SHORTCUT_NONE;
}

static uint8_t UI_Sequencer_IsDirectShortcutActive(void)
{
    return (s_return_target == UI_RETURN_TARGET_BAR_EDITOR &&
            s_direct_shortcut != UI_DIRECT_SHORTCUT_NONE) ? 1u : 0u;
}

static uint8_t UI_Sequencer_ReturnToBarEditor(ScreenExitReason reason)
{
    UiScreen* active = UI_ScreenRouter_GetActive();

    while (active && active != UI_PianoRollScreen_Get())
    {
        if (!UI_ScreenRouter_Pop(reason))
        {
            break;
        }
        active = UI_ScreenRouter_GetActive();
    }

    if (active == UI_PianoRollScreen_Get())
    {
        s_ui_mode = UI_MODE_STEP_PIANO;
        s_exit_confirm_active = 0u;
        s_exit_confirm_action = UI_EXIT_CONFIRM_ACTION_NONE;
        s_exit_confirm_message[0] = '\0';
        s_direct_chord_confirm.active = 0u;
        s_direct_chord_confirm.yes_selected = 0u;
        s_direct_chord_confirm.source = UI_DIRECT_CHORD_SOURCE_NONE;
        s_direct_chord_confirm_message[0] = '\0';
        UI_Sequencer_EndDirectShortcut();
        UI_Sequencer_EndDirectChordContext();
        /* Ensure stale direct-chord pixels cannot survive behind piano-roll incremental redraws. */
        UI_PianoRollScreen_ResetCache();
        UI_Sequencer_RefreshStepPianoContext(s_selected_step);
        UI_PianoRollScreen_ForceRedraw();
        if (active->on_draw)
        {
            active->on_draw();
        }
        return 1u;
    }

    return 0u;
}

static void UI_Sequencer_BeginExitConfirm(const char* message, UiExitConfirmAction action)
{
    if (!message || message[0] == '\0')
    {
        message = "LEAVE?";
    }

    s_exit_confirm_active = 1u;
    s_exit_confirm_yes_selected = 0u; /* default NO */
    s_exit_confirm_action = action;
    snprintf(s_exit_confirm_message, sizeof(s_exit_confirm_message), "%s", message);
    UI_Display_DrawYesNoQuestionModal(s_exit_confirm_message, s_exit_confirm_yes_selected);
}

static void UI_Sequencer_DismissExitConfirm(void)
{
    UiScreen* active = UI_ScreenRouter_GetActive();

    s_exit_confirm_active = 0u;
    s_exit_confirm_yes_selected = 0u;
    s_exit_confirm_action = UI_EXIT_CONFIRM_ACTION_NONE;
    s_exit_confirm_message[0] = '\0';
    s_direct_chord_ctx.active = 0u;
    s_direct_chord_ctx.step_index = 0u;
    s_direct_chord_ctx.slot_index = 0u;
    s_direct_chord_ctx.anchor_midi = (uint8_t)kLegacyPitchClassFallbackMidiBase;
    s_direct_chord_confirm.active = 0u;
    s_direct_chord_confirm.yes_selected = 0u;
    s_direct_chord_confirm.source = UI_DIRECT_CHORD_SOURCE_NONE;
    s_direct_chord_confirm.note_mask = 0u;
    s_direct_chord_confirm.root_key = 0u;
    s_direct_chord_confirm.chord_type = 0u;
    s_direct_chord_confirm_message[0] = '\0';

    if (active && active->on_draw)
    {
        active->on_draw();
    }
}

static void UI_Sequencer_HandleExitConfirmDelta(int8_t delta)
{
    if (!s_exit_confirm_active || delta == 0)
    {
        return;
    }

    s_exit_confirm_yes_selected = (delta > 0) ? 1u : 0u;
    UI_Display_DrawYesNoQuestionModal(s_exit_confirm_message, s_exit_confirm_yes_selected);
}

static void UI_Sequencer_ConfirmExitConfirmSelection(void)
{
    if (!s_exit_confirm_active)
    {
        return;
    }

    if (!s_exit_confirm_yes_selected)
    {
        UI_Sequencer_DismissExitConfirm();
        return;
    }

    s_exit_confirm_active = 0u;
    s_exit_confirm_yes_selected = 0u;

    switch (s_exit_confirm_action)
    {
        case UI_EXIT_CONFIRM_ACTION_CHORD:
            if (UI_Sequencer_IsDirectChordContextActive())
            {
                (void)UI_Sequencer_ReturnToBarEditor(SCREEN_EXIT_BACK);
                break;
            }

            if (s_ui_mode == UI_MODE_CHORD_PARAMS)
            {
                UI_Route_RecBackToChordMenu();
            }
            else
            {
                UI_Route_RecExitToGrid();
            }
            break;

        case UI_EXIT_CONFIRM_ACTION_TIMING:
            if (s_ui_mode == UI_MODE_TIMING_MENU)
            {
                UI_Sequencer_ExitTimingMenu(1u);
            }
            else if (UI_Sequencer_IsDirectShortcutActive())
            {
                UI_Sequencer_ExitToGrid();
            }
            else
            {
                UI_Route_RecTimingMenu();
            }
            break;

        case UI_EXIT_CONFIRM_ACTION_ARP:
            if (s_ui_mode == UI_MODE_ARP_MENU)
            {
                UI_Sequencer_ExitArpMenu(1u);
            }
            else if (s_return_target == UI_RETURN_TARGET_BAR_EDITOR)
            {
                (void)UI_Sequencer_ReturnToBarEditor(SCREEN_EXIT_BACK);
            }
            else
            {
                UI_Sequencer_ExitToGrid();
            }
            break;

        default:
            UI_Sequencer_DismissExitConfirm();
            break;
    }

    s_exit_confirm_action = UI_EXIT_CONFIRM_ACTION_NONE;
    s_exit_confirm_message[0] = '\0';
}

static uint8_t UI_Sequencer_HandleEncoderLongPressForDirectShortcut(void)
{
    if (!UI_Sequencer_IsDirectShortcutActive())
    {
        return 0u;
    }

    if (s_direct_chord_confirm.active)
    {
        return 1u;
    }

    if (s_exit_confirm_active)
    {
        return 1u;
    }

    if (s_ui_mode == UI_MODE_TIMING_MENU)
    {
        UI_Sequencer_BeginExitConfirm("LEAVE TIMING?", UI_EXIT_CONFIRM_ACTION_TIMING);
        return 1u;
    }

    if (s_ui_mode == UI_MODE_ARP_MENU)
    {
        UI_Sequencer_BeginExitConfirm("LEAVE ARP?", UI_EXIT_CONFIRM_ACTION_ARP);
        return 1u;
    }

    if (s_ui_mode == UI_MODE_CHORD_MENU || s_ui_mode == UI_MODE_CHORD_PARAMS)
    {
        if (s_direct_shortcut == UI_DIRECT_SHORTCUT_ARP)
        {
            UI_Sequencer_BeginExitConfirm("LEAVE ARP?", UI_EXIT_CONFIRM_ACTION_ARP);
        }
        else
        {
            UI_Sequencer_BeginExitConfirm("LEAVE CHORD?", UI_EXIT_CONFIRM_ACTION_CHORD);
        }
        return 1u;
    }

    return 0u;
}

static void UI_Sequencer_EnterChordFromBarEditor(void)
{
    UI_Sequencer_BeginDirectShortcut(UI_DIRECT_SHORTCUT_CHORD);
    UI_Sequencer_BeginDirectChordContext();
    UI_Sequencer_EnterChordParamsAtCursor(s_selected_step, 0u, UI_DIRECT_SHORTCUT_CHORD);
}

static void UI_Sequencer_EnterTimingFromBarEditor(void)
{
    UI_Sequencer_BeginDirectShortcut(UI_DIRECT_SHORTCUT_TIMING);
    UI_Sequencer_EnterTimingMenu(1u);
}

static void UI_Sequencer_EnterArpFromBarEditor(void)
{
    UI_Sequencer_BeginDirectShortcut(UI_DIRECT_SHORTCUT_ARP);
    UI_Sequencer_EnterArpMenu(0u);
}

static void UI_Sequencer_BeginDirectChordContext(void)
{
    s_direct_chord_ctx.active = 1u;
    s_direct_chord_ctx.step_index = (s_selected_step > 0u) ? (uint8_t)(s_selected_step - 1u) : 0u;
    s_direct_chord_ctx.slot_index = s_step_piano_slot;
    s_direct_chord_ctx.anchor_midi = s_step_piano_selected_midi;
}

static void UI_Sequencer_EndDirectChordContext(void)
{
    s_direct_chord_ctx.active = 0u;
    s_direct_chord_ctx.step_index = 0u;
    s_direct_chord_ctx.slot_index = 0u;
    s_direct_chord_ctx.anchor_midi = (uint8_t)kLegacyPitchClassFallbackMidiBase;
}

static uint8_t UI_Sequencer_IsDirectChordContextActive(void)
{
    return (s_direct_chord_ctx.active &&
            s_direct_shortcut == UI_DIRECT_SHORTCUT_CHORD &&
            s_return_target == UI_RETURN_TARGET_BAR_EDITOR) ? 1u : 0u;
}

static uint8_t UI_Sequencer_IsSlotOccupied(uint8_t step_index, uint8_t slot_index)
{
    BridgeLedgerSlot slot = Bridge_GetStepLedgerSlotMidi(step_index, slot_index);
    for (uint8_t i = 0u; i < 4u; ++i)
    {
        if (slot.notes[i] <= 127u)
        {
            return 1u;
        }
    }
    return 0u;
}

static uint8_t UI_Sequencer_FindNearestRootMidi(uint8_t root_key, uint8_t anchor_midi, uint8_t* out_root_midi)
{
    if (!out_root_midi) return 0u;

    const uint8_t root_pc = (uint8_t)(root_key % 12u);
    int16_t best_note = -1;
    uint16_t best_dist = 0xFFFFu;

    for (int16_t octave = 1; octave <= 8; ++octave)
    {
        const int16_t candidate = (int16_t)(octave * 12 + root_pc);
        if (candidate < 12 || candidate > 108) continue;

        const uint16_t dist = (uint16_t)((candidate > anchor_midi)
            ? (candidate - anchor_midi)
            : (anchor_midi - candidate));

        if (best_note < 0 || dist < best_dist || (dist == best_dist && candidate < best_note))
        {
            best_note = candidate;
            best_dist = dist;
        }
    }

    if (best_note < 12)
    {
        return 0u;
    }

    *out_root_midi = (uint8_t)best_note;
    return 1u;
}

static uint8_t UI_Sequencer_BuildAbsoluteNotesFromMask(uint16_t note_mask,
                                                       uint8_t root_key,
                                                       uint8_t anchor_midi,
                                                       BridgeLedgerSlot* out_slot)
{
    if (!out_slot) return 0u;

    out_slot->notes[0] = 0xFFu;
    out_slot->notes[1] = 0xFFu;
    out_slot->notes[2] = 0xFFu;
    out_slot->notes[3] = 0xFFu;

    note_mask = (uint16_t)(note_mask & 0x0FFFu);
    if (note_mask == 0u)
    {
        return 1u;
    }

    uint8_t root_midi = 0u;
    if (!UI_Sequencer_FindNearestRootMidi(root_key, anchor_midi, &root_midi))
    {
        return 0u;
    }

    uint8_t count = 0u;
    for (uint8_t offset = 0u; offset < 12u && count < 4u; ++offset)
    {
        const uint8_t pc = (uint8_t)((root_key + offset) % 12u);
        if ((note_mask & (uint16_t)(1u << pc)) == 0u)
        {
            continue;
        }

        const int16_t note = (int16_t)root_midi + offset;
        if (note < 12 || note > 108)
        {
            continue;
        }

        out_slot->notes[count++] = (uint8_t)note;
    }

    return (count > 0u) ? 1u : 0u;
}

static uint8_t UI_Sequencer_BuildAbsoluteNotesFromRootType(uint8_t root_key,
                                                           uint8_t chord_type,
                                                           uint8_t anchor_midi,
                                                           BridgeLedgerSlot* out_slot)
{
    const uint16_t note_mask = Bridge_GetChordMaskForUiParams(root_key, chord_type);
    return UI_Sequencer_BuildAbsoluteNotesFromMask(note_mask, root_key, anchor_midi, out_slot);
}

static void UI_Sequencer_BeginDirectChordConfirm(const char* message,
                                                 UiDirectChordSource source,
                                                 uint16_t note_mask,
                                                 uint8_t root_key,
                                                 uint8_t chord_type)
{
    if (!message || message[0] == '\0')
    {
        message = "REPLACE NOTES?";
    }

    s_direct_chord_confirm.active = 1u;
    s_direct_chord_confirm.yes_selected = 0u;
    s_direct_chord_confirm.source = source;
    s_direct_chord_confirm.note_mask = note_mask;
    s_direct_chord_confirm.root_key = root_key;
    s_direct_chord_confirm.chord_type = chord_type;
    snprintf(s_direct_chord_confirm_message, sizeof(s_direct_chord_confirm_message), "%s", message);
    UI_Display_DrawYesNoQuestionModal(s_direct_chord_confirm_message, s_direct_chord_confirm.yes_selected);
}

static void UI_Sequencer_DismissDirectChordConfirm(void)
{
    UiScreen* active = UI_ScreenRouter_GetActive();

    s_direct_chord_confirm.active = 0u;
    s_direct_chord_confirm.yes_selected = 0u;
    s_direct_chord_confirm.source = UI_DIRECT_CHORD_SOURCE_NONE;
    s_direct_chord_confirm.note_mask = 0u;
    s_direct_chord_confirm.root_key = 0u;
    s_direct_chord_confirm.chord_type = 0u;
    s_direct_chord_confirm_message[0] = '\0';

    if (active && active->on_draw)
    {
        active->on_draw();
    }
}

static void UI_Sequencer_HandleDirectChordConfirmDelta(int8_t delta)
{
    if (!s_direct_chord_confirm.active || delta == 0)
    {
        return;
    }

    s_direct_chord_confirm.yes_selected = (delta > 0) ? 1u : 0u;
    UI_Display_DrawYesNoQuestionModal(s_direct_chord_confirm_message, s_direct_chord_confirm.yes_selected);
}

static uint8_t UI_Sequencer_CommitDirectChordToSlot(UiDirectChordSource source,
                                                    uint16_t note_mask,
                                                    uint8_t root_key,
                                                    uint8_t chord_type,
                                                    uint8_t allow_replace)
{
    if (!UI_Sequencer_IsDirectChordContextActive())
    {
        return 0u;
    }

    const uint8_t step_index = s_direct_chord_ctx.step_index;
    const uint8_t slot_index = s_direct_chord_ctx.slot_index;
    const uint8_t occupied = UI_Sequencer_IsSlotOccupied(step_index, slot_index);

    if (!allow_replace && occupied)
    {
        const char* message = (source == UI_DIRECT_CHORD_SOURCE_CLEAR) ? "CLEAR NOTES?" : "REPLACE NOTES?";
        UI_Sequencer_BeginDirectChordConfirm(message, source, note_mask, root_key, chord_type);
        return 0u;
    }

    BridgeLedgerSlot slot = {{0xFFu, 0xFFu, 0xFFu, 0xFFu}};
    uint8_t ok = 1u;

    if (source == UI_DIRECT_CHORD_SOURCE_ROOT_TYPE)
    {
        ok = UI_Sequencer_BuildAbsoluteNotesFromRootType(root_key,
                                                         chord_type,
                                                         s_direct_chord_ctx.anchor_midi,
                                                         &slot);
    }
    else if (source == UI_DIRECT_CHORD_SOURCE_MASK)
    {
        ok = UI_Sequencer_BuildAbsoluteNotesFromMask(note_mask,
                                                     root_key,
                                                     s_direct_chord_ctx.anchor_midi,
                                                     &slot);
    }
    else if (source == UI_DIRECT_CHORD_SOURCE_CLEAR)
    {
        ok = 1u;
    }
    else
    {
        ok = 0u;
    }

    if (!ok)
    {
        return 0u;
    }

    Bridge_SetStepLedgerSlotMidi(step_index, slot_index, &slot);
    return 1u;
}

static uint8_t UI_Sequencer_ConfirmDirectChordSelection(void)
{
    if (!s_direct_chord_confirm.active)
    {
        return 0u;
    }

    if (!s_direct_chord_confirm.yes_selected)
    {
        UI_Sequencer_DismissDirectChordConfirm();
        return 1u;
    }

    const UiDirectChordSource source = s_direct_chord_confirm.source;
    const uint16_t note_mask = s_direct_chord_confirm.note_mask;
    const uint8_t root_key = s_direct_chord_confirm.root_key;
    const uint8_t chord_type = s_direct_chord_confirm.chord_type;

    s_direct_chord_confirm.active = 0u;
    s_direct_chord_confirm.yes_selected = 0u;
    s_direct_chord_confirm.source = UI_DIRECT_CHORD_SOURCE_NONE;
    s_direct_chord_confirm.note_mask = 0u;
    s_direct_chord_confirm.root_key = 0u;
    s_direct_chord_confirm.chord_type = 0u;
    s_direct_chord_confirm_message[0] = '\0';

    if (UI_Sequencer_CommitDirectChordToSlot(source, note_mask, root_key, chord_type, 1u))
    {
        UI_Sequencer_SetMainMode(UI_MAIN_MODE_STEP);
        (void)UI_Sequencer_ReturnToBarEditor(SCREEN_EXIT_SAVE);
    }
    return 1u;
}

static void UI_Sequencer_LoadSongChainDraft(void)
{
    s_song_length = Bridge_GetChainLength();
    if (s_song_length < 1) s_song_length = 1;
    if (s_song_length > 32) s_song_length = 32;

    for (uint8_t i = 0; i < s_song_length; i++)
    {
        s_song_chain[i] = Bridge_GetChainPatternAt(i);
    }

    if (s_song_cursor >= s_song_length)
    {
        s_song_cursor = (uint8_t)(s_song_length - 1);
    }
}

static void UI_Sequencer_SetActiveScreen(UiScreen* screen)
{
    UI_ScreenRouter_SwitchTo(screen);
}

static void UI_Sequencer_EnterChordMenu(uint8_t step)
{
    UiScreen* active;

    if (step < 1 || step > 12) return;

    s_selected_step = step;
    s_menu_step = step;
    s_ui_mode = UI_MODE_CHORD_MENU;

    UI_Sequencer_HydrateStepChordFromEngine(step);
    s_last_predefined_chord = s_step_chords[step - 1].chord_type;
    UI_ChordMenuScreen_SetContext(step, &s_step_chords[step - 1]);

    active = UI_ScreenRouter_GetActive();
    if (active == UI_PianoRollScreen_Get())
    {
        if (UI_ScreenRouter_Push(UI_ChordMenuScreen_Get()))
        {
            return;
        }
    }

    UI_Sequencer_SetActiveScreen(UI_ChordMenuScreen_Get());
}

static void UI_Sequencer_EnterChordParams(uint8_t step)
{
    UI_Sequencer_EnterChordParamsAtCursor(step, 0u, UI_DIRECT_SHORTCUT_NONE);
}

static void UI_Sequencer_EnterChordParamsAtCursor(uint8_t step, uint8_t param_cursor, UiDirectShortcut shortcut_tag)
{
    if (step < 1 || step > 12) return;

    s_selected_step = step;
    s_menu_step = step;
    s_ui_mode = UI_MODE_CHORD_PARAMS;
    s_chord_params_timing_quick = 0;
    s_param_cursor = (param_cursor < 4u) ? param_cursor : 0u;

    if (shortcut_tag != UI_DIRECT_SHORTCUT_NONE)
    {
        s_direct_shortcut = shortcut_tag;
    }

    UI_Sequencer_HydrateStepChordFromEngine(step);
    UI_Sequencer_UpdateDirectChordDisplayInfo();
    UI_ChordParamsScreen_SetContext(step, &s_step_chords[step - 1], s_param_cursor);
    {
        const uint8_t direct_mode = UI_Sequencer_IsDirectChordContextActive();
        UI_Display_SetChordParamsDirectMode(direct_mode);
        if (direct_mode)
        {
            UI_ChordParamsScreen_SetFooterAction(PARAM_ACTION_MAIN);
            UI_Display_SetSelectedParamAction(PARAM_ACTION_MAIN);
        }
        else
        {
            UI_ChordParamsScreen_SetFooterAction(PARAM_ACTION_MAIN);
            UI_Display_SetSelectedParamAction(PARAM_ACTION_MAIN);
        }
    }
    {
        UiScreen* active = UI_ScreenRouter_GetActive();
        UiScreen* chord_menu_screen = UI_ChordMenuScreen_Get();
        UiScreen* chord_params_screen = UI_ChordParamsScreen_Get();

        if (active == chord_menu_screen || active == UI_PianoRollScreen_Get())
        {
            if (!UI_ScreenRouter_Push(chord_params_screen))
            {
                UI_Sequencer_SetActiveScreen(chord_params_screen);
            }
        }
        else
        {
            UI_Sequencer_SetActiveScreen(chord_params_screen);
        }
    }
}

static void UI_Sequencer_RefreshChordParamsScreen(void)
{
    if (s_ui_mode != UI_MODE_CHORD_PARAMS) return;

    UI_Sequencer_UpdateDirectChordDisplayInfo();
    UI_ChordParamsScreen_SetContext(s_menu_step, &s_step_chords[s_menu_step - 1], s_param_cursor);
    UI_Display_SetChordParamsDirectMode(UI_Sequencer_IsDirectChordContextActive());
    UI_ChordParamsScreen_SetFooterAction(UI_Display_GetSelectedParamAction());
    {
        UiScreen* active = UI_ScreenRouter_GetActive();
        if (active && active->on_draw)
        {
            active->on_draw();
        }
    }
}

static void UI_Sequencer_UpdateDirectChordDisplayInfo(void)
{
    if (!UI_Sequencer_IsDirectChordContextActive())
    {
        UI_Display_SetDirectChordInfo(0u, 0u, 4u, NULL);
        return;
    }

    BridgeLedgerSlot preview = {{0xFFu, 0xFFu, 0xFFu, 0xFFu}};
    const ChordParams* chord = &s_step_chords[s_menu_step - 1u];
    const uint8_t bar_number = (uint8_t)(s_direct_chord_ctx.step_index + 1u);
    const uint8_t pos_number = (uint8_t)(s_direct_chord_ctx.slot_index + 1u);
    const uint8_t grid_division = Bridge_GetPatternStepDivision();

    if (chord->chord_type != 0u)
    {
        (void)UI_Sequencer_BuildAbsoluteNotesFromRootType(chord->root_key,
                                                          chord->chord_type,
                                                          s_direct_chord_ctx.anchor_midi,
                                                          &preview);
    }

    UI_Display_SetDirectChordInfo(bar_number, pos_number, grid_division, preview.notes);
}

static void UI_Sequencer_DrawSongChainMenu(void)
{
    uint8_t playing_slot = 0;
    uint8_t repeat_index = 0;
    uint8_t repeat_total = Bridge_GetPatternRepeatCount();

    if (Bridge_IsPlaying())
    {
        playing_slot = Bridge_GetChainCurrentPosition();
        repeat_index = Bridge_GetCurrentPatternRepeatProgress();
    }
    else
    {
        playing_slot = s_song_cursor;
    }

    UI_Display_DrawSongChainMenu(s_song_chain,
                                 s_song_length,
                                 s_song_cursor,
                                 playing_slot,
                                 repeat_index,
                                 repeat_total,
                                 s_song_blink_on);
}

static void UI_Sequencer_HandleChordParamAction(uint8_t action)
{
    if (action >= PARAM_ACTION_COUNT) return;

    UI_ChordParamsScreen_CopyToChord(&s_step_chords[s_menu_step - 1]);
    UI_Display_SetSelectedParamAction(action);
    UI_ChordParamsScreen_SetFooterAction(action);
    UI_Display_DrawParamFooterActions(action);

    if (UI_Sequencer_IsDirectChordContextActive())
    {
        if (action == PARAM_ACTION_SAVE)
        {
            const uint8_t root_key = s_step_chords[s_menu_step - 1].root_key;
            const uint8_t chord_type = s_step_chords[s_menu_step - 1].chord_type;
            const UiDirectChordSource source = (chord_type == 0u)
                ? UI_DIRECT_CHORD_SOURCE_CLEAR
                : UI_DIRECT_CHORD_SOURCE_ROOT_TYPE;

            if (UI_Sequencer_CommitDirectChordToSlot(source,
                                                     0u,
                                                     root_key,
                                                     chord_type,
                                                     0u))
            {
                UI_Sequencer_SetMainMode(UI_MAIN_MODE_STEP);
                (void)UI_Sequencer_ReturnToBarEditor(SCREEN_EXIT_SAVE);
            }
            else
            {
                /* If commit opened REPLACE/CLEAR modal, do not repaint over it. */
                if (!s_direct_chord_confirm.active)
                {
                    UI_Sequencer_RefreshChordParamsScreen();
                }
            }
            return;
        }

        /* Direct chooser ignores footer-style actions outside PLACE. */
        UI_Display_SetSelectedParamAction(PARAM_ACTION_MAIN);
        UI_ChordParamsScreen_SetFooterAction(PARAM_ACTION_MAIN);
        UI_Sequencer_RefreshChordParamsScreen();
        return;
    }

    if (action == PARAM_ACTION_MAIN)
    {
        UI_Route_RecBackToChordMenu();
    }
    else if (action == PARAM_ACTION_SAVE)
    {
        UI_Sequencer_CommitChordDraft();
        if (!s_chord_params_timing_quick)
        {
            UI_Sequencer_SetMainMode(UI_MAIN_MODE_STEP);
        }
        /* Do not leave footer parked on SAVE; that locks encoder param edits. */
        UI_Display_SetSelectedParamAction(PARAM_ACTION_MAIN);
        UI_ChordParamsScreen_SetFooterAction(PARAM_ACTION_MAIN);
        UI_Display_DrawParamFooterActions(PARAM_ACTION_MAIN);
        UI_Sequencer_RefreshChordParamsScreen();
    }
    else if (action == PARAM_ACTION_PREV)
    {
        uint8_t from_step = s_menu_step;
        UI_Sequencer_CommitChordDraft();
        s_menu_step = (s_menu_step <= 1) ? 12 : (s_menu_step - 1);
        s_selected_step = s_menu_step;

        /* Continue editing workflow: carry current step params into target step. */
        s_step_chords[s_menu_step - 1] = s_step_chords[from_step - 1];

        UI_Sequencer_RefreshChordParamsScreen();
    }
    else if (action == PARAM_ACTION_NEXT)
    {
        uint8_t from_step = s_menu_step;
        UI_Sequencer_CommitChordDraft();
        s_menu_step = (s_menu_step >= 12) ? 1 : (s_menu_step + 1);
        s_selected_step = s_menu_step;

        /* Continue editing workflow: carry current step params into target step. */
        s_step_chords[s_menu_step - 1] = s_step_chords[from_step - 1];

        UI_Sequencer_RefreshChordParamsScreen();
    }
}

static void UI_Sequencer_DrawTimingMenu(void)
{
    UI_Display_DrawTimingMenu(s_timing_step_count,
                              s_timing_step_division,
                              s_timing_ts_num,
                              s_timing_ts_den,
                              s_timing_swing,
                              s_timing_cursor,
                              UI_Sequencer_TimingDraftIsDirty(),
                              s_timing_save_rejected);
}

static void UI_Sequencer_DrawArpMenu(void)
{
    UI_Display_DrawArpMenu(s_arp_mode,
                           s_arp_rate,
                           s_arp_cursor,
                           UI_Sequencer_ArpDraftIsDirty());
}

static void UI_Sequencer_LoadArpDraft(void)
{
    uint8_t mode = Bridge_GetCurrentPatternArpMode();
    uint8_t rate = Bridge_GetPatternArpRate();

    if (mode > 4u) mode = 0u;
    if (rate > 3u) rate = 2u;

    s_arp_mode = mode;
    s_arp_rate = rate;
}

static void UI_Sequencer_CommitArpDraft(void)
{
    Bridge_PersistBegin();
    Bridge_SetPatternArpMode(s_arp_mode);
    Bridge_SetPatternArpRate(s_arp_rate);
    Bridge_PersistEnd();
    UI_Sequencer_LoadArpDraft();
}

static uint8_t UI_Sequencer_ArpDraftIsDirty(void)
{
    if (s_arp_mode != Bridge_GetCurrentPatternArpMode()) return 1u;
    if (s_arp_rate != Bridge_GetPatternArpRate()) return 1u;
    return 0u;
}

static void UI_Sequencer_LoadTimingDraft(void)
{
    s_timing_step_count = Bridge_GetPatternStepCount();
    s_timing_step_division = Bridge_GetPatternStepDivision();
    s_timing_ts_num = Bridge_GetTimeSigNumerator();
    s_timing_ts_den = Bridge_GetTimeSigDenominator();
    s_timing_swing = Bridge_GetSwing();
    s_timing_save_rejected = 0u;
}

static uint8_t UI_Sequencer_CommitTimingDraft(void)
{
    Bridge_PersistBegin();
    if (!Bridge_SetPatternTiming(s_timing_step_division, s_timing_ts_num, s_timing_ts_den))
    {
        Bridge_PersistEnd();
        return 0u;
    }
    Bridge_SetPatternStepCount(s_timing_step_count);
    Bridge_SetSwing(s_timing_swing);
    Bridge_PersistEnd();
    UI_Sequencer_LoadTimingDraft();
    return 1u;
}

static uint8_t UI_Sequencer_TimingDraftIsDirty(void)
{
    if (s_timing_step_count != Bridge_GetPatternStepCount()) return 1;
    if (s_timing_step_division != Bridge_GetPatternStepDivision()) return 1;
    if (s_timing_ts_num != Bridge_GetTimeSigNumerator()) return 1;
    if (s_timing_ts_den != Bridge_GetTimeSigDenominator()) return 1;
    if (s_timing_swing != Bridge_GetSwing()) return 1;
    return 0;
}

static void UI_Sequencer_DrawQuantTimingMenu(void)
{
    UI_Display_DrawQuantiserTimingMenu(s_quant_enabled,
                                       s_quant_grid_division,
                                       s_quant_strength,
                                       s_quant_humanize_ms,
                                       s_quant_lag_ms,
                                       s_quant_cursor,
                                       UI_Sequencer_QuantTimingDraftIsDirty());
}

static void UI_Sequencer_LoadQuantTimingDraft(void)
{
    s_quant_enabled = s_quant_saved_enabled;
    s_quant_grid_division = s_quant_saved_grid_division;
    s_quant_strength = s_quant_saved_strength;
    s_quant_humanize_ms = s_quant_saved_humanize_ms;
    s_quant_lag_ms = s_quant_saved_lag_ms;
}

static void UI_Sequencer_CommitQuantTimingDraft(void)
{
    s_quant_saved_enabled = s_quant_enabled;
    s_quant_saved_grid_division = s_quant_grid_division;
    s_quant_saved_strength = s_quant_strength;
    s_quant_saved_humanize_ms = s_quant_humanize_ms;
    s_quant_saved_lag_ms = s_quant_lag_ms;

    /* Immediate runtime linkage: swing reflects quantiser strength when enabled. */
    if (s_quant_enabled)
    {
        Bridge_SetSwing((uint8_t)((s_quant_strength > 75) ? 75 : s_quant_strength));
    }
}

static uint8_t UI_Sequencer_QuantTimingDraftIsDirty(void)
{
    if (s_quant_enabled != s_quant_saved_enabled) return 1;
    if (s_quant_grid_division != s_quant_saved_grid_division) return 1;
    if (s_quant_strength != s_quant_saved_strength) return 1;
    if (s_quant_humanize_ms != s_quant_saved_humanize_ms) return 1;
    if (s_quant_lag_ms != s_quant_saved_lag_ms) return 1;
    return 0;
}

static void UI_Sequencer_DrawQuanRouterMenu(void)
{
    UI_Display_DrawQuanRouterMenu(s_quan_router_source,
                                  s_quan_router_target,
                                  s_quan_router_cursor,
                                  s_quan_router_field,
                                  UI_Sequencer_QuanRouterDraftIsDirty());
}

static void UI_Sequencer_LoadQuanRouterDraft(void)
{
    memcpy(s_quan_router_source, s_quan_router_saved_source, sizeof(s_quan_router_source));
    memcpy(s_quan_router_target, s_quan_router_saved_target, sizeof(s_quan_router_target));
}

static void UI_Sequencer_CommitQuanRouterDraft(void)
{
    memcpy(s_quan_router_saved_source, s_quan_router_source, sizeof(s_quan_router_source));
    memcpy(s_quan_router_saved_target, s_quan_router_target, sizeof(s_quan_router_target));
}

static uint8_t UI_Sequencer_QuanRouterDraftIsDirty(void)
{
    if (memcmp(s_quan_router_source, s_quan_router_saved_source, sizeof(s_quan_router_source)) != 0) return 1;
    if (memcmp(s_quan_router_target, s_quan_router_saved_target, sizeof(s_quan_router_target)) != 0) return 1;
    return 0;
}

static void UI_Sequencer_CommitChordDraft(void)
{
    UI_Sequencer_SaveChordDraftForPattern();

    Bridge_PersistBegin();
    for (uint8_t i = 0; i < 12; i++)
    {
        Bridge_SetStepChordParams(i,
                                  s_step_chords[i].root_key,
                                  s_step_chords[i].chord_type,
                                  s_step_chords[i].arp_pattern,
                                  s_step_chords[i].duration,
                                  s_step_chords[i].loop_count);
    }
    Bridge_PersistEnd();
}

static char UI_Sequencer_CycleNameChar(char current, int8_t delta)
{
    static const char k_name_chars[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-";
    const int16_t count = (int16_t)(sizeof(k_name_chars) - 1);
    int16_t index = 0;

    for (int16_t i = 0; i < count; i++)
    {
        if (k_name_chars[i] == current)
        {
            index = i;
            break;
        }
    }

    index += delta;
    while (index < 0) index += count;
    while (index >= count) index -= count;
    return k_name_chars[index];
}

static void UI_Sequencer_StartUserChordNameEdit(void)
{
    if (s_last_saved_user_chord == 0xFF)
    {
        uint8_t count = Bridge_UserChord_GetCount();
        for (uint8_t i = 0; i < count; i++)
        {
            if (Bridge_UserChord_Get(i))
            {
                s_last_saved_user_chord = i;
                break;
            }
        }
        if (s_last_saved_user_chord == 0xFF) return;
    }

    const UserChordInfo* chord_info = Bridge_UserChord_Get(s_last_saved_user_chord);
    if (!chord_info)
    {
        return;
    }

    memset(s_user_chord_name_edit, 0, sizeof(s_user_chord_name_edit));
    strncpy(s_user_chord_name_edit, chord_info->name, 16);
    s_user_chord_name_edit[16] = '\0';

    for (uint8_t i = 0; i < 16; i++)
    {
        if (s_user_chord_name_edit[i] == '\0')
        {
            s_user_chord_name_edit[i] = ' ';
        }
    }

    s_user_chord_name_cursor = 0;
    UI_Sequencer_EnterUserChordName();
}

/* ── Init ────────────────────────────────────────────────────────────────── */
void UI_Sequencer_Init(void)
{
    s_ui_mode       = UI_MODE_GRID;  // Start in grid mode
    s_selected_step = 1;
    s_active_step   = 0;
    s_last_active   = 0;
    s_menu_step     = 1;
    s_param_cursor  = 0;
    s_timing_cursor = 0;
    s_repeat_flash_on = 0;
    s_repeat_flash_ms = 0;
    s_return_target = UI_RETURN_TARGET_GRID;
    s_direct_shortcut = UI_DIRECT_SHORTCUT_NONE;
    s_exit_confirm_active = 0u;
    s_exit_confirm_yes_selected = 0u;
    s_exit_confirm_action = UI_EXIT_CONFIRM_ACTION_NONE;
    s_exit_confirm_message[0] = '\0';

    /* Initialize per-pattern chord drafts (32 patterns x 12 steps). */
    for (uint8_t p = 0; p < 32; p++) {
        for (uint8_t i = 0; i < 12; i++) {
            s_pattern_step_chords[p][i].root_key = 0;    /* C */
            s_pattern_step_chords[p][i].chord_type = 0;  /* Clear */
            s_pattern_step_chords[p][i].arp_pattern = 0; /* Block */
            s_pattern_step_chords[p][i].duration = 1;    /* 8th notes */
            s_pattern_step_chords[p][i].loop_count = 1;
        }
    }

    UI_Sequencer_LoadChordDraftForPattern(Bridge_GetCurrentPattern());

    UI_ScreenRouter_Init();
    UI_Display_Init();    /* draws all boxes white, no selection */
    UI_Display_SetRepeatFlash(0, 0);
    UI_Sequencer_SetMainMode(s_main_mode);
    UI_Transport_Init();

    UI_MainGridScreen_SetContext(s_selected_step, s_active_step, NULL);
    UI_Sequencer_SetActiveScreen(UI_MainGridScreen_Get());

    // TEMP: Test chord menu on startup
    // s_ui_mode = UI_MODE_CHORD_MENU;
    // UI_Display_DrawChordMenu(1);
}

/* ── Called from SysTick — 1ms ───────────────────────────────────────────── */
void UI_Sequencer_Tick1ms(void)
{
    Bridge_Tick1ms();
}

/* ── Direct step access — for dedicated button matrix ───────────────────── */
void UI_Sequencer_SelectStep(uint8_t step)
{
    const UiStepSelectRoute* route;

    if (step < 1 || step > 12) return;

    s_selected_step = step;

    route = UI_Sequencer_FindStepSelectRoute(s_ui_mode);
    if (route && route->on_select)
    {
        route->on_select(step);
    }
    else
    {
        /* Ignore matrix step selects in other non-grid menus. */
    }
}

static void UI_Sequencer_HandleShiftTapEvent(uint8_t shift_tap)
{
    const UiShiftTapRoute* route;

    if (!shift_tap)
    {
        return;
    }

    route = UI_Sequencer_FindShiftTapRoute(s_ui_mode);
    if (route && route->on_shift_tap)
    {
        route->on_shift_tap();
    }
}

static void UI_Sequencer_HandlePlayRecInput(uint8_t play_pressed,
                                            uint8_t shift_play_pressed,
                                            uint8_t rec_pressed,
                                            uint8_t shift_rec_pressed)
{
    const UiInputRoute* route = UI_Sequencer_FindInputRoute(s_ui_mode);

    if (play_pressed && route && route->on_play)
    {
        route->on_play();
    }

    if (shift_play_pressed && route && route->on_shift_play)
    {
        route->on_shift_play();
    }

    if (shift_rec_pressed && route && route->on_shift_rec)
    {
        route->on_shift_rec();
    }

    if (rec_pressed && !shift_rec_pressed && route && route->on_rec)
    {
        route->on_rec();
    }
}

static void UI_Sequencer_HandleStepMatrixInput(uint8_t step_press, uint8_t shift_step_press)
{
    if (s_ui_mode == UI_MODE_TIMING_MENU ||
        s_ui_mode == UI_MODE_ARP_MENU ||
        s_ui_mode == UI_MODE_QUANT_TIMING ||
        s_ui_mode == UI_MODE_QUAN_ROUTER)
    {
        return;
    }

    if (s_step_matrix_rearm_required)
    {
        if (step_press == 0 && shift_step_press == 0)
        {
            s_step_matrix_rearm_required = 0;
        }
        return;
    }

    if (s_step_input_guard_ticks > 0)
    {
        s_step_input_guard_ticks--;
        return;
    }

    if (shift_step_press >= 1 && shift_step_press <= 12)
    {
        UI_Sequencer_DispatchStepPress(shift_step_press, 1);
        return;
    }

    /* Fallback for scanner edge-cases: if shift is physically held, treat
       a plain step press as shifted intent. */
    if (step_press >= 1 && step_press <= 12 && UI_Input_IsShiftHeld())
    {
        UI_Sequencer_DispatchStepPress(step_press, 1);
        return;
    }

    if (step_press >= 1 && step_press <= 12)
    {
        UI_Sequencer_DispatchStepPress(step_press, 0);
    }
}

static void UI_EncoderPress_Grid(void)
{
    switch (s_main_mode)
    {
        case UI_MAIN_MODE_STEP:
            /* BAR editor entry is matrix-step driven; encoder press does not enter it. */
            break;

        case UI_MAIN_MODE_CHORD:
            UI_Sequencer_EnterChordMenu(s_selected_step);
            break;

        case UI_MAIN_MODE_TIMING:
            UI_Sequencer_EnterTimingMenu(1);
            break;

        case UI_MAIN_MODE_PATTERN:
            UI_Sequencer_EnterSongChain((uint8_t)(s_selected_step - 1));
            break;

        case UI_MAIN_MODE_QUAN:
            if (s_selected_step >= 3 && s_selected_step <= 10)
            {
                UI_Sequencer_EnterQuanRouterMenu((uint8_t)(s_selected_step - 3));
            }
            else
            {
                UI_Sequencer_EnterQuanRouterMenu(0);
            }
            break;

        default:
            /* Keep unknown/default modes side-effect free on encoder press. */
            break;
    }
}

static void UI_EncoderPress_ChordMenuShift(void)
{
    uint8_t selected_idx = UI_ChordMenuScreen_GetSelection();
    if (selected_idx == 17)
    {
        UI_ChordMenuScreen_SetSelection(s_last_predefined_chord);
    }
    else
    {
        s_last_predefined_chord = selected_idx;
        UI_ChordMenuScreen_SetSelection(17);
    }

    {
        UiScreen* active = UI_ScreenRouter_GetActive();
        if (active && active->on_draw)
            active->on_draw();
    }
}

static void UI_EncoderPress_ChordMenu(void)
{
    uint8_t selected_idx = UI_ChordMenuScreen_GetSelection();

    if (selected_idx == 17)
    {
        UI_Sequencer_EnterUserChordMenu();
    }
    else if (selected_idx == 0)
    {
        s_step_chords[s_menu_step - 1].chord_type = 0;
        UI_Sequencer_CommitChordDraft();
        UI_Sequencer_SetMainMode(UI_MAIN_MODE_STEP);
        UI_Sequencer_ExitToGrid();
    }
    else
    {
        s_step_chords[s_menu_step - 1].chord_type = selected_idx;
        UI_Sequencer_EnterChordParams(s_menu_step);
    }
}

static void UI_EncoderPress_ChordParamsShift(void)
{
    if (UI_Sequencer_IsDirectChordContextActive())
    {
        /* Direct chooser keeps encoder presses for field navigation only. */
        UI_EncoderPress_ChordParams();
        return;
    }

    uint8_t action = UI_Display_GetSelectedParamAction();
    UI_Sequencer_HandleChordParamAction(action);
}

static void UI_EncoderPress_ChordParams(void)
{
    if (UI_Sequencer_IsDirectChordContextActive())
    {
        /* Direct chooser edits only ROOT/TYPE. */
        s_param_cursor = (uint8_t)((s_param_cursor == 0u) ? 1u : 0u);
    }
    else
    {
        s_param_cursor = (s_param_cursor + 1) % 4;
    }
    UI_Sequencer_RefreshChordParamsScreen();
}

static void UI_EncoderPress_ArpMenuShift(void)
{
    UI_Sequencer_ExitArpMenu(1u);
}

static void UI_EncoderPress_ArpMenu(void)
{
    s_arp_cursor = (uint8_t)((s_arp_cursor + 1u) % 2u);
    UI_Sequencer_DrawArpMenu();
}

static void UI_EncoderPress_TimingMenuShift(void)
{
    UI_Sequencer_ExitTimingMenu(1u);
}

static void UI_EncoderPress_TimingMenu(void)
{
    s_timing_cursor = (s_timing_cursor + 1) % 5;
    UI_Sequencer_DrawTimingMenu();
}

static void UI_EncoderPress_QuantTimingMenuShift(void)
{
    UI_Sequencer_ExitQuantTimingMenu(1u);
}

static void UI_EncoderPress_QuantTimingMenu(void)
{
    s_quant_cursor = (s_quant_cursor + 1) % 5;
    UI_Sequencer_DrawQuantTimingMenu();
}

static void UI_EncoderPress_QuanRouterMenuShift(void)
{
    UI_Sequencer_ExitQuanRouterMenu(1u);
}

static void UI_EncoderPress_QuanRouterMenu(void)
{
    s_quan_router_field = (uint8_t)!s_quan_router_field;
    UI_Sequencer_DrawQuanRouterMenu();
}

static void UI_EncoderPress_UserChordMenuShift(void)
{
    s_ui_mode = UI_MODE_CHORD_MENU;
    if (!UI_ScreenRouter_Pop(SCREEN_EXIT_BACK))
    {
        UI_ChordMenuScreen_SetContext(s_menu_step, &s_step_chords[s_menu_step - 1]);
        UI_Sequencer_SetActiveScreen(UI_ChordMenuScreen_Get());
    }
}

static void UI_EncoderPress_UserChordCreate(void)
{
    uint8_t key = UI_Display_GetSelectedPianoKey();
    UI_Display_TogglePianoKey(key);
}

static void UI_EncoderPress_UserChordName(void)
{
    s_user_chord_name_cursor = (uint8_t)((s_user_chord_name_cursor + 1) % 16);
    UI_Display_DrawUserChordNameEditor(s_user_chord_name_edit, s_user_chord_name_cursor);
}

static void UI_EncoderPress_StepPiano(void)
{
    if (s_step_piano_length_mode_active)
    {
        return;
    }
    UI_StepPiano_ActionNextPosition();
}

static void UI_EncoderLongPress_StepPiano(void)
{
    if (s_step_piano_length_mode_active)
    {
        return;
    }
    UI_StepPiano_ActionExitBarRequest();
}

static void UI_EncoderPress_StepPianoShift(void)
{
    UI_StepPiano_ActionClearBarRequest();
}

static void UI_EncoderPress_Default(void)
{
    UI_Sequencer_ExitToGrid();
}

static void UI_EncoderDelta_NoOp(int8_t delta)
{
    (void)delta;
}

static void UI_EncoderDelta_GridShiftBpm(int8_t delta)
{
    uint16_t bpm = UI_Transport_GetBPM();
    bpm = ((int16_t)bpm + delta < 30)  ? 30  :
          ((int16_t)bpm + delta > 300) ? 300 :
          bpm + delta;
    UI_Transport_SetBPM(bpm);
}

static void UI_EncoderDelta_TimingMenu(int8_t delta)
{
    /* Each grouped encoder detent advances a timing value by exactly one position. */
    static const uint8_t divisions[] = {1, 2, 4};

    if (delta == 0)
    {
        UI_Sequencer_DrawTimingMenu();
        return;
    }

    if (s_timing_cursor == 0)
    {
        int16_t v = (int16_t)s_timing_step_count + delta;
        if (v < 1) v = 1;
        if (v > 12) v = 12;
        s_timing_step_count = (uint8_t)v;
    }
    else if (s_timing_cursor == 1)
    {
        uint8_t idx = 0;
        for (uint8_t i = 0; i < 3; i++) if (divisions[i] == s_timing_step_division) { idx = i; break; }

        if (s_timing_step_division != divisions[idx])
        {
            idx = 2u;
        }

        int16_t n = (int16_t)idx + delta;
        while (n < 0) n += 3;
        while (n >= 3) n -= 3;
        s_timing_step_division = divisions[n];
    }
    else if (s_timing_cursor == 2)
    {
        int16_t v = (int16_t)s_timing_ts_num + delta;
        if (v < 1) v = 1;
        if (v > 12) v = 12;
        s_timing_ts_num = (uint8_t)v;
    }
    else if (s_timing_cursor == 3)
    {
        uint8_t dens[] = {2, 4, 8};
        uint8_t idx = (s_timing_ts_den == 2) ? 0 : (s_timing_ts_den == 8) ? 2 : 1;
        int16_t n = (int16_t)idx + delta;
        while (n < 0) n += 3;
        while (n >= 3) n -= 3;
        s_timing_ts_den = dens[n];
    }
    else if (s_timing_cursor == 4)
    {
        int16_t v = (int16_t)s_timing_swing + delta;
        if (v < 0) v = 0;
        if (v > 75) v = 75;
        s_timing_swing = (uint8_t)v;
    }

    s_timing_save_rejected = 0u;
    UI_Sequencer_DrawTimingMenu();
}

static void UI_EncoderDelta_QuantTimingMenu(int8_t delta)
{
    int8_t dir = (delta > 0) ? 1 : -1;

    if (s_quant_cursor == 0)
    {
        if (delta != 0)
        {
            s_quant_enabled = (uint8_t)!s_quant_enabled;
        }
    }
    else if (s_quant_cursor == 1)
    {
        int16_t v = (int16_t)s_quant_grid_division + dir;
        while (v < 0) v += 4;
        while (v >= 4) v -= 4;
        s_quant_grid_division = (uint8_t)v;
    }
    else if (s_quant_cursor == 2)
    {
        int16_t abs_delta = (delta < 0) ? (int16_t)(-delta) : (int16_t)delta;
        int16_t step_size = (abs_delta > 1) ? 10 : 5;
        int16_t v = (int16_t)s_quant_strength + (int16_t)(delta * step_size);
        if (v < 0) v = 0;
        if (v > 100) v = 100;
        s_quant_strength = (uint8_t)v;
    }
    else if (s_quant_cursor == 3)
    {
        int16_t v = (int16_t)s_quant_humanize_ms + delta;
        if (v < 0) v = 0;
        if (v > 12) v = 12;
        s_quant_humanize_ms = (uint8_t)v;
    }
    else if (s_quant_cursor == 4)
    {
        int16_t v = (int16_t)s_quant_lag_ms + delta;
        if (v < 0) v = 0;
        if (v > 20) v = 20;
        s_quant_lag_ms = (uint8_t)v;
    }

    UI_Sequencer_DrawQuantTimingMenu();
}

static void UI_EncoderDelta_QuanRouterMenu(int8_t delta)
{
    if (s_quan_router_cursor >= 8) return;

    if (s_quan_router_field == 0)
    {
        int16_t v = (int16_t)s_quan_router_source[s_quan_router_cursor] + delta;
        while (v < 0) v += 5;
        while (v >= 5) v -= 5;
        s_quan_router_source[s_quan_router_cursor] = (uint8_t)v;
    }
    else
    {
        int16_t v = (int16_t)s_quan_router_target[s_quan_router_cursor] + delta;
        while (v < 0) v += 9;
        while (v >= 9) v -= 9;
        s_quan_router_target[s_quan_router_cursor] = (uint8_t)v;
    }

    UI_Sequencer_DrawQuanRouterMenu();
}

static void UI_EncoderDelta_StepPiano(int8_t delta)
{
    if (delta == 0) return;

    if (s_step_piano_confirm_active)
    {
        s_step_piano_confirm_yes_selected = (uint8_t)!s_step_piano_confirm_yes_selected;
        UI_Sequencer_RefreshStepPianoContext(s_selected_step);
        return;
    }

    if (s_step_piano_length_mode_active)
    {
        if (s_selected_step < 1u || s_selected_step > 12u)
        {
            return;
        }

        const uint8_t step_index = (uint8_t)(s_selected_step - 1u);
        const uint8_t max_len = Bridge_GetStepEventMaxLength(step_index, s_step_piano_slot);
        if (max_len == 0u)
        {
            return;
        }

        const int8_t dir = (delta > 0) ? 1 : -1;
        int16_t next = (int16_t)s_step_piano_length_pending + (int16_t)dir;
        if (next < 1) next = 1;
        if (next > (int16_t)max_len) next = (int16_t)max_len;

        if ((uint8_t)next == s_step_piano_length_pending)
        {
            /* Boundary detent: silent no-op. */
            return;
        }

        s_step_piano_length_pending = (uint8_t)next;
        UI_Sequencer_RefreshStepPianoContext(s_selected_step);
        return;
    }

    UI_StepPiano_ActionMovePitch((delta > 0) ? 1 : -1);
}

static void UI_EncoderDelta_StepPianoShift(int8_t delta)
{
    UI_EncoderDelta_StepPiano(delta);
}

static void UI_EncoderDelta_ChordParams(int8_t delta)
{
    UiScreen* active = UI_ScreenRouter_GetActive();
    if (active && active->on_input)
    {
        active->on_input(INPUT_ENCODER_DELTA, delta);
        UI_ChordParamsScreen_CopyToChord(&s_step_chords[s_menu_step - 1]);
    }
}

static void UI_EncoderDelta_ArpMenu(int8_t delta)
{
    if (delta == 0) return;

    const int8_t dir = (delta > 0) ? 1 : -1;
    if (s_arp_cursor == 0u)
    {
        int16_t next = (int16_t)s_arp_mode + dir;
        while (next < 0) next += 5;
        while (next >= 5) next -= 5;
        s_arp_mode = (uint8_t)next;
    }
    else
    {
        int16_t next = (int16_t)s_arp_rate + dir;
        while (next < 0) next += 4;
        while (next >= 4) next -= 4;
        s_arp_rate = (uint8_t)next;
    }

    UI_Sequencer_DrawArpMenu();
}

static void UI_EncoderDelta_ChordMenu(int8_t delta)
{
    UiScreen* active = UI_ScreenRouter_GetActive();
    if (active && active->on_input)
    {
        active->on_input(INPUT_ENCODER_DELTA, delta);
    }
}

static void UI_EncoderDelta_UserChordMenu(int8_t delta)
{
    UI_Display_NavigateUserChordMenu(delta);
    UI_Display_DrawUserChordMenu();
}

static void UI_EncoderDelta_UserChordCreate(int8_t delta)
{
    UI_Display_NavigatePianoKeyboard(delta);
}

static void UI_EncoderDelta_UserChordLoad(int8_t delta)
{
    UI_Display_NavigateUserChordLoad(delta);
    UI_Display_DrawUserChordLoad();
}

static void UI_EncoderDelta_UserChordName(int8_t delta)
{
    s_user_chord_name_edit[s_user_chord_name_cursor] =
        UI_Sequencer_CycleNameChar(s_user_chord_name_edit[s_user_chord_name_cursor], delta);
    UI_Display_DrawUserChordNameEditor(s_user_chord_name_edit, s_user_chord_name_cursor);
}

static void UI_EncoderDelta_SongChain(int8_t delta)
{
    int16_t next = (int16_t)s_song_chain[s_song_cursor] + delta;
    while (next < 0) next += 32;
    while (next >= 32) next -= 32;
    s_song_chain[s_song_cursor] = (uint8_t)next;
    Bridge_SetChainPatternAt(s_song_cursor, s_song_chain[s_song_cursor]);
    UI_Sequencer_DrawSongChainMenu();
}

static void UI_Sequencer_HandleEncoderPressInput(uint8_t encoder_pressed)
{
    uint8_t shift_held;
    const UiEncoderPressRoute* route;

    if (!encoder_pressed)
    {
        return;
    }

    shift_held = UI_Input_IsShiftHeld();
    route = UI_Sequencer_FindEncoderPressRoute(s_ui_mode);

    if (route)
    {
        if (shift_held && route->on_shift_press)
        {
            route->on_shift_press();
            return;
        }

        if (route->on_press)
        {
            route->on_press();
            return;
        }
    }

    {
        /* Existing fallback for unexpected mode states. */
        UI_Sequencer_ExitToGrid();
    }
}

static void UI_Sequencer_HandleEncoderDeltaInput(int8_t delta)
{
    uint8_t shift_held;
    const UiEncoderDeltaRoute* route;

    if (delta == 0)
    {
        return;
    }

    shift_held = UI_Input_IsShiftHeld();

    if (s_ui_mode == UI_MODE_TIMING_MENU)
    {
        UI_EncoderDelta_TimingMenu(delta);
        return;
    }
    if (s_ui_mode == UI_MODE_ARP_MENU)
    {
        UI_EncoderDelta_ArpMenu(delta);
        return;
    }
    if (s_ui_mode == UI_MODE_QUANT_TIMING)
    {
        UI_EncoderDelta_QuantTimingMenu(delta);
        return;
    }
    if (s_ui_mode == UI_MODE_QUAN_ROUTER)
    {
        UI_EncoderDelta_QuanRouterMenu(delta);
        return;
    }

    route = UI_Sequencer_FindEncoderDeltaRoute(s_ui_mode);

    if (route)
    {
        if (shift_held && route->on_shift_delta)
        {
            route->on_shift_delta(delta);
            return;
        }

        if (route->on_delta)
        {
            route->on_delta(delta);
            return;
        }
    }

    if (shift_held)
    {
        /* Preserve fallback behavior: shifted encoder controls BPM. */
        UI_EncoderDelta_GridShiftBpm(delta);
    }
}

static void UI_Sequencer_UpdateActiveStepFromEngine(void)
{
    uint8_t engine_step = (uint8_t)(Bridge_GetCurrentStep() + 1); /* 0-based -> 1-based */

    if (Bridge_IsPlaying())
        s_active_step = engine_step;
    else
        s_active_step = 0;
}

static void UI_Sequencer_RedrawGridComposed(void)
{
    uint8_t has_chord_flags[12];
    for (uint8_t i = 0; i < 12; ++i)
    {
        has_chord_flags[i] = UI_Sequencer_StepHasNotes((uint8_t)(i + 1u));
    }

    UI_Display_DrawMainGridComposed(s_selected_step,
                                    s_active_step,
                                    has_chord_flags,
                                    UI_Transport_GetBPM(),
                                    UI_Transport_GetState(),
                                    UI_Transport_IsRecArmed(),
                                    Bridge_GetCurrentPattern(),
                                    (uint8_t)Bridge_GetCurrentStep(),
                                    Bridge_GetCompletedLoops(),
                                    Bridge_GetRunTimeMs());
}

static void UI_Sequencer_UpdateGridRepeatAndStepVisuals(void)
{
    uint32_t now_ms = HAL_GetTick();
    uint8_t repeat_flash_enabled = 0;
    uint8_t active_has_notes = 0;
    uint8_t need_redraw = 0;

    if (s_active_step >= 1 && s_active_step <= 12 && Bridge_IsPlaying())
    {
        active_has_notes = UI_Sequencer_StepHasNotes(s_active_step);
        if (s_step_chords[s_active_step - 1].loop_count > 1)
        {
            repeat_flash_enabled = 1;
        }
    }

    if (repeat_flash_enabled)
    {
        if (!s_repeat_flash_was_enabled)
        {
            s_repeat_flash_on = 1;
            s_repeat_flash_ms = now_ms;
            UI_Display_SetRepeatFlash(1, s_repeat_flash_on);
            need_redraw = 1;
        }

        if ((now_ms - s_repeat_flash_ms) >= 140)
        {
            s_repeat_flash_ms = now_ms;
            s_repeat_flash_on = (uint8_t)!s_repeat_flash_on;
            UI_Display_SetRepeatFlash(1, s_repeat_flash_on);
            need_redraw = 1;
        }
    }
    else
    {
        if (s_repeat_flash_was_enabled)
        {
            s_repeat_flash_on = 0;
            UI_Display_SetRepeatFlash(0, 0);
            need_redraw = 1;
        }
    }

    s_repeat_flash_was_enabled = repeat_flash_enabled;

    if (s_active_step != s_last_active)
    {
        need_redraw = 1;

        /* Update last active step for next comparison */
        s_last_active = s_active_step;
    }

    if (need_redraw)
    {
        (void)active_has_notes;
        UI_Sequencer_RedrawGridComposed();
    }
}

static void UI_Sequencer_UpdateSongChainBlink(uint32_t now)
{
    if (s_ui_mode == UI_MODE_SONG_CHAIN)
    {
        if ((now - s_song_blink_ms) >= 180)
        {
            s_song_blink_ms = now;
            s_song_blink_on = (uint8_t)!s_song_blink_on;
            UI_Sequencer_DrawSongChainMenu();
        }
    }
}

static void UI_Sequencer_UpdateStatusRowTick(uint32_t now)
{
    /* Main UI cadence: use the regular 10 ms frame to sample live Bar/Substep values.
     * This is much faster than the previous 100 ms status poll and still avoids
     * unnecessary redraws unless the displayed values actually change. */
    if ((now - s_last_status_ms) >= 10)
    {
        s_last_status_ms = now;

        /* Only redraw status row if values changed and not in chord menu */
        if (s_ui_mode == UI_MODE_GRID)
        {
            uint8_t  pattern   = Bridge_GetCurrentPattern();
            uint8_t  step      = Bridge_GetCurrentStep();
            uint8_t  substep   = Bridge_GetCurrentStepSubIndex();
            uint32_t loops     = Bridge_GetCompletedLoops();
            uint32_t run_time  = Bridge_GetRunTimeMs();
#if UI_MCP_DEBUG_OVERLAY
            uint8_t  mcp_online = 0;
            uint8_t  mcp_read_ok = 0;
            uint8_t  mcp_a = 0xFF;
            uint8_t  mcp_b = 0xFF;
            uint8_t  mcp_addr7 = 0x20;
            uint8_t  mcp_scan_mask = 0x00;
#endif

            if ((pattern != s_last_pattern) || (step != s_last_step) || (substep != s_last_substep) ||
                (loops != s_last_loops) || (run_time != s_last_run_time))
            {
                s_last_pattern   = pattern;
                s_last_step      = step;
                s_last_substep   = substep;
                s_last_loops     = loops;
                s_last_run_time  = run_time;

                UI_Display_DrawStatusRow(pattern, step, loops, run_time);
            }

#if UI_MCP_DEBUG_OVERLAY
            UI_Input_GetMcpDebug(&mcp_online, &mcp_read_ok, &mcp_a, &mcp_b, &mcp_addr7, &mcp_scan_mask);
            UI_Display_DrawMcpDebug(mcp_online, mcp_read_ok, mcp_a, mcp_b, mcp_addr7, mcp_scan_mask);
#endif
        }
    }
}

/* ── Update — called every 10ms from main loop ───────────────────────────── */
void UI_Sequencer_Update(void)
{
    ST7789_DebugSetStepPianoActive((s_ui_mode == UI_MODE_STEP_PIANO) ? 1u : 0u);

    uint8_t current_pattern = Bridge_GetCurrentPattern();
    if (current_pattern != s_cached_pattern)
    {
        UI_Sequencer_SaveChordDraftForPattern();
        UI_Sequencer_LoadChordDraftForPattern(current_pattern);
    }

    UI_Sequencer_HandleShiftTapEvent(UI_Input_GetShiftTap());

    if (s_exit_confirm_active || s_direct_chord_confirm.active)
    {
        (void)UI_Input_IsPlayPressed();
        (void)UI_Input_IsShiftPlayPressed();
        (void)UI_Input_IsRecPressed();
        (void)UI_Input_IsShiftRecPressed();
        (void)UI_Input_GetStepPressed();
        (void)UI_Input_GetShiftStepPressed();
    }
    else
    {
        /* ── Play / Record buttons ─────────────────────────────────────────── */
        uint8_t play_pressed       = UI_Input_IsPlayPressed();
        uint8_t shift_play_pressed = UI_Input_IsShiftPlayPressed();
        uint8_t rec_pressed        = UI_Input_IsRecPressed();
        uint8_t shift_rec_pressed  = UI_Input_IsShiftRecPressed();
        UI_Sequencer_HandlePlayRecInput(play_pressed, shift_play_pressed, rec_pressed, shift_rec_pressed);

        /* ── Direct step matrix presses (table-driven) ───────────────────── */
        uint8_t step_press = UI_Input_GetStepPressed();
        uint8_t shift_step_press = UI_Input_GetShiftStepPressed();
        UI_Sequencer_HandleStepMatrixInput(step_press, shift_step_press);
    }

    /* ── Encoder press — enter/exit chord menu ─────────────────────────── */
    if (s_ui_mode == UI_MODE_STEP_PIANO)
    {
        uint8_t long_press = UI_Input_IsEncoderLongPressed();
        uint8_t short_press = UI_Input_IsEncoderShortPressed();
        (void)UI_Input_IsEncoderPressed();

        if (long_press)
        {
            UI_EncoderLongPress_StepPiano();
        }
        else if (short_press)
        {
            UI_EncoderPress_StepPiano();
        }
    }
    else
    {
        uint8_t short_press = UI_Input_IsEncoderShortPressed();
        uint8_t long_press = UI_Input_IsEncoderLongPressed();

        if (s_direct_chord_confirm.active)
        {
            (void)UI_Input_IsEncoderPressed();
            if (short_press)
            {
                (void)UI_Sequencer_ConfirmDirectChordSelection();
            }
        }
        else
        if (s_exit_confirm_active)
        {
            (void)UI_Input_IsEncoderPressed();
            if (short_press)
            {
                UI_Sequencer_ConfirmExitConfirmSelection();
            }
        }
        else
        {
            (void)short_press;
            if (long_press && s_ui_mode == UI_MODE_TIMING_MENU)
            {
                UI_Sequencer_BeginExitConfirm("LEAVE TIMING?", UI_EXIT_CONFIRM_ACTION_TIMING);
                (void)UI_Input_IsEncoderPressed();
            }
            else if (!(long_press && UI_Sequencer_HandleEncoderLongPressForDirectShortcut()))
            {
                UI_Sequencer_HandleEncoderPressInput(UI_Input_IsEncoderPressed());
            }
            else
            {
                (void)UI_Input_IsEncoderPressed();
            }
        }
    }

    /* ── Runtime state refresh stages ─────────────────────────────────── */
    UI_Sequencer_UpdateActiveStepFromEngine();

    /* ── Encoder — move cursor or navigate menus ─────────────────────── */
    {
        const int8_t encoder_delta = UI_Input_GetEncoderDelta();
        if (s_direct_chord_confirm.active)
        {
            UI_Sequencer_HandleDirectChordConfirmDelta(encoder_delta);
        }
        else if (s_exit_confirm_active)
        {
            UI_Sequencer_HandleExitConfirmDelta(encoder_delta);
        }
        else
        {
            UI_Sequencer_HandleEncoderDeltaInput(encoder_delta);
        }
    }

    if (s_ui_mode == UI_MODE_GRID)
    {
        UI_Sequencer_UpdateGridRepeatAndStepVisuals();
    }

    {
        uint32_t now = HAL_GetTick();
        UI_Sequencer_UpdateSongChainBlink(now);
        UI_Sequencer_UpdateStatusRowTick(now);
    }

    if (s_ui_mode == UI_MODE_STEP_PIANO)
    {
        UiScreen* active = UI_ScreenRouter_GetActive();
        if (active && active->on_draw)
        {
            active->on_draw();
        }
    }

/* ── Process engine dirty flags ───────────────────────────────────── */
Bridge_Process();
}