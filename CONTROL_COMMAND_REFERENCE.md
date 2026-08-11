# S12 Control Command Reference (Manual Seed)

This page captures command behaviors requested and confirmed during development.
It is intended as a source page you can expand into a full manual later.

## Source-of-truth notes

- These are user-requested control rules from active firmware sessions.
- If behavior changes in code, update this page first, then reflect in the full manual.

## Mode Selection

1. Shift tap cycles the top mode label in this order:
   - STEP
   - CHORD
   - TIME
   - SONG
   - QUAN

## Matrix Behavior by Mode (Requested)

1. STEP mode:
   - Matrix step buttons enter the selected step note view.

2. CHORD mode:
   - Matrix step buttons enter chord selection for that step.
   - Encoder press returns/backs out (as requested behavior).

3. TIME mode:
   - Matrix step 1 opens Pattern Timing.
   - Matrix step 2 opens Quantizer Timing settings.

4. SONG mode:
   - Matrix step buttons map to song chain slot selection directly:
   - Step 1 -> Slot 1
   - Step 2 -> Slot 2
   - ...
   - Step 12 -> Slot 12

5. QUAN mode:
   - Matrix step 1 opens Quantizer Timing (focus on Quantize ON/OFF).
   - Matrix step 2 opens Quantizer Timing (focus on Grid).
   - Matrix steps 3..10 open dedicated QUAN CV Router slots S03..S10.
   - Encoder press opens Quantizer CV Router (focused to selected step slot when step is 3..10).

6. QUAN CV Router mode:
   - Matrix steps 3..10 retarget the active router slot (S03..S10).
   - Encoder turn edits selected field value.
   - Encoder press toggles edit field (Source/Target).
   - Shift+Encoder turn selects footer action (MAIN/SAVE).
   - Shift+Encoder press commits footer action.
   - Sources: OFF, ADC-IN1, ADC-IN2, ADC-IN3, ADC-IN4.
   - Targets: OFF, CV1..CV4, GATE1..GATE4.

## Encoder Behavior (Requested)

1. From grid context, encoder press follows selected top mode:
   - STEP -> Step Piano
   - CHORD -> Chord selector
   - TIME -> Pattern Timing
   - SONG -> Song Chain
   - QUAN -> Quantizer CV Router

## Working Rules to Keep Consistent

1. Shift chooses the operating context (STEP/CHORD/TIME/SONG/QUAN).
2. Matrix buttons perform the primary action for that context.
3. Song mode matrix buttons should address song slots by index.
4. Parameter access should remain predictable from grid encoder press.
5. TIME mode should provide direct access to both Pattern Timing and Quantizer Timing pages.
6. QUAN mode is reserved for dedicated quantizer/CV routing workflows as backend routing is added.

## Change Log

2026-07-23

1. Added SONG mode slot mapping request (Step N -> Slot N).
2. Captured unified encoder-to-parameter request.
3. Captured mode-by-mode matrix behavior summary for manual drafting.
4. Added TIME mode split routing: step 1 Pattern Timing, step 2 Quantizer Timing.