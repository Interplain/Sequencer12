# Sequencer12 (S12)

A 12-step chord sequencer / harmonic performance module for Eurorack,
built on STM32F405RGT6 with a 240×240 ST7789 SPI display.

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
- User chord slots: 128 (FRAM backed, stub in sim)
- Key signature with diatonic filtering and CV transpose
- Velocity and probability per step
- MIDI clock master/slave (DIN only)
- CV/Gate output via DAC8564
- 3-level UI: Song → Block → Chord editor

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

sequencer12/
├── core/                    # Scheduler
├── devices/sequencer/       # Engine — patterns, arp, chords
├── platform/midi/           # MIDI clock master/slave
├── lib/ST7789/              # Display driver + fonts
├── stm32/                   # STM32 hardware entry point
├── src/                     # Linux sim entry point
├── CMakeLists.txt           # Linux sim build
└── platformio.ini           # STM32 hardware build

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
- [ ] Engine → display connection
- [ ] Level 2 UI (block editor)
- [ ] Level 3 UI (chord editor)
- [ ] MCP23017 button input
- [ ] DAC8564 CV output
- [ ] FRAM save/load
- [x] Encoder navigation

## Author

Richard Phillips (Interplain)

## Licence

MIT