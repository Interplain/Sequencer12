/*!
 * @file    uclock_stm32_hal.cpp
 * @brief   STM32Cube/HAL backend for uClock v2.3.0
 * 
 * Implements TIM1-based timer control and linker reference for compile-only
 * footprint measurement. TIM1 is configured but NOT started in this phase.
 */

#include "uclock_stm32_hal.h"
#include "uClock.h"

// Global TIM1 handle (used if initTimer is called)
static TIM_HandleTypeDef htim1_uclock;

// ============================================================================
// INTERNAL: Initialize DWT Cycle Counter
// ============================================================================

/**
 * @brief Enable Data Watchpoint and Trace (DWT) cycle counter
 * 
 * DWT provides high-resolution cycle counter for microsecond precision
 * via the micros() function. Runs at SystemCoreClock (168 MHz).
 */
static void initDWT(void) {
    // Enable debug module (required for DWT access)
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    
    // Enable DWT cycle counter
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    
    // Reset counter to zero
    DWT->CYCCNT = 0;
}

// ============================================================================
// TIMER BACKEND: Dynamic TIM1 Configuration (Dormant in compile-only phase)
// ============================================================================

/**
 * @brief Initialize TIM1 for uClock timing
 * 
 * Configures TIM1 for dynamic microsecond intervals:
 * - Prescaler: 168-1 (creates 1 MHz base from 168 MHz APB2)
 * - Period: us_interval-1 (updates every us_interval microseconds)
 * - Interrupt: TIM1_UP_TIM10_IRQn (priority 8, non-critical)
 * 
 * COMPILE-ONLY PHASE: Function is present but NOT CALLED.
 * TIM1 is configured but NOT started (HAL_TIM_Base_Start_IT not called).
 * This measures footprint with zero runtime impact.
 * 
 * @param us_interval Timer update interval in microseconds
 */
void initTimer(uint32_t us_interval) {
    initDWT();  // Enable cycle counter for micros()
    
    // Enable TIM1 clock on APB2
    __HAL_RCC_TIM1_CLK_ENABLE();
    
    // Configure TIM1 for dynamic microsecond intervals
    // APB2 clock: 168 MHz
    // PSC: 168-1 → divides to 1 MHz (each tick = 1 µs)
    // ARR: us_interval-1 → period in microseconds
    htim1_uclock.Instance = TIM1;
    htim1_uclock.Init.Prescaler = 168 - 1;
    htim1_uclock.Init.Period = us_interval - 1;
    htim1_uclock.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim1_uclock.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim1_uclock.Init.RepetitionCounter = 0;
    htim1_uclock.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    
    HAL_TIM_Base_Init(&htim1_uclock);
    
    // Set interrupt priority (0 = highest, above display DMA at priority 1)
    // This ensures clock interrupts are processed immediately for timing accuracy
    HAL_NVIC_SetPriority(TIM1_UP_TIM10_IRQn, 0, 0);
    
    // Enable interrupt in NVIC and START the timer
    HAL_NVIC_EnableIRQ(TIM1_UP_TIM10_IRQn);
    
    // RUNTIME PHASE: Start the timer with interrupts enabled
    // This begins periodic interrupts at the configured interval
    HAL_TIM_Base_Start_IT(&htim1_uclock);
}

/**
 * @brief Update TIM1 interval dynamically
 * 
 * Changes timer period without stopping. Used when BPM/timing changes.
 * COMPILE-ONLY PHASE: NOT CALLED.
 * 
 * @param us_interval New interval in microseconds
 */
void setTimer(uint32_t us_interval) {
    // Update period configuration
    htim1_uclock.Init.Period = us_interval - 1;
    
    // Write to ARR (auto-reload register)
    TIM1->ARR = us_interval - 1;
    
    // Force update event to apply new period immediately
    TIM1->EGR |= TIM_EGR_UG;
}

// ============================================================================
// INTERRUPT HANDLER (Defined but dormant in compile-only phase)
// ============================================================================

/**
 * @brief TIM1 Update/TIM10 interrupt handler
 * 
 * Triggered when TIM1 counter reaches period value.
 * Routes interrupt to uClockHandler() in uClock core.
 * 
 * COMPILE-ONLY PHASE: Handler is present for completeness but
 * TIM1 is not started, so this is never called.
 */
