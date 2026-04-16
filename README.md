# Sequencer12 (S12)

A 12-step chord sequencer / harmonic performance module for Eurorack,
built on STM32F405RGT6 with a 240×240 ST7789 SPI display for test. Will be changing for 2.4 screen.

## Concept

The S12 is not a conventional step sequencer. Each step holds a full
chord rather than a single note, making it a harmonic composition tool
for Eurorack. Inspired by the MPC One workflow but designed specifically
for chord-based modular synthesis.

## Features

- 12-step chord progression sequencer
- Each step holds a chord (up to 12 notes via bitmask)
- 32 patterns per song, user-defined chain order
- Per-pattern: tempo multiplier, step count, playback mode
- Playback modes: Forward, PingPong, Loop, OneShot
- Repeat count per pattern before chain advance
- Arpeggiator engine: Up, Down, UpDown, DownUp, Random, AsPlayed
- Arp rate: 1/4, 1/8, 1/16, 1/32
- Chord library: 288 presets (12 roots × 24 types)
- User chord slots: 128 (RAM currently, FRAM integration scaffolded)
- Key signature with diatonic filtering and CV transpose
- Velocity and probability per step
- MIDI clock master/slave (DIN only)
- CV/Gate output via DAC8564
- 3-level UI: Song → Block → Chord editor
- Custom Chord workflow:
	- USER entry from chord menu footer
	- Custom submenu: CREATE / LOAD / NAME
	- Piano keyboard chord editor (12-note toggle, full-width layout)
	- Chord name editor (encoder character edit + save)
- Transport button repurpose in submenus:
	- Encoder rotate = navigate
	- Encoder press = select/toggle
	- Play = save/commit
	- Record = back
- Main grid transport remains standard:
	- Play/Stop, Shift+Play Reset
	- Rec Arm, Shift+Rec Clear

## Hardware

| Component       | Part                  |
|-----------------|-----------------------|
| MCU             | STM32F405RGT6         |
| Display         | ST7789 240×240 SPI    |
| CV DAC          | DAC8564               |
| Buttons         | MCP23017 I2C expander |
| Storage         | MB85RC256V FRAM       |
| Encoder         | EC12R quadrature      |
| MIDI            | DIN IN + OUT          |

## Pin Map

| Signal        | Pin  |
|---------------|------|
| SPI1 SCK      | PA5  |
| SPI1 MOSI     | PA7  |
| Display DC    | PA6  |
| Display RST   | PA8  |
| Display BLK   | PB0  |
| SPI2 DAC CS   | PB12 |
| SPI2 DAC SCK  | PB13 |
| SPI2 DAC MOSI | PB15 |
| I2C1 SCL      | PB8  |
| I2C1 SDA      | PB9  |
| Encoder A     | PA0  |
| Encoder B     | PA1  |
| Encoder SW    | PC15 |
| MIDI TX       | PA2  |
| MIDI RX       | PA3  |
| Clock IN      | PC0  |
| Clock OUT     | PC1  |

## Project Structure

```text
sequencer12/
├── core/                          # Scheduler core
├── devices/
│   └── sequencer/                 # Sequencer engine
│       ├── arp_engine.*           # Arpeggiator logic
│       ├── pattern_bank.*         # Pattern storage / chain support
│       ├── sequencer_device.*     # Main sequencer runtime
│       ├── sequencer_types.h      # Shared engine types
│       └── chords/
│           ├── chord_library.*    # Preset chord library
│           └── user_chords.*      # User chord slot model (128 slots)
├── platform/
│   ├── midi/                      # MIDI clock and parser
│   ├── mcp23017/                  # Button matrix / GPIO expander
│   ├── dac8564/                   # CV DAC driver
│   └── fram/
│       └── mb85rc256.*            # FRAM driver scaffold
├── ui/
│   ├── ui_sequencer.*             # UI state machine + mode routing
│   ├── ui_display.*               # Rendering (menus, piano, status)
│   ├── ui_input.*                 # Encoder/buttons input decode
│   ├── ui_transport.*             # Transport behavior
│   ├── ui_regions.h               # UI layout regions
│   └── ui_icons.h                 # Small icon assets
├── stm32/
│   ├── main_stm32.c               # Hardware app entry point
│   ├── sequencer_bridge.*         # C bridge between UI and engine
│   ├── user_chord_bridge.*        # C bridge for user chord library
│   ├── calibration.*              # DAC calibration helpers
│   └── hw/
│       └── hw_init.*              # Clock/GPIO/SPI/I2C/UART init
├── lib/ST7789/                    # Display driver + fonts
├── src/                           # Linux simulator entry point
├── app/                           # App-level interface wrapper
├── CMakeLists.txt                 # Linux simulator build
└── platformio.ini                 # STM32 hardware build
```

### Structure Map Notes

- Implemented now: engine, UI flow (Grid/Chord/Custom/Create/Load/Name), MCP23017 input, DAC path scaffold, user chord bridge.
- In progress / next: FRAM-backed persistence for user chords and song/pattern save strategy.
- Build output folders such as .pio/ and build/ are generated artifacts, not source modules.

## Building

### Linux Simulator
```bash
mkdir build && cd build
cmake ..
make
./sequencer12
```

### STM32 Hardware
```bash
platformio run -e genericSTM32F405RG --target upload
```

## Current UI Flow

- Main Grid (12 steps)
	- Encoder: move selection
	- Encoder press: open chord menu for selected step
- Chord Menu
	- Predefined chords + USER entry
	- Clear action returns to main grid
- USER Menu (Custom Chord)
	- CREATE: open piano chord editor
	- LOAD: select from saved user chords
	- NAME: rename last saved/selected user chord
	- Footer MAIN STEPS: return to main grid
- Create Chord (Piano)
	- Encoder rotate: move selected note
	- Encoder press: toggle note on/off
	- Play: save chord to user library
	- Record: back without save

 - ---------------------------------------------------------------------

   <img width="426" height="445" alt="image" src="https://github.com/user-attachments/assets/65facc62-3499-4315-a4b4-608dd8155e47" />

   <img width="426" height="445" alt="image" src="https://github.com/user-attachments/assets/e6802996-9e2c-48c3-9316-0eae1cd090a9" />

   <img width="426" height="445" alt="image" src="https://github.com/user-attachments/assets/813bbbe4-c461-432e-8ec4-c1ae83c08cf8" />

   <img width="426" height="445" alt="image" src="https://github.com/user-attachments/assets/120c8832-308b-4524-8161-239faa0964f4" />





## Status

- [x] Scheduler
- [x] Step engine
- [x] Pattern bank (32 patterns)
- [x] Pattern chain
- [x] Chord library (288 presets)
- [x] Arp engine
- [x] MIDI clock
- [x] Display driver (ST7789 240×240)
- [x] Level 1 UI (12 block grid)
- [x] Engine → display connection
- [x] Level 2 UI (chord menu)
- [x] Level 3 UI (chord params + custom chord screens)
- [x] MCP23017 button input
- [x] DAC8564 CV output path scaffold
- [ ] FRAM persistence for user chords
- [x] Encoder navigation

## Author

Richard Phillips (Interplain)

## Licence

MIT
