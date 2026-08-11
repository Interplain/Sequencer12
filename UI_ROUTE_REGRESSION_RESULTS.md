# UI Route Regression Results

Date: 2026-07-23
Scope: Incremental Step 2 migration verification for TIME and SONG editor stack routing.

Method:

- Code-path verification against `UI_ROUTE_REGRESSION_CHECKLIST.md`.
- Build verification with PlatformIO (`platformio run`).
- No physical input hardware interaction available in this environment.

## Build Status

- Result: PASS
- Evidence: firmware links successfully after TIME routing changes.

## Checklist Section E: TIME Main Mode and Pattern Timing

1. In TIME mode grid, matrix step 1 opens Pattern Timing editor.
- Status: PASS (code path)
- Evidence: `UI_StepRoute_GridTiming` calls `UI_Sequencer_EnterTimingMenu(1)`.

2. In TIME mode grid, Shift+matrix step 1..12 opens Chord Params focused on Gate for that step.
- Status: PASS (code path)
- Evidence: `UI_StepRoute_ShiftGridTiming` sets `s_param_cursor = 3` then refreshes chord params.

3. In Pattern Timing, encoder press cycles timing fields.
- Status: PASS (code path)
- Evidence: `s_timing_cursor = (s_timing_cursor + 1) % 5` in timing mode encoder-press path.

4. In Pattern Timing, Shift+encoder turn navigates footer actions.
- Status: PASS (code path)
- Evidence: `UI_Display_NavigateTimingFooter(delta)` in timing mode + Shift encoder path.

5. In Pattern Timing, Play commits timing draft and exits.
- Status: PASS (code path)
- Evidence: timing route `on_play` calls `UI_Sequencer_ExitTimingMenu(1u)`.

6. In Pattern Timing, Rec exits to grid.
- Status: PASS (code path)
- Evidence: timing route `on_rec` calls `UI_Route_RecTimingMenu`, which calls `UI_Sequencer_ExitTimingMenu(0u)`.

## Stack Migration Checks (TIME family)

1. Enter timing from TIME grid path pushes timing screen over main grid when possible.
- Status: PASS (code path)
- Evidence: `UI_Sequencer_EnterTimingMenu` attempts `UI_ScreenRouter_Push(&s_timing_screen)` when active screen is main grid.

2. Enter timing from Shift+encoder grid path uses same helper.
- Status: PASS (code path)
- Evidence: grid Shift+encoder press now calls `UI_Sequencer_EnterTimingMenu(0)`.

3. Exit timing pops timing screen back to grid when timing screen is active.
- Status: PASS (code path)
- Evidence: `UI_Sequencer_ExitTimingMenu` checks active screen and calls `UI_ScreenRouter_Pop(...)`.

4. Fallback remains safe when stack pop is unavailable.
- Status: PASS (code path)
- Evidence: falls back to `UI_Sequencer_SetActiveScreen(UI_MainGridScreen_Get())`.

## Stack Migration Checks (SONG family)

1. Enter song chain from SONG grid path uses shared enter helper.
- Status: PASS (code path)
- Evidence: `UI_StepRoute_GridPattern` calls `UI_Sequencer_EnterSongChain(...)`.

2. SONG grid step shortcuts still map Step N to Slot N selection intent.
- Status: PASS (code path)
- Evidence: requested slot `(step - 1)` is passed into `UI_Sequencer_EnterSongChain` and applied with clamp.

3. Song chain entry pushes screen over main grid when possible.
- Status: PASS (code path)
- Evidence: `UI_Sequencer_EnterSongChain` attempts `UI_ScreenRouter_Push(&s_song_chain_screen)` when active is main grid.

4. Song chain exit (PLAY/REC) uses shared exit helper with pop.
- Status: PASS (code path)
- Evidence: `UI_Route_PlaySongChain` and `UI_Route_RecSongChain` call `UI_Sequencer_ExitSongChain`, which pops when song screen is active.

5. Fallback remains safe when pop is unavailable.
- Status: PASS (code path)
- Evidence: `UI_Sequencer_ExitSongChain` falls back to `UI_Sequencer_SetActiveScreen(UI_MainGridScreen_Get())`.

## Step 2 Status

- Status: COMPLETE
- Summary: Minimal router/stack abstraction is integrated, and bounded families (CHORD params path, TIME editor, SONG chain) now use push/pop migration patterns with root-switch fallbacks.

## Step 3 Status

- Status: COMPLETE
- Focus: Convert remaining direct transition/display paths to routed screen-stack patterns.

### Step 3 Progress: STEP Piano family

1. Step piano entry now prepares `PianoRollContext` and attempts router push over main grid.
2. Step piano exit (PLAY/REC) now uses shared pop/fallback helper.
3. Step changes while in step piano now refresh context through the active routed screen path.
4. Build validation: PASS (`platformio run`).

