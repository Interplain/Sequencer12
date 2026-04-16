# Sequencer12 (S12)

12-step chord sequencer / harmonic performance module for Eurorack, built on STM32F405RGT6 with a 240x240 ST7789 SPI display (current test target; 2.4in display migration planned).

## Current State (April 2026)

This README is updated to reflect the latest firmware work since the last upload.

## What Is Working Now

- Main mode cycling on Shift tap:
  - STEP
  - CHORD
  - TIME
  - SONG
- Chord workflow updates:
  - Matrix 1-4 in chord params are footer actions:
    - 1 MAIN
    - 2 PREV
    - 3 NEXT
    - 4 SAVE
  - PREV/NEXT carries chord params to neighboring steps for rapid programming
  - SAVE no longer leaves footer locked on SAVE
- Timing UI cleanup and naming:
  - Timing -> Pattern Timing
  - Duration -> Gate
  - TS Num -> Beats Per Bar
  - TS Den -> Beat Unit
  - Value columns moved right to prevent overlap
- Per-step repeat behavior (engine-level):
  - Step Repeats is stored per step
  - Repeat plays step N multiple times before advancing
- Repeat visual feedback in grid:
  - Active repeating step flashes white/blue
  - Flicker fix applied to redraw only on transitions/toggle phases
- Song chain editor:
  - SONG mode matrix 1 opens chain editor
  - Matrix 1-4 select visible slots
  - Matrix 5 add slot
  - Matrix 6 remove slot
  - Encoder edits slot pattern assignment
  - Playing slot blinks in chain view
- Pattern step-count live update:
  - Pattern Steps now applies immediately while playing
- Chord-preservation fixes:
  - Per-pattern chord draft cache in UI
  - UI auto-switches chord cache on pattern change
  - Hydration from engine note-mask metadata for pink-step recovery in chord mode
- Main loop responsiveness:
  - Loop delay now targets a 10 ms floor instead of always adding 10 ms

## Known Issues / Needs Sorting

- Persistence:
  - FRAM save/load for full song, pattern, and user chord state is not complete
- User chord integration:
  - USER chord load path still uses placeholder behavior for mapping external chord data into per-step chord params
- Build hygiene:
  - Build artifacts under .pio/build should stay out of commits (current repo contains tracked binary build outputs)
- Test coverage:
  - No formal regression test harness yet for mode transitions and input maps

## Control Map (Current Firmware)

### Global Input Layer

- Shift tap: cycle main mode STEP -> CHORD -> TIME -> SONG
- Play: transport play/stop on grid, save/confirm in submenus
- Rec: rec arm on grid, back/cancel in submenus
- Shift+Play: transport reset on grid
- Shift+Rec: transport clear on grid
- Encoder turn:
  - Grid + Shift held: BPM adjust
  - Menu contexts: navigate/edit context-specific fields
- Encoder press:
  - Context-specific select/enter/toggle
- Matrix 1-12:
  - Context-specific (see mode tables)
- Shift+Matrix 1-12:
  - Context-specific (pattern select, timing jump, footer shortcuts)

### STEP Main Mode (Grid)

- Matrix 1-12: open per-step piano roll for that step
- Encoder turn (no Shift): currently ignored in grid
- Encoder press: open chord menu for selected step
- Shift+Encoder press: open Pattern Timing menu

### CHORD Main Mode (Grid)

- Matrix 1-12: open chord menu directly for that step

### TIME Main Mode (Grid)

- Matrix 1: open Pattern Timing menu focused to Step Grid
- Shift+Matrix 1-12: open that step's params focused on Gate

### SONG Main Mode (Grid)

- Matrix 1: open Song Chain editor
- Shift+Matrix 1-12: set current pattern P01-P12

### Chord Params Mode

- Matrix 1-4: footer actions (MAIN/PREV/NEXT/SAVE)
- Matrix 5-12: jump editing target step
- Shift+Matrix 1-4: same footer actions
- Shift+Encoder turn: navigate footer actions
- Encoder turn (normal): edit selected parameter

### Song Chain Mode

- Matrix 1-4: select visible slot on page
- Matrix 5: add slot
- Matrix 6: remove slot
- Encoder turn: change pattern assigned to selected slot

## Potentially Free Buttons / Combos (For Manual Planning)

These appear unassigned or effectively no-op in current code paths.

### High-value free combos

- Grid, STEP mode:
  - Encoder turn without Shift (currently intentionally ignored)
- Grid, TIME mode:
  - Matrix 2-12 (plain press)
- Grid, SONG mode:
  - Matrix 2-12 (plain press)

### Submenu free combos

- Song Chain mode:
  - Matrix 7-12
  - Shift+Matrix 1-12 (currently falls through and does nothing useful)
- Step Piano mode:
  - Encoder press is reserved (no action yet)
- Most submenu contexts:
  - Shift+Play
  - Shift+Rec
  (transport shift combos are only handled on grid)

### Gesture-level free space

- Long-press actions are not implemented
- Double-tap actions are not implemented
- Chorded combos beyond Shift modifier are not implemented

## Suggested Next Priorities

1. Finish FRAM-backed persistence for patterns, chain, and user chords.
2. Finalize USER chord load/write path so loaded user chords are represented consistently in step params.
3. Assign SONG mode spare matrix buttons (2-12) for direct slot/pattern workflows.
4. Add a simple input-map regression test checklist before each upload.

## Hardware

| Component | Part |
|---|---|
| MCU | STM32F405RGT6 |
| Display | ST7789 240x240 SPI |
| CV DAC | DAC8564 |
| Buttons | MCP23017 I2C expander |
| Storage | MB85RC256V FRAM |
| Encoder | EC12R quadrature |
| MIDI | DIN IN + OUT |

## Build

### STM32 Hardware

```bash
platformio run -e genericSTM32F405RG --target upload
```

### Linux Simulator

```bash
mkdir -p build && cd build
cmake ..
make
./sequencer12
```

## License

MIT
