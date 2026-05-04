# Sequencer12 (S12)

Structured modular composition system for Eurorack.

Built on STM32F405RGT6 with:
- calibrated multi-channel CV output
- interpreted ADC/CV input architecture
- clock input and output
- MIDI integration
- persistent FRAM storage
- deterministic scheduler-driven firmware
- and a 240x320 ST7789 SPI display UI.

S12 is designed not only to generate musical output, but to interpret external modular signals, timing, harmonic input, transport state, and performance gestures into structured musical composition.
Firmware is in a stable, committed state. Development is paused while the PCB is designed. The breadboard prototype is too fragile (loose wires causing intermittent ST-Link upload failures and unreliable hardware connections). The firmware built and runs correctly on the breadboard — gate and CV output were confirmed working before parking.

Rather than functioning purely as a traditional sequencer or generative chaos source, S12 aims to become a musically-aware embedded composition environment:
- chord aware
- progression aware
- transport aware
- song aware
- scale aware
- CV aware
- and MIDI aware.

The long-term goal is to create a deterministic modular instrument capable of transforming voltage, timing, harmony, and interaction into organised musical structure while remaining stable, modular, and architecturally coherent.

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
- DAC8564 calibration (all 4 channels):
  - Per-channel two-point calibration (low/high voltage reference measurements)
  - Calibration data persisted to FRAM and loaded at boot
  - All 4 CV output channels (A/B/C/D) calibrated independently
  - Calibration mode accessible via Shift+Encoder press in STEP grid
  - Real-world voltage accuracy corrected for DAC offset and gain error
- ST7789 display:
  - Running at 10.5 MHz (SPI1 prescaler /16), DMA disabled (ST7789_USE_DMA=0)
  - This is the confirmed stable config — DMA=1 and /4 prescaler caused a white-screen regression on breadboard (likely noise on SPI lines)
  - Once on PCB with clean traces, DMA=1 and /4 should be revisited
- CV / Gate hardware output (new, May 2026):
  - Gate A on PC5 — driven directly from sequencer step engine via BSRR in the 1 ms SysTick ISR
  - Gate is 50% duty cycle (gate_length_ms = step_interval / 2, min 5 ms)
  - CV on DAC CH:A — mapped from `GetCurrentNote()` using two-point calibration (-1V to +2V across notes C–B)
  - Default pattern (steps 0–7) loads C major scale so CV moves on every step
  - Confirmed working on breadboard: gate fires on all 8 note steps, rest steps silent
  - CV calibration (per-channel two-point, FRAM-persisted) applied at boot
- Step piano roll UI:
  - Completely redesigned — each row owns all its pixels, no partial repaints
  - Pastel green (white keys) and sky blue (black keys) lane colours
  - Clean 1 px row separators, no bleed artefacts
  - Header covers full 48 px (matches main screen header+status strip)
  - Shift indicator and sidebar suppressed while roll is active
- FRAM persistence (User Chords):
  - MB85RC256 32KB FRAM on dedicated I2C3 bus (PA8 SCL, PC9 SDA)
  - 128 user chord slots with ~2560 bytes total storage (offset 0x0010)
  - Auto-preload entire library at boot for instant menu access
  - Per-slot write optimization (~20 bytes per save instead of full block)
  - Persistent across power cycles with integrity validation
  - Full CRUD operations: Create, Save (with Shift+Play), Rename, Delete (with Shift+Rec)
- User Chord UI standardization:
  - Load Chord screen: flat-text footer ("PLAY LOAD" / "REC BACK")
  - Name Chord screen: flat-text footer ("PLAY SAVE" / "REC BACK")
  - Create Chord (piano keyboard): flat-text footer ("PLAY SAVE" / "REC BACK")
  - Step Piano Roll: flat-text footer ("PLAY SAVE" / "REC BACK")
  - Consistent visual language across all chord workflow screens
  - Live chord name identification on piano keyboard header

## Known Issues / Needs Sorting

- **Breadboard parked — move to PCB**: ST-Link upload was failing (exit code 1) due to loose SWDIO/SWDCLK wires on breadboard. This is a hardware problem, not a firmware bug.
- Display DMA deferred: `ST7789_USE_DMA=1` with SPI1 at /4 (42 MHz) caused white-screen boot failure on breadboard. Reverted to DMA=0, /16 (10.5 MHz). Revisit once PCB has clean SPI traces.
- CV output: only CH:A physically wired on breadboard. Channels B/C/D are calibrated in firmware but untested on hardware.
- Persistence (remaining):
  - FRAM save/load for full song and pattern state is not complete (user chords done)
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

--------------------------------------

<img width="420" height="440" alt="image" src="https://github.com/user-attachments/assets/27a54389-0bef-4fea-b638-a025a2c24736" />

--------------------------------------

<img width="420" height="440" alt="image" src="https://github.com/user-attachments/assets/4dd10511-03a8-4e69-b7f9-2b1f31ab992a" />

