# Sequencer12 Firmware Size Baseline

## Date
August 11, 2026 (Pre-uClock Integration)

## Build Details
- **Platform:** ST STM32 (19.5.0)
- **Board:** STM32F405RGT6
- **Hardware Specs:** 168MHz, 128KB RAM, 1MB Flash
- **Framework:** STM32Cube
- **Build Environment:** `genericSTM32F405RG`
- **Build Mode:** Release
- **Build Duration:** 3.803 seconds

## Memory Usage (From PlatformIO)
```
RAM:   [====      ]  42.6% (used 55804 bytes from 131072 bytes)
Flash: [==        ]  20.3% (used 212852 bytes from 1048576 bytes)
```

## Size Report (arm-none-eabi-size)
```
   text    data     bss     dec      hex     filename
 181128   32140   25220  238488    3a398   firmware.elf
```

### Breakdown
- **TEXT (Code):** 181,128 bytes (~177 KB) - Flash-resident executable code
- **DATA (Initialized):** 32,140 bytes (~31 KB) - Initialized global variables (in Flash, copied to RAM at startup)
- **BSS (Uninitialized):** 25,220 bytes (~25 KB) - Uninitialized globals and static data (RAM only)
- **Total (dec):** 238,488 bytes
- **Total (hex):** 0x3a398

## Binary Files
- **firmware.bin:** 209 KB (flashed to device)
- **firmware.elf:** 367 KB (debug symbols, includes BSS)

## Flash Breakdown
- **Used:** 212,852 bytes (20.3%)
- **Available:** 1,048,576 bytes (1 MB total)
- **Remaining:** 835,724 bytes (~816 KB free)

## RAM Breakdown
- **Used:** 55,804 bytes (42.6%)
- **Available:** 131,072 bytes (128 KB total)
- **Remaining:** 75,268 bytes (~73 KB free)

## Notes
- Compile-time is very fast (3.8 seconds) suggesting most symbols are stripped in release mode
- Flash headroom is good (~816 KB available for uClock integration)
- RAM usage is moderate (~73 KB available)

---

This baseline will be used to measure the firmware size overhead introduced by uClock integration.

**Use this for comparison after uClock is integrated:**
```bash
# Run after uClock integration
arm-none-eabi-size .pio/build/genericSTM32F405RG/firmware.elf
```
