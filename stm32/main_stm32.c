#include "stm32f4xx_hal.h"
#include "st7789.h"
#include "hw_init.h"
#include "mcp23017.h"
#include "platform/dac8564/dac8564.h"
#include "platform/fram/mb85rc1mt.h"
#include "calibration.h"
#include "sequencer_bridge.h"
#include "user_chord_bridge.h"
#include "ui/ui_display.h"
#include "ui/ui_sequencer.h"
#include "ui/ui_input.h"
#include <stdio.h>

#if defined(S12_FRAM_BACKUP_MODE)
#include "platform/fram/mb85rc256.h"
#include "platform/fram/fram_backup_dump.h"
#endif

#if defined(S12_FRAM_NEWCHIP_TEST_MODE)
#include "platform/fram/fram_newchip_test.h"
#endif

#if defined(S12_FRAM_RESTORE_MODE)
#include "platform/fram/fram_restore_migrate.h"
#endif

#if defined(S12_PATTERNSTORE_TEST_MODE)
#include "platform/fram/pattern_store_test.h"
#endif

#include "stm32/timing_probe.h"

#define UI_LOOP_MIN_MS 1

#ifndef MCP_MINIMAL_DEBUG
#define MCP_MINIMAL_DEBUG 0
#endif

int main(void)
{
    HW_Init();
#if defined(S12_TIMING_PROBE_ENABLE)
    TimingProbe_Init();
#endif

#if defined(S12_FRAM_BACKUP_MODE)
    /* Isolated, read-only backup path: read FRAM, compute CRC32, halt.
     * Deliberately returns before any calibration/display/MCP23017/DAC/UI
     * init below, so none of that code (including the calibration future-
     * partition write/restore self-test) ever runs in this build. */
    MB85RC256_Init(&hi2c3);
    FramBackupDump_Run();
    while (1)
    {
        __NOP();
    }
#endif

#if defined(S12_FRAM_NEWCHIP_TEST_MODE)
    /* Isolated, read-only MB85RC1MT probe: read-only on both banks, halt.
     * Returns before calibration/display/MCP23017/DAC/UI init, so nothing
     * else in the boot path can touch FRAM before this probe runs. */
    MB85RC1MT_Init(&hi2c3);
    FramNewchipTest_Run();
    while (1)
    {
        __NOP();
    }
#endif

#if defined(S12_FRAM_RESTORE_MODE)
    /* Isolated MB85RC1MT restore path: writes/verifies only 0x0000-0x7FFF
     * from a buffer pre-loaded by OpenOCD, then halts. Returns before
     * calibration/display/MCP23017/DAC/UI init, so nothing else in the boot
     * path can touch FRAM before this explicit, operator-triggered restore. */
    MB85RC1MT_Init(&hi2c3);
    FramRestoreMigrate_Run();
    while (1)
    {
        __NOP();
    }
#endif

#if defined(S12_PATTERNSTORE_TEST_MODE)
    /* Isolated pattern-store round-trip test: writes/reads/verifies ONE slot
     * in the new 0x8000+ region only, never touches the legacy 0x0000-0x7FFF
     * image. Returns before calibration/display/MCP23017/DAC/UI init. */
    MB85RC1MT_Init(&hi2c3);
    PatternStoreTest_Run();
    while (1)
    {
        __NOP();
    }
#endif

    /* Assert display RST low immediately and hold it throughout all early init.
     * The panel controller stays in hardware reset — its backlight may still
     * power up from VCC, but no image is visible because GRAM is not driven.
     * RST is released only just before ST7789_Init() below. */
    GPIOA->BSRR = (1U << (9 + 16));  /* RST LOW */

#if !MCP_MINIMAL_DEBUG
    /* Latch calibration-entry hold as early as possible in boot. */
    uint8_t enter_cal = Calibration_ShouldEnterOnBoot();
#endif

    /* Init FRAM on dedicated I2C3 bus (PA8=SCL / PC9=SDA) */
#if !MCP_MINIMAL_DEBUG
    MB85RC1MT_Init(&hi2c3);
#endif

    /* Init DAC8564 */
#if !MCP_MINIMAL_DEBUG
    DAC8564_Init(&hspi2);

    /* Apply saved calibration and clamp CVs to true 0V immediately. */
    Calibration_ApplySaved();
    Bridge_Init();
    Bridge_UserChord_Init();
    Bridge_ApplyZeroOutputCodes();
#endif

    /* Release RST and initialise display as late as possible */
    /* A brief low-to-high transition is all the panel needs.
     * The controller was held in reset so there was nothing to display until now. */
    HAL_Delay(20);
    GPIOA->BSRR = (1U << 9);         /* RST HIGH */
    HAL_Delay(120);
    ST7789_Init();

    /* Init MCP23017 after display and other hardware are ready */
    MCP23017_Init(&hi2c1);

    UI_Input_Init();

#if !MCP_MINIMAL_DEBUG
    if (enter_cal)
    {
        ST7789_DisplayOn();
        ST7789_Fill_Color(BLACK);
        ST7789_DrawStringScaled(10, 48, "DAC CAL", &Font16x24, 1, CYAN, BLACK);
        ST7789_DrawStringScaled(10, 86, "STARTING", &Font16x24, 1, WHITE, BLACK);
        ST7789_DrawStringScaled(10, 116, "CALIBRATION", &Font16x24, 1, WHITE, BLACK);

        Calibration_RunWizard();
    }

    /* Re-apply zero codes (wizard may have updated calibration). */
    Bridge_ApplyZeroOutputCodes();

    /* UI_Sequencer_Init performs the first full UI draw. */
    UI_Sequencer_Init();

    while (1)
    {
        /* Keep display reset line asserted high to avoid transient blanking. */
        GPIOA->BSRR = (1U << 9);

        uint32_t t0 = HAL_GetTick();
        Bridge_ServiceMusicalEvents();
        UI_Input_Poll();
        {
            uint32_t _tp_ui = TIMING_PROBE_BEGIN();
            UI_Sequencer_Update();
            TIMING_PROBE_END(g_timing_max_ui_update_us, _tp_ui);
        }
        /* After the screen has already redrawn: any deferred FRAM write
         * (V3 save + Pattern Store shadow-write) happens here, not inside
         * the modal-confirm call path itself. */
        {
            uint32_t _tp_persist = TIMING_PROBE_BEGIN();
            Bridge_ServicePersistence();
            TIMING_PROBE_END(g_timing_max_service_persistence_us, _tp_persist);
        }
#if defined(S12_PATTERNSTORE_VERIFYALL_ENABLE)
        /* Diagnostic/read-only, armed by writing g_ps_verify_trigger=1 over SWD;
         * never runs otherwise, never writes FRAM, never alters SRAM contents. */
        extern volatile uint8_t g_ps_verify_trigger;
        if (g_ps_verify_trigger)
        {
            g_ps_verify_trigger = 0u;
            Bridge_PatternStoreVerifyAll();
        }
#endif
#if defined(S12_PATTERNCACHE_PILOT_ENABLE)
        /* Diagnostic/shadow current+next Pattern cache: at most one small
         * chunked PatternStore read per pass, never affects playback. */
        Bridge_ServicePatternCache();
#endif
        uint32_t elapsed = HAL_GetTick() - t0;
        if (elapsed < UI_LOOP_MIN_MS)
            HAL_Delay(UI_LOOP_MIN_MS - elapsed);
    }
#endif

    return 0;
}