--------------------------------------

<img width="420" height="440" alt="image" src="https://github.com/user-attachments/assets/bda8e4f0-fadb-4365-a04b-f634995fcc8e" />

--------------------------------------

<img width="420" height="440" alt="image" src="https://github.com/user-attachments/assets/b09b02ee-3b01-46fa-93f2-72d0a6dcf1e9" />

--------------------------------------
  


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

### When Returning to Firmware (post-PCB)
1. Re-enable `ST7789_USE_DMA=1` and SPI1 prescaler `/4` — test on PCB with clean traces.
2. Wire and test CV channels B/C/D (gate B/C/D also on PC6/PC7/PC8).
3. Verify CV pitch accuracy on modular VCO across the full -1V to +2V range with saved calibration.
4. Extend FRAM persistence to patterns and song chain (user chords complete).
5. Finalize USER chord load/write path so loaded user chords are represented consistently in step params.

### PCB Priorities
- Clean SPI1 traces for display (no stubs, 50-ohm impedance control where possible)
- Decouple DAC8564 supply carefully — CV noise floor matters for pitch accuracy
- Confirm SWDIO/SWDCLK with proper pull-ups for reliable ST-Link on first boot
- PC5–PC8 and PC1 gate outputs — level shift or buffer if driving eurorack gate levels (3.3V may be marginal)
3. Assign SONG mode spare matrix buttons (2-12) for direct slot/pattern workflows.
4. Add a simple input-map regression test checklist before each upload.

## April 2026: Hardware Bring-up & Gate/Clock Pin Mapping

### DAC & Gate/Clock Output Integration Progress

- **DAC8564 4-Channel CV Output:**
  - All 4 DAC channels (CH_OUT_A/B/C/D) are now confirmed working and mapped to CV jacks via OPA4171 op-amps.
  - Output range is bipolar (±5V) as per schematic.
- **Gate Outputs:**
  - GATE_A → PC5
  - GATE_B → PC6
  - GATE_C → PC7
  - GATE_D → PC8
  - All mapped directly to STM32 for low-latency, sample-accurate timing.
- **Clock I/O:**
  - Clock_IN → PC0 (input, no pull)
  - Clock_OUT → PC1 (output, initialized LOW)
  - Clock_OUT is inverted by 74HC14, so firmware must invert logic for correct polarity at the jack.
- **DAC_CLR:**
  - DAC_CLR → PC4 (output, initialized HIGH)

### Next Steps (for firmware & repo):
- [ ] Implement sequencer engine logic to drive GATE_A/B/C/D and Clock_OUT pins in real time.
- [ ] Add code to read Clock_IN for external sync.
- [ ] Document pin mappings in code and README.
- [ ] Push all bring-up and pin mapping changes to GitHub after confirming hardware and code.

**Schematic and firmware are now in sync for all CV, Gate, and Clock outputs.**

---

# Philosophy

S12 is not intended to be another generative chaos module.

Its purpose is to transform modular signals, timing, harmonic input, and performance gestures into structured musical composition.

Rather than simply outputting voltages or random patterns, S12 is being designed as a musical interpretation system:
- chord aware
- progression aware
- transport aware
- song aware
- scale aware
- CV aware
- MIDI aware

The project sits between:
- the unpredictability of modular synthesis
- and the intentionality of arranged composition.

S12 aims to provide a structured compositional layer for Eurorack.

---

# Project Direction

The original project began as a 12-step chord sequencer for Eurorack.

As development progressed, the firmware architecture evolved into something much larger:
a deterministic embedded composition environment capable of interpreting musical context rather than merely sequencing notes.

The long-term vision is a modular composition instrument that can:
- interpret external CV musically
- manage harmonic structure
- coordinate transport and timing
- understand progression relationships
- integrate MIDI and modular workflows
- and remain deterministic and stable under real-time embedded constraints.

The project intentionally avoids:
- feature chaos
- architectural drift
- generic modulation spaghetti
- and imitation of existing Eurorack modules.

S12 is intended to become a unique instrument with its own design language and architectural identity.

---

# System Architecture

The firmware is evolving toward a layered embedded architecture.

## Hardware Layer

Responsible for:
- SPI
- I2C
- ADC
- DAC
- GPIO
- MIDI UART
- display communication
- encoder/buttons
- FRAM persistence

This layer understands signals and hardware only.

---

## Runtime / Scheduler Layer

Deterministic fixed-rate scheduler providing:
- 1ms ticks
- 5ms ticks
- 10ms ticks
- 20ms ticks

Responsible for:
- transport timing
- gate timing
- event scheduling
- sequencer runtime updates
- synchronization

This is the heartbeat of S12.

---

## UI / Screen System

The UI architecture is evolving toward modular screen ownership.

Goals:
- isolated screen state
- deterministic redraw ownership
- reusable rendering infrastructure
- minimal global state leakage
- scalable screen lifecycle management

