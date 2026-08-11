# S12 UI Route Regression Checklist

Purpose: freeze current UI behavior before modularization so route changes can be validated quickly and consistently.

Scope:

- input routing
- mode transitions
- editor enter/exit behavior
- confirmation and back behavior

Out of scope:

- deep visual pixel-perfect review
- long-duration transport stability tests
- hardware calibration procedures

## Test Setup

1. Boot firmware normally.
2. Confirm main grid appears.
3. Confirm transport is stopped.
4. Use default pattern unless a step asks otherwise.

Record for each test:

- pass or fail
- observed mode or screen
- unexpected behavior

## A. Global Input Layer

1. Shift tap cycles main mode in order STEP -> CHORD -> TIME -> SONG -> QUAN -> STEP.
2. Play in grid toggles transport play and stop.
3. Shift+Play in grid performs transport reset.
4. Rec in grid toggles record arm behavior.
5. Shift+Rec in grid performs record clear behavior.
6. Shift tap inside non-grid screens does not cycle top-level mode.

## B. STEP Main Mode

1. In STEP mode grid, matrix step 1..12 opens Step Piano Roll for that step.
2. In Step Piano Roll, Rec returns to grid.
3. In Step Piano Roll, Play confirms and returns to grid.
4. In STEP mode grid, encoder press opens Step Piano Roll for selected step.
5. In STEP mode grid, Shift+encoder press does not trigger alternate editor entry.
6. In STEP mode grid, plain encoder turn does not move selection.

## C. CHORD Main Mode

1. In CHORD mode grid, matrix step 1..12 opens Chord Menu for pressed step.
2. In Chord Menu, encoder turn changes chord selection.
3. In Chord Menu, Play on CLEAR clears chord and returns to grid.
4. In Chord Menu, Play on chord type enters Chord Params.
5. In Chord Menu, Rec returns to grid.
6. In Chord Menu, Shift+encoder press toggles USER selection versus last predefined selection.

## D. Chord Params

1. Encoder press cycles parameter cursor when Shift is not held.
2. Shift+encoder turn navigates footer actions.
3. Encoder turn edits selected parameter when footer action is not SAVE.
4. Matrix 1..4 triggers footer actions MAIN, PREV, NEXT, SAVE.
5. Matrix 5..12 changes target step while staying in Chord Params.
6. PREV and NEXT carry current step params into neighboring step.
7. SAVE commits and keeps editor active.
8. After SAVE in Chord Params, returning to grid keeps STEP routing semantics for matrix step buttons.
9. After SAVE then return to grid, matrix step 1..12 opens Step Piano Roll (not Chord Menu/Timing/Song actions).
10. MAIN commits and exits to grid.
11. Rec returns to Chord Menu.
12. Play commits and exits to grid.

## E. TIME Main Mode and Timing Editors

1. In TIME mode grid, matrix step 1 opens Pattern Timing editor.
2. In TIME mode grid, matrix step 2 opens Quantizer Timing editor.
3. In TIME mode grid, Shift+matrix step 1..12 opens Chord Params focused on Gate for that step.
4. In Pattern Timing, encoder press cycles timing fields.
5. In Pattern Timing, Shift+encoder turn navigates footer actions.
6. In Pattern Timing, Play commits timing draft and exits.
7. In Pattern Timing, Rec exits to grid.

## F. SONG Main Mode and Song Chain

1. In SONG mode grid, matrix step 1..12 opens Song Chain editor focused to pressed slot.
2. In SONG mode grid, Shift+matrix step 1..12 selects current pattern P01..P12.
3. In Song Chain, matrix 1..4 selects visible slots.
4. In Song Chain, matrix 5 adds slot when length < 32.
5. In Song Chain, matrix 6 removes slot when length > 1.
6. In Song Chain, encoder turn changes selected slot pattern.
7. In Song Chain, Rec exits to grid.
8. In Song Chain, Play exits to grid.

## G. QUAN Main Mode and Quantizer Router

1. In QUAN mode grid, matrix step 1 opens Quantizer Timing editor focused to Quantize ON/OFF.
2. In QUAN mode grid, matrix step 2 opens Quantizer Timing editor focused to Grid.
3. In QUAN mode grid, matrix steps 3..10 open Quantizer CV Router focused to S03..S10.
4. In QUAN mode grid, encoder press opens Quantizer CV Router.
5. In Quantizer CV Router, matrix steps 3..10 retarget active slot.
6. In Quantizer CV Router, encoder press toggles Source/Target field.
7. In Quantizer CV Router, encoder turn edits selected field values.
8. In Quantizer CV Router, Shift+encoder turn selects MAIN/SAVE footer action.
9. In Quantizer CV Router, Shift+encoder press applies MAIN/SAVE action.

## H. User Chord Workflow

1. From Chord Menu USER selection, Play enters User Chord Menu.
2. In User Chord Menu, encoder navigates CREATE, LOAD, NAME.
3. In User Chord Menu, Rec returns to grid.
4. CREATE path:
   - Play enters User Chord Create keyboard.
   - Encoder moves note selection.
   - Encoder press toggles selected note.
   - Play saves chord and returns to User Chord Menu.
   - Rec returns to User Chord Menu.
5. LOAD path:
   - Play enters User Chord Load list.
   - Encoder selects chord.
   - Play applies chord to current step and returns to grid.
   - Rec returns to User Chord Menu.
   - Shift+Rec deletes selected user chord.
6. NAME path:
   - Play enters User Chord Name editor.
   - Encoder turn cycles character.
   - Encoder press moves cursor.
   - Play saves name and returns to User Chord Menu.
   - Rec returns to User Chord Menu.

## I. Step Select Fallback and Non-Grid Behavior

1. In non-grid menus where no specific step route exists, matrix step press performs quick step select behavior.
2. Step changes in Chord Menu update context to new step without exiting screen.
3. Step changes in Chord Params preserve and commit current draft before switching target step.

## J. Status and Visual Consistency Checks

1. Grid status row updates pattern, step, loops, and run time while playing.
2. Repeat flash appears only when active step has repeat count > 1.
3. Song Chain playing slot blink continues while Song Chain screen is active.
4. Returning to grid from any editor restores expected header, status, and grid visuals.

## K. Pass Criteria

All section checks pass with no route regressions:

- no stuck mode
- no missing back path
- no missing confirm path
- no unintended mode cycle inside editors
- no broken step routing in TIME, SONG, and QUAN paths

If any check fails, log:

- failing section and test number
- exact key sequence used
- expected result
- observed result

## Suggested Use During Refactor

Run this checklist:

1. before router changes
2. after each routing refactor PR
3. before merging stack-based navigation changes
4. before introducing SYSTEM and COMPOSE hubs