### Step 3 Progress: USER chord family

1. User chord menu/create/load/name now use routed screen wrappers and stack transitions.
2. Entry from CHORD menu USER selection now uses a routed helper instead of direct detach/draw path.
3. Sub-screen exits (create/load/name -> user menu) now use shared pop/fallback helper.
4. User menu exit to CHORD menu now prefers stack pop with fallback to explicit switch.
5. User menu rec-to-grid path now uses shared grid-exit helper.
6. Build validation: PASS (`platformio run`).

### Step 3 Completion Summary

Step 3 is complete for the planned migration scope:

1. STEP Piano flow converted to routed push/pop.
2. TIME flow converted to routed push/pop.
3. SONG chain flow converted to routed push/pop.
4. USER chord flow converted to routed push/pop with fallback safety.

## Step 4 Status

- Status: IN PROGRESS
- Focus: Separate global input actions from screen-local handlers and reduce branching density in `ui/ui_sequencer.c`.

### Step 4 Progress: Input Stage Separation

1. Added dedicated input-stage helpers:
	- `UI_Sequencer_HandleShiftTapEvent`
	- `UI_Sequencer_HandlePlayRecInput`
	- `UI_Sequencer_HandleStepMatrixInput`
	- `UI_Sequencer_HandleEncoderPressInput`
	- `UI_Sequencer_HandleEncoderDeltaInput`
2. Refactored `UI_Sequencer_Update` to preserve event order while delegating stage behavior to helpers.
3. Build validation: PASS (`platformio run`).

### Step 4 Progress: Runtime Stage Separation

1. Extracted runtime update/render stages into dedicated helpers:
	- `UI_Sequencer_UpdateActiveStepFromEngine`
	- `UI_Sequencer_UpdateGridRepeatAndStepVisuals`
	- `UI_Sequencer_UpdateSongChainBlink`
	- `UI_Sequencer_UpdateStatusRowTick`
2. Kept the same update order and side-effect timing while reducing `UI_Sequencer_Update` complexity.
3. Build validation: PASS (`platformio run`).

### Step 4 Progress: Encoder Route Dispatch Separation

1. Converted remaining encoder mode-branch chains into route-table dispatch paths:
	- Added `UiEncoderPressRoute` and `UiEncoderDeltaRoute` tables.
	- Added lookup helpers to resolve handlers by `UiMode`.
2. Replaced large `if/else if` mode chains in encoder press/delta handlers with mode-dispatch + fallback logic.
3. Preserved behavior details:
	- Shift-aware actions in CHORD/TIMING/USER flows remain unchanged.
	- Grid shift+encoder BPM control remains intact.
	- Existing fallback exits for unexpected mode states are retained.
4. Build validation: PASS (`platformio run`).

### Step 4 Progress: Step-Matrix Route Hardening

1. Removed implicit non-grid fallback from `UI_Sequencer_DispatchStepPress`.
2. Added explicit step-matrix routes for intended step-selection contexts:
	- `UI_MODE_CHORD_MENU` -> `UI_StepRoute_SelectStep`
	- `UI_MODE_STEP_PIANO` -> `UI_StepRoute_SelectStep`
	- Includes both normal and shift+step paths for parity with prior behavior.
3. Modes without explicit step routes now ignore matrix step presses by design, preventing unintended cross-screen transitions.
4. Build validation: PASS (`platformio run`).

### Step 4 Progress: Step-Select Dispatch Separation

1. Refactored `UI_Sequencer_SelectStep` to remove mode `if/else if` chains.
2. Added explicit mode-based select dispatch table (`UiStepSelectRoute`) for:
	- `UI_MODE_GRID`
	- `UI_MODE_STEP_PIANO`
	- `UI_MODE_CHORD_MENU`
	- `UI_MODE_CHORD_PARAMS`
3. Preserved behavior by moving existing per-mode logic into dedicated handlers:
	- Enter step piano from grid
	- Retarget step while in step piano
	- Retarget step while in chord menu
	- Retarget step while in chord params (with draft commit)
4. Build validation: PASS (`platformio run`).

### Step 4 Progress: Play/Rec Route Unification

1. Extended `UiInputRoute` with explicit `on_shift_play` handler support.
2. Added `UI_MODE_GRID` transport mappings to the same input route table used by submenu modes.
3. Removed the `UI_MODE_GRID` special-case branch from `UI_Sequencer_HandlePlayRecInput` in favor of one uniform route-dispatch flow.
4. Preserved interaction semantics:
	- Play -> play/stop
	- Shift+Play -> reset
	- Rec -> record arm
	- Shift+Rec -> record clear
5. Build validation: PASS (`platformio run`).

### Step 4 Progress: Shift-Tap Route Dispatch Finalization