Current architectural direction includes:
- dedicated screen modules
- clear render contracts
- partial redraw support
- shared UI infrastructure
- deterministic input ownership

The piano roll screen is currently acting as the architectural proving ground for this transition.

---

## Transport & Timing Layer

Responsible for:
- play/stop/reset
- clock synchronization
- MIDI clock
- external clock input
- song position
- pattern transitions
- timing state
- transport coordination

S12 is intended to operate both as:
- a synchronized follower
- and a master transport authority.

---

## Composition Engine

This is the core identity of S12.

The composition engine is intended to manage:
- chords
- inversions
- progression logic
- arp systems
- pattern relationships
- phrase movement
- timing interpretation
- harmonic context

This layer transforms timing and CV input into structured musical behavior.

---

# CV / ADC Philosophy

One of the defining long-term goals of S12 is musical CV interpretation.

Most Eurorack modules treat CV as generic modulation.

S12 instead aims to interpret CV as musical intent.

Potential future uses include:
- harmonic transposition
- scale selection
- progression steering
- quantizer control
- rhythm influence
- phrase shaping
- inversion control
- transport manipulation
- probabilistic composition control

The ADC system is therefore being designed as a dedicated interpretation layer rather than simple parameter modulation.

---

# Current Hardware Platform

| Component | Part |
|---|---|
| MCU | STM32F405RGT6 |
| Display | ST7789 240x320 SPI |
| CV DAC | DAC8564 |
| CV Input | Multi-channel ADC architecture |
| Input Expander | MCP23017 |
| Persistent Storage | MB85RC256 FRAM |
| Encoder | EC12R quadrature |
| MIDI | DIN IN + OUT |
| Clock I/O | Dedicated clock input/output |

---

# Current Firmware State (May 2026)

## Core Runtime
- Deterministic scheduler system
- Device-oriented runtime architecture
- Modular transport state
- Pattern playback engine
- Sequencer timing engine
- Real-time UI update loop

---

## Display System
- ST7789 SPI display driver
- 42 MHz SPI operation
- DMA-based display writes
- Reduced redraw artefacts
- Structured redraw ownership improvements
- Piano roll screen redesign with isolated row ownership

---

## CV Output System
- DAC8564 integrated and operational
- Independent calibration per channel
- Two-point voltage calibration
- FRAM-persisted calibration data
- Bipolar CV output architecture

---

## ADC / CV Input Direction
- Multi-channel ADC architecture planned
- Interpreted CV input layer under architectural design
- Future harmonic and transport-aware CV interaction planned
- Quantizer-oriented input workflows planned
- External modular signal interpretation layer planned

---

## FRAM Persistence
- MB85RC256 integrated
- Persistent user chord library
- Integrity validation
- Optimized write strategy
- Boot-time preload for immediate access

---

## Song & Pattern System
- Pattern timing controls
- Song chain editor
- Step repeat behavior
- Per-pattern chord draft caching
- Pattern-aware chord hydration
- Real-time step-count updates

---

## UI Workflow

### Main Modes
- STEP
- CHORD
- TIME
- SONG

### Current Concepts
- contextual matrix input system
- footer action model
- screen-specific workflows
- transport-aware UI behavior
- modular piano roll editor

---

# Clock & Gate Architecture

## Gate Outputs
- GATE_A → PC5
- GATE_B → PC6
- GATE_C → PC7
- GATE_D → PC8

## Clock I/O
- Clock_IN → PC0
- Clock_OUT → PC1

Clock_OUT passes through 74HC14 inversion stage.

---

# Development Principles

S12 is being developed according to several core principles:

- deterministic behavior over uncontrolled complexity
- subsystem separation over global coupling
- reusable architecture over feature hacks
- musical interpretation over random generation
- stable timing over excessive abstraction
- embedded-first design decisions
- scalable screen ownership
- long-term maintainability

---

# Current Priorities

## Architecture
- modular screen ownership
- UI lifecycle separation
- render invalidation system
- state ownership cleanup

## Persistence
- full song/pattern FRAM persistence
- structured save/load architecture

## CV / ADC Layer
- interpreted CV input architecture
- quantizer integration
- musical control abstraction

## Transport
- external clock synchronization
- master/slave timing modes
- transport state coordination

---

# Long-Term Direction

S12 is evolving toward:

> a deterministic embedded composition environment for Eurorack.

The goal is not simply sequencing.

The goal is to create a system capable of:
- interpreting modular signals
- understanding musical structure
- organizing harmonic movement
- coordinating transport and timing
- and producing coherent composition from modular interaction.

S12 is intended to become:
a musical reasoning layer for modular synthesis.

---

# Build

## STM32 Hardware

```bash
platformio run -e genericSTM32F405RG --target upload
```

## Linux Simulator

```bash
mkdir -p build && cd build
cmake ..
make
./sequencer12
```

---

# License

MIT