extern "C" void TIM1_UP_TIM10_IRQHandler(void) {
    if (__HAL_TIM_GET_FLAG(&htim1_uclock, TIM_FLAG_UPDATE)) {
        __HAL_TIM_CLEAR_FLAG(&htim1_uclock, TIM_FLAG_UPDATE);
        
        // Call uClock's main interrupt handler (defined in uClock.cpp)
        uClockHandler();
    }
}

// ============================================================================
// PC1 CLOCK OUTPUT CALLBACK (Runtime Test Phase)
// ============================================================================

/**
 * @brief Toggle PC1 (Clock_OUT) on each uClock 24 PPQN sync tick
 * 
 * Called at 48 Hz when uClock runs at 120 BPM with 24 PPQN:
 *   120 BPM = 2 beats/sec × 24 PPQN = 48 ticks/sec
 * 
 * Each callback toggles the pin once (HIGH→LOW or LOW→HIGH).
 * Complete waveform cycle (HIGH→LOW→HIGH) = 2 callbacks = 41.667 ms
 * Measured frequency on PC1 = 24 Hz with 50% duty cycle
 * 
 * Uses static toggle state and direct BSRR writes for deterministic timing.
 * 
 * @param tick uClock tick counter (unused, called on every sync tick)
 */
extern "C" uint8_t Bridge_IsPlaying(void);

extern "C" void uClock_PC1_ClockOut_Toggle(uint32_t tick) {
    (void)tick;  // Suppress unused parameter warning

    if (!Bridge_IsPlaying()) {
        GPIOC->BSRR = (GPIO_PIN_1 << 16);  // Force Clock OUT low when transport is stopped
        return;
    }

    // Static toggle state: 0 = LOW, 1 = HIGH
    static uint8_t pc1_state = 0;

    // Toggle by writing to GPIO BSRR (Bit Set/Reset Register)
    // This is faster and more deterministic than ReadPin + WritePin
    if (pc1_state) {
        // Currently HIGH, set to LOW: write bit to reset
        GPIOC->BSRR = (GPIO_PIN_1 << 16);  // Reset register (upper 16 bits)
        pc1_state = 0;
    } else {
        // Currently LOW, set to HIGH: write bit to set
        GPIOC->BSRR = GPIO_PIN_1;  // Set register (lower 16 bits)
        pc1_state = 1;
    }
}

// ============================================================================
// RUNTIME STARTUP: Initialize uClock with PC1 clock output
// ============================================================================

/**
 * @brief Initialize and start uClock for runtime PC1 clock output test
 * 
 * Sets up uClock to generate a 24 Hz square wave on PC1 (Clock_OUT)
 * by toggling the pin on each 24 PPQN sync tick when running at 120 BPM.
 * 
 * Sequence:
 * 1. Register 24 PPQN sync callback (must be before init)
 * 2. Call init() to configure and START TIM1 with interrupts
 * 3. Set tempo to 120 BPM
 * 4. Start clock generation
 * 
 * This is an isolated hardware test; uClock is NOT connected to the
 * sequencer engine, DAC, gates, MIDI, or any other sequencer logic.
 * 
 * @return Zero on success
 */
extern "C" int uClock_StartRuntime(void) {
    using namespace umodular::clock;
    
    // 1. Register PC1 toggle callback for 24 PPQN sync before init()
    //    This ensures the callback is installed before TIM1 starts firing
    uClock.setOnSync(uClockClass::PPQN_24, uClock_PC1_ClockOut_Toggle);
    
    // 2. Initialize TIM1 backend and START the timer
    //    This configures TIM1 and calls HAL_TIM_Base_Start_IT()
    //    (timer will NOT fire until start() is called)
    uClock.init();
    
    // 3. Set tempo to 120 BPM
    //    This calculates the TIM1 period: 20833 µs (48 ticks/sec)
    uClock.setTempo(120.0f);
    
    // 4. Start clock generation
    //    This begins firing TIM1 interrupts at the calculated interval
    //    Each interrupt calls TIM1_UP_TIM10_IRQHandler → uClockHandler()
    //    uClockHandler() invokes the 24 PPQN callback for each tick
    uClock.start();
    
    return 0;  // Success
}
