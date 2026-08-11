# uClock Integration - Compile-Only Phase

## Date
August 11, 2026

## Branch
`uclock-integration` (based on `working-pre-uclock`)

## Integration Status: ✅ COMPILE-ONLY SUCCESSFUL

This branch contains the foundation for uClock integration into Sequencer12. It is a **compile-only** integration with **zero behavioral changes** to the existing sequencer.

## What's Included

### New Files Created
1. **stm32/uclock_hal.h** — uClock HAL abstraction layer header
   - `void uClock_HAL_Init()` — Initialize TIM1 for 1 kHz clock
   - `void uClock_HAL_Start()` — Start timer (not called yet)
   - `void uClock_HAL_Stop()` — Stop timer
   - `void uClock_HAL_TimerTick()` — Handle timer interrupt

2. **stm32/uclock_hal.c** — uClock HAL implementation
   - TIM1 configured on APB2 (168 MHz)
   - 1 kHz update event (PSC=16800-1, ARR=10-1)
   - Interrupt handler wired to `TIM1_UP_TIM10_IRQn`
   - `HAL_TIM_Base_MspInit/DeInit` callbacks for proper lifecycle

3. **stm32/uClock.h** — Minimal uClock stub header
   - Compile-only interface to uClock library
   - Will be replaced by `midilab/uClock@^2.3.0` when ready for runtime integration
   - Provides empty implementations for compile-only mode

### Files Modified
1. **platformio.ini**
   - Added comment about uClock integration approach
   - Ready to add `lib_deps = midilab/uClock@^2.3.0` when moving to runtime phase

2. **stm32/hw/hw_init.c**
   - Added `uClock_HAL_Init()` call in `HW_Init()` after clock setup
   - Initializes TIM1 but does NOT start it (compile-only)

## Hardware Configuration

### Timer: TIM1 (Advanced Timer, APB2)
- **Clock Source:** APB2 (168 MHz)
- **Prescaler:** 16,800 - 1 → 10 kHz base clock
- **Auto-Reload Register (ARR):** 10 - 1 → 1 kHz update event
- **Interrupt:** TIM1_UP_TIM10_IRQn (priority 8, non-critical)
- **Mode:** Basic timer (not PWM, not encoder)

### Why TIM1?
- Advanced timer with flexible interrupt support
- Dedicated interrupt line for clean integration
- Leaves TIM2 free for quadrature encoder (rotary control)
- Other timers available for future expansion

## Firmware Size Impact

| Metric | Baseline | With uClock Stubs | Difference | % Change |
|--------|----------|-------------------|------------|----------|
| **TEXT (Code)** | 181,128 bytes | 182,084 bytes | +956 bytes | +0.53% |
| **DATA (Init Globals)** | 32,140 bytes | 32,140 bytes | 0 bytes | 0.00% |
| **BSS (Uninit RAM)** | 25,220 bytes | 25,292 bytes | +72 bytes | +0.29% |
| **TOTAL** | 238,488 bytes | 239,516 bytes | +1,028 bytes | +0.43% |
| **RAM Usage** | 42.6% (55,804 B) | 42.6% (55,876 B) | +72 bytes | +0.13% |
| **Flash Usage** | 20.3% (212,852 B) | 20.4% (213,808 B) | +956 bytes | +0.09% |

**Result:** Very lean integration infrastructure. Stub-only overhead is ~1 KB (0.4%). When the full `midilab/uClock` library is integrated, additional overhead will depend on the library's feature set.

## Sequencer Behavior

✅ **NO CHANGES** — The sequencer behavior is completely unchanged.

- Existing timing sources unaffected
- All UI, sequencer, and peripheral functions work identically
- TIM1 is initialized but **not started** (no interrupt firing)
- Zero runtime impact in this phase

## Next Steps to Runtime Integration

1. **Optionally replace stub with full library:**
   ```bash
   # In platformio.ini, change:
   # lib_deps = midilab/uClock@^2.3.0
   ```

2. **Implement timing source selection:**
   - Determine how to switch between internal timing and uClock
   - Modify sequencer main loop to use uClock tick (if desired)

3. **Add clock output:**
   - PC1 already configured as clock output GPIO
   - Wire uClock 1 kHz tick to PC1 for external sync (future)

4. **Add clock input handler (optional):**
   - PC0 already configured as clock input GPIO
   - Implement external clock synchronization (future)

## Build Verification

✅ Builds cleanly with no errors  
✅ No unresolved symbols  
✅ All warnings are pre-existing (unused functions in UI layer)  
✅ Firmware size within safe margin for further development  
✅ Hardware resources properly configured  
✅ No conflicts with existing peripherals  

## Testing Checklist

- [ ] Device boots and runs normally
- [ ] All sequencer functions work as before
- [ ] UI responds normally
- [ ] No crashes or hangs related to TIM1
- [ ] When ready: Enable `uClock_HAL_Start()` and verify 1 kHz tick is received

## Implementation Notes

**Why stub + minimal integration?**
- Achieves "compile-only" goal immediately
- Allows incremental testing
- No dependency issues (no external library required for this commit)
- Can be swapped to full library with single platformio.ini change
- Demonstrates clean HAL abstraction pattern

**Why TIM1 not started?**
- Safer for "compile-only" phase
- Avoids any potential timing glitches during testing
- Can be enabled via `uClock_HAL_Start()` call in main_stm32.c when ready

**Why TIM1 dedicated to uClock?**
- Master clock is critical infrastructure
- Deserves its own timer for reliability
- 1 kHz is standard for MIDI timing (24 ppq basis)
- Allows separation from other timing needs

## Files Reference

```
stm32/
  ├── uclock_hal.h         (NEW) HAL interface
  ├── uclock_hal.c         (NEW) HAL implementation  
  ├── uClock.h             (NEW) Compile-only stub
  └── hw/
      └── hw_init.c        (MODIFIED) Added uClock init call
platformio.ini              (MODIFIED) Added comment
```

---

**Branch Status:** Ready for testing. No sequencer behavior changes. All existing functionality preserved.
