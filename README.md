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

Rather than functioning purely as a traditional sequencer or generative chaos source, S12 aims to become a musically-aware embedded composition environment:
- chord aware
- progression aware
- transport aware
- song aware
- scale aware
- CV aware
- and MIDI aware.

The long-term goal is to create a deterministic modular instrument capable of transforming voltage, timing, harmony, and interaction into organised musical structure while remaining stable, modular, and architecturally coherent.

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