1. Added explicit shift-tap route table (`UiShiftTapRoute`) by `UiMode`.
2. Moved GRID shift-tap mode-cycle behavior into dedicated route handler (`UI_Route_ShiftTapGrid`).
3. Replaced remaining shift-tap mode branch in `UI_Sequencer_HandleShiftTapEvent` with route-table dispatch.
4. Build validation: PASS (`platformio run`).

## Step 4 Completion Status

- Status: COMPLETE (code refactor scope)
- Summary:
	1. Input stages extracted and route-dispatched.
	2. Runtime update stages extracted into focused helpers.
	3. Encoder press/delta chains converted to route tables.
	4. Step-matrix and step-select flows moved to explicit route dispatch.
	5. Play/Rec and Shift-tap input flows unified under route-table dispatch.

On-target interaction checks remain listed below as hardware validation items.

## Post-Step 4 Behavior Extension: TIME Step 2 Quantizer Timing Page

Date: 2026-07-23

1. Added `UI_MODE_QUANT_TIMING` screen and routed TIME mode matrix step 2 to open it.
2. Added dedicated Quantizer Timing page with editable fields:
	- Quantizer (ON/OFF)
	- Grid (1/4, 1/8, 1/16, 1/32)
	- Strength (%)
	- Humanize (ms)
	- Input Lag (ms)
3. Added save/main footer flow aligned with Timing menu behavior.
4. Integrated new mode into play/rec and encoder route tables.
5. Immediate runtime linkage: saving Quantizer Timing applies strength cap to swing when Quantizer is enabled.
6. Build validation: PASS (`platformio run`).

## Post-Step 4 Behavior Extension: QUAN Shift Mode Scaffold

Date: 2026-07-23

1. Added new top mode `UI_MAIN_MODE_QUAN` to SHIFT tap cycle and status color strip.
2. Extended GRID mode routes with dedicated QUAN behavior:
	- Matrix step 1 -> Quantizer Timing page (cursor 0)
	- Matrix step 2 -> Quantizer Timing page (cursor 1)
	- Encoder press in QUAN -> Quantizer CV Router page
3. Added fast-turn acceleration for Quantizer Amount field:
	- Slow turns: 5% step
	- Fast turns (batched deltas): 10% per detent
4. Build validation: PASS (`platformio run`).

## Post-Step 4 Behavior Extension: Dedicated QUAN CV Router Screen

Date: 2026-07-23

1. Added new routed screen mode `UI_MODE_QUAN_ROUTER` with dedicated list UI for step slots S03..S10.
2. Added explicit QUAN-mode matrix routing:
	- Step 1 -> Quantizer Timing (cursor 0)
	- Step 2 -> Quantizer Timing (cursor 1)
	- Steps 3..10 -> QUAN Router slots 0..7 (S03..S10)
3. Added editable assignment draft state:
	- Source per slot: OFF/IN1/IN2/IN3/IN4
	- Target per slot: OFF/OUT1/OUT2/OUT3/OUT4/GATE1/GATE2/GATE3/GATE4
4. Added QUAN Router controls:
	- Encoder press toggles edit field (Source/Target)
	- Encoder delta edits selected field
	- Shift+encoder footer MAIN/SAVE and commit/back behavior
	- Play/Rec routes mapped to SAVE/BACK parity
5. Added draft commit/dirty tracking for router assignments.
6. Build validation: PASS (`platformio run`).

## On-Target Validation Pending

The following still require manual hardware execution to fully mark PASS:

- Visible redraw behavior for timing enter/exit.
- Rec/Play tactile behavior parity with previous firmware.
- No unintended mode-cycling side effects under rapid input.

## Regression Log: Chord SAVE Return-to-STEP Routing

Date: 2026-07-23

Issue:

- Returning to grid after saving chord params could leave non-STEP main-mode routing active.
- Symptom: pressing matrix step after chord save did not open Step Piano Roll as expected.

Repro sequence:

1. Enter CHORD flow and open Chord Params for a step.
2. Trigger SAVE action.
3. Return to grid.
4. Press matrix step button.

Expected:

- STEP routing semantics active on return.
- Matrix step opens Step Piano Roll.

Observed before fix:

- Main mode was not always restored to STEP from the SAVE path.
- Matrix step could follow non-STEP route mapping.

Fix applied:

- In Chord Params SAVE action path, explicitly restore main mode to STEP immediately after commit.

Implementation note:

- Updated `UI_Sequencer_HandleChordParamAction` SAVE branch in `ui/ui_sequencer.c` to call `UI_Sequencer_SetMainMode(UI_MAIN_MODE_STEP)` after `UI_Sequencer_CommitChordDraft()`.

Validation:

- Build status: PASS (`platformio run`).
- On-target interaction verification: pending.
